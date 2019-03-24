//
// mod_automod.cpp
// ***************
//
// Copyright (c) 2019 Sharon W (sharon at aegis dot gg)
//
// Distributed under the MIT License. (See accompanying file LICENSE)
// 

#include "mod_automod.h"
#include "AegisBot.h"
#include <aegis/gateway/objects/message.hpp>
#include <aegis/channel.hpp>
#include <aegis/guild.hpp>
#if defined(AEGIS_HEADER_ONLY)
#include <aegis/impl/user.cpp>
#include <aegis/impl/channel.cpp>
#include <aegis/impl/guild.cpp>
#endif
#include <aegis/user.hpp>

std::string mod_automod::r_prefix = "automod";

mod_automod::mod_automod(Guild & data, asio::io_context & _io)
    : Module(data, _io)
{
    commands["automod"] = std::bind(&mod_automod::automod, this, std::placeholders::_1);
    commands["am"] = std::bind(&mod_automod::automod, this, std::placeholders::_1);
    message_process = std::bind(&mod_automod::process, this, std::placeholders::_1);
    redis_key = fmt::format("{}:automod", g_data.redis_prefix);
}

mod_automod::~mod_automod()
{
}

std::vector<std::string> mod_automod::get_db_entries()
{
    return std::vector<std::string>();
}

void mod_automod::do_stop()
{

}

bool mod_automod::check_command(std::string cmd, shared_data & sd)
{
    auto it = commands.find(cmd);
    if (it == commands.end())
        return false;

    return it->second(sd);
}

void mod_automod::remove()
{
    delete this;
}

void mod_automod::load(AegisBot & bot)
{
    try
    {
        auto & mod_data = g_data._modules[modules::Automod];

        if (!mod_data->enabled && !mod_data->admin_override)
            return;//mod not enabled or overridden

        if (auto r_banned_words = bot.get_array(fmt::format("{}:bkw", redis_key)); !r_banned_words.empty())
            for (auto const &[k,v]: r_banned_words)
                banned_keywords.emplace(k, static_cast<action_taken>(std::stol(v)));

        if (auto r_ignored_roles = bot.get_vector(fmt::format("{}:i_r", redis_key)); !r_ignored_roles.empty())
            for (auto & ignored_role : r_ignored_roles)
                ignored_roles.emplace_back(std::stoull(ignored_role));

        if (auto r_ignored_users = bot.get_vector(fmt::format("{}:i_u", redis_key)); !r_ignored_users.empty())
            for (auto & ignored_user : r_ignored_users)
                ignored_users.emplace_back(std::stoull(ignored_user));

        auto t = bot.get(fmt::format("{}:log_target", redis_key));
        if (!t.empty())
            log_target = std::stoull(t);
    }
    catch (std::exception & e)
    {
        bot.log->error("Exception loading automod module data for guild [{}] e[{}]", g_data.guild_id, e.what());
    }
}

//return true if message is safe, false if it should be ignored by command handlers
//TODO: make a way for specific commands to be usable regardless. eg, not whitelist user wants to report
// by command a user with banned word in username (though they could use id)
bool mod_automod::process(shared_data & sd)
{
    if (banned_keywords.empty())
        return true;

    //check for words
    for (const auto & u : ignored_users)
        if (sd.member_id == u)
            return true;
    for (const auto & u : ignored_roles)
        if (sd.guild.member_has_role(sd.member_id, u))
            return true;

    for (const auto & keyword : banned_keywords)
    {
        if (sd.content.find(keyword.first.c_str()) != std::string::npos)
        {
            //found
            aegis::channel * c = nullptr;
            if (log_target != 0)
                c = sd.guild.get_channel(log_target);
            switch (keyword.second)
            {
                case action_taken::Log:
                    if (c) c->create_message(fmt::format("User said banned word : {}", sd.user.get_mention()));
                    return false;
                case action_taken::Warn:
                    if (c) c->create_message(fmt::format("User said banned word : {}", sd.user.get_mention()));
                    sd.channel.create_message(fmt::format("Please do not use that language {}", sd.user.get_mention()));
                    return false;
                case action_taken::Delete:
                    if (c) c->create_message(fmt::format("User said banned word : {}\nAction: Delete", sd.user.get_mention()));
                    sd.msg.msg.delete_message();
                    //sd._channel.create_message(fmt::format("Deleted : {}", sd._member.get_mention()));
                    return false;
                case action_taken::Kick:
                    if (c) c->create_message(fmt::format("User said banned word : {}\nAction: Kick", sd.user.get_mention()));
                    sd.guild.remove_guild_member(sd.member_id);
                    return false;
                case action_taken::Ban:
                    if (c) c->create_message(fmt::format("User said banned word : {}\nAction: Ban", sd.user.get_mention()));
                    sd.guild.create_guild_ban(sd.member_id, 1, "Banned word");
                    return false;
            }
        }
    }
    return true;
}

