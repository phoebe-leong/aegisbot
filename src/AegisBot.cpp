//
// AegisBot.cpp
// ************
//
// Copyright (c) 2018 Sharon W (sharon at aegis dot gg)
//
// Distributed under the MIT License. (See accompanying file LICENSE)
// 


#include "AegisBot.h"
#include <aegis.hpp>
#include "mod_auction.h"
#include "mod_automod.h"
#include "mod_music.h"

using namespace cppdogstatsd;

AegisBot::AegisBot(asio::io_context & _io, aegis::core & bot)
    : redis(_io)
    , redis_commands(_io)
    , redis_logger(_io)
    , memory_stats(_io)
    , bot(bot)
    , _io_service(_io)
// #ifdef WIN32
//     , statsd(_io, "aegisdbg", "165.227.115.46")
// #else
    , statsd(_io, "aegisbot")
//#endif
    , stats_timer(_io)
    , web_stats_timer(_io)
    , web_log_timer(_io)
    , status_timer(_io)
    , maint_timer(_io)
    , strand(_io)
{
    log = spdlog::get("aegis");
    load_config();

    stats_timer.expires_after(std::chrono::seconds(5));
    stats_timer.async_wait(std::bind(&AegisBot::push_stats, this));

    status_timer.expires_after(std::chrono::seconds(5));
    status_timer.async_wait(std::bind(&AegisBot::update_statuses, this));

    maint_timer.expires_after(std::chrono::seconds(5));
    maint_timer.async_wait(std::bind(&AegisBot::maint, this));

    web_stats_timer.expires_after(std::chrono::milliseconds(250));
    web_stats_timer.async_wait(std::bind(&AegisBot::update_stats, this));

    web_log_timer.expires_after(std::chrono::seconds(5));
    web_log_timer.async_wait(std::bind(&AegisBot::web_log, this));

    admin_replacements.emplace("$token", [&](shared_data & sd)
    {
        return sd.ab.bot.get_token();
    });

    admin_replacements.emplace("$shardid", [&](shared_data & sd)
    {
        return std::to_string(sd.msg._shard->get_id());
    });

    admin_replacements.emplace("$shardseq", [&](shared_data & sd)
    {
        return std::to_string(sd.msg._shard->get_sequence());
    });

    admin_replacements.emplace("$shardtransfer", [&](shared_data & sd)
    {
        return sd.msg._shard->get_transfer_str();
    });

    admin_replacements.emplace("$sharduptime", [&](shared_data & sd)
    {
        return sd.msg._shard->uptime_str();
    });

    admin_replacements.emplace("$uptime", [&](shared_data & sd)
    {
        return sd.ab.bot.uptime_str();
    });

    replacements.emplace("$username", [&](shared_data & sd)
    {
        return std::string{ sd.username };
    });

    replacements.emplace("$userid", [&](shared_data & sd)
    {
        return std::to_string(sd.member_id);
    });

    replacements.emplace("$messageid", [&](shared_data & sd)
    {
        return std::to_string(sd.message_id);
    });

    replacements.emplace("$channelname", [&](shared_data & sd)
    {
        return sd._channel.get_name();
    });

    replacements.emplace("$guildid", [&](shared_data & sd)
    {
        return std::to_string(sd.guild_id);
    });

    replacements.emplace("$guildname", [&](shared_data & sd)
    {
        return sd._guild.get_name();
    });

//     bot.set_on_shard_connect([&](aegis::shards::shard * _shard)
//     {
//         json j;
//         j["shard:connects"] = 1;
//         event_log(j, "/shards");
//     });

    bot.set_on_shard_disconnect([&](aegis::shards::shard * _shard)
    {
        json j;
        j["shard:disconnects"] = 1;
        event_log(j, "/shards");
    });

//     advanced_replacements.emplace("@user", [&](shared_data & sd)
//     {
//         return std::string{ sd._guild. };
//     });
// 
//     replacements.emplace("$userid", [&](shared_data & sd)
//     {
//         return std::string{ sd._channel.get_id() };
//     });
// 
//     replacements.emplace("$userid", [&](shared_data & sd)
//     {
//         return std::string{ sd._channel.get_id() };
//     });

//     memory_stats.expires_after(std::chrono::milliseconds(100));
//     memory_stats.async_wait(std::bind(&AegisBot::send_memory_stats, this));
}

void AegisBot::maint()
{
    try
    {
        std::unique_lock<std::shared_mutex> l(m_message);
        auto timenow = std::chrono::system_clock::now();
        for (auto msg = message_history.begin(); msg != message_history.end();)
        {
            if ((timenow - std::chrono::milliseconds(msg->first.get_time())).time_since_epoch() > std::chrono::minutes(60))
            {
                //bot.log->info("Message deleted: {} from {}\n{}", msg->second.get_id(), msg->second.get_member().get_full_name(), msg->second.get_content());
                msg = message_history.erase(msg);
            }
            else
                ++msg;
        }
    }
    catch (...)
    {
    }
    maint_timer.expires_after(std::chrono::seconds(30));
    maint_timer.async_wait(std::bind(&AegisBot::maint, this));
}

void AegisBot::web_log()
{
    // shard history

    try
    {
        for (uint32_t i = 0; i < bot.shard_max_count; ++i)
        {
            auto & s = bot.get_shard_by_id(i);

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

            json _shard_stats;

            std::string shard_path(fmt::format("config:log:shard:{}", s.get_id()));

            _shard_stats["id"] = s.get_id();
            _shard_stats["seq"] = s.get_sequence();
            _shard_stats["servers"] = shard_guild_c[s.get_id()].guilds;
            _shard_stats["members"] = shard_guild_c[s.get_id()].members;
            //_shard_stats["uptime"] = s.uptime();
            _shard_stats["transfer"] = s.get_transfer();
            _shard_stats["reconnects"] = s.counters.reconnects;
            _shard_stats["connected"] = s.is_connected();

            do_raw("LPUSH", { shard_path, _shard_stats.dump() });
            do_raw("LTRIM", { shard_path, "0", "999" });
        }


        json _bot_stats;

        _bot_stats["mem"] = aegis::utility::getCurrentRSS();

        do_raw("LPUSH", { "config:log:bot", _bot_stats.dump() });
        do_raw("LTRIM", { "config:log:bot", "0", "999" });
    }
    catch (...)
    {

    }
    web_log_timer.expires_after(std::chrono::seconds(5));
    web_log_timer.async_wait(std::bind(&AegisBot::web_log, this));
}

void AegisBot::update_statuses()
{
    status_timer.expires_at(std::chrono::steady_clock::now() + std::chrono::seconds(30));
    try
    {
        std::string toset;
        int type = 0;

        switch (rand() % 6)
        {
            case 0:
                toset = fmt::format("@{} help", bot.self()->get_username());
                break;
            case 1:
                toset = fmt::format("{} users", bot.members.size());
                type = 3;
                break;
            case 2:
                toset = fmt::format("{} servers", bot.guilds.size());
                type = 3;
                break;
            case 3:
                toset = fmt::format("{} Uptime", bot.uptime_str());
                break;
            case 4:
                toset = fmt::format("{} shards", bot.get_shard_mgr().shard_count());
                break;
            case 5:
                toset = "Running on C++";
                break;
        }

        json j = {
            { "op", 3 },
            {
                "d",
                {
                    { "game",
                        {
                            { "name", toset },
                            { "type", type }
                        }
                    },
                    { "status", "online" },
                    { "since", json::value_t::null },
                    { "afk", false }
                }
            }
        };
        bot.send_all_shards(j);
    }
    catch (...)
    {
    }
    status_timer.async_wait(std::bind(&AegisBot::update_statuses, this));
}

void AegisBot::push_stats()
{
    stats_timer.expires_at(std::chrono::steady_clock::now() + std::chrono::seconds(10));
    try
    {
        aegis::rest::request_params rp;
        rp.host = logging_address;
        rp.port = logging_port;
        if (is_production)
            rp.path = "/bot";
        else
            rp.path = "/test/bot";

        json m;

        {
            for (auto & c : command_counters)
            {
                json j;
                j[fmt::format("cmd:{}", c.first)] = c.second;
                m.push_back(j);

                c.second = 0;
            }

            for (auto & c : http_codes)
            {
                json j;
                j[fmt::format("code:{}", c.first)] = c.second;
                m.push_back(j);

                c.second = 0;
            }
        }

        {
            json j;
            j["members"] = bot.members.size();
            j["guilds"] = bot.guilds.size();
            j["memory"] = aegis::utility::getCurrentRSS();
            j["dms"] = counters.dms.fetch_and(0);
            j["msgs"] = counters.messages.fetch_and(0);
            j["presences"] = counters.presences.fetch_and(0);
            j["rest_time"] = counters.rest_time.fetch_and(0);
            j["rest"] = counters._rest.fetch_and(0);
            j["events"] = counters.events.fetch_and(0);
            j["commands"] = counters.commands.fetch_and(0);

            m.push_back(j);
        }

        rp.body = m.dump();
        bot.get_rest_controller().execute2(std::forward<aegis::rest::request_params>(rp));

        {
            aegis::rest::request_params rp;
            rp.host = logging_address;
            rp.port = logging_port;
            if (is_production)
                rp.path = "/cmds";
            else
                rp.path = "/test/cmds";

            json ev;
            for (auto &[k, v] : counters._msg)
            {
                ev[k + ".count"] = v.count;
                ev[k + ".time"] = v.time;
                v.count = v.time = 0;
            }
            for (auto &[k, v] : counters._js)
            {
                ev[k + ".js.count"] = v.count;
                ev[k + ".js.time"] = v.time;
                v.count = v.time = 0;
            }
            rp.body = ev.dump();
            bot.get_rest_controller().execute2(std::forward<aegis::rest::request_params>(rp));
        }


        statsd.Metric<Gauge>("member", bot.members.size(), 1);
        statsd.Metric<Gauge>("guilds", bot.guilds.size(), 1);


        for (auto & c : command_counters)
        {
            statsd.Metric<Count>("cmds", c.second, 1, { fmt::format("cmd:{}", c.first) });
            c.second = 0;
        }

        for (auto & c : http_codes)
        {
            statsd.Metric<Count>("http_codes", c.second, 1, { fmt::format("code:{}", c.first) });
            c.second = 0;
        }
    }
    catch (...)
    {
    }
    stats_timer.async_wait(std::bind(&AegisBot::push_stats, this));
}

// Webpanel stats
void AegisBot::update_stats()
{
    try
    {
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

        //w << fmt::format("  Total transfer: {} Memory usage: {}\n", format_bytes(count), format_bytes(aegis::utility::getCurrentRSS()));

        for (uint32_t i = 0; i < bot.shard_max_count; ++i)
        {
            auto & s = bot.get_shard_by_id(i);
            auto time_count = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - bot.get_shard_mgr().get_shard(s.get_id()).lastwsevent).count();

            std::vector<std::string> toadd;
            toadd.emplace_back(fmt::format("config:shards:{}", s.get_id()));
            toadd.emplace_back("id");
            toadd.emplace_back(std::to_string(s.get_id()));
            toadd.emplace_back("seq");
            toadd.emplace_back(std::to_string(s.get_sequence()));
            toadd.emplace_back("servers");
            toadd.emplace_back(std::to_string(shard_guild_c[s.get_id()].guilds));
            toadd.emplace_back("members");
            toadd.emplace_back(std::to_string(shard_guild_c[s.get_id()].members));
            toadd.emplace_back("uptime");
            toadd.emplace_back(s.uptime_str());
            toadd.emplace_back("last_message");
            toadd.emplace_back(std::to_string(time_count));
            toadd.emplace_back("transfer");
            toadd.emplace_back(s.get_transfer_str());
            toadd.emplace_back("reconnects");
            toadd.emplace_back(std::to_string(s.counters.reconnects));
            toadd.emplace_back("connected");
            toadd.emplace_back(s.is_connected()?"true":"false");
            hmset(toadd);
        }
        //publish("", "");
    }
    catch (...)
    {
    }
    web_stats_timer.expires_after(std::chrono::milliseconds(250));
    web_stats_timer.async_wait(std::bind(&AegisBot::update_stats, this));
}

