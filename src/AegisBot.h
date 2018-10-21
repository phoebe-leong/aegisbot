//
// AegisBot.h
// **********
//
// Copyright (c) 2018 Sharon W (sharon at aegis dot gg)
//
// Distributed under the MIT License. (See accompanying file LICENSE)
// 

#pragma once

//#include <aegis/core.hpp>
//#include <aegis/gateway/objects/message.hpp>
//#include <aegis.hpp>
#include <aegis/gateway/events/message_create.hpp>
#include <aegis/snowflake.hpp>
#include <redisclient/redissyncclient.h>
#include <redisclient/redisasyncclient.h>
#include <shared_mutex>
#include <future>
#include <spdlog/spdlog.h>
// #include <algorithm>
// #include <string>
// #include <algorithm>
// #include <stdint.h>
// #include <nlohmann/json.hpp>
// #include <unordered_map>
// #include <vector>
// #include <list>
// #include <asio.hpp>
#include "structs.h"
#include "cppdogstatsd.h"
#include "Module.h"
#include "Guild.h"
#include "redis_wrap.h"

using json = nlohmann::json;
using asio::ip::udp;
using namespace std::chrono_literals;
using namespace std::literals;

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
    const aegis::snowflake & channel_id;
    const aegis::snowflake & guild_id;
    const aegis::snowflake & message_id;
    const aegis::snowflake & member_id;
    const aegis::snowflake & guild_owner_id;

    std::string_view username;

    aegis::member & _member;
    aegis::channel & _channel;
    aegis::guild & _guild;
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
        auto & e = counters._msg[msg];
        e.count++;
        e.time += us;
        counters.events++;
        statsd.Metric<cppdogstatsd::Timer>("msg_time", us, 1, { "cmd:" + msg });
        statsd.Metric<cppdogstatsd::Count>("command", 1, 1, { "cmd:" + msg });
    }

    void js_end(std::chrono::steady_clock::time_point start_time, const std::string & msg)
    {
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_time).count();
//         if (msg != "PRESENCE_UPDATE" && msg != "TYPING_START")
//             bot.log->debug("js_end : {} dura: {}us", msg, us);
        auto & e = counters._js[msg];
        e.count++;
        e.time += us;
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
    void load_config();

    const aegis::snowflake bot_owner_id = 171000788183678976LL;
    const aegis::snowflake bug_report_channel_id = 382210262964502549LL;
    const aegis::snowflake bot_guild_id = 287048029524066334LL;
    const aegis::snowflake bot_control_channel = 288707540844412928LL;

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
    bool is_production = true;

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

    void do_log(std::string msg);

    void do_peek(std::string msg)
    {
        asio::post(_io_service, asio::bind_executor(strand, [msg, this]()
        {
            std::lock_guard<std::mutex> lock(r_mutex);
            redis.command("PUBLISH", { "aegis:peek", msg });
        }));
    }

    bool create_message(aegis::channel & _channel, const std::string & str, bool _override = false) noexcept;

    asio::io_context & _io_service;

    const std::string r_config = "config:guild";
    const std::string r_m_config = "config:member";
    const std::string r_bot_config = "config";

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

    std::shared_ptr<spdlog::logger> log;

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

    std::set<aegis::snowflake> peeks;

    bool has_perms(Guild & g_data, aegis::member & _member, aegis::guild & _guild, std::string cmd);

    bool module_enabled(modules mod) const noexcept;

    bool module_enabled(Guild & g_data, modules mod) const noexcept;

    Guild & get_guild(aegis::snowflake id);

    asio::io_context::strand strand;

    std::unordered_map<uint64_t, Guild> guild_data;

    std::vector<std::string> commands;

    std::unordered_map<std::string, uint16_t> command_counters;

    struct event_data
    {
        uint32_t time;
        uint32_t count;
    };

    struct
    {

        std::atomic_uint32_t dms = 0;
        std::atomic_uint32_t messages = 0;
        std::atomic_uint32_t presences = 0;
        std::atomic_uint32_t rest_time = 0;
        std::atomic_uint32_t _rest = 0;
        std::atomic_uint32_t events = 0;

        std::map<std::string, event_data> _js;
        std::map<std::string, event_data> _msg;

        //uint32_t 
    } counters;

    std::unordered_map<uint16_t, uint32_t> http_codes;

    std::set<aegis::snowflake> ignored;

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

    std::unordered_map<aegis::snowflake, aegis::gateway::objects::message> message_history;

    std::unordered_map<aegis::snowflake, aegis::snowflake> dm_channels;

    void event_log(const json & j, const std::string & path);

    std::string get_module_name(modules m)
    {
        if (m >= modules::MAX_MODULES)
            return "invalid module";
        return bot_modules[m].name;
    }

    bool valid_command(std::string_view cmd);

    bool command_enabled(aegis::snowflake guild_id, std::string cmd);

    bool command_enabled(Guild & g_data, std::string cmd);

    bool toggle_command(std::string_view cmd, aegis::snowflake guild_id, bool enabled)
    {
        std::string c;
        std::transform(cmd.begin(), cmd.end(), c.begin(), [](const unsigned char c) { return std::tolower(c); });
        if (hset({ fmt::format("{}:{}:cmds", r_config, guild_id), fmt::format("{}", c), enabled?"1":"0" }))
            return true;
        return false;
    }

    enum mention_type
    {
        Fail,
        User,
        Nickname,
        Channel,
        Role,
        Emoji,
        AnimatedEmoji
    };

    int32_t is_mention(const std::string_view str) const noexcept
    {
        if ((str.front() == '<') &&  (str.back() == '>'))
        {
            //redis commands
            std::string res;
            if ((str[1] == '@') && (str[2] == '!'))
            {

            }
            if ((str[1] == '@') || (str[1] == '#'))
            {

            }
        }
    }

    aegis::snowflake get_snowflake(const std::string_view name, aegis::guild & _guild) const noexcept;

    std::tuple<mention_type, aegis::snowflake> analyze_mention(std::string_view str) const noexcept;

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

    bool sadd(const std::string_view key, const std::vector<std::string> & value, Guild & g_data);

    bool srem(const std::string_view key, const std::vector<std::string> & value, Guild & g_data);

    bool basic_action(const std::string_view action, const std::vector<std::string> & value);

    std::string result_action(const std::string_view action, const std::vector<std::string> & value);

    /// Specify key separately
    bool basic_action(const std::string_view action, const std::string_view key, const std::vector<std::string> & value);

    /// Specify key separately
    std::string result_action(const std::string_view action, const std::string_view key, const std::vector<std::string> & value);

    std::future<std::string> async_get_result(const std::string_view action, const std::string_view key, const std::vector<std::string> & value);

    std::future<aegis::rest::rest_reply> req_success(aegis::gateway::objects::message & msg);

    std::future<aegis::rest::rest_reply> req_fail(aegis::gateway::objects::message & msg);

    std::future<aegis::rest::rest_reply> req_permission(aegis::gateway::objects::message & msg);

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

    void load_guild(aegis::snowflake guild_id, Guild & g_data);

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

    std::map<std::string, std::function<std::string(shared_data & sd)>> admin_replacements;
    std::map<std::string, std::function<std::string(shared_data & sd)>> advanced_replacements;
    std::map<std::string, std::function<std::string(shared_data & sd)>> replacements;

    std::string replace_str(const std::string & needle, const std::string & repl, const std::string & haystack)
    {
        std::string temp_res = "";
        std::string::size_type n = haystack.find(needle, 0);
        if (n != std::string::npos)
        {
            temp_res.resize(haystack.size() + repl.size() + needle.size() * 2, 0);
            std::copy_n(haystack.begin(), n, temp_res.data());
            std::copy_n(repl.begin(), repl.size(), temp_res.data() + n);
            std::copy_n(
                haystack.begin() + n + needle.size(),
                haystack.size() - n - needle.size(),
                temp_res.data() + n + repl.size());
            temp_res[haystack.size() - needle.size() + repl.size()] = 0;
            temp_res.resize(haystack.size() - needle.size() + repl.size());
            return std::move(temp_res);
        }
        return haystack;
    }

    std::string admin_string_replace(const std::string & s, shared_data & sd)
    {
        std::string temp = s;
        for (const auto &[k, v] : admin_replacements)
        {
            temp = replace_str(k, v(sd), temp);
        }
        return std::move(temp);
    }

    std::string string_replace(const std::string & s, shared_data & sd)
    {
        std::string temp = s;
        for (const auto &[k, v] : replacements)
        {
            temp = replace_str(k, v(sd), temp);
        }
        return std::move(temp);
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

