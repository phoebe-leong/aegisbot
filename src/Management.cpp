//
// Management.cpp
// **************
//
// Copyright (c) 2019 Sharon W (sharon at aegis dot gg)
//
// Distributed under the MIT License. (See accompanying file LICENSE)
// 


#include "AegisBot.h"
#include <aegis/gateway/objects/embed.hpp>
#include "Guild.h"
#include <aegis.hpp>
#if !defined(AEGIS_MSVC)
#include <dlfcn.h>
#endif
#include <aegis/guild.hpp>
#include <aegis/channel.hpp>
#include <aegis/user.hpp>

using aegis::snowflake;
using aegis::user;
using aegis::channel;
using aegis::guild;
using aegis::rest::rest_reply;
using aegis::gateway::objects::message;

bool AegisBot::process_admin_messages(aegis::gateway::events::message_create & obj, shared_data & sd)
{
    if (sd.member_id != bot_owner_id)
        return false;

    aegis::core & bot = *_bot;

    const snowflake & channel_id = sd.channel_id;
    const snowflake & guild_id = sd.guild_id;
    const snowflake & message_id = sd.message_id;
    const snowflake & member_id = sd.member_id;
    const snowflake & guild_owner_id = sd.guild_owner_id;

    std::string_view username = sd.username;

    user & _member = sd.user;
    channel & _channel = sd.channel;
    guild & _guild = sd.guild;
    std::string_view content = sd.content;

    Guild & g_data = sd.g_data;

    std::vector<std::string_view> & toks = sd.toks;

    bool is_guild_owner = (member_id == guild_owner_id || member_id == bot_owner_id);

    std::string request{ content };

    // return true if params are not enough
    auto check_params = [&_channel](std::vector<std::string_view> & toks, size_t req, std::string example = "")
    {
        size_t count = toks.size();
        if (count < req)
        {
            if (example.empty())
                _channel.create_message(fmt::format("Not enough parameters. Need {} given {}", req, count));
            else
                _channel.create_message(fmt::format("Not enough parameters. Need {} given {} - example: {}", req, count, example));
            return true;
        }
        return false;
    };

#if !defined(AEGIS_MSVC)
    auto exec = [&](std::string cmd) -> std::string
    {
        bot.log->info("EXEC: {} : {}", _member.get_id(), cmd);
        std::array<char, 128> buffer;
        std::string result;
        std::shared_ptr<FILE> pipe(popen(cmd.c_str(), "r"), pclose);
        if (!pipe)
        {
            _channel.create_message("Error opening pipe.");
            throw std::runtime_error("_popen() failed!");
        }
        while (!feof(pipe.get()))
        {
            if (fgets(buffer.data(), 128, pipe.get()) != nullptr)
                result += buffer.data();
        }
        return result;
    };
#endif

    if (toks[0] == "msg_count")
    {
        _channel.create_message(fmt::format("Messages: {}", message_history.size()));
        return true;
    }

    if (toks[0] == "clear_messages")
    {
        std::string ret;
        {
            ret = aegis::utility::perf_run("clear_messages", [&]()
            {
                std::unique_lock<std::shared_mutex> l(m_message);
                message_history.clear();
            });
        }
        _channel.create_message(fmt::format("{}", ret));
        //req_success(obj.msg);
    }

    if (toks[0] == "dc")
    {
        bot.get_shard_mgr().close(obj.shard, 1003);
        return true;
    }

    if (toks[0] == "thread")
    {

    }

    if (toks[0] == "socketcheck")
    {
        std::stringstream w;
        for (uint32_t i = 0; i < bot.get_shard_mgr().shard_count(); ++i)
        {
            auto & shd = bot.get_shard_mgr().get_shard(i);
            w << fmt::format("#{} state: {} null:{}\n", shd.get_id(), static_cast<int>(shd.connection_state), (shd.get_connection() == nullptr?"true":"false"));
        }
        _channel.create_message(fmt::format("```\n{}```", w.str()));
    }

#if !defined(AEGIS_MSVC)
    if (toks[0] == "bash")
    {
        if (toks.size() == 1)
            return true;
        try
        {
            std::string torun{ toks[1].data() };
            std::string result = exec(torun);
            _channel.create_message(fmt::format("Result: ```\n{}\n```", result));
        }
        catch (std::exception & e)
        {
            bot.log->error("Exception: {}", e.what());
            return true;
        }
        return true;
    }

    if (toks[0] == "eval")
    {
        std::string code;

        std::string::size_type n, nxt, offset = 0;
        std::size_t addmore = 0;
        n = content.find("```cpp\n");
        if (n == std::string::npos)
        {
            addmore = 4;
            n = content.find("```\n");
        }
        else
            addmore = 7;

        if (n != std::string::npos)
        {
            nxt = content.rfind("```");
            if (nxt == std::string::npos)
            {
                //no end found
                _channel.create_message("No terminating \\``` found.");
                return true;
            }

            code = std::string{ content.substr(n + addmore, nxt - n - 4 - addmore + 4) };
        }

        std::stringstream ss;

        if (code.find("main") == std::string::npos)
        {

            ss <<
                R"(

#include <iostream>
#include <string>
#include <stdint.h>
#include "../src/AegisBot.h"

extern "C" std::string AegisExec(AegisBot & bot, aegis::gateway::events::message_create & obj)
{
)";
            ss << code << "return \"\"; }";

        }

        std::string basefilename = fmt::format("{}-{}", _member.get_id(), sd.message_id);
        std::string compilefilename = fmt::format("src/{}.cpp", basefilename);

        {
            std::fstream codefile(compilefilename, std::fstream::binary | std::fstream::out);

            if (!codefile.is_open())
            {
                _channel.create_message("Error opening file.");
                throw std::runtime_error("fstream::open failed!");
            }
            codefile << ss.str();
            codefile.close();
        }
        {
            std::fstream codefile("main.cpp", std::fstream::binary | std::fstream::out);

            if (!codefile.is_open())
            {
                _channel.create_message("Error opening file.");
                throw std::runtime_error("fstream::open failed!");
            }
            codefile << ss.str();
            codefile.close();
        }

        try
        {
            static int32_t incr = 0;
            ++incr;
            auto compile_start = std::chrono::steady_clock::now();
            std::string compileresult = exec(fmt::format("g++-7 -std=gnu++17 -O2 -DAEGIS_PROFILING=1 main.cpp -shared -fPIC -o main{}.so -I../src/ 2>&1", incr));
            auto compile_count = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - compile_start).count();
            std::string result = exec(fmt::format(R"(if [ -f main{}.so ] ; then echo "SUCCESS" ; else echo "FAIL" ; fi)", incr));

            if (result == "SUCCESS\n")
            {
                bot.log->info("Loading: main{}.so", incr);
                auto evaltoload = dlopen(fmt::format("{}/main{}.so", getenv("PWD"), incr).c_str(), RTLD_NOW);
                if (!evaltoload)
                {
                    const char *dlsym_error = dlerror();
                    bot.log->info("dlerror : {}", dlsym_error);
                    _channel.create_message(fmt::format("Failed to load so: {}", dlsym_error));
                    return false;
                }

                using aegisexec_t = std::string(*)(AegisBot&, aegis::gateway::events::message_create &);

                // reset errors
                dlerror();
                aegisexec_t aegisexec = (aegisexec_t)dlsym(evaltoload, "AegisExec");
                const char *dlsym_error = dlerror();
                if (dlsym_error)
                {
                    bot.log->info("dlsym_error : {}", dlsym_error);
                    _channel.create_message(fmt::format("Failed to run so: {}", dlsym_error));
                    dlclose(evaltoload);
                    return true;
                }

                auto run_start = std::chrono::steady_clock::now();
                std::string execresult = aegisexec(*this, obj);
                auto run_count = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - run_start).count();


                dlclose(evaltoload);
                exec("rm -rf main.*");

                _channel.create_message(fmt::format("Result: compile({}ms) execution({}ms)\n```\n{}\n```", compile_count, run_count, execresult));
            }
            else
            {
                _channel.create_message(fmt::format("Failed to compile\n```\n{}\n```", compileresult));
            }
        }
        catch (std::exception & e)
        {
            bot.log->error("Exception: {}", e.what());
            return true;
        }
        return true;
    }

    if (toks[0] == "gcc")
    {
        auto now = std::chrono::steady_clock::now();

        std::string code;

        std::string::size_type n, nxt, offset = 0;
        std::size_t addmore = 0;
        n = content.find("```cpp\n");
        if (n == std::string::npos)
        {
            addmore = 4;
            n = content.find("```\n");
        }
        else
            addmore = 7;

        if (n != std::string::npos)
        {
            nxt = content.rfind("```");
            if (nxt == std::string::npos)
            {
                //no end found
                _channel.create_message("No terminating \\``` found.");
                return true;
            }

            code = std::string{ content.substr(n + addmore, nxt - n - 4 - addmore + 4) };
        }

        std::stringstream ss;

        if (code.find("main") == std::string::npos)
        {

            ss <<
R"(

#include <iostream>
#include <string>
#include <stdint.h>

int main()
{
)";
            ss << code << "return 0; }";

        }
        else
        {
            ss << code;
        }

        std::string basefilename = fmt::format("{}-{}", _member.get_id(), sd.message_id);
        std::string compilefilename = fmt::format("src/{}-e.cpp", basefilename);

        {
            std::fstream codefile(compilefilename, std::fstream::binary | std::fstream::out);

            if (!codefile.is_open())
            {
                _channel.create_message("Error opening file.");
                throw std::runtime_error("fstream::open failed!");
            }
            codefile << ss.str();
            codefile.close();
        }
        {
            std::fstream codefile("main.cpp", std::fstream::binary | std::fstream::out);

            if (!codefile.is_open())
            {
                _channel.create_message("Error opening file.");
                throw std::runtime_error("fstream::open failed!");
            }
            codefile << ss.str();
            codefile.close();
        }
        /*
?eval
```
std::cout << "Testing\n";
```
         */

        try
        {
            auto compile_start = std::chrono::steady_clock::now();
            std::string compileresult = exec("g++-7 -std=gnu++17 -O2 main.cpp -o main.out 2>&1");
            auto compile_count = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - compile_start).count();
            std::string result = exec(R"(if [ -f main.out ] ; then echo "SUCCESS" ; else echo "FAIL" ; fi)");

            if (result == "SUCCESS\n")
            {
                auto run_start = std::chrono::steady_clock::now();
                std::string runresult = exec("./main.out 2>&1");
                auto run_count = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - run_start).count();
                _channel.create_message(fmt::format("Result: compile({}ms) execution({}ms)\n```\n{}\n```", compile_count, run_count, runresult));
            }
            else
            {
                _channel.create_message(fmt::format("Failed to compile\n```\n{}\n```", compileresult));
            }
            exec("rm -rf main.*");
        }
        catch (std::exception & e)
        {
            bot.log->error("Exception: {}", e.what());
            return true;
        }
        return true;
    }