void AegisBot::send_memory_stats()
{
//     std::string output = fmt::format("Stats: Map: {:>11} Vector: {:>11} String: {:>11}",
//                                      format_bytes(aegis::map_storage.load(std::memory_order_relaxed)),
//                                      format_bytes(aegis::vector_storage.load(std::memory_order_relaxed)),
//                                      format_bytes(aegis::string_storage.load(std::memory_order_relaxed)));
//     basic_action("PUBLISH", { "aegis:stats", output});
//     memory_stats.expires_after(std::chrono::milliseconds(100));
//     memory_stats.async_wait(std::bind(&AegisBot::send_memory_stats, this));
}

void AegisBot::load_config()
{
    auto configfile = std::fopen("config.json", "r+");

    if (!configfile)
    {
        std::perror("File opening failed");
        throw std::runtime_error("config.json does not exist");
    }

    std::fseek(configfile, 0, SEEK_END);
    std::size_t filesize = std::ftell(configfile);

    std::fseek(configfile, 0, SEEK_SET);
    std::vector<char> buffer(filesize + 1);
    std::memset(buffer.data(), 0, filesize + 1);
    size_t rd = std::fread(buffer.data(), sizeof(char), buffer.size() - 1, configfile);

    std::fclose(configfile);

    json cfg = json::parse(buffer.data());

    if (!cfg["bot"].is_null())
    {
        //load bot specific configurations
        auto cfg_bot = cfg["bot"];
        if (!cfg_bot["production"].is_null())
            is_production = cfg_bot["production"];
        if (!cfg_bot["logging-address"].is_null())
            logging_address = cfg_bot["logging-address"].get<std::string>();
        if (!cfg_bot["logging-port"].is_null())
            logging_port = cfg_bot["logging-port"].get<std::string>();

    }
}

void AegisBot::do_log(std::string msg)
{
    bot.log->debug(msg);
    asio::post(_io_service, asio::bind_executor(strand, [msg, this]()
    {
        std::lock_guard<std::mutex> lock(r_mutex);
        redis.command("PUBLISH", { "aegis:log", msg });
    }));
}

bool AegisBot::create_message(aegis::channel & _channel, const std::string & str, bool _override) noexcept
{
    auto guild_id = _channel.get_guild_id();
    if (!guild_id) return false;
    auto & g_info = get_guild(guild_id);
    if (!_override && g_info.is_channel_ignored(_channel.get_id()))
        return false;
    std::error_code ec;
    _channel.create_message(ec, str);
    if (ec)
        return false;
    return true;
}

void AegisBot::init()
{
#ifdef WIN32
    ipaddress = "192.168.184.136";
#else
    ipaddress = "127.0.0.1";
#endif
    port = 6379;

    bot.log->info("Connecting to Redis.");
    
    asio::ip::address connectaddress = asio::ip::make_address(ipaddress);
    std::string errmsg;
    if (!redis.connect(connectaddress, port, errmsg))
    {
        bot.log->critical("Can't connect to redis: {}", errmsg);
        throw std::runtime_error("Can't connect to redis.");
    }

    bot.log->info("Redis connected");

    redisclient::RedisValue result;
    if (!password.empty())
    {
        result = redis.command("AUTH", { password });
        if (result.isError())
        {
            bot.log->critical("AUTH error: {}", result.toString());
            throw std::runtime_error("Unable to auth Redis");
        }
    }

    //redis_logger.subscribe(connectaddress, port, "aegis:log");

    redis_commands.subscribe(connectaddress, port, "aegis:commands", std::bind(&AegisBot::redis_cmd, this, std::placeholders::_1));

    //command initialization
    for (auto &[c,z] : commands_defaults)
    {
        //auto &[b,a,t] = z;
        commands.push_back(c);
    }

    //module initialization

    auto mods = get_vector(fmt::format("{}:modules", r_bot_config));
    for (auto & m : mods)
    {
        auto arr = get_array(fmt::format("{}:modules:{}", r_bot_config, m));
        const std::string & prefix = m;
        std::string name = arr["name"];
        bool enabled = to_bool(arr["enabled"]);
        modules id = static_cast<modules>(std::stoll(arr["id"]));

        bot_modules.emplace(id, s_module_data{ name, prefix, enabled });
        bot.log->info("Module [{}] loaded with prefix [{}] id [{}] and default enabled [{}]", name, prefix, id, enabled);
    }

    bot.set_on_message_end(std::bind(&AegisBot::message_end, this, std::placeholders::_1, std::placeholders::_2));
    bot.set_on_js_end(std::bind(&AegisBot::js_end, this, std::placeholders::_1, std::placeholders::_2));

    bot.set_on_rest_end([&](std::chrono::steady_clock::time_point start_time, uint16_t _code)
    {
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_time).count();
        ++http_codes[_code];
        counters.rest_time += us;
        counters._rest++;
        statsd.Metric<cppdogstatsd::Timer>("rest_time", us, 1);
    });

    //redis_store_keys[""] = 


    // load ignored members
    {
        auto ignored_v = get_vector("config:ignored");

        for (auto & ignr : ignored_v)
            ignored.emplace(std::stoull(ignr));
    }

    // load dm channels
    {
        auto dm_ch = get_array("config:dms");

        for (auto &[k, v] : dm_ch)
            dm_channels.emplace(std::stoull(k), std::stoull(v));
    }


    // set up shard data
    {
        do_raw("DEL", { "config:shards" });
        std::vector<std::string> toadd;
        toadd.emplace_back("config:shards");
        for (uint32_t x = 0; x < bot.get_shard_mgr().shard_max_count; ++x)
            toadd.push_back(std::to_string(x));
        sadd(toadd);
    }
}

std::string replace_all(std::string str, const std::string& from, const std::string& to)
{
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos)
    {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length();
    }
    return str;
}

void AegisBot::redis_cmd(const std::vector<char> &buf)
{
    //currently using PUBSUB but a blocking list pop may be better as that would not be affected
    //by any potential bot downtime
    std::string s(buf.begin(), buf.end());
    s = replace_all(s, "\\\"", "\"");
    do_log(fmt::format("redis command: {}", s));

    std::vector<std::string_view> t = tokenize(s, " ");

    auto respond = [&](const std::string text)
    {
        basic_action("publish", { "aegis:response", text });
    };

    if (t[0] == "message")
    {
        if (t[1] == "count")
        {
            respond(fmt::format("R: Message Count: {}", message_history.size()));
            return;
        }
        if (t[1] == "clear")
        {
            std::string ret;
            {
                ret = aegis::utility::perf_run("clear_messages", [&]()
                {
                    std::unique_lock<std::shared_mutex> l(m_message);
                    message_history.clear();
                });
            }
            respond(fmt::format("R: Message Clear: {}", ret));
            return;
        }
        return;
    }
    if (t[0] == "admin")
    {
        if (t.size() != 4)//temp - admin shard connect $id
            return;//failure

        if (t[1] == "shard")
        {
            int32_t snum = std::stoi(std::string{ t[3] });
            auto & s = bot.get_shard_by_id(snum);
            if (t[2] == "connect")
            {
                if (!s.is_connected())
                    bot.get_shard_mgr().queue_reconnect(s);
            }
            else if (t[2] == "disconnect")
            {
                if (s.is_connected())
                    bot.get_shard_mgr().close(s, 1001, "", aegis::shard_status::Shutdown);
            }
            else if (t[2] == "reconnect")
            {
                if (s.is_connected())
                    bot.get_shard_mgr().close(s, 1001, "", aegis::shard_status::Shutdown);
                bot.get_shard_mgr().queue_reconnect(s);
            }
            return;
        }
    }
    //website executions
    if (t[0] == "website")
    {
        if (t.size() == 1)
            return;//failure

        aegis::snowflake guild_id = std::stoull(std::string{ t[1] });
        auto target_guild = bot.find_guild(guild_id);
        if (target_guild == nullptr)
            return;//no guild
        auto g_data_it = guild_data.find(guild_id);
        if (g_data_it == guild_data.end())
            return;
        auto & g_data = g_data_it->second;
        if (t[2] == "nickname")
        {
            if (t.size() == 3)
            {
                //empty name
                if (g_data.nick.length() != 0)
                {
                    //not empty. reset it.
                    g_data.nick = "";
                    target_guild->modify_my_nick("");
                }
            }
            else
            {
                std::string newname = base64_decode(std::string{ t[3] });
                if (g_data.nick != newname)
                {
                    g_data.nick = newname;
                    target_guild->modify_my_nick(newname);
                }
            }
            return;
        }
        else if (t[2] == "leave")
        {
            if (auto g = bot.find_guild(guild_id); g)
            {
                g->leave();
            }
            return;
        }

        //check by size for code simplification. string checks above these if they may include these values
        if (t.size() == 4)
        {
            if (t[2] == "ignorebots")
            {
                g_data.ignore_bots = to_bool(t[3]);
            }
            else if (t[2] == "ignoreusers")
            {
                g_data.ignore_users = to_bool(t[3]);
            }
            else if (t[2] == "modrole")
            {
                g_data.mod_role = std::stoull(std::string{ t[3] });
            }
            else if (t[2] == "enablemod")
            {
                for (auto & it : bot_modules)
                {
                    if (it.second.name == t[3])
                    {
                        //matched
                        toggle_mod(g_data, it.first, true);
                        return;
                    }
                }
            }
            else if (t[2] == "disablemod")
            {
                for (auto & it : bot_modules)
                {
                    if (it.second.name == t[3])
                    {
                        //matched
                        toggle_mod(g_data, it.first, false);
                        return;
                    }
                }
            }
            else if (t[2] == "addprefix")
            {
                std::string pfx = base64_decode(std::string{ t[3] });

                auto pfxit = std::find(g_data.cmd_prefix_list.begin(), g_data.cmd_prefix_list.end(), pfx);
                if (pfxit == g_data.cmd_prefix_list.end())
                {
                    //add prefix
                    g_data.cmd_prefix_list.push_back(pfx);
                }
            }
            else if (t[2] == "removeprefix")
            {
                std::string pfx = base64_decode(std::string{ t[3] });

                auto pfxit = std::find(g_data.cmd_prefix_list.begin(), g_data.cmd_prefix_list.end(), pfx);
                if (pfxit != g_data.cmd_prefix_list.end())
                {
                    //remove prefix
                    g_data.cmd_prefix_list.erase(pfxit);
                }
            }
            return;
        }
    }
}

