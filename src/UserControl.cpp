//
// UserControl.cpp
// ***************
//
// Copyright (c) 2019 Sharon W (sharon at aegis dot gg)
//
// Distributed under the MIT License. (See accompanying file LICENSE)
// 

#include "AegisBot.h"
#include "Guild.h"
#include <aegis/core.hpp>
#if defined(AEGIS_HEADER_ONLY)
#include <aegis/impl/channel.cpp>
#include <aegis/impl/guild.cpp>
#endif
#include <aegis/guild.hpp>
#include <aegis/channel.hpp>
#include <aegis/user.hpp>

using aegis::snowflake;
using aegis::user;
using aegis::channel;
using aegis::guild;
using namespace cppdogstatsd;

bool AegisBot::process_user_messages(aegis::gateway::events::message_create & obj, shared_data & sd)
{
    //exclusive bot owner admin level commands

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


    if (toks[0] == "modulelist")
    {
        auto mods_enabled = get_vector(fmt::format("{}:modules", g_data.redis_prefix));

        std::stringstream w_o;
        {
            std::stringstream w;
            w << "Modules enabled in Redis:\n";

            for (const auto& i : mods_enabled)
            {
                auto res = static_cast<modules>(std::stoi(i));
                auto it = g_data._modules.find(res);
                if (it == g_data._modules.end())
                    continue;

                w << get_module_name(res) << ", ";
            }

            std::string out = w.str();
            out.erase(out.size() - 2);
            w_o << out;
        }

        w_o << "\n\nModules actually enabled:\n";

        for (auto & m : g_data._modules)
        {
            if (m.second->enabled)
            {
                w_o << get_module_name(m.first) << ", ";
            }
        }

        std::string out = w_o.str();
        out.erase(out.size() - 2);
        w_o << out;

        _channel.create_message(out);
        return true;
    }

    if (toks[0] == "prefix")
    {
        if (check_params(toks, 2, "prefix (add|remove|list) [prefix]"))
            return true;

        if (toks[1] == "add")
        {
            if (check_params(toks, 3, "prefix add `prefix`"))
                return true;

            g_data.cmd_prefix_list.emplace_back(std::string{ toks[2] });
            sadd({ fmt::format("{}:prefix", g_data.redis_prefix), std::string{ toks[2] } });
            req_success(obj.msg);
        }
        else if (toks[1] == "remove")
        {
            if (check_params(toks, 3, "prefix remove `prefix`"))
                return true;

            auto it = std::find(g_data.cmd_prefix_list.begin(), g_data.cmd_prefix_list.end(), std::string{ toks[2] });
            if (it == g_data.cmd_prefix_list.end())
            {
                req_fail(obj.msg);
                return true;
            }

            g_data.cmd_prefix_list.erase(it);

            srem({ fmt::format("{}:prefix", g_data.redis_prefix), std::string{ toks[2] } });
            req_success(obj.msg);
        }
        else if (toks[1] == "list")
        {
            auto res = get_vector({ fmt::format("{}:prefix", g_data.redis_prefix) });

            std::stringstream mw;
            mw << "#1) " << bot.mention << '\n';
            int i = 1;
            for (auto & pfx : res)
            {
                mw << fmt::format("#{}) {}\n", ++i, pfx);
            }
            aegis::gateway::objects::embed e;
            e.title("Bot prefixes");
            e.description(mw.str());
            e.color(0);
            e.footer({ "Prefixes" });
            _channel.create_message_embed("", e);
        }
        return true;
    }
 
    if (toks[0] == "enmod")
    {
        if (check_params(toks, 2, "enmod `modname`"))
            return true;

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
        _channel.create_message(fmt::format("Module `{}` does not exist.", toks[1]));
        return true;
    }

    if (toks[0] == "dismod")
    {
        if (check_params(toks, 2, "dismod `modname`"))
            return true;

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
        _channel.create_message(fmt::format("Module `{}` does not exist.", toks[1]));
        return true;
    }

    if (toks[0] == "cmd")
    {
        if (check_params(toks, 3, "cmd `command` [status|enable|disable|default_access|access_type] [value]"))
            return true;

        if (!valid_command(toks[1]))
        {
            _channel.create_message(fmt::format("Invalid command: {}", toks[1]));
            return true;
        }

        auto & cmd = g_data.cmds[std::string{ toks[1] }];

        if (toks[2] == "status")
        {
            _channel.create_message(fmt::format("Command status:\nEnabled: {}\nDefault Access: {}\nAccess Type: {}", cmd.enabled ? "true" : "false"
                                                    , (cmd.default_access == s_command_data::perm_access::Allow ? "Allow" : "Deny")
                                                    , (cmd.access_type == s_command_data::perm_type::User ? "User" : (cmd.access_type == s_command_data::perm_type::Role ? "Role" : "Internal"))));
            return true;
        }
        else if (toks[2] == "enable")
        {
            if (check_params(toks, 3))
                return true;
            toggle_command(toks[1], _guild.guild_id, true);
            cmd.enabled = true;
            _channel.create_message(fmt::format("Successfully enabled command **{}**", toks[1]));
            return true;
        }
        else if (toks[2] == "disable")
        {
            if (check_params(toks, 3))
                return true;
            toggle_command(toks[1], _guild.guild_id, false);
            cmd.enabled = false;
            _channel.create_message(fmt::format("Successfully disabled command **{}**", toks[1]));
            return true;
        }
        else if (toks[2] == "default_access")
        {
            if (check_params(toks, 4, "cmd `command` default_access [true|false]"))
                return true;
            std::string res;
            if (toks[3] == "true")
            {
                res = "1";
                cmd.default_access = s_command_data::perm_access::Allow;
            }
            else
            {
                res = "0";
                cmd.default_access = s_command_data::perm_access::Deny;
            }
            hset({ fmt::format("{}:cmds:{}", g_data.redis_prefix, toks[1]), "default_access", res });
            _channel.create_message(fmt::format("Successfully set **default_access** to `{}`", res == "1" ? "true" : "false"));
            return true;
        }
        else if (toks[2] == "access_type")
        {
            if (check_params(toks, 4, "cmd `command` access_type [user|role]"))
                return true;
            std::string res;
            if (toks[3] == "user")
            {
                res = "0";
                cmd.access_type = s_command_data::perm_type::User;
            }
            if (toks[3] == "role")
            {
                res = "1";
                cmd.access_type = s_command_data::perm_type::Role;
            }
            else if (toks[3] == "internal")
            {
                res = "2";
                cmd.access_type = s_command_data::perm_type::Internal;
            }
            hset({ fmt::format("{}:cmds:{}", g_data.redis_prefix, toks[1]), "access_type", res });
            _channel.create_message(fmt::format("Successfully set **access_type** to `{}`", (res == "0" ? "user" : (res == "1" ? "role" : "internal"))));
            return true;
        }
    }

    if (toks[0] == "set")
    {
        if (check_params(toks, 3, "set `option` value"))
            return true;

        if (toks[1] == "ignorebots")
        {
            if ((toks[2] == "true") || (toks[2] == "yes") || (toks[2] == "on"))
            {
                g_data.ignore_bots = true;
                if (hset({ g_data.redis_prefix, "ignorebots", "1" }))
                    _channel.create_message("Ignoring bot messages.");
                else
                    _channel.create_message("Ignoring bot messages (with errors).");
                return true;
            }
            else if ((toks[2] == "false") || (toks[2] == "no") || (toks[2] == "off"))
            {
                g_data.ignore_bots = false;
                if (hset({ g_data.redis_prefix, "ignorebots", "0" }))
                    _channel.create_message("No longer ignoring bot messages.");
                else
                    _channel.create_message("No longer ignoring bot messages (with errors).");
                return true;
            }
        }
        return false;
    }

    if (toks[0] == "perm")
    {
        if (check_params(toks, 2, "perm `command` [user|role] [add|rem|clear] [`id` [allow|deny]]"))
            return true;

        if (toks[1] == "list")
        {
            json entries;

            for (auto &[k, v] : g_data.cmds)
            {
                std::stringstream r_p;

                r_p << "User\n";
                for (auto & e : v.user_perms)
                    r_p << e.first << " | " << e.second << '\n';
                r_p << "\nRole\n";
                for (auto & e : v.role_perms)
                    r_p << e.first << " | " << e.second << '\n';
//                     r_p << "\nInternal\n";
//                     for (auto & e : v.int_perms)
//                         r_p << e.first << " | " << e.second << '\n';

                entries.emplace_back(json({ { "name", k },{ "value", r_p.str() },{ "inline", true } }));
            }

            if (entries.empty())
                entries.emplace_back(json({ { "name", "No permissions set" },{ "value", "n\\a" },{ "inline", true } }));

            json t = {
                { "title", "Permission list" },
                { "color", 10599460 },
                { "fields", entries },
                { "footer",{ { "icon_url", "https://cdn.discordapp.com/emojis/289276304564420608.png" },{ "text", "AegisBot" } } }
            };
            _channel.create_message_embed({}, t);
            return true;
        }

        if (check_params(toks, 4, "perm `command` [user|role] [add|rem|clear] [`id` [allow|deny]]"))
            return true;

        std::string cmd{ toks[1] };
        std::string type{ toks[2] };

        if (toks[3] == "add")
        {
            if (check_params(toks, 6, "perm `command` [user|role] add `id` allow|deny"))
                return true;
            auto new_mem = get_snowflake(std::string{ toks[4] }, _guild);
            if (type == "user")
            {
                //auto & new_user = bot.get_member_by_any(new_mem);
                if (new_mem == 0)
                {
                    _channel.create_message(fmt::format("Failed to find **user** `{}`", new_mem, cmd));
                    return true;
                }
                if (toks[5] == "allow")
                {
                    g_data.cmds[cmd].user_perms[new_mem] = s_command_data::perm_access::Allow;
                    hset({ fmt::format("{}:cmds:{}:u_perms", g_data.redis_prefix, cmd), fmt::format("{}", new_mem), "1" });
                    _channel.create_message(fmt::format("Successfully added **user** `{}` allow perms to command `{}`", new_mem, cmd));
                }
                else if (toks[5] == "deny")
                {
                    g_data.cmds[cmd].user_perms[new_mem] = s_command_data::perm_access::Deny;
                    hset({ fmt::format("{}:cmds:{}:u_perms", g_data.redis_prefix, cmd), fmt::format("{}", new_mem), "0" });
                    _channel.create_message(fmt::format("Successfully added **user** `{}` deny perms to command `{}`", new_mem, cmd));
                }
            }
            else if (type == "role")
            {
                std::shared_lock<std::shared_mutex> l(_guild.mtx());
                auto it = _guild.get_roles().find(new_mem);
                if (it == _guild.get_roles().end())
                {
                    _channel.create_message(fmt::format("Role `{}` does not exist", new_mem));
                    return true;
                }
                auto & new_role = it->second;
                if (toks[5] == "allow")
                {
                    g_data.cmds[cmd].role_perms[new_mem] = s_command_data::perm_access::Allow;
                    hset({ fmt::format("{}:cmds:{}:r_perms", g_data.redis_prefix, cmd), fmt::format("{}", new_role.role_id), "1" });
                    _channel.create_message(fmt::format("Successfully added **role** `{}` allow perms to command `{}`", new_mem, cmd));
                }
                else if (toks[5] == "deny")
                {
                    g_data.cmds[cmd].role_perms[new_mem] = s_command_data::perm_access::Deny;
                    hset({ fmt::format("{}:cmds:{}:r_perms", g_data.redis_prefix, cmd), fmt::format("{}", new_role.role_id), "0" });
                    _channel.create_message(fmt::format("Successfully added **role** `{}` deny perms to command `{}`", new_mem, cmd));
                }
            }
            else if (type == "internal")
            {
                auto it = g_data.ranks.find(new_mem);
                if (it == g_data.ranks.end())
                {
                    _channel.create_message(fmt::format("Internal **rank** `{}` does not exist", new_mem));
                    return true;
                }
                auto & new_int_role = it->second;
                if (toks[5] == "allow")
                {
                    g_data.cmds[cmd].int_perms[new_mem] = s_command_data::perm_access::Allow;
                    hset({ fmt::format("{}:cmds:{}:i_perms", g_data.redis_prefix, cmd), fmt::format("{}", new_int_role.rank_id), "1" });
                    _channel.create_message(fmt::format("Successfully added **internal** `{}` allow perms to command `{}`", new_mem, cmd));
                }
                else if (toks[5] == "deny")
                {
                    g_data.cmds[cmd].int_perms[new_mem] = s_command_data::perm_access::Deny;
                    hset({ fmt::format("{}:cmds:{}:i_perms", g_data.redis_prefix, cmd), fmt::format("{}", new_int_role.rank_id), "0" });
                    _channel.create_message(fmt::format("Successfully added **internal** `{}` deny perms to command `{}`", new_mem, cmd));
                }
            }
        }
        if (toks[3] == "rem")
        {
            if (check_params(toks, 6, "perm `command` [user|role] rem `id`"))
                return true;
            auto new_mem = get_snowflake(toks[4], _guild);
            if (type == "user")
            {
                //auto & new_user = bot.get_member_by_any(new_mem);
                hdel({ fmt::format("{}:cmds:{}:u_perms", g_data.redis_prefix, cmd), fmt::format("{}", new_mem) });
                _channel.create_message(fmt::format("Successfully removed **user** `{}` allow perms from command `{}`", new_mem, cmd));
            }
            else if (type == "role")
            {
                std::shared_lock<std::shared_mutex> l(_guild.mtx());
                auto it = _guild.get_roles().find(new_mem);
                if (it == _guild.get_roles().end())
                {
                    _channel.create_message(fmt::format("Role `{}` does not exist", new_mem));
                    return true;
                }
                auto & new_role = it->second;
                hdel({ fmt::format("{}:cmds:{}:r_perms", g_data.redis_prefix, cmd), fmt::format("{}", new_role.role_id), "1" });
                _channel.create_message(fmt::format("Successfully removed **role** `{}` allow perms from command `{}`", new_mem, cmd));
            }
            else if (type == "internal")
            {
                auto it = g_data.ranks.find(new_mem);
                if (it == g_data.ranks.end())
                {
                    _channel.create_message(fmt::format("Internal **rank** `{}` does not exist", new_mem));
                    return true;
                }
                auto & new_int_role = it->second;
                hdel({ fmt::format("{}:cmds:{}:i_perms", g_data.redis_prefix, cmd), fmt::format("{}", new_int_role.rank_id), "1" });
                _channel.create_message(fmt::format("Successfully removed **internal** `{}` allow perms from command `{}`", new_mem, cmd));
            }
        }
        if (toks[3] == "clear")
        {
            if (!check_params(toks, 4, "perm `command` [user|role] [add|rem|clear] [`id` [allow|deny]]"))
                return true;
            if (type == "user")
            {
                g_data.cmds[cmd].user_perms.clear();
                if (del(fmt::format("{}:cmds:{}:u_perms", g_data.redis_prefix, cmd)))
                    _channel.create_message(fmt::format("Successfully wiped **user** list for command `{}`", cmd));
            }
            else if (type == "role")
            {
                g_data.cmds[cmd].role_perms.clear();
                if (del(fmt::format("{}:cmds:{}:r_perms", g_data.redis_prefix, cmd)))
                    _channel.create_message(fmt::format("Successfully wiped **role** list for command `{}`", cmd));
            }
            else if (type == "internal")
            {
                g_data.cmds[cmd].int_perms.clear();
                if (del(fmt::format("{}:cmds:{}:i_perms", g_data.redis_prefix, cmd)))
                    _channel.create_message(fmt::format("Successfully wiped **internal** list for command `{}`", cmd));
            }
        }
        return true;
    }

    if (toks[0] == "kick")
    {
        if (check_params(toks, 2, "kick `user`"))
            return true;
            
        snowflake tar = get_snowflake(toks[1], _guild);
        if (tar == 0)
        {
            _channel.create_message(fmt::format("User not found: {}", toks[1].data()));
        }

        statsd.Metric<Count>("kicks", 1, 1);
        if (auto reply = _guild.remove_guild_member(tar).get(); !reply)
            _channel.create_message(fmt::format("Unable to kick: {}", toks[1].data()));
        else
        {
            auto tmem = bot.find_user(tar);
            if (!tmem)
                _channel.create_message(fmt::format("Kicked: {}", toks[1].data()));
            else
                _channel.create_message(fmt::format("Kicked: <@{}>", tar));
        }
    }

    if (toks[0] == "ban")
    {
        if (check_params(toks, 2, "ban `user` [reason]"))
            return true;

        snowflake tar = get_snowflake(toks[1], _guild);
        std::string reason;
        if (tar == 0)
        {
            _channel.create_message(fmt::format("User not found: {}", toks[1].data()));
            return true;
        }

        if (toks.size() > 2)
            reason = toks[2].data();

        statsd.Metric<Count>("bans", 1, 1);
        if (auto reply = _guild.create_guild_ban(tar, 0, reason).get(); !reply)
            _channel.create_message(fmt::format("Unable to ban: {}", toks[1].data()));
        else
        {
            auto tmem = bot.find_user(tar);
            if (!tmem)
                _channel.create_message(fmt::format("Banned: {}", toks[1].data()));
            else
                _channel.create_message(fmt::format("Banned: <@{}>", tar));
        }
        return true;
    }

    if (toks[0] == "softban")
    {
        if (check_params(toks, 2, "softban `user` [reason]"))
            return true;

        snowflake tar = get_snowflake(toks[1].data(), _guild);
        std::string reason;
        if (tar == 0)
        {
            _channel.create_message(fmt::format("User not found: {}", toks[1].data()));
            return true;
        }

        if (toks.size() > 2)
            reason = toks[2].data();

        statsd.Metric<Count>("softbans", 1, 1);
        if (auto reply = _guild.create_guild_ban(tar, 1, reason).get(); !reply)
            _channel.create_message(fmt::format("Unable to softban: {}", toks[1].data()));
        else
        {
            auto tmem = bot.find_user(tar);
            if (auto remove_reply = _guild.remove_guild_ban(tar).get(); !remove_reply)
            {
                if (!tmem)
                    _channel.create_message(fmt::format("Softbanned: {}", toks[1].data()));
                else
                    _channel.create_message(fmt::format("Softbanned: <@{}>", tar));
            }
            else
            {
                if (!tmem)
                    _channel.create_message(fmt::format("Banned: {}", toks[1].data()));
                else
                    _channel.create_message(fmt::format("Banned: <@{}>", tar));
            }
        }
        return true;
    }


/*
    if (toks[0] == "help")
    {



        / *json t = {
            { "title", "Server List" },
            { "description", "" },
            { "color", 10599460 }
        };
        _channel.create_message_embed("", t);* /
//         _channel.create_message(
//             R"(This bot is nearing release. If you'd like more information on it, please join the discord server https://discord.gg/Kv7aP5K
// This space will contain all the commands available to be used with this bot.
// A short list of non-exhaustive available commands so far: kick, ban, server, info, shard, shards, set, prefix, enmod, dismod, cmd, perm
// Most commands will give helpful information by typing them out without parameters.
// 
// A server must be 'activated' for the bot to perform actions for normal users. `set active true` otherwise the bot will not respond to anyone but the owner.
// Permissions for bot commands can be done on a per-user or per-role with the `perm` command.
// `perm ban user add @Sara#0429 allow`
// Members can be referenced by a mention `@Sharon#0429`, a full name `Sharon#0429`, or by snowflake `171000788183678976`
// The `cmd` command will allow configuring of individual commands for permissions.
// `cmd ban access_type role`
// will make the command use guild roles for permissions for this command. (changing all commands at once to come later)
// `cmd info default_access false`
// will make members unable to perform that command unless granted explicit permission by `perm`
// 
// )");
        _channel.create_message("This is where the help will go. For now, you can get help @ https://discord.gg/Kv7aP5K");
        return true;
    }*/

    if (toks[0] == "ignore_channel")
    {
        auto it = std::find(g_data.ignored_channels.begin(), g_data.ignored_channels.end(), channel_id);
        if (it == g_data.ignored_channels.end())
        {
            g_data.ignored_channels.push_back(channel_id);
            create_message(_channel, "Channel ignore set to: true", true);
            sadd("ignored_channels", { std::to_string(channel_id) }, g_data);
            return true;
        }

        g_data.ignored_channels.erase(it);
        create_message(_channel, "Channel ignore set to: false", true);
        srem("ignored_channels", { std::to_string(channel_id) }, g_data);
        return true;
    }

    if (toks[0] == "channel_status")
    {
        bool res = false;
        auto it = std::find(g_data.ignored_channels.begin(), g_data.ignored_channels.end(), channel_id);
        if (it != g_data.ignored_channels.end())
            res = true;

        create_message(_channel, fmt::format("Channel ignore currently set to: {}", res), true);
        return true;
    }

    if (toks[0] == "setplugins")
    {
        umod_servers[guild_id] = channel_id;
        req_success(obj.msg);
        hset({ "config:umod_servers", std::to_string(guild_id), std::to_string(channel_id) });
        return true;
    }

    if (toks[0] == "clearplugins")
    {
        auto it = umod_servers.find(guild_id);
        if (it != umod_servers.end())
            umod_servers.erase(it);
        req_success(obj.msg);
        hdel({ "config:umod_servers", std::to_string(guild_id) });
        return true;
    }

    return false;
}