#endif

    if (toks[0] == "+memes")
    {
        json j = {
            { "op", 4 },
            {
                "d",
                {
                    { "guild_id", "81384788765712384" },
                    { "channel_id", "131937933270712320" },
                    { "self_mute", false },
                    { "self_deaf", false }
                }
            }
        };
        bot.get_shard_by_guild(81384788765712384).send(j.dump());
        do_log("Joined channel.");
        return false;
    }

    if (toks[0] == "-memes")
    {
        json j = {
            { "op", 4 },
            {
                "d",
                {
                    { "guild_id", "81384788765712384" },
                    { "channel_id", nullptr },
                    { "self_mute", false },
                    { "self_deaf", false }
                }
            }
        };
        bot.get_shard_by_guild(81384788765712384).send(j.dump());
        do_log("Left channel.");
        return false;
    }

/*
    if (toks[0] == "dm1")
    {
        if (check_params(toks, 2))
            return true;

        snowflake tar = obj.get_member().get_id();

        auto res = bot.get_rest_controller().execute("/users/@me/channels", fmt::format(R"({{ "recipient_id": "{}" }})", tar), "POST");
        bot.log->info("DM ch result:\n{}", res.content);
        auto js = json::parse(res.content);
        snowflake dm_id = js["id"];
        auto res2 = bot.get_rest_controller().execute(fmt::format("/channels/{}/messages", dm_id), R"({ "content": "test" })", "POST");
        bot.log->info("DM result:\n{}", res2.content);
    }*/
//     if (toks[0] == "dm2")
//     {
//         aegis::promise<aegis::rest::rest_reply> pr;
//         aegis::future<aegis::rest::rest_reply> tres = pr.get_future();
//         bot.log->info("Start DM1");
//         auto res1 = aegis::utility::perf_run("basic", [=, &tres, id = obj.get_member().get_id()]()
//         {
//             tres = bot.create_dm_message(id, "test message");
//         });
// 
//         tres.get();
// 
//         bot.log->info("Start DM2");
//         auto res2 = aegis::utility::perf_run("wait", [=, id = obj.get_member().get_id()]()
//         {
//             bot.create_dm_message(id, "test message").get();
//         });
//         bot.log->info("\nres1: {}res2: {}", res1, res2);
//     }
    if (toks[0] == "ws")
    {
        if (check_params(toks, 4))
            return true;

        try
        {
            if (toks[1] == "shard")
            {
                std::string tok3 = std::string{ toks[2] };
                int32_t snum = std::stoi(tok3);
                auto & s = bot.get_shard_by_id(snum);
                if (toks[3] == "connect")
                {
                    if (s.is_connected())
                    {
                        req_fail(obj.msg);
                        return true;
                    }
                    bot.get_shard_mgr().queue_reconnect(s);
                }
                else if (toks[3] == "forceconnect")
                {
                    if (s.is_connected())
                    {
                        req_fail(obj.msg);
                        return true;
                    }
                    s.connect();
                }
                else if (toks[3] == "close")
                {
                    bot.get_shard_mgr().close(s);
                }
            }
            req_success(obj.msg);
        }
        catch (std::exception & e)
        {
            do_log(fmt::format("Failed to execute websocket command [{}]", request));
        }

        return true;
    }

    if (toks[0] == "playing")
    {
        if (check_params(toks, 2))
            return true;

        std::string toset;

        if (toks[1] == "default")
            toset = bot.self_presence;
        else
            toset = std::string{ toks[1] };

        json j = {
            { "op", 3 },
            {
                "d",
                {
                    { "game",
                        {
                            { "name", toset },
                            { "type", 0 }
                        }
                    },
                    { "status", "online" },
                    { "since", json::value_t::null },
                    { "afk", false }
                }
            }
        };
        bot.send_all_shards(j);
        req_success(obj.msg);
        return true;
    }

    if (toks[0] == "voice")
    {
        json j = {
            { "op", 4 },
            {
                "d",
                {
                    { "guild_id", "287048029524066334" },
                    { "channel_id", "429457846569271317" },
                    { "self_mute", false },
                    { "self_deaf", false }
                }
            }
        };
        obj.shard.send(j.dump());
        req_success(obj.msg);
        return true;
    }

    if (toks[0] == "shardstats")
    {

        std::stringstream w;
        w << "```diff\n";

        uint64_t count = 0;
        count = bot.get_shard_transfer();

        struct guild_count_data
        {
            size_t guilds;
            size_t members;
        };

        std::vector<guild_count_data> shard_guild_c(bot.shard_max_count);

        for (auto & v : bot.guilds)
        {
            ++shard_guild_c[v.second->shard_id].guilds;
            shard_guild_c[v.second->shard_id].members += v.second->get_members().size();
        }

        w << fmt::format("  Total transfer: {} Memory usage: {}\n", aegis::utility::format_bytes(count), aegis::utility::format_bytes(aegis::utility::getCurrentRSS()));
        w << fmt::format("  shard#   sequence servers members            uptime last message transferred reconnects\n");
        w << fmt::format("  ------ ---------- ------- ------- ----------------- ------------ ----------- ----------\n");

        for (uint32_t i = 0; i < bot.shard_max_count; ++i)
        {
            auto & s = bot.get_shard_by_id(i);
            auto time_count = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - shards[s.get_id()].last_message).count();
            if (s.is_connected())
                w << '+';
            else
                w << '-';
            w << fmt::format(" {:6} {:10}    {:4} {:7}   {:>16} {:9}ms {:>11}        {:3}\n",
                             s.get_id(),
                             s.get_sequence(),
                             shard_guild_c[s.get_id()].guilds,
                             shard_guild_c[s.get_id()].members,
                             s.uptime_str(),
                             time_count,
                             s.get_transfer_str(),
                             s.counters.reconnects);
        }
        w << "```";
        _channel.create_message(w.str());
        return true;
    }

    if (toks[0] == "shardstats2")
    {

        std::stringstream w;
        w << "This shard: " << obj.shard.get_id() << "```diff\n";

        uint64_t count = 0;
        count = bot.get_shard_transfer();

        struct guild_count_data
        {
            size_t guilds;
            size_t members;
        };

        std::vector<guild_count_data> shard_guild_c(bot.shard_max_count);

        for (auto & v : bot.guilds)
        {
            ++shard_guild_c[v.second->shard_id].guilds;
            shard_guild_c[v.second->shard_id].members += v.second->get_members().size();
        }

        w << fmt::format("  Total transfer: {} Memory usage: {}\n", aegis::utility::format_bytes(count), aegis::utility::format_bytes(aegis::utility::getCurrentRSS()));
        w << fmt::format("  shard#   sequence servers members            uptime last message transferred reconnects\n");
        w << fmt::format("  ------ ---------- ------- ------- ----------------- ------------ ----------- ----------\n");

        for (uint32_t i = 0; i < bot.shard_max_count; ++i)
        {
            auto & s = bot.get_shard_by_id(i);
            auto time_count = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - bot.get_shard_mgr().get_shard(s.get_id()).lastwsevent).count();
            if (s.is_connected())
                w << '+';
            else
                w << '-';
            w << fmt::format(" {:6} {:10}    {:4} {:7}   {:>16} {:9}ms {:>11}        {:3}\n",
                             s.get_id(),
                             s.get_sequence(),
                             shard_guild_c[s.get_id()].guilds,
                             shard_guild_c[s.get_id()].members,
                             s.uptime_str(),
                             time_count,
                             s.get_transfer_str(),
                             s.counters.reconnects);
        }
        w << "```";
        _channel.create_message(w.str());
        return true;
    }