void AegisBot::inject()
{
    using namespace std::placeholders;

    for (uint32_t i = 0; i < bot.shard_max_count; ++i)
        shards.push_back({});

    bot.set_on_message_create(std::bind(&AegisBot::MessageCreate, this, _1));
    bot.set_on_message_delete(std::bind(&AegisBot::MessageDelete, this, _1));
    bot.set_on_message_delete_bulk(std::bind(&AegisBot::MessageDeleteBulk, this, _1));
    bot.set_on_message_update(std::bind(&AegisBot::MessageUpdate, this, _1));
    bot.set_on_message_create_dm(std::bind(&AegisBot::MessageCreateDM, this, _1));
    bot.set_on_channel_create(std::bind(&AegisBot::ChannelCreate, this, _1));
    bot.set_on_channel_update(std::bind(&AegisBot::ChannelUpdate, this, _1));
    bot.set_on_channel_delete(std::bind(&AegisBot::ChannelDelete, this, _1));
    bot.set_on_guild_create(std::bind(&AegisBot::GuildCreate, this, _1));
    bot.set_on_guild_delete(std::bind(&AegisBot::GuildDelete, this, _1));
    bot.set_on_guild_update(std::bind(&AegisBot::GuildUpdate, this, _1));
    bot.set_on_guild_member_add(std::bind(&AegisBot::GuildMemberAdd, this, _1));
    bot.set_on_guild_member_remove(std::bind(&AegisBot::GuildMemberRemove, this, _1));
    bot.set_on_guild_member_update(std::bind(&AegisBot::GuildMemberUpdate, this, _1));
    bot.set_on_guild_member_chunk(std::bind(&AegisBot::GuildMemberChunk, this, _1));
    bot.set_on_guild_ban_add(std::bind(&AegisBot::GuildBanAdd, this, _1));
    bot.set_on_guild_ban_remove(std::bind(&AegisBot::GuildBanRemove, this, _1));
    bot.set_on_presence_update(std::bind(&AegisBot::PresenceUpdate, this, _1));
    bot.set_on_voice_state_update(std::bind(&AegisBot::VoiceStateUpdate, this, _1));
    bot.set_on_voice_server_update(std::bind(&AegisBot::VoiceServerUpdate, this, _1));
    bot.set_on_ready(std::bind(&AegisBot::Ready, this, _1));
    bot.set_on_resumed(std::bind(&AegisBot::Resumed, this, _1));
    bot.set_on_guild_role_create(std::bind(&AegisBot::GuildRoleCreate, this, _1));
    bot.set_on_guild_role_update(std::bind(&AegisBot::GuildRoleUpdate, this, _1));
    bot.set_on_guild_role_delete(std::bind(&AegisBot::GuildRoleDelete, this, _1));
    bot.set_on_user_update(std::bind(&AegisBot::UserUpdate, this, _1));

//     bot.i_typing_start = std::bind(&AegisBot::TypingStart, this, _1);
//     
//     bot.i_guild_emojis_update = std::bind(&AegisBot::GuildEmojisUpdate, this, _1);
//     bot.i_guild_integrations_update = std::bind(&AegisBot::GuildIntegrationsUpdate, this, _1);
//     
}

void AegisBot::TypingStart(aegis::gateway::events::typing_start obj)
{
    //obj.bot->log->info("Typing start");
    return;
}

void AegisBot::MessageCreateDM(aegis::gateway::events::message_create obj)
{
    statsd.Metric<Count>("dms", 1, 1);
    counters.dms++;
    
    const auto &[channel_id, guild_id, message_id, member_id] = obj.msg.get_related_ids();

    auto _member = obj._member;
    if (_member == nullptr)
    {
        //sanity checks
        if (obj._channel == nullptr)
        {
            bot.log->critical("DM Member AND channel does not exist message_id: [{}] channel_id: [{}] msg: [{}]", message_id, channel_id, obj.msg.get_content());
            return;
        }
        bot.log->critical("DM Member does not exist message_id: [{}] channel_id: [{}] member_id: [{}] msg: [{}]", message_id, channel_id, obj.msg.author.id, obj.msg.get_content());
        return;
    }

    if (obj._member->get_username() != obj.msg.author.username)
        bot.log->critical("DM Member name does not match msg: [{}] cache: [{}] message_id: [{}] channel_id: [{}] member_id: [{}] msg: [{}]", obj.msg.author.username, obj._member->get_username(), message_id, channel_id, obj.msg.author.id, obj.msg.get_content());


    std::string_view username{ obj._member->get_username() };

    if (obj._member && obj._member->get_id() == obj.bot->self()->get_id())
        return;

    auto _channel = obj._channel;

    std::string_view content{ obj.msg.get_content() };

    auto toks = split(content, ' ');
    if (toks.empty())
        return;

    std::string lowercommand;
    std::transform(toks[0].begin(), toks[0].end(), lowercommand.begin(), [](const unsigned char c) { return std::tolower(c); });

    //statsd.Metric<Count>("cmds", 1, 1, { fmt::format("cmd:{}", lowercommand) });

/*
    if (bot_control_channel)
    {
        auto ctrl_channel = obj.bot->find_channel(bot_control_channel);
        if (ctrl_channel)
            ctrl_channel->create_message(fmt::format("User: {}\nMessage: {}", username, content));
    }*/

    if (toks[0] == "help")
        _channel->create_message("This bot is in development. If you'd like more information on it, please join the discord server https://discord.gg/Kv7aP5K");

    if (toks[0] == obj.bot->mention)
    {
        //user is mentioning me
        if (toks.empty())
            return;
        if (toks[1] == "help")
        {
            _channel->create_message("This bot is in development. If you'd like more information on it, please join the discord server https://discord.gg/Kv7aP5K");
            return;
        }
        return;
    }

    //check if is owner of bot
    auto is_owner = (member_id == bot_owner_id);



}

std::vector<std::string> AegisBot::lineize(const std::string s)
{
    std::vector<std::string> toks;
    std::string target;
    std::string::size_type n, nxt, offset = 0;
    while (true)
    {
        n = s.find_first_not_of('\n', offset);
        if (n != std::string::npos)
        {
            nxt = s.find_first_of('\n', n);
            if (nxt != std::string::npos)
            {
                std::string ts = s.substr(n, nxt - n);
                if (!ts.empty())
                    toks.emplace_back(ts);
                offset = nxt;
            }
            else//no space found, end of content
            {
                toks.emplace_back(s.substr(n, nxt - n));
                break;
            }
        }
        else//nothing else found
            break;
    }
    return toks;
}

std::vector<std::string_view> AegisBot::tokenize(const std::string_view s, const std::string & delim, std::vector<std::string> groups, bool inclusive)
{
    std::vector<std::string_view> toks;
    std::string target;
    std::string delims = fmt::format(" \n{}", delim);
    std::string::size_type n, nxt, offset = 0;
    while (true)
    {
        n = s.find_first_not_of(delims.c_str(), offset);
        if (n != std::string::npos)//non-space found
        {
            if (s[n] == '"')
            {
                //this is a quoted string so capture it all
                nxt = s.find_first_of('"', n + 1);
                if (nxt != std::string::npos)
                {
                    //second quote found
                    toks.emplace_back(s.substr(n + 1, nxt - n - 1));
                    offset = nxt + 1;
                }
                else
                {
                    //only single quote given
                    //assume to end of content
                    toks.emplace_back(s.substr(n, s.size() - n));
                    break;
                }
            }
            else//first non-space is regular text
            {
                nxt = s.find_first_of(delims.c_str(), n + 1);
                if (nxt != std::string::npos)//found another space
                {
                    toks.emplace_back(s.substr(n, nxt - n));
                    offset = nxt;
                }
                else//no space found, end of content
                {
                    toks.emplace_back(s.substr(n, s.size() - n));
                    break;
                }
            }
        }
        else//nothing else found
            break;
    }
    return toks;
}

std::string AegisBot::tokenize_one(const std::string s, char delim, uint32_t & ctx, std::vector<std::string> groups, bool inclusive)
{
    std::string tok;
    std::string target;
    std::string::size_type n, nxt, offset = 0;
    while (true)
    {
        n = s.find_first_not_of(' ', offset);
        if (n != std::string::npos)//non-space found
        {
            if (s[n] == '"')
            {
                //this is a quoted string so capture it all
                nxt = s.find_first_of('"', n + 1);
                if (nxt != std::string::npos)
                {
                    //second quote found
                    return s.substr(n + 1, nxt - n - 1);
                }
                else
                {
                    //only single quote given
                    throw std::runtime_error("Invalid parameters. No matching quote found.");
                }
            }
            else//first non-space is regular text
            {
                nxt = s.find_first_of(' ', n + 1);
                if (nxt != std::string::npos)//found another space
                {
                    return s.substr(n, nxt - n);
                }
                else//no space found, end of content
                {
                    return s.substr(n, nxt - n);
                }
            }
        }
        else//nothing else found
            break;
    }
    return tok;
}

bool AegisBot::has_perms(Guild & g_data, aegis::member & _member, aegis::guild & _guild, std::string cmd)
{
//     if (g_data._modules[modules::Auction]->enabled)
//     {
//         auto ti = auction_commands_defaults.find(cmd);
//         if (ti != auction_commands_defaults.end())
//             return true;
//     }

    auto ti = g_data.cmds.find(cmd);
    if (ti == g_data.cmds.end())
        return false;

    auto & c = ti->second;
    if (!c.enabled)
        return false;

    if (c.access_type == s_command_data::perm_type::User)
    {
        for (auto & perm : c.user_perms)
        {
            if (perm.first == _member.get_id())
            {
                if (perm.second == s_command_data::perm_access::Allow)
                    return true;
                else
                    return false;
            }
        }
    }
    else if (c.access_type == s_command_data::perm_type::Role)
    {
        for (auto & perm : c.role_perms)
        {
            if (_guild.member_has_role(_member.get_id(), perm.first))
            {
                if (perm.second == s_command_data::perm_access::Allow)
                    return true;
                else
                    return false;
            }
        }
    }
//     else if (c.access_type == s_command_data::perm_type::Internal)
//     {
//         for (auto & perm : c.int_perms)
//         {
//             auto & it = g_data.ranks.find(perm.first);
//             if (it == g_data.ranks.end())
//                 return false;
//             it->second.rank_value
// 
// 
//             if (_guild.member_has_role(_member.get_id(), perm.first))
//             {
//                 if (perm.second.Allow)
//                     return true;
//                 else
//                     return false;
//             }
//         }
//         return false;
//     }

    if (c.default_access == s_command_data::perm_access::Allow)
        return true;

    return false;
}

