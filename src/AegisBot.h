//
// AegisBot.h
// **********
//
// Copyright (c) 2018 Sharon W (sharon at aegis dot gg)
//
// Distributed under the MIT License. (See accompanying file LICENSE)
// 

#pragma once

#include <aegis/config.hpp>
#include <aegis.hpp>
#include <algorithm>
#include <redisclient/redissyncclient.h>
#include <redisclient/redisasyncclient.h>
#include <string>
#include <algorithm>
#include <stdint.h>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <vector>
#include <list>
#include <asio.hpp>
#include "structs.h"
#include "cppdogstatsd.h"
#include "Module.h"
#include "Guild.h"
#include "redis_wrap.h"
#include "futures.h"

using json = nlohmann::json;
using asio::ip::udp;
using namespace std::chrono_literals;
using namespace std::literals;

class member;
class channel;
class guild;
class Guild;
class AegisBot;

enum redis_store_key_type
{
    HASH,
    SET,
    KV
};

struct shared_data
{
    const snowflake & channel_id;
    const snowflake & guild_id;
    const snowflake & message_id;
    const snowflake & member_id;
    const snowflake & guild_owner_id;

    std::string_view username;

    member & _member;
    channel & _channel;
    guild & _guild;
    std::string_view content;
    Guild & g_data;
    std::vector<std::string_view> & toks;
    aegis::gateway::events::message_create & msg;
    AegisBot & ab;
};

class AegisBot
{
public:
    static constexpr const char * ZWSP = u8"\u200b";

    AegisBot(asio::io_context & _io, aegis::core & bot);
    ~AegisBot() = default;

    template<typename Out>
    void split(const std::string_view s, char delim, Out result)
    {
        std::stringstream ss;
        ss.str(s.data());
        std::string item;
        while (std::getline(ss, item, delim))
        {
            if (!item.empty())
                *(result++) = item;
        }
    }

    struct shard_data
    {
        std::chrono::time_point<std::chrono::steady_clock> last_message;
    };

    std::vector<shard_data> shards;