//     if (toks[0] == "shardstats2")
//     {
//         std::stringstream w;
//         w << "```diff\n";
// 
//         uint64_t count = 0, count_u = 0;
//         for (auto & s : bot.shards)
//         {
//             count += s->get_transfer();
//             count_u += s->get_transfer_u();
//         }
// 
//         std::vector<uint32_t> shard_guild_c(obj.bot->shards.size());
// 
//         for (auto & v : obj.bot->guilds)
//         {
//             ++shard_guild_c[v.second->shard_id];
//         }
// 
//         w << "Total transfer(" << format_bytes(count) << ") transfer_u(" << format_bytes(count_u) << ") (" << fmt::format("{:2.3f}", ((double(count) / double(count_u)) * 100)) << "%)\n";
//         for (auto & s : bot.shards)
//         {
//             if (s->is_connected())
//                 w << '+';
//             else
//                 w << '-';
//             w << fmt::format(" Shard#{:2}: guilds({:4}) seq({:10}) transfer({:>11}) transfer_u({:>11}) reconnects({})\n", s->get_id(), shard_guild_c[s->get_id()], s->get_sequence(), s->get_transfer_str(), s->get_transfer_u_str(), s->counters.reconnects);
//         }
//         w << "```";
//         _channel.create_message(w.str());
//         return true;
//     }

    if (toks[0] == "shard")
    {
        _channel.create_message(fmt::format("I am shard#[{}]", obj.shard.get_id()));
        return true;
    }

//     if (toks[0] == "shards")
//     {
//         std::string s = "```\n";
// 
//         struct guild_count_data
//         {
//             size_t guilds;
//             size_t channels;
//             size_t members;
//         };
// 
//         std::vector<guild_count_data> shard_guild_c(obj.bot->shards.size());
// 
//         for (auto & v : obj.bot->guilds)
//         {
//             ++shard_guild_c[v.second->shard_id].guilds;
//             shard_guild_c[v.second->shard_id].channels += v.second->channels.size();
//             shard_guild_c[v.second->shard_id].members += v.second->members.size();
//         }
// 
//         for (auto & shard : obj.bot->shards)
//         {
//             s += fmt::format("shard#[{:4}] tracking {:4} guilds {:5} channels {:7} members {:9} messages {:10} presence\n", shard->get_id(), shard_guild_c[shard->get_id()].guilds, shard_guild_c[shard->get_id()].channels, shard_guild_c[shard->get_id()].members, shard->counters.messages, shard->counters.presence_changes);
//         }
//         s += "```";
//         _channel.create_message(s);
//         return true;
//     }

    if (toks[0] == "reloadguild")
    {
        if (toks.size() < 2)
        {
            load_guild(guild_id, g_data);
            req_success(obj.msg);
            return true;
        }

        std::string gsn{ toks[1] };
        snowflake g_sn = std::stoll(gsn);
        if (g_sn)
        {
            auto g_data2 = find_guild(g_sn);
			if (!g_data2) return true;
            load_guild(g_sn, *g_data2);
            req_success(obj.msg);
            return true;
        }
        req_fail(obj.msg);
    }

    //exclusive bot owner admin level commands
    if (toks[0] == "!!")
    {
        if (toks.size() > 1)
        {
            snowflake target = get_snowflake(toks[1], _guild);
            ignored.emplace(target);
            sadd({ fmt::format("{}:ignored", r_bot_config), std::to_string(target) });
            req_success(obj.msg);
        }
        return true;
    }
    if (toks[0] == "--")
    {
        if (toks.size() > 1)
        {
            snowflake target = get_snowflake(toks[1], _guild);
            ignored.erase(target);
            srem({ fmt::format("{}:ignored", r_bot_config), std::to_string(target) });
            req_success(obj.msg);
        }
        return true;
    }

    if (toks[0] == "wsdbg")
    {
        bot.wsdbg = !bot.wsdbg;
        req_success(obj.msg);
        return true;
    }

    if (toks[0] == "voicedbg")
    {
        voice_debug = !voice_debug;
        req_success(obj.msg);
        return true;
    }

    if (toks[0] == "global_override")
    {
        if (check_params(toks, 2, "global_override [on|off]"))
            return true;

        //matched
        if (toks[1] == "on" || toks[1] == "true")
        {
            for (auto & gd : guild_data)
            {
                for (auto & m : bot_modules)
                    toggle_mod_override(gd.second, m.first, true);
            }
        }
        else if (toks[1] == "off" || toks[1] == "false")
        {
            for (auto & gd : guild_data)
            {
                for (auto & m : bot_modules)
                    toggle_mod_override(gd.second, m.first, true);
            }
        }
        return true;
    }
    if (toks[0] == "override")
    {
        if (check_params(toks, 3, "override guild_id [module] [on|off]"))
            return true;
        snowflake g_id = std::stoull(std::string{ toks[1] });
        auto gd = find_guild(g_id);
		if (!gd) return true;
        if (toks[2] == "on" || toks[2] == "true")
        {
            for (auto & m : bot_modules)
                toggle_mod_override(g_data, m.first, true);
            _channel.create_message(fmt::format("Full Override enabled on [{}]", g_id));
            return true;
        }
        else if (toks[2] == "off" || toks[2] == "false")
        {
            for (auto & m : bot_modules)
                toggle_mod_override(g_data, m.first, false);
            _channel.create_message(fmt::format("Full Override disabled on [{}]", g_id));
            return true;
        }
        else
        {
            if (check_params(toks, 3, "override guild_id module [on|off]"))
                return true;
            
            auto mod = get_module(toks[2]);
            if (mod == MAX_MODULES)
            {
                _channel.create_message(fmt::format("Module `{}` does not exist.", toks[2]));
                return true;
            }

            if (toks[2] == "auction")
            {
                if (toks[3] == "on" || toks[3] == "true")
                    toggle_mod_override(g_data, mod, true);
                else if (toks[3] == "off" || toks[3] == "false")
                    toggle_mod_override(g_data, mod, false);
            }
            else if (toks[2] == "music")
            {
                if (toks[3] == "on" || toks[3] == "true")
                    toggle_mod_override(g_data, mod, true);
                else if (toks[3] == "off" || toks[3] == "false")
                    toggle_mod_override(g_data, mod, false);
            }
            _channel.create_message(fmt::format("Single Override enabled for [{}] on [{}]", toks[2], g_id));
            return true;
        }
        return true;
    }
    if (toks[0] == "global_enmod")
    {
        if (check_params(toks, 2))
            return true;

        auto mod = get_module(toks[1]);
        if (mod == MAX_MODULES)
        {
            _channel.create_message(fmt::format("Module `{}` does not exist.", toks[2]));
            return true;
        }

        for (auto & gd : guild_data)
        {
            for (auto & m : bot_modules)
            {
                toggle_mod(gd.second, m.first, true);
                gd.second._modules[m.first]->load(*this);
            }
        }
        _channel.create_message(fmt::format("{} globally disabled.", bot_modules[mod].name));
        return true;
    }
    if (toks[0] == "global_dismod")
    {
        if (check_params(toks, 2))
            return true;

        auto mod = get_module(toks[1]);
        if (mod == MAX_MODULES)
        {
            _channel.create_message(fmt::format("Module `{}` does not exist.", toks[2]));
            return true;
        }

        for (auto & gd : guild_data)
        {
            for (auto & m : bot_modules)
                toggle_mod(gd.second, m.first, false);
        }
        _channel.create_message(fmt::format("{} globally disabled.", bot_modules[mod].name));
        return true;
    }
    if (toks[0] == "admin_enmod")
    {
        if (check_params(toks, 2))
            return true;

        auto mod = get_module(toks[1]);
        if (mod == MAX_MODULES)
        {
            _channel.create_message(fmt::format("Module `{}` does not exist.", toks[2]));
            return true;
        }

        for (auto & it : bot_modules)
        {
            if (it.second.name == toks[1])
            {
                //matched
                toggle_mod(g_data, it.first, true);
                g_data._modules[it.first]->load(*this);
                _channel.create_message(fmt::format("{} enabled.", bot_modules[it.first].name));
                return true;
            }
        }
        return true;
    }
    if (toks[0] == "admin_dismod")
    {
        if (check_params(toks, 2))
            return true;

        auto mod = get_module(toks[1]);
        if (mod == MAX_MODULES)
        {
            _channel.create_message(fmt::format("Module `{}` does not exist.", toks[2]));
            return true;
        }

        for (auto & it : bot_modules)
        {
            if (it.second.name == toks[1])
            {
                //matched
                toggle_mod(g_data, it.first, false);
                _channel.create_message(fmt::format("{} disabled.", bot_modules[it.first].name));
                return true;
            }
        }
        return true;
    }
    if (toks[0] == "redis")
    {
        if (toks[1] == "put")
        {
            if (check_params(toks, 4))
                return true;
            put(toks[2], toks[3]);
            return true;
        }
        if (toks[1] == "get")
        {
            if (check_params(toks, 3))
                return true;
            std::string res = get(toks[2]);
            _channel.create_message(fmt::format("Result: {}", res));
            return true;
        }
        if (toks[1] == "hset")
        {
            if (check_params(toks, 5))
                return true;
            hset(toks[2], { std::string{toks[3]}, std::string{ toks[4] } }, g_data);
            obj.msg.create_reaction("success:429554838083207169");
            return true;
            obj.msg.create_reaction("fail:429554869611921408");
        }
        if (toks[1] == "hget")
        {
            if (check_params(toks, 3))
                return true;
            std::string res;
            if (toks.size() > 3)
                res = hget({ std::string{ toks[2] }, { std::string{ toks[3] } } });
            else
                res = hget({ g_data.redis_prefix, { std::string{ toks[2] } } });

            _channel.create_message(fmt::format("Result: {}", res));
            return true;
        }
        if (toks[1] == "hgetall")
        {
            if (check_params(toks, 2))
                return true;
            std::string key = "";
            if (toks.size() > 2)
            {
                if (toks[2] == "this")
                    key = g_data.redis_prefix;
                else
                    key = std::string{ toks[2] };
            }
            else
                key = fmt::format("{}:{}", g_data.redis_prefix, toks[2]);
            std::unordered_map<std::string, std::string> res = get_array(key);
            std::stringstream w;
            w << "Results of " << key << '\n';
            for (const auto&[k, v] : res)
            {
                w << '\n' << k << " - " << v;
            }
            _channel.create_message(w.str());
            return true;
        }
        if (toks[1] == "getarray")
        {
            if (check_params(toks, 3))
                return true;
            std::vector<std::string> res = get_vector(toks[2]);
            std::stringstream w;
            w << "Results of " << toks[2] << '\n';
            for (const auto & k : res)
            {
                w << '\n' << k;
            }
            _channel.create_message(w.str());
            return true;
        }
        if (toks[1] == "r")
        {
            if (check_params(toks, 3))
                return true;
            std::vector<std::string_view> s_toks = toks;
            s_toks.erase(s_toks.begin());
            s_toks.erase(s_toks.begin());
            std::string_view cmd = s_toks[0];
            s_toks.erase(s_toks.begin());
            std::vector<std::string> res = run_v(cmd, s_toks);
            fmt::MemoryWriter w;
            w << "Results of " << cmd << "\n```";
            for (const auto & k : res)
            {
                w << '\n' << k;
            }
            w << "```";
            if (w.size() > 1950)
            {
                _channel.create_message(w.str().substr(0, 1950) + "```");
                _channel.create_message("```" + w.str().substr(1950));
            }
            else
                _channel.create_message(w.str());
            return true;
        }
        if (toks[1] == "raw")
        {
            if (check_params(toks, 4))
                return true;
            std::string res = run(std::string{ toks[2] }, { toks[3] });
            _channel.create_message(fmt::format("Result: {}", res));
            return true;
        }
        return true;
    }
    if (toks[0] == "exit")
    {
        bot.find_channel(channel_id)->create_message("exiting...");
        bot.shutdown();
        return true;
    }
    if (toks[0] == "testexit")
    {
        bot.find_channel(channel_id)->create_message("exiting...");
        bot.shutdown();
        return true;
    }
    if (toks[0] == "role_count")
    {
        std::reference_wrapper<guild> _g = _channel.get_guild();
        if (toks.size() > 1)
        {
            _g = *bot.find_guild(std::stoll(std::string(toks[1])));
            if (&_g.get() == nullptr)
            {
                _channel.create_message("I am not in that guild.");
                return true;
            }
        }
        std::unordered_map<std::string, int32_t> count;
        guild & _guild = _g.get();
        std::cout << "Guild found: " << _guild.get_name() << '\n';
        for (const auto & r : _guild.get_roles())
        {
            count[r.second.name] = 0;
            for (const auto & u : _guild.get_members())
                if (_guild.member_has_role(u.second->get_id(), r.second.role_id))
                    ++count[r.second.name];
        }

        std::stringstream w;
        w << "```cpp\n";
        for (auto o : count)
            w << o.first << " : " << o.second << '\n';
        w << "```";
        std::cout << w.str();
        _channel.create_message(w.str());
        return true;
    }