bool mod_automod::automod(shared_data & sd)
{
    const aegis::snowflake & channel_id = sd.channel_id;
    const aegis::snowflake & guild_id = sd.guild_id;
    const aegis::snowflake & message_id = sd.message_id;
    const aegis::snowflake & member_id = sd.member_id;
    const aegis::snowflake & guild_owner_id = sd.guild_owner_id;

    std::string_view username = sd.username;

    aegis::user & _member = sd.user;
    aegis::channel & _channel = sd.channel;
    aegis::guild & _guild = sd.guild;
    std::string_view content = sd.content;

    Guild & g_data = sd.g_data;

    std::vector<std::string_view> & toks = sd.toks;

    bool is_guild_owner = (member_id == guild_owner_id || member_id == sd.ab.bot_owner_id);

    std::string request{ content };

    toks.erase(toks.begin());

    if (toks.empty())
        return true;

//     if (member_id != _guild.get_owner())
//     {
//         auto bit = std::find(blocklist.begin(), blocklist.end(), member_id);
//         if (bit != blocklist.end())
//             return true;//blocked
//     }
//     
    //sd.ab.sadd({ fmt::format("{}:bkw", redis_key), std::to_string(target_user) });
    //sd.ab.srem({ fmt::format("{}:i_r", redis_key), std::to_string(target_user) });
    if (toks[0] == "banword")
    {
        if (toks[1] == "log_target")
        {
            if (check_params(_channel, toks, 2, "banword log_target <channel>"))
                return true;

            log_target = sd.ab.get_snowflake(toks[2], sd.guild);
            if (log_target)
            {
                sd.channel.create_message(fmt::format("Logging channel set to <#{}>", log_target));
                sd.ab.put(fmt::format("{}:log_target", redis_key), std::to_string(log_target));
            }
            else
            {
                sd.channel.create_message("Unable to set channel");
            }
            return true;
        }
        else if (toks[1] == "add")
        {
            if (check_params(_channel, toks, 4, "banword add <log|warn|kick|ban> <content>"))
                return true;

            std::string subcmd = to_lower(toks[2]);
            std::string bword{ toks[3] };
            if (subcmd == "log")
            {
                banned_keywords.emplace(bword, action_taken::Log);
                sd.channel.create_message("Keyword added to ban list with action set to `Log`.");
                sd.ab.hset({ fmt::format("{}:bkw", redis_key), bword, std::to_string(static_cast<int>(action_taken::Log)) });
            }
            else if (subcmd == "warn")
            {
                banned_keywords.emplace(std::string(toks[3]), action_taken::Warn);
                sd.channel.create_message("Keyword added to ban list with action set to `Warn`.");
                sd.ab.hset({ fmt::format("{}:bkw", redis_key), bword, std::to_string(static_cast<int>(action_taken::Warn)) });
            }
            else if (subcmd == "delete")
            {
                banned_keywords.emplace(std::string(toks[3]), action_taken::Delete);
                sd.channel.create_message("Keyword added to ban list with action set to `Delete`.");
                sd.ab.hset({ fmt::format("{}:bkw", redis_key), bword, std::to_string(static_cast<int>(action_taken::Delete)) });
            }
            else if (subcmd == "kick")
            {
                banned_keywords.emplace(std::string(toks[3]), action_taken::Kick);
                sd.channel.create_message("Keyword added to ban list with action set to `Kick`.");
                sd.ab.hset({ fmt::format("{}:bkw", redis_key), bword, std::to_string(static_cast<int>(action_taken::Kick)) });
            }
            else if (subcmd == "ban")
            {
                banned_keywords.emplace(std::string(toks[3]), action_taken::Ban);
                sd.channel.create_message("Keyword added to ban list with action set to `Ban`.");
                sd.ab.hset({ fmt::format("{}:bkw", redis_key), bword, std::to_string(static_cast<int>(action_taken::Ban)) });
            }
            return true;
        }
        else if (toks[1] == "rem")
        {
            if (check_params(_channel, toks, 3, "banword rem <content>"))
                return true;

            std::string bword{ toks[2] };
            auto res = banned_keywords.find(bword);
            if (res != banned_keywords.end())
            {
                banned_keywords.erase(res);
                sd.channel.create_message("Keyword removed from ban list.");
                sd.ab.hdel({ fmt::format("{}:bkw", redis_key), bword });
            }
            return true;
        }
        else if (toks[1] == "ignore")
        {
            if (check_params(_channel, toks, 5, "banword ignore <role|user> <add|rem> <id>"))
                return true;

            std::string cmdtype = to_lower(toks[2]);
            if (cmdtype == "role")
            {
                std::string subcmd = to_lower(toks[3]);
                if (subcmd == "add")
                {
                    aegis::snowflake role = sd.ab.get_snowflake(toks[4], sd.guild);
                    if (!role)
                    {
                        //can't find based on snowflake -- try a basic name check
                        std::shared_lock<std::shared_mutex> l(_guild.mtx());
                        for (auto & r : sd.guild.get_roles())
                        {
                            if (to_lower(r.second.name) == to_lower(toks[4]))
                            {
                                //match
                                ignored_roles.emplace_back(r.second.role_id);
                                sd.channel.create_message("Added role to ignore list.");
                                sd.ab.sadd({ fmt::format("{}:i_r", redis_key), std::to_string(r.second.role_id) });
                                return true;
                            }
                        }
                        sd.channel.create_message("Invalid role.");
                        return true;
                    }

                    ignored_roles.emplace_back(role);
                    sd.channel.create_message("Added role to ignore list.");
                    sd.ab.sadd({ fmt::format("{}:i_r", redis_key), std::to_string(role) });
                    return true;
                }
                else if (subcmd == "rem")
                {
                    aegis::snowflake role = sd.ab.get_snowflake(toks[4], sd.guild);
                    if (!role)
                    {
                        //can't find based on snowflake -- try a basic name check
                        std::shared_lock<std::shared_mutex> l(_guild.mtx());
                        for (auto & r : sd.guild.get_roles())
                        {
                            if (to_lower(r.second.name) == to_lower(toks[4]))
                            {
                                //match
                                auto res = std::find(ignored_roles.begin(), ignored_roles.end(), r.second.role_id);
                                if (res != ignored_roles.end())
                                {
                                    ignored_roles.erase(res);
                                    sd.channel.create_message("Removed role from ignore list.");
                                    sd.ab.srem({ fmt::format("{}:i_r", redis_key), std::to_string(r.second.role_id) });
                                    return true;
                                }
                            }
                        }
                        sd.channel.create_message("Invalid role.");
                        return true;
                    }

                    auto res = std::find(ignored_roles.begin(), ignored_roles.end(), role);
                    if (res != ignored_roles.end())
                    {
                        ignored_roles.erase(res);
                        sd.channel.create_message("Removed role from ignore list.");
                        sd.ab.srem({ fmt::format("{}:i_r", redis_key), std::to_string(role) });
                        return true;
                    }
                    sd.channel.create_message("Role not found in list.");
                    return true;
                }
            }
            else if (cmdtype == "user")
            {
                std::string subcmd = to_lower(toks[3]);
                if (subcmd == "add")
                {
                    aegis::snowflake user = sd.ab.get_snowflake(toks[4], sd.guild);
                    if (!user)
                    {
                        sd.channel.create_message("Invalid user.");
                        return true;
                    }

                    ignored_users.emplace_back(user);
                    sd.ab.sadd({ fmt::format("{}:i_u", redis_key), std::to_string(user) });
                    sd.channel.create_message("Added user to ignore list.");
                    return true;
                }
                else if (subcmd == "rem")
                {
                    aegis::snowflake user = sd.ab.get_snowflake(toks[4], sd.guild);
                    if (!user)
                    {
                        sd.channel.create_message("Invalid user.");
                        return true;
                    }

                    auto res = std::find(ignored_users.begin(), ignored_users.end(), user);
                    if (res != ignored_users.end())
                    {
                        ignored_users.erase(res);
                        sd.ab.srem({ fmt::format("{}:i_u", redis_key), std::to_string(user) });
                        sd.channel.create_message("Removed user from ignore list.");
                        return true;
                    }
                    sd.channel.create_message("User not found in list.");
                    return true;
                }
            }
        }
    }
    return true;
}