void AegisBot::MessageCreate(aegis::gateway::events::message_create obj)
{
    if (obj.msg.type == aegis::gateway::objects::GuildMemberJoin)
        return;

//     {
//         aegis::rest::request_params rp;
//         rp.host = "165.227.115.46";
//         rp.port = "9998";
//         json j;
//         j["msgs"] = 1;
//         rp.body = j.dump();
//         bot.get_rest_controller().execute2(rp);
//     }

    counters.messages++;
    statsd.Metric<Count>("msgs", 1, 1);

    shards[obj._shard->get_id()].last_message = std::chrono::steady_clock::now();

    {
        std::unique_lock<std::shared_mutex> l(m_message);
        message_history.insert({ obj.msg.get_id(), obj.msg });
    }
//     auto msg_map_ptr = message_history.load(std::memory_order_acquire);
//     msg_map_ptr->insert({ obj.msg.get_id(), obj.msg });
//     message_history.store(msg_map_ptr, std::memory_order_release);

    const auto &[channel_id, guild_id, message_id, member_id] = obj.msg.get_related_ids();

    if (!obj.has_member())
    {
        if (obj.msg.author.is_webhook())
            return;

        //sanity checks
        if (!obj.has_channel())
            bot.log->error("Member AND channel does not exist message_id: [{}] channel_id: [{}] msg: [{}]", message_id, channel_id, obj.msg.get_content());
        else
            bot.log->error("Member does not exist message_id: [{}] channel_id: [{}] member_id: [{}] msg: [{}]", message_id, channel_id, obj.msg.author.id, obj.msg.get_content());
        return;
    }

    auto & _member = obj.get_member();

    if (_member.get_username() != obj.msg.author.username)
        bot.log->error("Member name does not match msg: [{}] cache: [{}] message_id: [{}] channel_id: [{}] member_id: [{}] msg: [{}]", obj.msg.author.username, obj._member->get_username(), message_id, channel_id, obj.msg.author.id, obj.msg.get_content());

    std::string_view username{ obj._member->get_username() };

    auto & _channel = obj.get_channel();
    auto & _guild = _channel.get_guild();

    Guild & g_data = get_guild(guild_id);

    if (obj.get_member().get_id() == obj.bot->self()->get_id())
    {
        if (obj.msg.nonce == checktime)
        {
            ws_checktime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() - checktime;
            {
                std::lock_guard<std::mutex> lk(m_ping_test);
                cv_ping_test.notify_all();
            }
            return;
        }
    }

    auto it = peeks.find(_channel.get_id());
    if (it != peeks.end())
    {
        std::string peekmsg = fmt::format("Peek: [{}:#{}] [{}]: {}", _guild.get_name(), _channel.get_name(), _member.get_full_name(), obj.msg.get_content());
        bot.log->info(peekmsg);
        asio::post(_io_service, std::bind(&AegisBot::do_peek, this, peekmsg));
    }

    //check if user belongs to global ignore
    {
        auto it = ignored.find(member_id);
        if (it != ignored.end())
            return;
    }

    if (g_data.ignore_bots && obj.msg.is_bot())
        return;

    const aegis::snowflake & guild_owner_id = _guild.get_owner();

    //check if user is guild owner or not
    //override perms for emergency and assistance. this could be accomplished in private
    //without anyone knowing, but instead i would rather it be transparent.
    //alternative "private" methods exist to also manage a guild as bot owner
    //for extreme scenarios where i am unable to communicate with the bot in that
    //specific guild
    bool is_guild_owner = (member_id == guild_owner_id || member_id == bot_owner_id);

    //check if guild is active (guild owner flags their guild to be active) or loaded at all
    //an unloaded guild is one that typically either the bot fails to receive the guild create
    //for or the redis data failed to load
    if (!g_data.loaded && !is_guild_owner)
    {
        //bail out. guild is not set up yet or even loaded and member is not guild owner.
        return;
    }

    std::string content(obj.msg.get_content());
    std::string request;

    bool is_command = false;
    bool is_mention_command = false;

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

    // check if mentioning bot
    if (!obj.msg.mentions.empty())
    {
        if (const auto it = std::find(obj.msg.mentions.begin(), obj.msg.mentions.end(), bot.get_id()); it != obj.msg.mentions.end())
        {
            is_command = is_mention_command = true;
        }
    }
    else
    {
        if (g_data.cmd_prefix_list.empty())
        {
            //prefix does not exist/not set/guild not configured/etc
            //TODO: potentially send DM to guild owner if owner triggered this case to inform of setting a prefix
            return;
        }

        is_command = [&]()
        {
            for (auto & prefix : g_data.cmd_prefix_list)
            {
                if (content.size() == 1 || (content.size() - prefix.size()) == 0 || content.substr(0, prefix.size()) != prefix)
                    continue;

                request = content = content.substr(prefix.size());
                return true;
            }
            return false;
        }();
    }

    //process tokens
    //std::vector<std::string> toks = tokenize(content, ' ', { R"(")" }, false);
    std::vector<std::string_view> toks = tokenize(content, " ");

    if (toks.empty())
    {
        //this should not be possible, but just in case
        return;
    }

    //verified to be a potential command

    if (is_mention_command)
        if ((toks[0][0] == '<') && (toks[0][toks[0].size() - 1] == '>'))
            toks.erase(toks.begin());

    std::string lowercommand(toks[0].size(), ' ');
    std::transform(toks[0].begin(), toks[0].end(), lowercommand.begin(), ::tolower);

    //create shared data object
    shared_data sd{channel_id, guild_id, message_id, member_id, guild_owner_id, username, _member, _channel, _guild, content, g_data, toks, obj, *this};

    try
    {
        if (is_command)
        {
            if (member_id != bot_owner_id && member_id != guild_owner_id)
            {
                if (!has_perms(g_data, _member, _guild, std::string{ toks[0] }))
                    //bot.log->critical("No permission [{}] [{}]", member_id, toks[0]);
                    return;

                if (g_data.ignore_users)
                {
                    //check for mod perms
                    //if (_guild.get_permissions(&_member, &_channel).can_manage_guild)
                    auto & member_g_info = _member.get_guild_info(guild_id);
                    {
                        std::shared_lock<std::shared_mutex> l(_member.mtx());
                        auto & mod_role = _guild.get_role(g_data.mod_role);
                        for (auto & r : member_g_info.roles)
                        {
                            auto & rl = _guild.get_role(r);
                            if (rl.position >= mod_role.position)
                                goto IGNOREUSERSFALSE;
                        }
                    }
                    return;//does not have permission
                }
            IGNOREUSERSFALSE:;
            }

            if (process_admin_messages(obj, sd))
            {
                // is an actual command. log something
                do_log(fmt::format("Admin Command: Guild[{}] G[{}] Channel[{}] User[{}] Cmd[{}]",
                                   _guild.get_name(), _guild.get_id(),
                                   _channel.get_name(), _member.get_full_name(),
                                   obj.msg.get_content()));
                ++command_counters[lowercommand];
                return;
            }

            if (process_user_messages(obj, sd))
            {
                // is an actual command. log something
                do_log(fmt::format("User Command: Guild[{}] G[{}] Channel[{}] User[{}] Cmd[{}]",
                                   _guild.get_name(), _guild.get_id(),
                                   _channel.get_name(), _member.get_full_name(),
                                   obj.msg.get_content()));
                ++command_counters[lowercommand];
                ++counters.commands;
                return;
            }
        }

        for (auto & mod : g_data._modules)
        {
            if (mod.second != nullptr)
            {
                if (mod.second->message_process)
                    if (!mod.second->message_process(sd))
                        return;

                if (is_command)
                    if (mod.second->check_command(std::string{ toks[0] }, sd))
                    {
                        // is an actual command. log something
                        do_log(fmt::format("Module Command: Guild[{}] G[{}] Channel[{}] User[{}] Cmd[{}]",
                                           _guild.get_name(), _guild.get_id(),
                                           _channel.get_name(), _member.get_full_name(),
                                           obj.msg.get_content()));
                        ++command_counters[lowercommand];
                        return;
                    }
            }
            else
                throw std::runtime_error(fmt::format("{} module data does not exist guild_id [{}].", bot_modules[mod.first].name, guild_id));
        }
//         if (module_enabled(g_data, modules::Auction))
//         {
//             if (g_data.auction_data == nullptr)
//             g_data.auction_data->check_command(std::string{ toks[0] }, sd);
//         }
// 
//         if (module_enabled(g_data, modules::Music))
//         {
//             if (g_data.music_data == nullptr)
//                 throw std::runtime_error(fmt::format("Music module data does not exist guild_id [{}].", guild_id));
//             //if (process_music_messages(obj, sd))
//             //  return;
//         }
    }
    catch (std::runtime_error & e)
    {
        do_log(fmt::format("MessageCreate std::runtime_error : {}", e.what()));
        return;
    }
    catch (aegis::rest::rest_reply & e)
    {
        do_log(fmt::format("MessageCreate rest_reply : [{}]", e.reply_code));
        return;
    }
    catch (std::out_of_range & e)
    {
        do_log(fmt::format("MessageCreate std::out_of_range : {}", e.what()));
        return;
    }
    catch (std::error_code & e)
    {
        do_log(fmt::format("MessageCreate std::error_code : [{}] {}", e.value(), e.message()));
        return;
    }
}

void AegisBot::MessageUpdate(aegis::gateway::events::message_update obj)
{
    if (!obj.msg.has_channel() || !obj.msg.has_guild())
    {
        do_log("Guild:Channel not found for message update.");
        return;
    }

    // first update, everything - embed
    // attachments - array
    // author - object
    // channel_id
    // content
    // edited_timestamp
    // embeds - empty array
    // guild_id
    // id
    // mention_everyone
    // mention_roles
    // mentions
    // nonce
    // pinned
    // timestamp
    // tts
    // type
    // 
    // second update, channel_id embeds guild_id id
    {
        std::shared_lock<std::shared_mutex> l(m_message);
        aegis::snowflake message_id = obj.msg.get_id();
        auto it = message_history.find(message_id);
        if (it != message_history.end())
        {
            //findy
            auto & msg = it->second;
            if (obj._member != nullptr)
            {
                //regular message update
                //std::string old_content = msg.get_content();
                //std::string new_content = obj.msg.get_content();
                //do_log(fmt::format("Guild:Channel [{}:{}] - User [{}#{}]\nOriginal:\n{}\nChanged to:\n{}", obj._channel->get_guild().get_name(), obj._channel->get_name(), obj.msg.author.username, obj.msg.author.discriminator, msg.get_content(), obj.msg.get_content()));
                msg = obj.msg;
            }
            else
            {
                //embed update
//                 if (msg.has_member())
//                     do_log(fmt::format("Guild:Channel [{}:{}] - User [{}] Embed update", obj._channel->get_guild().get_name(), obj._channel->get_name(), msg.get_member().get_full_name()));
//                 else
//                     do_log(fmt::format("Guild:Channel [{}:{}] - User [unknown] Embed update", obj._channel->get_guild().get_name(), obj._channel->get_name()));
                if (obj.msg.embeds.empty())
                    msg = obj.msg;
                else
                    msg.embeds = obj.msg.embeds;
            }
            return;
        }
        //no findy
        l.unlock();
        {
            do_log(fmt::format("Guild:Channel [{}:{}] message not found. inserting.", obj._channel->get_guild().get_name(), obj._channel->get_name()));
            std::unique_lock<std::shared_mutex> l(m_message);
            message_history.insert({ obj.msg.get_id(), obj.msg });
        }
        return;
    }
// 
//     if (obj._member == nullptr)
//         do_log(fmt::format("Channel [{}] - Guild [{}] - User [{}#{}] - {}", obj._channel->get_name(), obj._channel->get_guild().get_name(), obj.msg.author.username, obj.msg.author.discriminator, obj.msg.get_content()));
//     else
//         do_log(fmt::format("Channel [{}] - Guild [{}] - User [{}] - {}", obj._channel->get_name(), obj._channel->get_guild().get_name(), obj._member->get_full_name(), obj.msg.get_content()));
//     return;
}

void AegisBot::MessageDelete(aegis::gateway::events::message_delete obj)
{
    auto msg = message_history.find(obj.id);
    if (msg == message_history.end())
        return;//unknown message
    
    //do_log(fmt::format("Guild:Channel [{}:{}] - User [{}#{}] Deleted:\n{}", obj._channel->get_guild().get_name(), obj._channel->get_name(), msg->second.author.username, msg->second.author.discriminator, msg->second.get_content()));
}

void AegisBot::MessageDeleteBulk(aegis::gateway::events::message_delete_bulk obj)
{
    return;
}