//     if (toks[0] == "addrole")
//     {
//         _channel.get_guild().add_guild_member_role(_member.get_id(), std::stoull(std::string{ toks[1] })).then([&](aegis::gateway::objects::role _role) mutable
//         {
//             _channel.create_message(fmt::format("Add role : {} {}", _role.name, _role.role_id));
//         });
//         return true;
//     }
//     if (toks[0] == "remrole")
//     {
//         _channel.get_guild().remove_guild_member_role(_member.get_id(), std::stoull(std::string{ toks[1] })).then([&](aegis::rest::rest_reply res) mutable
//         {
//             _channel.create_message(fmt::format("Remove role : {} {}", res.reply_code, res.content));
//         });
//         
//         return true;
//     }
    if (toks[0] == "server")
    {
        // TODO: verify whether this is valid - verified to work. get second opinion on whether valid
        std::reference_wrapper<guild> _g = _channel.get_guild();
        if (toks.size() > 1)
        {
            _g = *bot.find_guild(std::stoll(std::string(toks[1])));
            if (&_g.get() == nullptr)
            {
                _channel.create_message("I am not in that guild.");
                return true;
            }
        }
        fmt::MemoryWriter w;

        guild & _guild = _g.get();

        w << "```";
        w.write("  Server: {}\n", _guild.get_name());
        w.write("   Shard: {}\n", _guild.shard_id);
        w.write("      ID: {}\n", _guild.guild_id);
        w.write("  Region: {}\n", _guild.get_region());
        w.write(" Members: {}\n", _guild.get_members().size());
        w.write("Channels: {}\n", _guild.get_channels().size());
        uint32_t bot_count = 0;
        std::shared_lock<std::shared_mutex> l(_guild.mtx());
        for (auto gm : _guild.get_members())
            if (gm.second->is_bot())
                ++bot_count;
        w.write("    Bots: {}\n", bot_count);
        auto _member = bot.find_user(_guild.get_owner());
        w.write("   Owner: {} ({})\n", _member->get_full_name(), _member->get_id());

        std::chrono::system_clock::time_point starttime = std::chrono::system_clock::from_time_t(_guild.guild_id.get_time() / 1000);
        std::chrono::time_point<std::chrono::system_clock> now_t = std::chrono::system_clock::now();
        auto time_is = now_t - starttime;

        using seconds = std::chrono::duration<int, std::ratio<1, 1>>;
        using minutes = std::chrono::duration<int, std::ratio<60, 1>>;
        using hours = std::chrono::duration<int, std::ratio<3600, 1>>;
        using days = std::chrono::duration<int, std::ratio<24 * 3600, 1>>;
        using months = std::chrono::duration<int, std::ratio<24 * 3600 * 30, 1>>;
        using years = std::chrono::duration<int, std::ratio<24 * 3600 * 365, 1>>;

        std::stringstream ss;
        auto y = std::chrono::duration_cast<years>(time_is).count();
        time_is -= years(y);
        auto mo = std::chrono::duration_cast<months>(time_is).count();
        time_is -= months(mo);
        auto d = std::chrono::duration_cast<days>(time_is).count();
        time_is -= days(d);
        auto h = std::chrono::duration_cast<hours>(time_is).count();
        time_is -= hours(h);
        auto m = std::chrono::duration_cast<minutes>(time_is).count();
        time_is -= minutes(m);
        auto s = std::chrono::duration_cast<seconds>(time_is).count();

        if (y)
            ss << y << "y ";
        if (mo)
            ss << mo << "mo ";
        if (d)
            ss << d << "d ";
        if (h)
            ss << h << "h ";
        if (m)
            ss << m << "m ";
        ss << s << "s ";


        w.write(" Created: {}ago\n", ss.str());
        w.write("   Roles: {}\n", _guild.get_roles().size());
        std::stringstream r;
        for (auto & _role : _guild.get_roles())
            r << _role.second.name << ", ";
        std::string t = r.str();
        w << t.substr(0, t.size() - 2);
        w << "```";

        if (w.size() > 1900)
        {
            _channel.create_message(w.str().substr(0, 1900) + "```");
            _channel.create_message("```" + w.str().substr(1900));
        }
        else
            _channel.create_message(w.str());
        return true;
    }
