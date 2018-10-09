//
// Management.cpp
// **************
//
// Copyright (c) 2018 Sharon W (sharon at aegis dot gg)
//
// Distributed under the MIT License. (See accompanying file LICENSE)
// 


#include "AegisBot.h"
#include <aegis/objects/embed.hpp>
#include "Guild.h"
#if !defined(AEGIS_MSVC)
#include <dlfcn.h>
#endif

bool AegisBot::process_admin_messages(aegis::gateway::events::message_create & obj, shared_data & sd)
{
    if (sd.message_id == bot_owner_id)
        return false;

    const snowflake & channel_id = sd.channel_id;
    const snowflake & guild_id = sd.guild_id;
    const snowflake & message_id = sd.message_id;
    const snowflake & member_id = sd.member_id;
    const snowflake & guild_owner_id = sd.guild_owner_id;

    std::string_view username = sd.username;

    member & _member = sd._member;
    channel & _channel = sd._channel;
    guild & _guild = sd._guild;
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
        bot.get_shard_mgr().close(*obj._shard, 1003);
        return true;
    }

    if (toks[0] == "thread")
    {

    }

    if (toks[0] == "socketcheck")
    {
        fmt::MemoryWriter w;
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
        try
        {
            std::string torun{ content.data() + toks[0].size() + 1 };
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
            std::string compileresult = exec(fmt::format("g++-7 -std=gnu++17 -O2 main.cpp -shared -fPIC -o main{}.so -I../src/ 2>&1", incr));
            auto compile_count = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - compile_start).count();
            std::string result = exec(fmt::format(R"(if [ -f main{}.so ] ; then echo "SUCCESS" ; else echo "FAIL" ; fi)", incr));

            if (result == "SUCCESS\n")
            {
                bot.log->info("Loading: main{}.so", incr);
                auto evaltoload = dlopen(fmt::format("{}/main{}.so", getenv("PWD"), incr).c_str(), RTLD_NOW);
                if (!evaltoload)
                {
                    bot.log->info("dlerror : {}", dlerror());
                    _channel.create_message("Failed to load so");
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
                    _channel.create_message("Failed to run so");
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
    if (toks[0] == "dm2")
    {
        std::future<aegis::rest::rest_reply> tres;
        bot.log->info("Start DM1");
        auto res1 = aegis::utility::perf_run("basic", [=, &tres, id = obj.get_member().get_id()]()
        {
            tres = bot.create_dm_message(id, "test message");
        });

        tres.get();

        bot.log->info("Start DM2");
        auto res2 = aegis::utility::perf_run("wait", [=, id = obj.get_member().get_id()]()
        {
            bot.create_dm_message(id, "test message").get();
        });
        bot.log->info("\nres1: {}res2: {}", res1, res2);
    }
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
        obj.bot->send_all_shards(j);
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
        obj._shard->send(j.dump());
        req_success(obj.msg);
        return true;
    }

    if (toks[0] == "shardstats")
    {

        fmt::MemoryWriter w;
        w << "```diff\n";

        uint64_t count = 0;
        count = bot.get_shard_transfer();

        struct guild_count_data
        {
            size_t guilds;
            size_t members;
        };

        std::vector<guild_count_data> shard_guild_c(bot.shard_max_count);

        for (auto & v : obj.bot->guilds)
        {
            ++shard_guild_c[v.second->shard_id].guilds;
            shard_guild_c[v.second->shard_id].members += v.second->get_members().size();
        }

        w << fmt::format("  Total transfer: {} Memory usage: {}\n", format_bytes(count), format_bytes(aegis::utility::getCurrentRSS()));
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
            w << fmt::format(" {:6} {:10}    {:4} {:7}   {:>16}  {:9}ms {:>11}        {:3}\n",
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

        fmt::MemoryWriter w;
        w << "This shard: " << obj._shard->get_id() << "```diff\n";

        uint64_t count = 0;
        count = bot.get_shard_transfer();

        struct guild_count_data
        {
            size_t guilds;
            size_t members;
        };

        std::vector<guild_count_data> shard_guild_c(bot.shard_max_count);

        for (auto & v : obj.bot->guilds)
        {
            ++shard_guild_c[v.second->shard_id].guilds;
            shard_guild_c[v.second->shard_id].members += v.second->get_members().size();
        }

        w << fmt::format("  Total transfer: {} Memory usage: {}\n", format_bytes(count), format_bytes(aegis::utility::getCurrentRSS()));
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
            w << fmt::format(" {:6} {:10}    {:4} {:7}   {:>16}  {:9}ms {:>11}        {:3}\n",
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
//         fmt::MemoryWriter w;
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
        _channel.create_message(fmt::format("I am shard#[{}]", obj._shard->get_id()));
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
            auto & g_data2 = guild_data[g_sn];
            load_guild(g_sn, g_data2);
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
        auto & gd = guild_data[g_id];
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
            if (check_params(toks, 4))
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
            std::string res = hget(toks[2], { std::string{ toks[3] } }, g_data);
            _channel.create_message(fmt::format("Result: {}", res));
            return true;
        }
        if (toks[1] == "hgetall")
        {
            if (check_params(toks, 2))
                return true;
            std::string key = "";
            if (toks.size() > 2)
                key = fmt::format("{}:{}", g_data.redis_prefix, toks[2]);
            else
                key = g_data.redis_prefix;
            std::unordered_map<std::string, std::string> res = get_array(key);
            fmt::MemoryWriter w;
            w << "Results of " << key << '\n';
            for (const auto&[k, v] : res)
            {
                w << '\n' << k << " - " << v;
            }
            _channel.create_message(w.str());
            return true;
        }
        if (toks[1] == "getmap")
        {
            if (check_params(toks, 3))
                return true;
            std::unordered_map<std::string, std::string> res = get_array(toks[2]);
            fmt::MemoryWriter w;
            w << "Results of " << toks[2] << '\n';
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
            fmt::MemoryWriter w;
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
        obj.bot->find_channel(channel_id)->create_message("exiting...");
        obj.bot->shutdown();
        return true;
    }
    if (toks[0] == "role_count")
    {
        std::reference_wrapper<guild> _g = _channel.get_guild();
        if (toks.size() > 1)
        {
            _g = *obj.bot->find_guild(std::stoll(std::string(toks[1])));
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

        fmt::MemoryWriter w;
        w << "```cpp\n";
        for (auto o : count)
            w << o.first << " : " << o.second << '\n';
        w << "```";
        std::cout << w.str();
        _channel.create_message(w.str());
        return true;
    }
    if (toks[0] == "addrole")
    {
        auto res = _channel.get_guild().add_guild_member_role(_member.get_id(), std::stoull(std::string{toks[1]})).get();
        _channel.create_message(fmt::format("Add role : {} {}",res.reply_code, res.content));
        return true;
    }
    if (toks[0] == "remrole")
    {
        auto res = _channel.get_guild().add_guild_member_role(_member.get_id(), std::stoull(std::string{ toks[1] })).get();
        _channel.create_message(fmt::format("Remove role : {} {}", res.reply_code, res.content));
        return true;
    }
    if (toks[0] == "server")
    {
        // TODO: verify whether this is valid - verified to work. get second opinion on whether valid
        std::reference_wrapper<guild> _g = _channel.get_guild();
        if (toks.size() > 1)
        {
            _g = *obj.bot->find_guild(std::stoll(std::string(toks[1])));
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
        auto _member = obj.bot->find_member(_guild.get_owner());
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
        fmt::MemoryWriter r;
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
//         fmt::MemoryWriter r;
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
        for (auto & g : obj.bot->guilds)
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
        
        if (auto apireply = _channel.create_message("Pong", checktime).get(); apireply)
        {
            aegis::gateway::objects::message msg = json::parse(apireply.content);
            std::string to_edit = fmt::format("Ping reply: REST [{}ms]", (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() - checktime));
            msg.edit(to_edit);
            if (cv_ping_test.wait_for(lk, 20s) == std::cv_status::no_timeout)
            {
                msg.edit(fmt::format("{} WS [{}ms]", to_edit, ws_checktime));
                return true;
            }
            else
            {
                msg.edit(fmt::format("{} WS [timeout20s]", to_edit));
                return true;
            }
        }
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

        if (auto r = _channel.create_message_embed("", t).get(); !r)
            obj.msg.create_reaction("fail:429554869611921408");
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

        std::error_code ec;
        
        if (auto reply = ch->create_message(ec, toks[2].data()).get(); !reply)
            _channel.create_message(fmt::format("Unable to send message http:{} reason: {}", static_cast<int32_t>(reply.reply_code), reply.content));
        else
            obj.msg.create_reaction("success:429554838083207169");
            
        //obj.msg.create_reaction("<:success:429554838083207169>");
        //obj.msg.create_reaction("<:fail:429554869611921408>");
        
        return true;
    }
    if (toks[0] == "createguild")
    {
        if (check_params(toks, 2, "createguild name"))
            return true;

        if (auto reply = bot.create_guild(std::string{ toks[1] }); reply)
        {
            json r = json::parse(reply.content);

            json obj;
            obj["max_age"] = 86400;
            obj["max_uses"] = 0;
            obj["temporary"] = false;
            obj["unique"] = true;

            if (auto r_create = bot.call({ fmt::format("/channels/{}/invites", std::stoull(r["id"].get<std::string>())), aegis::rest::Post, obj.dump() }); r_create)
            {
                json i_create = json::parse(reply.content);
                _channel.create_message(fmt::format("Guild: [{}:{}] Channel: [{}:#{}] https://discord.gg/{}",
                                                    i_create["guild"]["id"].get<std::string>(), i_create["guild"]["name"].get<std::string>(),
                                                    i_create["channel"]["id"].get<std::string>(), i_create["channel"]["name"].get<std::string>(),
                                                    i_create["code"].get<std::string>()));
            }
            else
                _channel.create_message("Guild made but no invite.");
        }
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
        if (auto reply = g->get_channels().begin()->second->create_channel_invite(0, 1, false, true).get(); reply)
        {
            json r = json::parse(reply.content);
            _channel.create_message(fmt::format("Guild: [{}:{}] Channel: [{}:#{}] https://discord.gg/{}",
                            r["guild"]["id"].get<std::string>(), r["guild"]["name"].get<std::string>(),
                            r["channel"]["id"].get<std::string>(), r["channel"]["name"].get<std::string>(),
                            r["code"].get<std::string>()));
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
        _channel.modify_channel({}, {}, {}, {}, {}, {}, {}, {}, num);
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
        auto f = aegis::async([&]()
        {
            std::this_thread::sleep_for(2s);
            do_log(fmt::format("Test log int {}", 1));
            return 1;
        }).then([&](auto i)
        {
            do_log(fmt::format("Test log int {}", ++i));
            return i;
        }).then([&](auto i)
        {
            do_log(fmt::format("Test log int {}", ++i));
            return i;
        }).then([&](auto i)
        {
            do_log(fmt::format("Test log int {}", ++i));
            return i;
        }).then([&](auto i)
        {
            do_log(fmt::format("Test log int {}", ++i));
            return i;
        }).then([&](auto i)
        {
            do_log(fmt::format("Test log int {}", ++i));
            return i;
        });

        auto g = aegis::async([&]()
        {
            std::this_thread::sleep_for(2s);
            do_log("Test log void 1");
        }).then([&]()
        {
            do_log("Test log void 2");
        }).then([&]()
        {
            do_log("Test log void 3");
        }).then([&]()
        {
            do_log("Test log void 4");
        }).then([&]()
        {
            do_log("Test log void 5");
        }).then([&]()
        {
            do_log("Test log void 6");
        }).then([&]()
        {
            do_log("Test log void 7");
        });

        std::string s = fmt::format("Retrieved before function exit : {}", f.get());
        do_log(s);
        _channel.create_message(s);
    }
    if (toks[0] == "async_nowait")
    {
        auto f = aegis::async([&]()
        {
            std::this_thread::sleep_for(2s);
            do_log(fmt::format("Test log int {}", 1));
            return 1;
        }).then([&](auto i)
        {
            do_log(fmt::format("Test log int {}", ++i));
            return i;
        }).then([&](auto i)
        {
            do_log(fmt::format("Test log int {}", ++i));
            return i;
        }).then([&](auto i)
        {
            do_log(fmt::format("Test log int {}", ++i));
            return i;
        }).then([&](auto i)
        {
            do_log(fmt::format("Test log int {}", ++i));
            return i;
        }).then([&](auto i)
        {
            do_log(fmt::format("Test log int {}", ++i));
            return i;
        });

        auto g = aegis::async([&]()
        {
            std::this_thread::sleep_for(2s);
            do_log("Test log void 1");
        }).then([&]()
        {
            do_log("Test log void 2");
        }).then([&]()
        {
            do_log("Test log void 3");
        }).then([&]()
        {
            do_log("Test log void 4");
        }).then([&]()
        {
            do_log("Test log void 5");
        }).then([&]()
        {
            do_log("Test log void 6");
        }).then([&]()
        {
            do_log("Test log void 7");
        });
    }
    return false;
}