void AegisBot::GuildCreate(aegis::gateway::events::guild_create obj)
{
//     {
//         aegis::rest::request_params rp;
//         rp.host = "165.227.115.46";
//         rp.port = "9998";
//         json j;
//         j["guilds"] = 1;
//         rp.body = j.dump();
//         bot.get_rest_controller().execute2(rp);
//     }
    statsd.Metric<Gauge>("guilds", bot.guilds.size(), 1);
 
    aegis::snowflake & guild_id = obj._guild.guild_id;
   
    auto & g_data = guild_data[guild_id];

    if (g_data.loaded)
        return;//already loaded. this catches an outage creating an existing guild

    do_log(fmt::format("Loading guild [{}] on shard [{}]", guild_id, obj._shard->get_id()));

    g_data.redis_prefix = fmt::format("{}:{}", r_config, guild_id);
    g_data.cached = to_bool(hget({ g_data.redis_prefix, "cached" }));
//     std::string guild_msg = fmt::format("Shard#{} : CREATED Guild: {} [T:{}] [{}]", obj._shard->get_id(), obj._guild.guild_id, bot.guilds.size(), obj._guild.name);
//     if (!g_data.cached && bot_control_channel)
//     {
//         auto control_channel = bot.find_channel(bot_control_channel);
//         if (control_channel != nullptr)
//             control_channel->create_message(guild_msg);
//     }

    //store roles
    {
        std::string guild_roles_key{ fmt::format("{}:roles", g_data.redis_prefix) };
        for (const auto & r : obj._guild.roles)
        {
            std::string role_key{ fmt::format("{}:roles:{}", g_data.redis_prefix, r.role_id) };

            std::vector<std::string> vals;
            vals.emplace_back(role_key);
            vals.emplace_back("name");
            vals.emplace_back(r.name);
            vals.emplace_back("position");
            vals.emplace_back(std::to_string(r.position));
            vals.emplace_back("color");
            vals.emplace_back(std::to_string(r.color));
            vals.emplace_back("hoist");
            vals.emplace_back(r.hoist?"1":"0");
            vals.emplace_back("managed");
            vals.emplace_back(r.managed?"1":"0");
            vals.emplace_back("mentionable");
            vals.emplace_back(r.mentionable?"1":"0");
            vals.emplace_back("permission");
            vals.emplace_back(std::to_string((int64_t)r._permission));
            hmset(vals);
        }
        std::vector<std::string> rls;
        rls.emplace_back(guild_roles_key);
        for (const auto & r : obj._guild.roles)
            rls.emplace_back(std::to_string(r.role_id));

        sadd(rls);
    }

    //store channels
    {
        std::string guild_channels_key{ fmt::format("{}:channels", g_data.redis_prefix) };
        for (const auto & r : obj._guild.channels)
        {
            std::string channel_key{ fmt::format("{}:channels:{}", g_data.redis_prefix, r.channel_id) };

            std::vector<std::string> vals;
            vals.emplace_back(channel_key);
            vals.emplace_back("name");
            vals.emplace_back(r.name);
            vals.emplace_back("position");
            vals.emplace_back(std::to_string(r.position));
            vals.emplace_back("bitrate");
            vals.emplace_back(std::to_string(r.bitrate));
            vals.emplace_back("userlimit");
            vals.emplace_back(std::to_string(r.userlimit));
            vals.emplace_back("type");
            vals.emplace_back(std::to_string(r.type));
            vals.emplace_back("topic");
            vals.emplace_back(r.topic);
            vals.emplace_back("nsfw");
            vals.emplace_back(std::to_string(r.nsfw));
            vals.emplace_back("parent_id");
            vals.emplace_back(std::to_string(r.parent_id));
            hmset(vals);
        }
        std::vector<std::string> rls;
        rls.emplace_back(guild_channels_key);
        for (const auto & r : obj._guild.channels)
            rls.emplace_back(std::to_string(r.channel_id));

        sadd(rls);
    }

    load_guild(guild_id, g_data);

    //save data

    std::vector<std::string> vals;
    vals.emplace_back(g_data.redis_prefix);
    vals.emplace_back("name");
    vals.emplace_back(obj._guild.name);
    vals.emplace_back("verification_level");
    vals.emplace_back(std::to_string(obj._guild.verification_level));
    vals.emplace_back("region");
    vals.emplace_back(obj._guild.region);
    vals.emplace_back("mfa_level");
    vals.emplace_back(std::to_string(obj._guild.mfa_level));
    vals.emplace_back("icon");
    vals.emplace_back(obj._guild.icon);
    vals.emplace_back("joined_at");
    vals.emplace_back(obj._guild.joined_at);
    vals.emplace_back("splash");
    vals.emplace_back(obj._guild.splash);
    hmset(vals);
    auto g = bot.find_guild(guild_id);
    if (!g)
        return;
    hset({ g_data.redis_prefix, "perms", std::to_string(g->base_permissions()) });
}

void AegisBot::load_guild(aegis::snowflake guild_id, Guild & g_data)
{
    aegis::shards::shard * _shard = &bot.get_shard_by_guild(guild_id);

    if (!g_data.cached)
    {
        g_data.cached = true;
        hset({ g_data.redis_prefix, "cached", "1" });
    }

    // load guild data
    g_data.guild_id = guild_id;

    auto gid = std::to_string(guild_id);
    sadd({ "config:guilds", gid });
    srem({ "config:guilds_deleted", gid });

    // Set up a new guild
    if (std::string isnew = hget({ g_data.redis_prefix, "setup" }); isnew.empty())
    {
        for (auto & cmd : commands)
        {
            auto &[b, a, t] = commands_defaults[cmd];

            std::string key_path{ fmt::format("{}:cmds:{}", g_data.redis_prefix, cmd) };

            auto & c = g_data.cmds[cmd];
            c.enabled = (b == "1" ? true : false);
            c.default_access = (a == "1" ? s_command_data::perm_access::Allow : s_command_data::perm_access::Deny);
            c.access_type = (t == "0" ? s_command_data::perm_type::User : (t == "1" ? s_command_data::perm_type::Role : s_command_data::perm_type::Internal));

//             if (!hset({ key_path, "enabled", b }))
//             {
//                 bot.log->error("Unable to set up new guild enabled redis values: [{}]", cmd);
//                 return;
//             }
            hmset({ key_path, "enabled", b, "default_access", a, "access_type", t });
//             hset({ key_path, "enabled", b });
//             hset({ key_path, "default_access", a });
//             hset({ key_path, "access_type", t });
//             if (!hset({ key_path, "default_access", a }))
//             {
//                 bot.log->error("Unable to set up new guild default_access redis values: [{}]", cmd);
//                 return;
//             }
//             if (!hset({ key_path, "access_type", t }))
//             {
//                 bot.log->error("Unable to set up new guild access_type redis values: [{}]", cmd);
//                 return;
//             }
        }
        hmset({ g_data.redis_prefix, "ignorebots", "1", "ignoreusers", "0", "nickname", "", "modrole", "0", "setup", "1" });
    }
    else
    {
        //load settings data
        auto gsettings = get_array(g_data.redis_prefix);
        g_data.ignore_bots = to_bool(gsettings["ignorebots"]);
        g_data.ignore_users = to_bool(gsettings["ignoreusers"]);
        g_data.mod_role = to_bool(gsettings["modrole"]);
        g_data.nick = gsettings["nickname"];
            
        if (auto prefix = get_vector(fmt::format("{}:prefix", g_data.redis_prefix)); !prefix.empty())
        {
            g_data.cmd_prefix_list = std::move(prefix);
        }
    }

    //load module data for this guild

    auto mods_enabled = get_vector(fmt::format("{}:modules", g_data.redis_prefix));

    for (const auto & i : mods_enabled)
    {
        //check if module is already enabled. don't double enable
        auto res = static_cast<modules>(std::stoi(i));
        auto it = g_data._modules.find(res);
        if (it != g_data._modules.end())
            continue;
        if (res == modules::Auction)
            g_data._modules.emplace(res, new mod_auction(g_data, bot.get_io_context()));
        else if (res == modules::Music)
            g_data._modules.emplace(res, new mod_music(g_data, bot.get_io_context()));
        else if (res == modules::Announcer)
            g_data._modules.emplace(res, new mod_announcer(g_data, bot.get_io_context()));
        else if (res == modules::Autoresponder)
            g_data._modules.emplace(res, new mod_autoresponder(g_data, bot.get_io_context()));
        else if (res == modules::Autoroles)
            g_data._modules.emplace(res, new mod_autoroles(g_data, bot.get_io_context()));
        else if (res == modules::Moderation)
            g_data._modules.emplace(res, new mod_moderation(g_data, bot.get_io_context()));
        else if (res == modules::Tags)
            g_data._modules.emplace(res, new mod_tag(g_data, bot.get_io_context()));
        else if (res == modules::Log)
            g_data._modules.emplace(res, new mod_log(g_data, bot.get_io_context()));
        else if (res == modules::Automod)
            g_data._modules.emplace(res, new mod_automod(g_data, bot.get_io_context()));
        else
            continue;
        g_data._modules[res]->admin_override = to_bool(hget({ fmt::format("{}:{}", g_data.redis_prefix, bot_modules[res].prefix), "admin_override" }));
        g_data._modules[res]->enabled = true;
        g_data._modules[res]->load(*this);
    }

    for (auto & cmd : commands)
    {
        auto &[b, a, t] = commands_defaults[cmd];

        //g_data.cmds.emplace(cmd, s_command_data{ b, a, t });


        std::string key_path{ fmt::format("{}:cmds:{}", g_data.redis_prefix, cmd) };

        auto r_cmds = get_array(key_path);

        //ensure all commands exist
        if (const auto & it = g_data.cmds.find(cmd); it == g_data.cmds.end())
        {
            //command not set up for server
            auto & c = g_data.cmds[cmd];
            c.enabled = (b == "1" ? true : false);
            c.default_access = (a == "1" ? s_command_data::perm_access::Allow : s_command_data::perm_access::Deny);
            c.access_type = (t == "0" ? s_command_data::perm_type::User : (t == "1" ? s_command_data::perm_type::Role : s_command_data::perm_type::Internal));
            if (!hset({ key_path, "enabled", b }))
                bot.log->error("Unable to set up new guild enabled redis values: [{}]", cmd);
            if (!hset({ key_path, "default_access", a }))
                bot.log->error("Unable to set up new guild default_access redis values: [{}]", cmd);
            if (!hset({ key_path, "access_type", t }))
                bot.log->error("Unable to set up new guild access_type redis values: [{}]", cmd);
        }
        else
        {
            auto & c = g_data.cmds[cmd];
            c.enabled = (r_cmds["enabled"] == "1" ? true : false);
            c.default_access = (r_cmds["default_access"] == "1" ? s_command_data::perm_access::Allow : s_command_data::perm_access::Deny);
            c.access_type = (r_cmds["access_type"] == "0" ? s_command_data::perm_type::User : (r_cmds["access_type"] == "1" ? s_command_data::perm_type::Role : s_command_data::perm_type::Internal));
        }

        if (auto u_perms = get_array(fmt::format("{}:u_perms", key_path)); !u_perms.empty())
            for (auto &[uid, v] : u_perms)
                g_data.cmds[cmd].user_perms[std::stoull(uid)] = (v == "1" ? s_command_data::perm_access::Allow : s_command_data::perm_access::Deny);

        if (auto r_perms = get_array(fmt::format("{}:r_perms", key_path)); !r_perms.empty())
            for (auto &[uid, v] : r_perms)
                g_data.cmds[cmd].role_perms[std::stoull(uid)] = (v == "1" ? s_command_data::perm_access::Allow : s_command_data::perm_access::Deny);

        if (auto i_perms = get_array(fmt::format("{}:i_perms", key_path)); !i_perms.empty())
            for (auto &[uid, v] : i_perms)
                g_data.cmds[cmd].int_perms[std::stoull(uid)] = (v == "1" ? s_command_data::perm_access::Allow : s_command_data::perm_access::Deny);
    }

    //load user permissions
    if (std::string ranks = get(fmt::format("{}:ranks", g_data.redis_prefix)); !ranks.empty())
    {

    }

    //ignored channels
    {
        if (auto ignored_ch = get_vector(fmt::format("{}:ignored_channels", g_data.redis_prefix)); !ignored_ch.empty())
        {
            for (const auto & id : ignored_ch)
                g_data.ignored_channels.emplace_back(std::stoull(id));
        }
    }

    g_data.loaded = true;
}