//     if (toks[0] == "channellist")
//     {
//         if (check_params(toks, 2, "channellist `guildid`"))
//             return true;
// 
//         snowflake s_g = std::stoll(std::string{ toks[1] });
// 
//         auto g = bot.get_guild(s_g);
// 
//         if (g == nullptr)
//         {
//             obj.msg.create_reaction("fail:429554869611921408");
//             return true;
//         }
// 
//         embed e;
//         e.set_color(10599460);
//         e.set_title("Channel List");
//         e.add_field(" ", " ", true);
// 
//         int i = g->channels.size() % 20;
// 
//         std::stringstream r;
//         for (auto & c : g->channels)
//         {
//             auto & ch = *c.second;
//             r << "`[" << ch.get_id() << "]`\n`" << ch.get_name() << "`\n";
//         }
// 
//         for (auto & _role : _guild.roles)
//             r << _role.second.name << ", ";
//         std::string t = r.str();
//         w << t.substr(0, t.size() - 2);
//         w << "```";
// 
// 
//         if (auto r = _channel.create_message_embed("", e).get(); !r)
//             obj.msg.create_reaction("fail:429554869611921408");
//         return true;
// 
//         
//         if (w.size() > 1900)
//         {
//             _channel.create_message(w.str().substr(0, 1900) + "```");
//             _channel.create_message("```" + w.str().substr(1900));
//         }
//         else
//             _channel.create_message(w.str());
//         return;
//     }
    if (toks[0] == "serverlist")
    {
        std::stringstream w;
        for (auto & g : bot.guilds)
        {
            auto gld = g.second.get();
            w << "`[" << gld->guild_id << "]`  :  `" << gld->get_name() << "`\n";
        }

        json t = {
            { "title", "Server List" },
            { "description", w.str() },
            { "color", 10599460 }
        };
        _channel.create_message_embed("", t);
        return true;
    }
    if (toks[0] == "ping")
    {
        std::unique_lock<std::mutex> lk(m_ping_test);
        checktime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        
        auto msg = _channel.create_message("Pong", checktime).get();
        std::string to_edit = fmt::format("Ping reply: REST [{}ms]", (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() - checktime));
        msg.edit(to_edit);
        if (cv_ping_test.wait_for(lk, 20s) == std::cv_status::no_timeout)
            msg.edit(fmt::format("{} WS [{}ms]", to_edit, ws_checktime));
        else
            msg.edit(fmt::format("{} WS [timeout20s]", to_edit));
        return true;
    }
    if (toks[0] == "channellist")
    {
        if (check_params(toks, 2, "channellist `guildid`"))
            return true;

        snowflake s_g = std::stoll(std::string{ toks[1] });

        auto g = bot.find_guild(s_g);

        if (g == nullptr)
        {
            obj.msg.create_reaction("fail:429554869611921408");
            return true;
        }

        std::stringstream w;
        std::shared_lock<std::shared_mutex> l(_guild.mtx());
        for (auto & c : g->get_channels())
        {
            auto & ch = *c.second;
            w << "`[" << ch.get_id() << "]`  :  `" << ch.get_name() << "`\n";
        }

        json t = {
            { "title", "Channel List" },
            { "description", w.str() },
            { "color", 10599460 }
        };

//         if (auto r = _channel.create_message_embed("", t).get(); !r)
//             obj.msg.create_reaction("fail:429554869611921408");
        return true;
    }
    if (toks[0] == "peek")
    {
        if (check_params(toks, 2, "peek {+|-|clear}`channelid`"))
            return true;

        if (toks[1][0] == '+')
        {
            toks[1].remove_prefix(1);
            peeks.insert(std::stoll(std::string{ toks[1] }));
        }
        else if (toks[1][0] == '-')
        {
            toks[1].remove_prefix(1);
            peeks.erase(std::stoll(std::string{ toks[1] }));
        }
        else if (toks[1] == "clear")
        {
            peeks.clear();
        }
        else
        {
            obj.msg.create_reaction("fail:429554869611921408");
            return true;
        }
        obj.msg.create_reaction("success:429554838083207169");
        return true;
    }
    if (toks[0] == "say")
    {
        if (check_params(toks, 3, "say `channelid` message"))
            return true;

        snowflake s_ch = std::stoll(std::string{ toks[1] });
        auto ch = bot.find_channel(s_ch);

        if (ch == nullptr)
        {
            obj.msg.create_reaction("fail:429554869611921408");
            return true;
        }
        
//         if (auto reply = ch->create_message( toks[2].data()).get(); !reply)
//             _channel.create_message(fmt::format("Unable to send message http:{} reason: {}", static_cast<int32_t>(reply.reply_code), reply.content));
//         else
//             obj.msg.create_reaction("success:429554838083207169");
            
        //obj.msg.create_reaction("<:success:429554838083207169>");
        //obj.msg.create_reaction("<:fail:429554869611921408>");
        
        return true;
    }
    if (toks[0] == "createguild")
    {
        if (check_params(toks, 2, "createguild name"))
            return true;

        bot.create_guild(std::string{ toks[1] }).then([this, sd = sd](aegis::gateway::objects::guild && g)
        {
            json obj;
            obj["max_age"] = 86400;
            obj["max_uses"] = 0;
            obj["temporary"] = false;
            obj["unique"] = true;
            aegis::rest::request_params && rp = aegis::rest::request_params{ fmt::format("/channels/{}/invites", g.guild_id), aegis::rest::Post, obj.dump() };
            auto r_create = _bot->call(std::move(rp));
            if (auto r_create = _bot->call({ fmt::format("/channels/{}/invites", g.guild_id), aegis::rest::Post, obj.dump() }); r_create)
            {
                json i_create = json::parse(r_create.content);
                sd.channel.create_message(fmt::format("Guild: [{}:{}] Channel: [{}:#{}] https://discord.gg/{}",
                                                    i_create["guild"]["id"].get<std::string>(), i_create["guild"]["name"].get<std::string>(),
                                                    i_create["channel"]["id"].get<std::string>(), i_create["channel"]["name"].get<std::string>(),
                                                    i_create["code"].get<std::string>()));
            }
            else
                sd.channel.create_message("Guild made but no invite.");
        });
        return true;
    }
    if (toks[0] == "rinvite")
    {
        if (check_params(toks, 2, "rinvite guild_id"))
            return true;

        auto g = bot.find_guild(std::stoull(std::string{ toks[1] }));
        if (g == nullptr)
        {
            _channel.create_message("I am not in that guild");
            return true;
        }
        if (g->get_channels().empty())
        {
            _channel.create_message("Guild has no channels.");
            return true;
        }
        for (auto & _ch : g->get_channels())
        {
            if (_ch.second->get_type() == aegis::gateway::objects::channel::channel_type::Text)
            {
                _ch.second->create_channel_invite(0, 1, false, true).then([&](aegis::rest::rest_reply reply)
                {
                    json r = json::parse(reply.content);
                    _channel.create_message(fmt::format("Guild: [{}:{}] Channel: [{}:#{}] https://discord.gg/{}",
                                                        r["guild"]["id"].get<std::string>(), r["guild"]["name"].get<std::string>(),
                                                        r["channel"]["id"].get<std::string>(), r["channel"]["name"].get<std::string>(),
                                                        r["code"].get<std::string>()));
                });
                break;
            }
        }

        return true;
    }
    if (toks[0] == "leave")
    {
        if (check_params(toks, 2, "leave guild_id"))
            return true;

        auto g = bot.find_guild(std::stoull(std::string{ toks[1] }));
        if (g == nullptr)
        {
            _channel.create_message("I am not in that guild");
            return true;
        }
        if (auto reply = g->leave().get(); !reply)
            obj.msg.create_reaction("fail:429554869611921408");
        else
            obj.msg.create_reaction("success:429554838083207169");
        return true;
    }
    if (toks[0] == "deleteguild")
    {
        if (check_params(toks, 2, "deleteguild guild_id"))
            return true;

        auto g = bot.find_guild(std::stoull(std::string{ toks[1] }));
        if (g == nullptr)
        {
            _channel.create_message("I am not in that guild");
            return true;
        }
        if (auto reply = g->delete_guild().get(); !reply)
            obj.msg.create_reaction("fail:429554869611921408");
        else
            obj.msg.create_reaction("success:429554838083207169");
        return true;
    }
    if (toks[0] == "slow")
    {
        uint32_t num = 0;
        if (toks.size() >= 2)
            num = std::stoul(std::string{ toks[1] });
        _channel.modify_channel(aegis::modify_channel_t().rate_limit_per_user(num));
        obj.msg.create_reaction("success:429554838083207169");
    }
    if (toks[0] == "new")
    {
        using field = aegis::gateway::objects::field;
        using embed = aegis::gateway::objects::embed;
        _channel.create_message_embed({}, embed().color(rand())
                                      .description("description")
                                      .title("title")
                                      .fields({
                                                  field("field1", "v1"),
                                                  field("field2", "v2"),
                                                  field("field3", "v3")
                                              }));
        
    }
    if (toks[0] == "async_wait")
    {
        auto f = async([&]()
        {
            std::this_thread::sleep_for(2s);
            do_log(fmt::format("Test log int {}", 1));
            return 1;
        }).then([&](auto i) { do_log(fmt::format("Test log int {}", ++i)); return i;
        }).then([&](auto i) { do_log(fmt::format("Test log int {}", ++i)); return i;
        }).then([&](auto i) { do_log(fmt::format("Test log int {}", ++i)); return i;
        }).then([&](auto i) { do_log(fmt::format("Test log int {}", ++i)); return i;
        }).then([&](auto i) { do_log(fmt::format("Test log int {}", ++i)); return i;
        });

        auto g = async([&]()
        {
            std::this_thread::sleep_for(2s);
            do_log("Test log void 1");
        }).then([&]() { do_log("Test log void 2");
        }).then([&]() { do_log("Test log void 3");
        }).then([&]() { do_log("Test log void 4");
        }).then([&]() { do_log("Test log void 5");
        }).then([&]() { do_log("Test log void 6");
        }).then([&]() { do_log("Test log void 7");
        });

        std::string s = fmt::format("Retrieved before function exit : {}", f.get());
        do_log(s);
        _channel.create_message(s);
    }
    if (toks[0] == "async_nowait")
    {
        auto f = async([&]()
        {
            std::this_thread::sleep_for(2s);
            do_log(fmt::format("Test log int {}", 1));
            return 1;
        }).then([&](auto i) { do_log(fmt::format("Test log int {}", ++i)); return i;
        }).then([&](auto i) { do_log(fmt::format("Test log int {}", ++i)); return i;
        }).then([&](auto i) { do_log(fmt::format("Test log int {}", ++i)); return i;
        }).then([&](auto i) { do_log(fmt::format("Test log int {}", ++i)); return i;
        }).then([&](auto i) { do_log(fmt::format("Test log int {}", ++i)); return i;
        });

        auto g = async([&]()
        {
            std::this_thread::sleep_for(2s);
            do_log("Test log void 1");
        }).then([&]() { do_log("Test log void 2");
        }).then([&]() { do_log("Test log void 3");
        }).then([&]() { do_log("Test log void 4");
        }).then([&]() { do_log("Test log void 5");
        }).then([&]() { do_log("Test log void 6");
        }).then([&]() { do_log("Test log void 7");
        });
    }