    void message_end(std::chrono::steady_clock::time_point start_time, const std::string & msg)
    {
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_time).count();
//         if (msg != "PRESENCE_UPDATE" && msg != "TYPING_START")
//             bot.log->debug("message_end : {} dura: {}us", msg, us);

//         if ((msg == "GUILD_MEMBERS_CHUNK") && (msg != "GUIILD_CREATE") && (msg != "GUIILD_UPDATE"))
//             bot.log->debug("message_end : {} dura: {}ms", msg, uint64_t(us/1000));
        statsd.Metric<cppdogstatsd::Timer>("msg_time", us, 1, { "cmd:" + msg });
        statsd.Metric<cppdogstatsd::Count>("command", 1, 1, { "cmd:" + msg });
    }

    void call_end(std::chrono::steady_clock::time_point start_time)
    {
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_time).count();
//         bot.log->debug("call_end : dura: {}us", us);

        statsd.Metric<cppdogstatsd::Timer>("rest_time", us, 1);
    }

    void js_end(std::chrono::steady_clock::time_point start_time, const std::string & msg)
    {
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_time).count();
//         if (msg != "PRESENCE_UPDATE" && msg != "TYPING_START")
//             bot.log->debug("js_end : {} dura: {}us", msg, us);
        statsd.Metric<cppdogstatsd::Timer>("js_time", std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_time).count(), 1, { "cmd:" + msg });
    }

    std::vector<std::string> split(const std::string_view s, char delim)
    {
        std::vector<std::string> elems;
        split(s, delim, std::back_inserter(elems));
        return elems;
    }

    std::vector<std::string> lineize(const std::string s);
    std::vector<std::string_view> tokenize(const std::string_view s, const std::string & delim, std::vector<std::string> groups = {}, bool inclusive = false);
    std::string tokenize_one(const std::string s, char delim, uint32_t & ctx, std::vector<std::string> groups = {}, bool inclusive = false);
    void redis_cmd(const std::vector<char> &buf);

    const snowflake bot_owner_id = 171000788183678976LL;
    const snowflake bug_report_channel_id = 382210262964502549LL;
    const snowflake bot_guild_id = 287048029524066334LL;
    const snowflake bot_control_channel = 288707540844412928LL;

    redisclient::RedisSyncClient redis;
    //redisclient::RedisAsyncClient redis_logger;
    redis_subscriber redis_commands;
    redis_subscriber redis_logger;
    asio::steady_timer memory_stats;
    std::string ipaddress;
    uint16_t port;
    std::string password;
    aegis::core & bot;
    bool voice_debug = false;

    std::string format_bytes(uint64_t size)
    {
        if ((size > 1024ull * 5) && (size < 1024ull * 1024 * 5))// over 5KB and up to 5MB show KB
        {
            return fmt::format("{:.3f} KB", double(size) / 1024);
        }
        if ((size > 1024ull * 1024 * 5) && (size < 1024ull * 1024 * 1024 * 5))// over 5MB and up to 5GB show MB
        {
            return fmt::format("{:.3f} MB", (double(size) / 1024) / 1024);
        }
        if (size > 1024ull * 1024 * 1024 * 5)// over 5GB show GB
        {
            return fmt::format("{:.3f} GB", ((double(size) / 1024) / 1024) / 1024);
        }
        return fmt::format("{} B", size);
    }

    void asdf()
    {
        //, typename E
        //asio::bind_executor();
        asio::io_context::strand a = strand;
    }

    void do_log(std::string msg)
    {
        asdf();
        bot.log->debug(msg);
        asio::post(_io_service, asio::bind_executor(strand, [msg, this]()
        {
            std::lock_guard<std::mutex> lock(r_mutex);
            redis.command("PUBLISH", { "aegis:log", msg });
        }));
    }

    void do_peek(std::string msg)
    {
        asio::post(_io_service, asio::bind_executor(strand, [msg, this]()
        {
            std::lock_guard<std::mutex> lock(r_mutex);
            redis.command("PUBLISH", { "aegis:peek", msg });
        }));
    }

    asio::io_context & _io_service;

    std::string_view r_config = "config:guild";
    std::string_view r_m_config = "config:member";
    std::string_view r_bot_config = "config";

    int64_t checktime = 0;
    int64_t ws_checktime = 0;
    int64_t pingnonce = 0;

    std::mutex m_ping_test;
    std::condition_variable cv_ping_test;

    cppdogstatsd::Dogstatsd statsd;
    asio::steady_timer stats_timer;
    asio::steady_timer web_stats_timer;
    asio::steady_timer web_log_timer;
    asio::steady_timer status_timer;
    asio::steady_timer maint_timer;

    void maint();

    void update_statuses();
    void web_log();
    void push_stats();
    void update_stats();
    void send_memory_stats();

    // Messages you want to process
    void inject();

    void init();

    std::mutex r_mutex;

    std::unordered_map<modules, s_module_data> bot_modules;

    std::set<snowflake> peeks;

    bool has_perms(Guild & g_data, member & _member, guild & _guild, std::string cmd);

    bool module_enabled(modules mod) const noexcept;

    bool module_enabled(Guild & g_data, modules mod) const noexcept;

    Guild & get_guild(snowflake id);

    asio::io_context::strand strand;

    std::unordered_map<uint64_t, Guild> guild_data;

    std::vector<std::string> commands;

    std::unordered_map<std::string, uint16_t> command_counters;

    std::unordered_map<uint16_t, uint32_t> http_codes;

    std::set<snowflake> ignored;

    std::unordered_map<std::string, s_tag_data> tag_list;

    //command name, enabled, default_access, perm_type
    std::unordered_map<std::string, std::tuple<std::string, std::string, std::string>> commands_defaults =
    {
        //shared
        { "set",        { "1" , "0", "0" } },

        //default
        { "help",       { "1" , "1", "0" } },
        { "info",       { "1" , "1", "0" } },
        { "kick",       { "1" , "0", "0" } },
        { "ban",        { "1" , "0", "0" } },
        { "redis",      { "0" , "0", "0" } },
        { "source",     { "1" , "1", "0" } },
        { "perm",       { "1" , "0", "0" } },
        { "server",     { "1" , "1", "0" } },
        { "shard",      { "1" , "1", "0" } },
        { "shards",     { "1" , "1", "0" } },
        { "serverlist", { "0" , "0", "0" } },
        { "events",     { "1" , "1", "0" } },
        { "test",       { "1" , "1", "0" } },
        { "tag",        { "1" , "1", "0" } },
        { "feedback",   { "1" , "1", "0" } },
        { "reportbug",  { "1" , "1", "0" } },
        { "ping",       { "0" , "0", "0" } },
        { "stats",      { "1" , "1", "0" } }
    };

        //Auction - set all to default enabled
        //because Auction mod has built-in permission
        //checking
    std::unordered_map<std::string, std::tuple<std::string, std::string, std::string>> auction_commands_defaults =
    {
        { "reset",      { "1", "1", "0" } },
        { "register",   { "1", "1", "0" } },
        { "start",      { "1", "1", "0" } },
        { "playerlist", { "1", "1", "0" } },
        { "nom",        { "1", "1", "0" } },
        { "startfunds", { "1", "1", "0" } },
        { "pause",      { "1", "1", "0" } },
        { "resume",     { "1", "1", "0" } },
        { "bid",        { "1", "1", "0" } },
        { "end",        { "1", "1", "0" } },
        { "setname",    { "1", "1", "0" } },
        { "standings",  { "1", "1", "0" } },
        { "retain",     { "1", "1", "0" } },
        { "skip",       { "1", "1", "0" } },
        { "setfunds",   { "1", "1", "0" } },
        { "undobid",    { "1", "1", "0" } },
        { "bidtime",    { "1", "1", "0" } },
        { "auctionhelp",{ "1", "1", "0" } },
        { "withdraw",   { "1", "1", "0" } },
        { "addfunds",   { "1", "1", "0" } },
        { "removefunds",{ "1", "1", "0" } },
        { "addplayers", { "1", "1", "0" } },
        { "addbidder",  { "1", "1", "0" } },
        { "delbiddder", { "1", "1", "0" } },
        { "teamlist",   { "1", "1", "0" } },

        //Music
        { "musichelp",  { "1", "0", "0" } }
    };

    std::unordered_map<std::string, redis_store_key_type> redis_store_keys;

    std::unordered_map<std::string, std::string> docs;

    std::shared_mutex m_message;

    std::unordered_map<snowflake, aegis::gateway::objects::message> message_history;

    std::unordered_map<snowflake, snowflake> dm_channels;

    std::string get_module_name(modules m)
    {
        if (m >= modules::MAX_MODULES)
            return "invalid module";
        return bot_modules[m].name;
    }

    bool valid_command(std::string_view cmd);

    bool command_enabled(snowflake guild_id, std::string cmd);

    bool command_enabled(Guild & g_data, std::string cmd);

    bool toggle_command(std::string_view cmd, snowflake guild_id, bool enabled)
    {
        std::string c;
        std::transform(cmd.begin(), cmd.end(), c.begin(), [](const unsigned char c) { return std::tolower(c); });
        if (hset({ fmt::format("{}:{}:cmds", r_config, guild_id), fmt::format("{}", c), enabled?"1":"0" }))
            return true;
        return false;
    }

    int32_t is_mention(const std::string_view str) const noexcept
    {
        if (str[0] == '<')
        {
            //redis commands
            std::string res;
            if ((str[1] == '@') || (str[1] == '#'))
            {

            }
        }
    }

    snowflake get_snowflake(const std::string_view name, guild & _guild) const noexcept
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

    /// Automatically reference key [config:guild:{guild_id}:{key}]
    bool hset(const std::string_view key, std::vector<std::string> value, Guild & g_data);

    std::string hget(const std::string_view key, std::vector<std::string> value, Guild & g_data);

    bool hset(const std::vector<std::string> & value)
    {
        return basic_action("HSET", value);
    }

    bool hmset(const std::vector<std::string> & value)
    {
        return basic_action("HMSET", value);
    }

    std::string hget(const std::vector<std::string> & value)
    {
        return result_action("HGET", value);
    }

    bool hdel(const std::vector<std::string> & value)
    {
        return basic_action("HDEL", value);
    }

    std::string run(const std::string_view cmd, const std::vector<std::string> value)
    {
        return result_action(cmd, value);
    }

    std::string run(const std::string_view cmd, const std::vector<std::string_view> value)
    {
        std::vector<std::string> n;
        for (const auto & v : value)
            n.push_back(std::string{ v });
        return result_action(cmd, n);
    }

    std::vector<std::string> run_v(const std::string_view cmd, const std::vector<std::string> & value)
    {
        return get_raw(cmd, value);
    }

    std::vector<std::string> run_v(const std::string_view cmd, const std::vector<std::string_view> & value)
    {
        std::vector<std::string> n;
        for (const auto & v : value)
            n.push_back(std::string{ v });
        return get_raw(cmd, n);
    }

    std::string get(const std::string_view key)
    {
        return result_action("GET", { std::string{ key } });
    }

    bool put(const std::string_view key, const std::string_view value)
    {
        return basic_action("SET", { std::string{ key }, std::string{ value } });
    }

    bool del(const std::string_view key)
    {
        return basic_action("DEL", { std::string{ key } });
    }

    bool del(const std::vector<std::string_view> & value)
    {
        std::vector<std::string> n;
        for (const auto & v : value)
            n.push_back(std::string{ v });
        return basic_action("DEL", n);
    }

    bool del(const std::vector<std::string> & value)
    {
        return basic_action("DEL", value);
    }

    bool publish(const std::string_view key, const std::string_view value)
    {
        return basic_action("PUBLISH", { std::string{ key }, std::string{ value } });
    }

    void expire(const std::string_view key, const int64_t value)
    {
        basic_action("EXPIRE", { std::string{ key }, std::to_string(value) });
    }

    std::string getset(const std::string_view key, const std::string_view value)
    {
        return result_action("GETSET", { std::string{ key }, std::string{ value } });
    }

    std::unordered_map<std::string, std::string> get_array(const std::string_view key)
    {
        std::lock_guard<std::mutex> lock(r_mutex);
        redisclient::RedisValue result;
        result = redis.command("HGETALL", { std::string{key} });

        std::unordered_map<std::string, std::string> ret;
        if (result.isOk())
        {
            if (result.isArray())
            {
                auto t = result.toArray();
                for (auto it = t.begin(); it != t.end(); ++it)
                {
                    std::string key = (it++)->toString();
                    ret[key] = it->toString();
                }
                return ret;
            }
        }
        return {};
    }

    std::vector<std::string> get_vector(const std::string_view key)
    {
        std::lock_guard<std::mutex> lock(r_mutex);
        redisclient::RedisValue result;
        result = redis.command("SMEMBERS", { std::string(key) });

        std::vector<std::string> ret;
        if (result.isOk())
        {
            if (result.isArray())
            {
                auto t = result.toArray();
                for (auto & it : t)
                    ret.push_back(it.toString());
                return ret;
            }
        }
        return {};
    }

    std::unordered_map<std::string, std::string> get_array(const std::string_view key, Guild & g_data);

    std::vector<std::string> get_vector(const std::string_view key, Guild & g_data);

    std::vector<std::string> get_raw(const std::string_view cmd, const std::vector<std::string> & value)
    {
        std::lock_guard<std::mutex> lock(r_mutex);
        redisclient::RedisValue result;
        std::deque<redisclient::RedisBuffer> v;
        for (auto & k : value)
            v.emplace_back(k.data());
        result = redis.command(std::string{ cmd }, v);

        std::vector<std::string> ret;
        if (result.isOk())
        {
            if (result.isArray())
            {
                auto t = result.toArray();
                for (auto & it : t)
                    ret.push_back(it.toString());
                return ret;
            }
        }
        return {};
    }

    bool do_raw(const std::string_view cmd)
    {
        std::lock_guard<std::mutex> lock(r_mutex);
        redisclient::RedisValue result;
        result = redis.command(std::string{ cmd });

        if (result.isOk())
            return true;
        return false;
    }

    bool do_raw(const std::string_view cmd, const std::vector<std::string> & value)
    {
        std::lock_guard<std::mutex> lock(r_mutex);
        redisclient::RedisValue result;
        std::deque<redisclient::RedisBuffer> v;
        for (auto & k : value)
            v.emplace_back(k.data());
        result = redis.command(std::string{ cmd }, v);

        if (result.isOk())
            return true;
        return false;
    }

    bool sadd(const std::vector<std::string> & value)
    {
        return basic_action("SADD", value);
    }

    bool srem(const std::vector<std::string> & value)
    {
        return basic_action("SREM", value);
    }

    bool basic_action(const std::string_view action, const std::vector<std::string> & value)
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

    std::string result_action(const std::string_view action, const std::vector<std::string> & value)
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

    /// Specify key separately
    bool basic_action(const std::string_view action, const std::string_view key, const std::vector<std::string> & value)
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

    /// Specify key separately
    std::string result_action(const std::string_view action, const std::string_view key, const std::vector<std::string> & value)
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

    std::future<std::string> async_get_result(const std::string_view action, const std::string_view key, const std::vector<std::string> & value)
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

    std::future<aegis::rest::rest_reply> req_success(aegis::gateway::objects::message & msg)
    {
        return msg.create_reaction("success:429554838083207169");
    }

    std::future<aegis::rest::rest_reply> req_fail(aegis::gateway::objects::message & msg)
    {
        return msg.create_reaction("fail:429554869611921408");
    }

    std::future<aegis::rest::rest_reply> req_permission(aegis::gateway::objects::message & msg)
    {
        return msg.create_reaction("no_permission:451495171779985438");
    }

    const bool to_int64(const std::string & s) const noexcept
    {
        return std::stoull(s);
    }

    const bool to_int64(const std::string_view & s) const noexcept
    {
        return std::stoull(std::string{ s });
    }

    const bool to_bool(const std::string & s) const noexcept
    {
        return (s == "1");
    }

    const bool to_bool(const std::string_view & s) const noexcept
    {
        return (s == "1");
    }

    void toggle_mod(Guild & g_data, const modules mod, const bool toggle);

    void toggle_mod_override(Guild & g_data, const modules mod, const bool toggle);

    void load_guild(snowflake guild_id, Guild & g_data);

    //extensions
    bool process_admin_messages(aegis::gateway::events::message_create & obj, shared_data & sd);
    bool process_user_messages(aegis::gateway::events::message_create & obj, shared_data & sd);

    modules get_module(std::string name)
    {
        for (auto & it : bot_modules)
            if (it.second.name == name)
                return it.first;
        return MAX_MODULES;
    }

    modules get_module(std::string_view name)
    {
        for (auto & it : bot_modules)
            if (it.second.name == name)
                return it.first;
        return MAX_MODULES;
    }


    std::string base64_encode(const std::string &in)
    {
        static constexpr char LOOKUP[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        std::string out;
        out.reserve(((in.size()+3) / 3) * 4);

        uint32_t val = 0;
        int32_t valb = -6;
        for (uint8_t c : in)
        {
            val = (val << 8) + c;
            valb += 8;
            while (valb >= 0)
            {
                out.push_back(LOOKUP[(val >> valb) & 0x3F]);
                valb -= 6;
            }
        }
        if (valb > -6) out.push_back(LOOKUP[((val << 8) >> (valb + 8)) & 0x3F]);
        while (out.size() % 4) out.push_back('=');
        return out;
    }

    std::string base64_decode(const std::string &in)
    {
        static constexpr char LOOKUP[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        std::string out;

        std::vector<int> T(256, -1);
        for (int i = 0; i < 64; i++) T[LOOKUP[i]] = i;

        uint32_t val = 0;
        int32_t valb = -8;
        for (uint8_t c : in)
        {
            if (T[c] == -1) break;
            val = (val << 6) + T[c];
            valb += 6;
            if (valb >= 0)
            {
                out.push_back(char((val >> valb) & 0xFF));
                valb -= 8;
            }
        }
        return out;
    }

    template<typename E, typename T, typename V = std::result_of_t<T()>/*, typename std::enable_if_t<!std::is_void<std::result_of_t<T()>>::value> = 0*/>
    aegis::future<V> async(E ex, T f)
    {
        aegis::promise<V> pr;
        auto fut = pr.get_future();

        asio::post(_io_service, asio::bind_executor(*ex, [pr = std::move(pr), f = std::move(f)]() mutable
        {
            pr.set_value(f());
        }));
        return fut;
    }

    template<typename E, typename T>
    aegis::future<void> async(E ex, T f)
    {
        aegis::promise<void> pr;
        auto fut = pr.get_future();

        asio::post(_io_service, asio::bind_executor(*ex, [pr = std::move(pr), f = std::move(f)]() mutable
        {
            f();
            pr.set_value();
        }));
        return fut;
    }

    // All the hooks into the websocket stream
    void TypingStart(aegis::gateway::events::typing_start obj);

    void MessageCreate(aegis::gateway::events::message_create msg);

    void MessageCreateDM(aegis::gateway::events::message_create msg);
    
    void MessageUpdate(aegis::gateway::events::message_update obj);

    void MessageDelete(aegis::gateway::events::message_delete obj);

    void MessageDeleteBulk(aegis::gateway::events::message_delete_bulk obj);

    void GuildCreate(aegis::gateway::events::guild_create);

    void GuildUpdate(aegis::gateway::events::guild_update obj);

    void GuildDelete(aegis::gateway::events::guild_delete obj);

    void UserUpdate(aegis::gateway::events::user_update obj);

    void Ready(aegis::gateway::events::ready obj);

    void Resumed(aegis::gateway::events::resumed obj);

    void ChannelCreate(aegis::gateway::events::channel_create obj);

    void ChannelUpdate(aegis::gateway::events::channel_update obj);

    void ChannelDelete(aegis::gateway::events::channel_delete obj);

    void GuildBanAdd(aegis::gateway::events::guild_ban_add obj);

    void GuildBanRemove(aegis::gateway::events::guild_ban_remove obj);

    void GuildEmojisUpdate(aegis::gateway::events::guild_emojis_update obj);

    void GuildIntegrationsUpdate(aegis::gateway::events::guild_integrations_update obj);

    void GuildMemberAdd(aegis::gateway::events::guild_member_add obj);

    void GuildMemberRemove(aegis::gateway::events::guild_member_remove obj);

    void GuildMemberUpdate(aegis::gateway::events::guild_member_update obj);

    void GuildMemberChunk(aegis::gateway::events::guild_members_chunk obj);
    
    void GuildRoleCreate(aegis::gateway::events::guild_role_create obj);

    void GuildRoleUpdate(aegis::gateway::events::guild_role_update obj);

    void GuildRoleDelete(aegis::gateway::events::guild_role_delete obj);

    void PresenceUpdate(aegis::gateway::events::presence_update obj);

    void VoiceStateUpdate(aegis::gateway::events::voice_state_update obj);

    void VoiceServerUpdate(aegis::gateway::events::voice_server_update obj);

    void MessageReactionAdd(aegis::gateway::events::message_reaction_add obj);

    void MessageReactionRemove(aegis::gateway::events::message_reaction_remove obj);

    void MessageReactionRemoveAll(aegis::gateway::events::message_reaction_remove_all obj);

    void WebhooksUpdate(aegis::gateway::events::webhooks_update obj);

    void ChannelPinsUpdate(aegis::gateway::events::channel_pins_update obj);

    const json make_info_obj(aegis::shards::shard * _shard) const noexcept;
};