void AegisBot::toggle_mod(Guild & g_data, const modules mod, const bool toggle)
{
    if (toggle)
        sadd({ fmt::format("{}:modules", g_data.redis_prefix), std::to_string(mod) });
    else
        srem({ fmt::format("{}:modules", g_data.redis_prefix), std::to_string(mod) });
    if (!toggle)
    {
        if (g_data._modules[mod] == nullptr)
        {
            bot.log->error("Guild mod({}) toggle: [{}] does not exist", mod, g_data.guild_id);
            return;
        }
        g_data._modules[mod]->do_stop();
        g_data._modules[mod]->enabled = toggle;
    }
    else
    {
        if (mod == modules::Auction)
            g_data._modules.emplace(mod, new mod_auction(g_data, bot.get_io_context()));
        else if (mod == modules::Music)
            g_data._modules.emplace(mod, new mod_music(g_data, bot.get_io_context()));
        else if (mod == modules::Announcer)
            g_data._modules.emplace(mod, new mod_announcer(g_data, bot.get_io_context()));
        else if (mod == modules::Autoresponder)
            g_data._modules.emplace(mod, new mod_autoresponder(g_data, bot.get_io_context()));
        else if (mod == modules::Autoroles)
            g_data._modules.emplace(mod, new mod_autoroles(g_data, bot.get_io_context()));
        else if (mod == modules::Moderation)
            g_data._modules.emplace(mod, new mod_moderation(g_data, bot.get_io_context()));
        else if (mod == modules::Tags)
            g_data._modules.emplace(mod, new mod_tag(g_data, bot.get_io_context()));
        else if (mod == modules::Log)
            g_data._modules.emplace(mod, new mod_log(g_data, bot.get_io_context()));
        else if (mod == modules::Automod)
            g_data._modules.emplace(mod, new mod_automod(g_data, bot.get_io_context()));
        else
            throw std::runtime_error(fmt::format("{} invalid module id - guild_id [{}].", mod, g_data.guild_id));
        g_data._modules[mod]->enabled = toggle;
        g_data._modules[mod]->load(*this);
    }
}

void AegisBot::toggle_mod_override(Guild & g_data, const modules mod, const bool toggle)
{
    hset({ fmt::format("{}:{}", g_data.redis_prefix, bot_modules[mod].prefix), "admin_override", (toggle ? "1" : "0") });
    g_data._modules[mod]->admin_override = toggle;
}

aegis::snowflake AegisBot::get_snowflake(const std::string_view name, aegis::guild & _guild) const noexcept
{
    if (name.empty())
        return { 0 };
    try
    {
        if (name[0] == '<')
        {
            //mention param

            std::string::size_type pos = name.find_first_of('>');
            if (pos == std::string::npos)
                return { 0 };
            if (name[2] == '!')//mobile mention. strip <@!
                return std::stoull(std::string{ name.substr(3, pos - 1) });
            else  if (name[2] == '&')//role mention. strip <@&
                return std::stoull(std::string{ name.substr(3, pos - 1) });
            else  if (name[1] == '#')//channel mention. strip <#
                return std::stoull(std::string{ name.substr(2, pos - 1) });
            else//regular mention. strip <@
                return std::stoull(std::string{ name.substr(2, pos - 1) });
        }
        else if (std::isdigit(name[0]))
        {
            //snowflake param
            return std::stoull(std::string{ name });
        }
        else
        {
            //most likely username#discriminator param
            std::string::size_type n = name.find('#');
            if (n != std::string::npos)
            {
                //found # separator
                std::shared_lock<std::shared_mutex> l(_guild.mtx());
                for (auto & m : _guild.get_members())
                    if (m.second->get_full_name() == name)
                        return { m.second->get_id() };
                return { 0 };
            }
            return { 0 };//# not found. unknown parameter. unicode may trigger this.
        }
    }
    catch (std::invalid_argument &)
    {
        return { 0 };
    }
}

std::tuple<AegisBot::mention_type, aegis::snowflake> AegisBot::analyze_mention(std::string_view str) const noexcept
{
    if (str.empty())
        return { Fail, {0} };
    try
    {
        if ((str.front() == '<') && (str.back() == '>'))
        {
            // nickname
            if ((str[1] == '@') && (str[2] == '!'))
            {
                str.remove_suffix(1);
                str.remove_prefix(3);
                return { Nickname, std::stoull(std::string{ str }) };
            }
            // role
            else if ((str[1] == '@') && (str[2] == '&'))
            {
                str.remove_suffix(1);
                str.remove_prefix(3);
                return { Role, std::stoull(std::string{ str }) };
            }
            // user
            else if (str[1] == '@')
            {
                str.remove_suffix(1);
                str.remove_prefix(2);
                return { User, std::stoull(std::string{ str }) };
            }
            // channel
            else if (str[1] == '#')
            {
                str.remove_suffix(1);
                str.remove_prefix(2);
                return { Channel, std::stoull(std::string{ str }) };
            }
            // emoji
            else if (str[1] == ':')
            {
                auto pos = str.find_last_of(':');
                if (pos == std::string::npos)
                    return { Fail, {0} };
                str.remove_suffix(1);
                str.remove_prefix(pos+1);
                return { Emoji, std::stoull(std::string{ str }) };
            }
            // animated emoji
            else if ((str[1] == 'a') && (str[2] == ':'))
            {
                auto pos = str.find_last_of(':');
                if (pos == std::string::npos)
                    return { Fail, {0} };
                str.remove_suffix(1);
                str.remove_prefix(pos+1);
                return { AnimatedEmoji, std::stoull(std::string{ str }) };
            }
            else
            {
                return { Fail, {0} };
            }
        }
    }
    catch (std::invalid_argument &)
    {
    }
    return { Fail, {0} };
}

bool AegisBot::hset(const std::string_view key, std::vector<std::string> value, Guild & g_data)
{
    return basic_action("HSET", fmt::format("{}:{}", g_data.redis_prefix, key), value);
}

std::string AegisBot::hget(const std::string_view key, std::vector<std::string> value, Guild & g_data)
{
    return result_action("HGET", fmt::format("{}:{}", g_data.redis_prefix, key), value);
}

std::unordered_map<std::string, std::string> AegisBot::get_array(const std::string_view key, Guild & g_data)
{
    return get_array(fmt::format("{}:{}", g_data.redis_prefix, key));
}

std::vector<std::string> AegisBot::get_vector(const std::string_view key, Guild & g_data)
{
    return get_vector(fmt::format("{}:{}", g_data.redis_prefix, key));
}

std::string AegisBot::result_action(const std::string_view action, const std::vector<std::string> & value)
{
    std::lock_guard<std::mutex> lock(r_mutex);
    try
    {
        redisclient::RedisValue result;
        std::deque<redisclient::RedisBuffer> v;
        for (auto & k : value)
            v.emplace_back(k.data());
        result = redis.command(std::string{ action }, v);
        if (result.isOk())
            return result.toString();
        else if (result.isError())
        {
            std::stringstream ss;
            for (const auto & a : value)
                ss << a << ' ';
            throw aegis::exception(fmt::format("result_action({}) failure: {} || {}", action, result.toString(), ss.str()), aegis::make_error_code(aegis::error::bad_redis_request));
        }
        throw aegis::exception(fmt::format("result_action({}) failure (no error, no ok)", action), aegis::make_error_code(aegis::error::bad_redis_request));
    }
    catch (std::exception & e)
    {
        //reconstruct query
        fmt::MemoryWriter w;
        w << action;
        for (auto & v : value)
            w << ' ' << v;
        bot.log->error("E: {} || {}", e.what(), w.str());
    }
    return {};
}

std::string AegisBot::result_action(const std::string_view action, const std::string_view key, const std::vector<std::string> & value)
{
    std::lock_guard<std::mutex> lock(r_mutex);
    try
    {
        redisclient::RedisValue result;
        std::deque<redisclient::RedisBuffer> v;
        v.emplace_back(key.data());
        for (auto & k : value)
            v.emplace_back(k.data());
        result = redis.command(std::string{ action }, v);
        if (result.isOk())
            return result.toString();
        else if (result.isError())
        {
            std::stringstream ss;
            ss << key << ' ';
            for (const auto & a : value)
                ss << a << ' ';
            throw aegis::exception(fmt::format("result_action({}) failure: {} || {}", action, result.toString(), ss.str()), aegis::make_error_code(aegis::error::bad_redis_request));
        }
        throw aegis::exception(fmt::format("result_action({}) failure (no error, no ok)", action), aegis::make_error_code(aegis::error::bad_redis_request));
    }
    catch (std::exception & e)
    {
        //reconstruct query
        fmt::MemoryWriter w;
        w << action;
        for (auto & v : value)
            w << ' ' << v;
        bot.log->error("E: {} || {}", e.what(), w.str());
    }
    return {};
}

std::future<std::string> AegisBot::async_get_result(const std::string_view action, const std::string_view key, const std::vector<std::string> & value)
{
    using result = asio::async_result<asio::use_future_t<>, void(std::string)>;
    using handler = typename result::completion_handler_type;

    handler exec(asio::use_future);
    result ret(exec);

    asio::post(_io_service, asio::bind_executor(strand, [exec, this, action = std::string{ action }, key = std::string{ key }, value]() mutable
    {
        exec(result_action(action, key, value));
    }));

    return ret.get();
}

std::future<aegis::rest::rest_reply> AegisBot::req_success(aegis::gateway::objects::message & msg)
{
    return msg.create_reaction("success:429554838083207169");
}

std::future<aegis::rest::rest_reply> AegisBot::req_fail(aegis::gateway::objects::message & msg)
{
    return msg.create_reaction("fail:429554869611921408");
}

std::future<aegis::rest::rest_reply> AegisBot::req_permission(aegis::gateway::objects::message & msg)
{
    return msg.create_reaction("no_permission:451495171779985438");
}

bool AegisBot::sadd(const std::string_view key, const std::vector<std::string> & value, Guild & g_data)
{
    return basic_action("SADD", fmt::format("{}:{}", g_data.redis_prefix, key), value);
}

bool AegisBot::srem(const std::string_view key, const std::vector<std::string> & value, Guild & g_data)
{
    return basic_action("SREM", fmt::format("{}:{}", g_data.redis_prefix, key), value);
}