#if !defined(AEGIS_MSVC)
    if (toks[0] == "bash_r")
    {
        /*
curl \
-H "Authorization: Bot $token" \
https://discordapp.com/api/users/1
{"code": 10013, "message": "Unknown User"}
         **/

        try
        {
            std::string torun{ content.data() + toks[0].size() + 1 };
            std::string result = exec(string_replace(admin_string_replace(torun, sd), sd));



            _channel.create_message(fmt::format("Result: ```\n{}\n```", result));
        }
        catch (std::exception & e)
        {
            //bot.log->error("Exception: {}", e.what());
            _channel.create_message("Exception");
            bot.log->error("Exception: {}", e.what());
            return true;
        }
        return true;
    }
#endif
    if (toks[0] == "test_replacements")
    {
        if (toks.size() <= 1)
            return true;

        bot.log->info("Replacements: {}", string_replace(admin_string_replace(toks[1].data(), sd), sd));
        return true;
    }

    if (toks[0] == "gperms2")
    {
        if (check_params(toks, 2))
            return true;

        auto _guild = bot.find_guild(std::stoll(std::string(toks[1])));

        if (_guild == nullptr)
        {
            _channel.create_message("Not in that guild");
            return true;
        }

        aegis::permission perm = _guild->base_permissions(*_guild->self());

        json msg =
        {
            { "title", "AegisBot" },
            { "description", fmt::format("Perms for [{}] : [{}]", _guild->get_name(), _guild->guild_id) },
            { "color", rand() % 0xFFFFFF },//10599460 },
            { "fields",
            json::array(
                {
                    { { "name", "canInvite" },{ "value", fmt::format("{}", perm.can_invite()) },{ "inline", true } },
                    { { "name", "canKick" },{ "value", fmt::format("{}", perm.can_kick()) },{ "inline", true } },
                    { { "name", "canBan" },{ "value", fmt::format("{}", perm.can_ban()) },{ "inline", true } },
                    { { "name", "isAdmin" },{ "value", fmt::format("{}", perm.is_admin()) },{ "inline", true } },
                    { { "name", "canManageChannels" },{ "value", fmt::format("{}", perm.can_manage_channels()) },{ "inline", true } },
                    { { "name", "canManageGuild" },{ "value", fmt::format("{}", perm.can_manage_guild()) },{ "inline", true } },
                    { { "name", "canAddReactions" },{ "value", fmt::format("{}", perm.can_add_reactions()) },{ "inline", true } },
                    { { "name", "canViewAuditLogs" },{ "value", fmt::format("{}", perm.can_view_audit_logs()) },{ "inline", true } },
                    { { "name", "canReadMessages" },{ "value", fmt::format("{}", perm.can_read_messages()) },{ "inline", true } },
                    { { "name", "canSendMessages" },{ "value", fmt::format("{}", perm.can_send_messages()) },{ "inline", true } },
                    { { "name", "canTTS" },{ "value", fmt::format("{}", perm.can_tts()) },{ "inline", true } },
                    { { "name", "canManageMessages" },{ "value", fmt::format("{}", perm.can_manage_messages()) },{ "inline", true } },
                    { { "name", "canEmbed" },{ "value", fmt::format("{}", perm.can_embed()) },{ "inline", true } },
                    { { "name", "canAttachFiles" },{ "value", fmt::format("{}", perm.can_attach_files()) },{ "inline", true } },
                    { { "name", "canReadHistory" },{ "value", fmt::format("{}", perm.can_read_history()) },{ "inline", true } },
                    { { "name", "canMentionEveryone" },{ "value", fmt::format("{}", perm.can_mention_everyone()) },{ "inline", true } },
                    { { "name", "canExternalEmoiji" },{ "value", fmt::format("{}", perm.can_external_emoiji()) },{ "inline", true } },
                    { { "name", "canChangeName" },{ "value", fmt::format("{}", perm.can_change_name()) },{ "inline", true } },
                    { { "name", "canManageNames" },{ "value", fmt::format("{}", perm.can_manage_names()) },{ "inline", true } },
                    { { "name", "canManageRoles" },{ "value", fmt::format("{}", perm.can_manage_roles()) },{ "inline", true } },
                    { { "name", "canManageWebhooks" },{ "value", fmt::format("{}", perm.can_manage_webhooks()) },{ "inline", true } },
                    { { "name", "canManageEmojis" },{ "value", fmt::format("{}", perm.can_manage_emojis()) },{ "inline", true } },
                    { { "name", "canMentionEveryone" },{ "value", fmt::format("{}", perm.can_mention_everyone()) },{ "inline", true } },
                    { { "name", "canVoiceConnect" },{ "value", fmt::format("{}", perm.can_voice_connect()) },{ "inline", true } },
                    { { "name", "canVoiceMute" },{ "value", fmt::format("{}", perm.can_voice_mute()) },{ "inline", true } },
                    { { "name", "canVoiceSpeak" },{ "value", fmt::format("{}", perm.can_voice_speak()) },{ "inline", true } },
                    { { "name", "canVoiceDeafen" },{ "value", fmt::format("{}", perm.can_voice_deafen()) },{ "inline", true } },
                    { { "name", "canVoiceMove" },{ "value", fmt::format("{}", perm.can_voice_move()) },{ "inline", true } },
                    { { "name", "canVoiceActivity" },{ "value", fmt::format("{}", perm.can_voice_activity()) },{ "inline", true } }
                })
            }
        };
        _channel.create_message_embed({}, msg);
        return true;
    }

    if (toks[0] == "gperms")
    {
        if (check_params(toks, 2))
            return true;

        auto _guild = bot.find_guild(std::stoll(std::string(toks[1])));

        if (_guild == nullptr)
        {
            _channel.create_message("Not in that guild");
            return true;
        }

        aegis::permission perm = _guild->base_permissions(*_guild->self());

        std::stringstream w;

        w << fmt::format("Perms for [{}] : [{}]\n", _guild->get_name(), _guild->guild_id);
        if (perm.is_admin())
            w << "```\nisAdmin: true\n```";
        else
        {
            w << "```\n";
            w << fmt::format("canInvite: {}\n", perm.can_invite());
            w << fmt::format("canKick: {}\n", perm.can_kick());
            w << fmt::format("canBan: {}\n", perm.can_ban());
            w << fmt::format("isAdmin: {}\n", perm.is_admin());
            w << fmt::format("canManageChannels: {}\n", perm.can_manage_channels());
            w << fmt::format("canManageGuild: {}\n", perm.can_manage_guild());
            w << fmt::format("canAddReactions: {}\n", perm.can_add_reactions());
            w << fmt::format("canViewAuditLogs: {}\n", perm.can_view_audit_logs());
            w << fmt::format("canReadMessages: {}\n", perm.can_read_messages());
            w << fmt::format("canSendMessages: {}\n", perm.can_send_messages());
            w << fmt::format("canTTS: {}\n", perm.can_tts());
            w << fmt::format("canManageMessages: {}\n", perm.can_manage_messages());
            w << fmt::format("canEmbed: {}\n", perm.can_embed());
            w << fmt::format("canAttachFiles: {}\n", perm.can_attach_files());
            w << fmt::format("canReadHistory: {}\n", perm.can_read_history());
            w << fmt::format("canMentionEveryone: {}\n", perm.can_mention_everyone());
            w << fmt::format("canExternalEmoiji: {}\n", perm.can_external_emoiji());
            w << fmt::format("canChangeName: {}\n", perm.can_change_name());
            w << fmt::format("canManageNames: {}\n", perm.can_manage_names());
            w << fmt::format("canManageRoles: {}\n", perm.can_manage_roles());
            w << fmt::format("canManageWebhooks: {}\n", perm.can_manage_webhooks());
            w << fmt::format("canManageEmojis: {}\n", perm.can_manage_emojis());
            w << fmt::format("canVoiceConnect: {}\n", perm.can_voice_connect());
            w << fmt::format("canVoiceMute: {}\n", perm.can_voice_mute());
            w << fmt::format("canVoiceSpeak: {}\n", perm.can_voice_speak());
            w << fmt::format("canVoiceDeafen: {}\n", perm.can_voice_deafen());
            w << fmt::format("canVoiceMove: {}\n", perm.can_voice_move());
            w << fmt::format("canVoiceActivity: {}\n", perm.can_voice_activity());
            w << "```";
        }

        _channel.create_message(w.str());
        return true;
    }

    if (toks[0] == "cperms")
    {
        if (check_params(toks, 2))
            return true;

        auto _ch = bot.find_channel(std::stoll(std::string(toks[1])));

        if (_ch == nullptr)
        {
            _channel.create_message("Not in that channel");
            return true;
        }

        aegis::permission perm = _ch->perms();

        std::stringstream w;

        w << fmt::format("Perms for [{}] : [{}]\n", _ch->get_guild().get_name(), _ch->get_guild().guild_id);
        if (perm.is_admin())
            w << "```\nisAdmin: true\n```";
        else
        {
            w << "```\n";

            if (_ch->get_type() == aegis::gateway::objects::channel::Text)
            {
                w << fmt::format("canInvite: {}\n", perm.can_invite());
                w << fmt::format("canManageChannels: {}\n", perm.can_manage_channels());
                w << fmt::format("canManageRoles: {}\n", perm.can_manage_roles());
                w << fmt::format("canManageWebhooks: {}\n", perm.can_manage_webhooks());
                w << fmt::format("canReadMessages: {}\n", perm.can_read_messages());
                w << fmt::format("canSendMessages: {}\n", perm.can_send_messages());
                w << fmt::format("canTTS: {}\n", perm.can_tts());
                w << fmt::format("canManageMessages: {}\n", perm.can_manage_messages());
                w << fmt::format("canEmbed: {}\n", perm.can_embed());
                w << fmt::format("canAttachFiles: {}\n", perm.can_attach_files());
                w << fmt::format("canReadHistory: {}\n", perm.can_read_history());
                w << fmt::format("canMentionEveryone: {}\n", perm.can_mention_everyone());
                w << fmt::format("canExternalEmoiji: {}\n", perm.can_external_emoiji());
                w << fmt::format("canAddReactions: {}\n", perm.can_add_reactions());
                w << fmt::format("canChangeName: {}\n", perm.can_change_name());
                w << fmt::format("canManageNames: {}\n", perm.can_manage_names());
                w << fmt::format("canManageEmojis: {}\n", perm.can_manage_emojis());
                w << fmt::format("canMentionEveryone: {}\n", perm.can_mention_everyone());
            }
            else
            {
                w << fmt::format("canInvite: {}\n", perm.can_invite());
                w << fmt::format("canManageChannels: {}\n", perm.can_manage_channels());
                w << fmt::format("canManageRoles: {}\n", perm.can_manage_roles());
                w << fmt::format("canManageWebhooks: {}\n", perm.can_manage_webhooks());

                w << fmt::format("canVoiceConnect: {}\n", perm.can_voice_connect());
                w << fmt::format("canVoiceMute: {}\n", perm.can_voice_mute());
                w << fmt::format("canVoiceSpeak: {}\n", perm.can_voice_speak());
                w << fmt::format("canVoiceDeafen: {}\n", perm.can_voice_deafen());
                w << fmt::format("canVoiceMove: {}\n", perm.can_voice_move());
                w << fmt::format("canVoiceActivity: {}\n", perm.can_voice_activity());
                w << fmt::format("hasPrioritySpeaker: {}\n", perm.has_priority_speaker());
            }

            w << "```";
        }

        _channel.create_message(w.str());
        return true;
    }

    if (toks[0] == "or_say")
    {
        if (toks.size() > 1)
        {
            create_message(_channel, string_replace(admin_string_replace(toks[1].data(), sd), sd), true);
        }
        return true;
    }

    if (toks[0] == "echo")
    {
        if (toks.size() > 1)
        {
            auto fut = create_message(_channel, toks[1].data()).then([&](message && msg)
            {
                return msg.edit("Edited");
            }).handle_exception([&](std::exception_ptr e)
            {
                try { std::rethrow_exception(e); }
                catch (std::exception & e) { bot.log->error("err: {}", e.what()); }
                return message();
            });

            auto result = fut.get();
            //bot.log->info("Result of echo: {}", result.success()?"Success":"Failure");
        }
        return true;
    }

    if (toks[0] == "analyze")
    {
        if (toks.size() == 1)
            return true;

        auto [type, id] = analyze_mention(toks[1]);
        std::string strtype;
        switch (type)
        {
            case Nickname: strtype = "Nickname"; break;
            case Channel: strtype = "Channel"; break;
            case User: strtype = "User"; break;
            case Role: strtype = "Role"; break;
            case Emoji: strtype = "Emoji"; break;
            case AnimatedEmoji: strtype = "AnimatedEmoji"; break;
            case Fail: strtype = "Fail"; break;
        }

        create_message(_channel, fmt::format("Analysis: type({}) value({})", strtype, id));
        return true;
    }

    if (toks[0] == "~test")
    {
        aegis::create_message_embed_t _to_send;
        _to_send.content("content");
        using aegis::gateway::objects::embed;
        using aegis::gateway::objects::field;
        using aegis::gateway::objects::message;
        _to_send.embed(
            embed()
            .description("desc")
            .title("title")
            .fields(
                {
                    field().name("name1").value("inline true").is_inline(true),
                    field().name("name2").value("inline false").is_inline(false),
                    field().name("name3").value("inline true").is_inline(true),
                    field().name("name4").value("inline true").is_inline(true)
                }
            )
            .color(rand() % 0xFFFFFF)
            .url("https://www.google.com")
        );
        using aegis::gateway::objects::message;
        using aegis::rest::rest_reply;
        _channel.create_message_embed(_to_send)
            .then([](message result_msg) mutable {
            return result_msg.create_reaction("success:429554838083207169")
                .then([result_msg = std::move(result_msg)](rest_reply reply) mutable {

                if (!reply)
                    return aegis::make_ready_future<rest_reply>();

                return result_msg.edit("Edited").then([result_msg = std::move(result_msg)](auto) mutable {
                    return result_msg.create_reaction("aegis:288902947046424576");
                });
            });
        });
        return true;
    }

    if (toks[0] == "count")
    {
        for (int i = 0; i < std::stoi(std::string{ toks[1] }); ++i)
        {
            _channel.create_message(std::to_string(i));
        }
    }

    if (toks[0] == "emoji")
    {
        if (toks.size() < 2)
            return true;
        static const std::array<std::string, 10> emojis = { "aegis:288902947046424576", "success:429554838083207169", "l_n:388391489983610880", "l_i:388391527346601985", "l_c:388391577661341707", "l_e:388390972096249867",
        "l_m:388390914659450882", "I_e:388391023052587018", "I_m:388390995466780703", "_e:388391054887354368"};
        for (int i = 0; i < std::stoi(std::string{ toks[1] }); ++i)
        {
            sd.msg.msg.create_reaction(emojis[i]).get();
        }
    }

    if (toks[0] == "fdc")
    {
        bot.send_all_shards(std::string(R"({ "op": 20 })"));
        return true;
    }

    if (toks[0] == "thread")
    {
        if (toks.size() < 2)
            return true;

        if (toks[1] == "count")
        {
            int i = 0;
            for (auto & t : bot.threads)
                if (t->active)
                    ++i;
            create_message(_channel, fmt::format("Scheduler thread count: `{}`", i));
            return true;
        }
        else if (toks[1] == "reduce")
        {
            if (toks.size() == 3)
                bot.reduce_threads(std::stoi(std::string{ toks[2] }));
            return true;
        }
        else if (toks[1] == "spawn")
        {
            if (toks.size() == 3)
            {
                std::size_t old_size = bot.threads.size();
                for (int i = 0; i < std::stoi(std::string{ toks[2] }); ++i)
                    bot.add_run_thread();
                create_message(_channel, fmt::format("Scheduler thread count increased from `{}` to `{}`", old_size, bot.threads.size()));
                return true;
            }
            create_message(_channel, fmt::format("Scheduler thread count: `{}`", bot.add_run_thread()));
            return true;
        }
    }

    if (toks[0] == "events")
    {
        uint64_t eventsseen = 0;
        std::stringstream ss;

        for (auto & evt : bot.message_count)
        {
            ss << "[" << evt.first << "]:" << evt.second << ' ';
            eventsseen += evt.second;
        }

        json msg =
        {
            { "title", fmt::format("Messages received: {}", [&]() -> int64_t { int64_t c = 0; for (auto & s : bot.get_shard_mgr().get_shards()) { c += s->get_sequence(); } return c; }()) },
            { "description", ss.str() },
            { "color", rand() % 0xFFFFFF }
        };

        _channel.create_message_embed({}, msg);
        return true;
    }

    if (toks[0] == "clear")
    {
        int count = 10;
        bool bot_clear = false;
        snowflake target_id;
        if (toks.size() > 2)
        {
            if (toks[1] == "self")
                target_id = bot.get_id();
            else if (toks[1] == "bot")
                bot_clear = true;
            else
                target_id = get_snowflake(toks[1], _guild);
            count = std::stoi(std::string{ toks[2] });
        }
        else if (toks.size() > 1)
            count = std::stoi(std::string{ toks[1] });

        std::vector<aegis::snowflake> messages;
        {
            std::unique_lock<std::shared_mutex> l(m_message);
            for (auto it = message_history.rbegin(); it != message_history.rend(); ++it)
            {
                if (it->second.get_channel_id() == _channel.get_id())
                {
                    if (it->second.get_id() == message_id)
                        continue;
                    if (bot_clear == true)
                    {
                        if (it->second.author.is_bot())
                            if (count-- > 0)
                                messages.push_back(it->second.get_id());
                    }
                    else if (target_id == 0 || it->second.author.id == target_id)
                        if (count-- > 0)
                            messages.push_back(it->second.get_id());
                }
                if (count == 0)
                    break;
            }
        }
        if (!_channel.perms().can_manage_messages())
        {
            req_permission(obj.msg);
            return true;
        }
        if (messages.size() > 2 && messages.size() < 100)
            _channel.bulk_delete_message(messages);
        else
            //manually delete each
            for (const auto & it : messages)
                _channel.delete_message(it);

        _channel.create_message(fmt::format("{} messages cleared.", messages.size()))
            .then([obj](message reply) mutable
        {
            std::this_thread::sleep_for(5s);
            obj.msg.delete_message();
            reply.delete_message();
        });
        return true;
    }

    if (toks[0] == "testfunc")
    {
        _channel.create_message(fmt::format("By ID: <@{}>", member_id));
        auto mbr = bot.find_user(member_id);
        if (mbr)
        {
            _channel.create_message(fmt::format("By Find: {} | {}", mbr->get_full_name(), mbr->get_guild_info(guild_id).nickname.value_or("")));
        }
        return true;
    }

    if (toks[0] == "intended")
    {
        _channel.create_message("@everyone test");
        obj.msg.delete_message();
        return true;
    }

    if (toks[0] == "b64")
    {
        if (toks.size() < 3)
            return false;

        if (toks[1] == "d" || toks[1] == "decode")
        {
            auto t = base64_decode(toks[2].data());
            _channel.create_message(fmt::format("```\n{}\n```", t));
        }
        else if (toks[1] == "e" || toks[1] == "encode")
        {
            auto t = base64_encode(toks[2].data());
            if (t.size() > 1900)
                t = "Encoded text too large.";
            _channel.create_message(fmt::format("```\n{}\n```", t));
        }
        return true;
    }

	if (toks[0] == "e64")
	{
		if (toks.size() < 2)
			return false;

		auto t = base64_encode(toks[1].data());
		if (t.size() > 1900)
			t = "Encoded text too large.";
		_channel.create_message(fmt::format("```\n{}\n```", t));
        return true;
    }

	if (toks[0] == "d64")
	{
		if (toks.size() < 2)
			return false;

		auto t = base64_decode(toks[1].data());

// 		std::ostringstream escaped;
// 		escaped.fill('0');
// 		escaped << std::hex;
// 
// 		for (std::string::value_type c : t)
// 		{
// 			// Keep alphanumeric and other accepted characters intact
// 			//if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/' || c == '\\')
//             if (c >= '!' && c <= 255)
// 			{
// 				escaped << c;
// 				continue;
// 			}
// 
// 			// Any other characters are percent-encoded
// 			escaped << std::uppercase;
// 			escaped << "\\u000" << std::setw(1) << int((unsigned char)c);
// 			escaped << std::nouppercase;
// 		}
// 
// 		t = escaped.str();


// 		std::string tt;
// 		for (auto& c : t)
// 		{
// 			if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '1' && c <= '0'))
// 				tt.append(c);
// 			else
// 				tt.append({ '\\', 'u', '0', '0', '0', utility::hex(c) });
// 		}
		if (t.size() > 1900)
			t = "Decoded text too large.";
		_channel.create_message(fmt::format("```\n{}\n```", t));
        return true;
    }