bool AegisBot::basic_action(const std::string_view action, const std::string_view key, const std::vector<std::string> & value)
{
    asio::post(_io_service, asio::bind_executor(strand, [this, key = std::string{ key }, action = std::string{ action }, value = std::move(value)]()
    {
        std::lock_guard<std::mutex> lock(r_mutex);
        try
        {
            redisclient::RedisValue result;
            std::deque<redisclient::RedisBuffer> v;
            v.emplace_back(key.data());
            for (auto & k : value)
                v.emplace_back(k.data());
            result = redis.command(std::string{ action }, v);
            if (result.isOk())
                return;
            else if (result.isError())
            {
                std::stringstream ss;
                ss << key << ' ';
                for (const auto & a : value)
                    ss << a << ' ';
                throw aegis::exception(fmt::format("basic_action({}) failure: {} || {}", action, result.toString(), ss.str()), aegis::make_error_code(aegis::error::bad_redis_request));
            }
            throw aegis::exception(fmt::format("result_action({}) failure (no error, no ok)", action), aegis::make_error_code(aegis::error::bad_redis_request));
        }
        catch (std::exception & e)
        {
            bot.log->error("E: {}", e.what());
        }
    }));
    return true;
}

bool AegisBot::basic_action(const std::string_view action, const std::vector<std::string> & value)
{
    asio::post(_io_service, asio::bind_executor(strand, [this, action = std::string{ action }, value = std::move(value)]()
    {
        try
        {
            std::lock_guard<std::mutex> lock(r_mutex);
            redisclient::RedisValue result;
            std::deque<redisclient::RedisBuffer> v;
            for (auto & k : value)
                v.emplace_back(k.data());
            result = redis.command(std::string{ action }, v);
        }
        catch (std::exception & e)
        {
            bot.log->error("E: {}", e.what());
        }
    }));
    return true;
}

void AegisBot::event_log(const json & j, const std::string & path)
{
    aegis::rest::request_params rp;
    rp.host = logging_address;
    rp.port = logging_port;
    if (is_production)
        rp.path = path;
    else
        rp.path = "/test" + path;

    rp.body = j.dump();
    bot.get_rest_controller().execute2(std::forward<aegis::rest::request_params>(rp));
}

bool AegisBot::valid_command(std::string_view cmd)
{
    auto it = std::find(commands.begin(), commands.end(), std::string{ cmd });
    if (it == commands.end())
        return false;
    return true;
}

bool AegisBot::command_enabled(aegis::snowflake guild_id, std::string cmd)
{
    auto it = guild_data.find(guild_id);
    if (it == guild_data.end())
        return false;
    return command_enabled(it->second, cmd);
}

bool AegisBot::command_enabled(Guild & g_data, std::string cmd)
{
    auto it = g_data.cmds.find(cmd);
    if (it == g_data.cmds.end())
        return false;
    return it->second.enabled;
}

bool AegisBot::module_enabled(modules mod) const noexcept
{
    auto m = bot_modules.find(mod);
    if (m == bot_modules.end())
        return false;//module does not exist
    if (m->second.enabled)
        return true;
    return false;
}

bool AegisBot::module_enabled(Guild & g_data, modules mod) const noexcept
{
    auto m = g_data._modules.find(mod);
    if (m == g_data._modules.end())
        return false;//module does not exist
    if ((m->second->enabled) || (m->second->admin_override))
        return module_enabled(mod);
    return false;
}

void AegisBot::GuildUpdate(aegis::gateway::events::guild_update obj)
{
    return;
}

void AegisBot::GuildDelete(aegis::gateway::events::guild_delete obj)
{
    //outage. ignore
    if (obj.unavailable)
        return;
  
    aegis::snowflake & guild_id = obj.guild_id;

    auto & g_data = guild_data[guild_id];

    auto _guild = bot.find_guild(guild_id);
    if (_guild == nullptr)
    {
        bot.log->critical("Guild Delete: [{}] does not exist", guild_id);
        //this should never happen
        return;
    }

    g_data.cached = false;
    g_data.loaded = false;
    hset({ g_data.redis_prefix, "cached", "0" });
/*    hdel({ "" });*/

    auto gid = std::to_string(guild_id);
    sadd({ "config:guilds_deleted", gid });
    srem({ "config:guilds", gid });

//     std::string guild_msg = fmt::format("Shard#{} : DELETED Guild: {} [T:{}] [{}]", obj._shard->get_id(), guild_id, bot.guilds.size(), _guild->get_name());
//     if (bot_control_channel && bot.find_channel(bot_control_channel) != nullptr)
//         bot.find_channel(bot_control_channel)->create_message(guild_msg);
}

Guild & AegisBot::get_guild(aegis::snowflake id)
{
    auto it = guild_data.find(id);
    if (it == guild_data.end())
    {
        auto a = guild_data.emplace(id, Guild());
        return a.first->second;
    }
    return it->second;
}