//     if (toks[0] == "glac")
//     {
//         aegis::rest::request_params rp;
//         rp.host = "glaca.nostale.club";
//         rp.method = aegis::rest::Get;
//         rp.path = "/api/pl/1";
// 
//         rest_reply ret = _bot->get_rest_controller().execute2(std::forward<aegis::rest::request_params>(rp));
//         json response = json::parse(ret.content);
// 
//         _channel.create_message(fmt::format("Angels: {} VS Demons: {}", response["angels"]["progress"].get<std::string>(), response["demons"]["progress"].get<std::string>()));
//     }

/*
	//
	if (toks[0] == "startplugincheck")
	{
		aegis::rest::request_params rp;
		rp.host = "umod.org";
		rp.method = aegis::rest::Get;
		rp.path = "/plugins/search.json?query=&page=1&sort=published_at&sortdir=desc&filter=";
		rest_reply ret = _bot->get_rest_controller().execute2(std::forward<aegis::rest::request_params>(rp));
		json response = json::parse(ret.content);

		for (const auto& j : response["data"])
		{
			std::cout << fmt::format("name: {} - url: {}", j["name"].get<std::string>(), j["url"].get<std::string>());
			using field = aegis::gateway::objects::field;
			using embed = aegis::gateway::objects::embed;
			using thumbnail = aegis::gateway::objects::thumbnail;
			using footer = aegis::gateway::objects::footer;
			std::string iconurl = j["icon_url"];
			if (iconurl.empty()) iconurl = j["author_icon_url"];
			_channel.create_message_embed({}, embed().color(rand())
				.title("**Plugin Added/Updated!**")
				.fields({
							field("Plugin:", fmt::format("**[{}]({})**", j["name"].get<std::string>(), j["url"].get<std::string>())).is_inline(true),
							field("Version:", fmt::format("**{}**", j["latest_release_version"].get<std::string>())).is_inline(true),
							field("Description:", fmt::format("```\n{}\n```", j["description"].get<std::string>())).is_inline(false)
					})
				.timestamp(j["latest_release_at_atom"])
				//.image("")
				.thumbnail({ j["icon_url"].get<std::string>() })
				.footer({ "CodeXive.org" })
			);
			//return true;
		}
		return true;
	}*/

	if (toks[0] == "cset")
	{
		if (toks.size() < 3)
			return true;

		cdata_set(std::string(toks[1]), std::string(toks[2]));
		return true;
	}

	if (toks[0] == "cget")
	{
		if (toks.size() < 2)
			return true;

		cdata_get(std::string(toks[1]));
		return true;
	}

    return false;
}