void AegisBot::UserUpdate(aegis::gateway::events::user_update obj)
{
    auto & m = obj._user;

    // config:member:id
    std::string user_key{ fmt::format("{}:{}", r_m_config, obj._user.id) };

    // user specific data
    std::vector<std::string> vals;
    vals.emplace_back(user_key);
    vals.emplace_back("avatar");
    vals.emplace_back(m.avatar);
    vals.emplace_back("discriminator");
    vals.emplace_back(m.discriminator);
    vals.emplace_back("bot");
    vals.emplace_back(std::to_string(m.is_bot()));
    vals.emplace_back("username");
    vals.emplace_back(m.username);
    vals.emplace_back("last_update");
    vals.emplace_back(std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
    hmset(vals);
}

void AegisBot::Ready(aegis::gateway::events::ready obj)
{
    shards[obj._shard->get_id()].last_message = std::chrono::steady_clock::now();

    json j;
    j["shard:connects"] = 1;
    event_log(j, "/shards");
}

void AegisBot::Resumed(aegis::gateway::events::resumed obj)
{
    shards[obj._shard->get_id()].last_message = std::chrono::steady_clock::now();

    json j;
    j["shard:resumes"] = 1;
    event_log(j, "/shards");
}

void AegisBot::ChannelCreate(aegis::gateway::events::channel_create obj)
{
    if (obj._channel.type == aegis::gateway::objects::channel::DirectMessage)
    {
        auto it = dm_channels.find(obj._channel.recipients.at(0).id);
        if (it == dm_channels.end())
        {
            dm_channels.emplace(obj._channel.recipients.at(0).id, obj._channel.channel_id);
            hset({ "config:dms", std::to_string(obj._channel.recipients.at(0).id), std::to_string(obj._channel.channel_id) });
        }
    }
}

void AegisBot::ChannelUpdate(aegis::gateway::events::channel_update obj)
{
}

void AegisBot::ChannelDelete(aegis::gateway::events::channel_delete obj)
{
}

void AegisBot::GuildBanAdd(aegis::gateway::events::guild_ban_add obj)
{
}

void AegisBot::GuildBanRemove(aegis::gateway::events::guild_ban_remove obj)
{
    struct profiler_data
    {
        profiler_data(std::chrono::steady_clock::time_point s, std::string & name) : start_time(s), segment_name(name) {}
        std::chrono::steady_clock::time_point start_time;
        std::chrono::steady_clock::time_point stop_time;
        std::string segment_name;
    };

    std::string name;
    profiler_data * _pd = nullptr;

    std::vector<profiler_data> res;

    res.emplace_back(std::chrono::steady_clock::now(), name);
}

// void AegisBot::GuildEmojisUpdate(aegis::gateway::events::guild_emojis_update obj)
// {
// }
// 
// void AegisBot::GuildIntegrationsUpdate(aegis::gateway::events::guild_integrations_update obj)
// {
// }

void AegisBot::GuildMemberAdd(aegis::gateway::events::guild_member_add obj)
{
    if (obj._member._user.id > 0)
    {
        aegis::snowflake guild_id = obj._member.guild_id;
        Guild & g_data = get_guild(guild_id);

        std::string user_key{ fmt::format("{}:{}", r_m_config, obj._member._user.id) };
        std::string guild_key{ fmt::format("{}:{}:guild:{}", r_m_config, obj._member._user.id, guild_id) };

        auto & m = obj._member._user;
        auto & md = obj._member;

        {
            auto sdm_ch = dm_channels.find(md._user.id);
            if (sdm_ch == dm_channels.end())
            {
                std::string sdm_ch_str = hget({ "config:dms", std::to_string(md._user.id) });
                if (!sdm_ch_str.empty())
                {
                    try
                    {
                        dm_channels.emplace(md._user.id, std::stoull(sdm_ch_str));
                    }
                    catch (std::exception & e)
                    {}
                }
            }
            else
                dm_channels.emplace(md._user.id, sdm_ch->second);
        }

        std::vector<std::string> vals;
        vals.emplace_back(user_key);
        vals.emplace_back("avatar");
        vals.emplace_back(m.avatar);
        vals.emplace_back("discriminator");
        vals.emplace_back(m.discriminator);
        vals.emplace_back("bot");
        vals.emplace_back(std::to_string(m.is_bot()));
        vals.emplace_back("username");
        vals.emplace_back(m.username);
        hmset(vals);

        hmset({ guild_key, "nick", md.nick, "joined_at", md.joined_at });

//         hset({ user_key, "avatar", m.avatar });
//         hset({ user_key, "discriminator", m.discriminator });
//         hset({ user_key, "bot", std::to_string(m.is_bot()) });
//         hset({ user_key, "username", m.username });
// 
//         hset({ guild_key, "nick", md.nick });
//         hset({ guild_key, "joined_at", md.joined_at });

        if (!md.roles.empty())
        {
            std::string roles_key{ fmt::format("{}:{}:guild:{}:roles", r_m_config, obj._member._user.id, guild_id) };

            del(fmt::format("{}:guild:{}:roles", user_key, guild_id));

            std::vector<std::string> rls;
            rls.emplace_back(roles_key);
            for (const auto & r : md.roles)
                rls.emplace_back(std::to_string(r));

            sadd(rls);
        }
    }
}

void AegisBot::GuildMemberRemove(aegis::gateway::events::guild_member_remove obj)
{
}

void AegisBot::GuildMemberUpdate(aegis::gateway::events::guild_member_update obj)
{
    auto & m = obj._user;

    aegis::snowflake guild_id = obj.guild_id;
    Guild & g_data = get_guild(guild_id);

    // config:member:id
    std::string user_key{ fmt::format("{}:{}", r_m_config, obj._user.id) };
    // config:member:id:guild:guildid
    std::string guild_key{ fmt::format("{}:{}:guild:{}", r_m_config, obj._user.id, guild_id) };

    // user specific data
    {
        std::vector<std::string> vals;
        vals.emplace_back(user_key);
        vals.emplace_back("avatar");
        vals.emplace_back(m.avatar);
        vals.emplace_back("discriminator");
        vals.emplace_back(m.discriminator);
        vals.emplace_back("bot");
        vals.emplace_back(std::to_string(m.is_bot()));
        vals.emplace_back("username");
        vals.emplace_back(m.username);
        vals.emplace_back("last_update");
        vals.emplace_back(std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
        hmset(vals);
    }

    // guild specific data
    hset({ guild_key, "nick", obj.nick });

    if (!obj.roles.empty())
    {
        // config:member:id:guild:guildid:roles
        std::string roles_key{ fmt::format("{}:guild:{}:roles", user_key, guild_id) };

        del(fmt::format("{}:guild:{}:roles", user_key, guild_id));

        std::vector<std::string> rls;
        rls.emplace_back(roles_key);
        for (const auto & r : obj.roles)
            rls.emplace_back(std::to_string(r));

        sadd(rls);
    }
}

void AegisBot::GuildMemberChunk(aegis::gateway::events::guild_members_chunk obj)
{
    auto old_time = std::chrono::steady_clock::now();
    
    for (const auto & md : obj.members)
    {
        if (md._user.id > 0)
        {
            aegis::snowflake guild_id = obj.guild_id;
            //Guild & g_data = get_guild(guild_id);

            // config:member:id
            std::string user_key{ fmt::format("{}:{}", r_m_config, md._user.id) };
            // config:member:id:guild:guildid
            std::string guild_key{ fmt::format("{}:{}:guild:{}", r_m_config, md._user.id, guild_id) };

            auto & m = md._user;

            auto sdm_ch = dm_channels.find(md._user.id);
            if (sdm_ch != dm_channels.end())
                dm_channels.emplace(md._user.id, sdm_ch->second);

            {
                std::vector<std::string> vals;
                vals.emplace_back(user_key);
                vals.emplace_back("avatar");
                vals.emplace_back(m.avatar);
                vals.emplace_back("discriminator");
                vals.emplace_back(m.discriminator);
                vals.emplace_back("bot");
                vals.emplace_back(std::to_string(m.is_bot()));
                vals.emplace_back("username");
                vals.emplace_back(m.username);
                vals.emplace_back("last_update");
                vals.emplace_back(std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
                hmset(vals);
            }
            {
                std::vector<std::string> vals;
                vals.emplace_back(guild_key);
                vals.emplace_back("nick");
                vals.emplace_back(md.nick);
                vals.emplace_back("joined_at");
                vals.emplace_back(md.joined_at);
                hmset(vals);
            }

            if (!md.roles.empty())
            {
                std::string roles_key{ fmt::format("{}:guild:{}:roles", user_key, guild_id) };

                del(fmt::format("{}:guild:{}:roles", user_key, guild_id));

                std::vector<std::string> rls;
                rls.emplace_back(roles_key);
                for (const auto & r : md.roles)
                    rls.emplace_back(std::to_string(r));

                sadd(rls);
            }
        }
    }
    bot.log->trace("count:{} dura:{}us", obj.members.size(), std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - old_time).count());
}

void AegisBot::GuildRoleCreate(aegis::gateway::events::guild_role_create obj)
{
    aegis::snowflake guild_id = obj.guild_id;
    Guild & g_data = get_guild(guild_id);

    auto & r = obj._role;

    std::string role_key{ fmt::format("{}:roles:{}", g_data.redis_prefix, r.role_id) };

    std::vector<std::string> vals;
    vals.emplace_back(role_key);
    vals.emplace_back("name");
    vals.emplace_back(r.name);
    vals.emplace_back("position");
    vals.emplace_back(std::to_string(r.position));
    vals.emplace_back("color");
    vals.emplace_back(std::to_string(r.color));
    vals.emplace_back("hoist");
    vals.emplace_back(r.hoist?"1":"0");
    vals.emplace_back("managed");
    vals.emplace_back(r.managed?"1":"0");
    vals.emplace_back("mentionable");
    vals.emplace_back(r.mentionable?"1":"0");
    vals.emplace_back("permission");
    vals.emplace_back(std::to_string((int64_t)r._permission));
    hmset(vals);

    sadd({ fmt::format("{}:roles", g_data.redis_prefix), std::to_string(r.role_id) });
}

void AegisBot::GuildRoleUpdate(aegis::gateway::events::guild_role_update obj)
{
    aegis::snowflake guild_id = obj.guild_id;
    Guild & g_data = get_guild(guild_id);

    auto & r = obj._role;

    std::string role_key{ fmt::format("{}:roles:{}", g_data.redis_prefix, r.role_id) };

    std::vector<std::string> vals;
    vals.emplace_back(role_key);
    vals.emplace_back("name");
    vals.emplace_back(r.name);
    vals.emplace_back("position");
    vals.emplace_back(std::to_string(r.position));
    vals.emplace_back("color");
    vals.emplace_back(std::to_string(r.color));
    vals.emplace_back("hoist");
    vals.emplace_back(r.hoist?"1":"0");
    vals.emplace_back("managed");
    vals.emplace_back(r.managed?"1":"0");
    vals.emplace_back("mentionable");
    vals.emplace_back(r.mentionable?"1":"0");
    vals.emplace_back("permission");
    vals.emplace_back(std::to_string((int64_t)r._permission));
    hmset(vals);
}

void AegisBot::GuildRoleDelete(aegis::gateway::events::guild_role_delete obj)
{
    aegis::snowflake guild_id = obj.guild_id;
    Guild & g_data = get_guild(guild_id);

    auto & r = obj.role_id;

    std::string role_key{ fmt::format("{}:roles:{}", g_data.redis_prefix, r) };
    std::string guild_roles_key{ fmt::format("{}:roles", g_data.redis_prefix) };

    del(role_key);
    srem({ guild_roles_key, std::to_string(r) });
}

void AegisBot::PresenceUpdate(aegis::gateway::events::presence_update obj)
{
//     {
//         aegis::rest::request_params rp;
//         rp.host = "165.227.115.46";
//         rp.port = "9998";
//         json j;
//         j["presence"] = 1;
//         rp.body = j.dump();
//         bot.get_rest_controller().execute2(rp);
//     }
    counters.presences++;
    statsd.Metric<Count>("presence", 1, 1);
}

const json AegisBot::make_info_obj(aegis::shards::shard * _shard) const noexcept
{
    int64_t guild_count = bot.guilds.size();
    int64_t member_count_unique = bot.members.size();
    int64_t channel_count = bot.channels.size();

    int64_t eventsseen = 0;

    for (auto & e : bot.message_count)
        eventsseen += e.second;

    std::string members = fmt::format("{}", member_count_unique);
    std::string channels = fmt::format("{}", channel_count);
    std::string guilds = fmt::format("{}", guild_count);
    std::string events = fmt::format("{}", eventsseen);

    fmt::MemoryWriter w;

#if defined(DEBUG) || defined(_DEBUG)
    std::string misc = fmt::format("I am shard # {} of {} running on `{}` in `DEBUG` mode", _shard->get_id()+1, bot.shard_max_count, aegis::utility::platform::get_platform());

    w << "[Latest bot source](https://github.com/zeroxs/aegis.cpp)\n[Official Bot Server](https://discord.gg/Kv7aP5K)\n\nMemory usage: "
        << double(aegis::utility::getCurrentRSS()) / (1024 * 1024) << "MB\nMax Memory: "
        << double(aegis::utility::getPeakRSS()) / (1024 * 1024) << "MB";
#else
    std::string misc = fmt::format("I am shard # {} of {} running on `{}`", _shard->get_id(), bot.shard_max_count, aegis::utility::platform::get_platform());

    w << "[Latest bot source](https://github.com/zeroxs/aegis.cpp)\n[Official Bot Server](https://discord.gg/Kv7aP5K)\n\nMemory usage: "
        << double(aegis::utility::getCurrentRSS()) / (1024 * 1024) << "MB";
#endif

    std::string stats = w.str();

    json t = {
        { "title", "AegisBot" },
        { "description", stats },
        { "color", rand() % 0xFFFFFF },
        { "fields",
        json::array(
            {
                { { "name", "Members" },{ "value", members },{ "inline", true } },
                { { "name", "Channels" },{ "value", channels },{ "inline", true } },
                { { "name", "Uptime" },{ "value", bot.uptime_str() },{ "inline", true } },
                { { "name", "Guilds" },{ "value", guilds },{ "inline", true } },
                { { "name", "Events Seen" },{ "value", events },{ "inline", true } },
                { { "name", u8"\u200b" },{ "value", u8"\u200b" },{ "inline", true } },
                { { "name", "misc" },{ "value", misc },{ "inline", false } }
            }
            )
        },
        { "footer",{ { "icon_url", "https://cdn.discordapp.com/emojis/289276304564420608.png" },{ "text", fmt::format("Made in C++{} running {}", CXX_VERSION, AEGIS_VERSION_TEXT) } } }
    };
    return t;
}


/*
const json AegisBot::make_info_obj(aegis::shard * _shard) const noexcept
{
    int64_t guild_count = bot.guilds.size();
    int64_t member_count = 0;
    int64_t member_count_unique = bot.members.size();
    int64_t member_online_count = 0;
    int64_t member_idle_count = 0;
    int64_t member_dnd_count = 0;
    int64_t channel_count = bot.channels.size();
    int64_t channel_text_count = 0;
    int64_t channel_voice_count = 0;
    int64_t member_count_active = 0;

    int64_t eventsseen = 0;

    {
        for (auto & e : bot.message_count)
            eventsseen += e.second;

        for (auto & v : bot.members)
        {
            if (v.second->status == member::member_status::Online)
                member_online_count++;
            else if (v.second->status == member::member_status::Idle)
                member_idle_count++;
            else if (v.second->status == member::member_status::DoNotDisturb)
                member_dnd_count++;
        }

        for (auto & v : bot.channels)
        {
            if (v.second->get_type() == aegis::gateway::objects::channel_type::Text)
                channel_text_count++;
            else
                channel_voice_count++;
        }

        member_count = bot.get_member_count();
    }

    std::string members = fmt::format("{} seen\n{} unique\n{} online\n{} idle\n{} dnd", member_count, member_count_unique, member_online_count, member_idle_count, member_dnd_count);
    std::string channels = fmt::format("{} total\n{} text\n{} voice", channel_count, channel_text_count, channel_voice_count);
    std::string guilds = fmt::format("{}", guild_count);
    std::string events = fmt::format("{}", eventsseen);
#if defined(DEBUG) || defined(_DEBUG)
    std::string build_mode = "DEBUG";
#else
    std::string build_mode = "RELEASE";
#endif
    std::string misc = fmt::format("I am shard {} of {} running on `{}` in `{}` mode", _shard->get_id() + 1, bot.shard_max_count, aegis::utility::platform::get_platform(), build_mode);

    fmt::MemoryWriter w;
    w << "[Latest bot source](https://github.com/zeroxs/aegis.cpp)\n[Official Bot Server](https://discord.gg/Kv7aP5K)\n\nMemory usage: "
        << double(aegis::utility::getCurrentRSS()) / (1024 * 1024) << "MB\nMax Memory: "
        << double(aegis::utility::getPeakRSS()) / (1024 * 1024) << "MB";
    std::string stats = w.str();


    json t = {
        { "title", "AegisBot" },
        { "description", stats },
        { "color", rand() % 0xFFFFFF },
        { "fields",
        json::array(
            {
                { { "name", "Members" },{ "value", members },{ "inline", true } },
                { { "name", "Channels" },{ "value", channels },{ "inline", true } },
                { { "name", "Uptime" },{ "value", bot.uptime() },{ "inline", true } },
                { { "name", "Guilds" },{ "value", guilds },{ "inline", true } },
                { { "name", "Events Seen" },{ "value", events },{ "inline", true } },
                { { "name", u8"\u200b" },{ "value", u8"\u200b" },{ "inline", true } },
                { { "name", "misc" },{ "value", misc },{ "inline", false } }
            }
            )
        },
        { "footer",{ { "icon_url", "https://cdn.discordapp.com/emojis/289276304564420608.png" },{ "text", fmt::format("Made in C++{} running {}", CXX_VERSION, AEGIS_VERSION_TEXT) } } }
    };
    return t;
}*/

void AegisBot::VoiceStateUpdate(aegis::gateway::events::voice_state_update obj)
{
    if (!voice_debug)
        return;
    bot.log->error("voice_state_update: session({})", obj.session_id);
}

void AegisBot::VoiceServerUpdate(aegis::gateway::events::voice_server_update obj)
{
    if (!voice_debug)
        return;
    bot.log->error("voice_server_update: token({}) endpoint({})", obj.token, obj.endpoint);
}
