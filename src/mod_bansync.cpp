//
// mod_bansync.cpp
// ***************
//
// Copyright (c) 2019 Sharon W (sharon at aegis dot gg)
//
// Distributed under the MIT License. (See accompanying file LICENSE)
// 

#include "mod_bansync.h"
#include "AegisBot.h"
#include <aegis/guild.hpp>
#include <aegis/channel.hpp>
#include <aegis/user.hpp>

std::string mod_bansync::r_prefix = "bansync";

mod_bansync::mod_bansync(Guild & data, asio::io_context & _io)
    : Module(data, _io)
    , sync_timer(_io)
{
    commands["bansync"] = std::bind(&mod_bansync::bansync, this, std::placeholders::_1);
    commands["bs"] = std::bind(&mod_bansync::bansync, this, std::placeholders::_1);
}

mod_bansync::~mod_bansync()
{
}

std::vector<std::string> mod_bansync::get_db_entries()
{
    return std::vector<std::string>();
}

void mod_bansync::do_stop()
{

}

bool mod_bansync::check_command(std::string cmd, shared_data & sd)
{
    auto it = commands.find(cmd);
    if (it == commands.end())
        return false;

    return it->second(sd);
}

void mod_bansync::remove()
{
    delete this;
}

void mod_bansync::load(AegisBot & bot)
{
    log = spdlog::get("aegis");

    redis_key = fmt::format("{}:{}", g_data.redis_prefix, r_prefix);//config:guild:id:bansync

    auto bans = bot.get_array({ fmt::format("{}:bans", redis_key) });
	auto perms = bot.get_vector({ fmt::format("{}:perms", redis_key) });
	auto servers = bot.get_array({ fmt::format("{}:servers", redis_key) });

    for (auto &[k, v] : bans)
    {
		json j = json::parse(v);
		active_bans.emplace(std::stoll(k), bansync_ban{ std::stoll(j["added_by"].get<std::string>()), std::stoll(j["time_added"].get<std::string>()), j["reason"].get<std::string>() });
    }

    for (auto & v : perms)
		allowed_to_sync.insert(std::stoll(v));

	for (auto &[k,v] : servers)
		other_servers.emplace(std::stoll(k), std::stoll(v));


    //bot.req_success(sd.msg.msg);

}

bool mod_bansync::bansync(shared_data & sd)
{
#pragma region stuff
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

	if (!is_guild_owner)
		return true;//only owner can modify this module

    std::string request{ content };

    toks.erase(toks.begin());

    if (toks.empty())
        return true;

#pragma endregion

    if (toks[0] == "allow")
    {
        if (check_params(_channel, toks, 2, "bansync allow <user>"))
            return true;

		aegis::snowflake userid = sd.ab.get_snowflake(toks[1], sd.guild);

		if (userid)
		{
			sd.ab.sadd({ fmt::format("{}:perms", redis_key), std::to_string(userid) });
			allowed_to_sync.insert(userid);
			sd.ab.req_success(sd.msg.msg);
		}
		else
			sd.ab.req_fail(sd.msg.msg);

        return true;
    }

    if (toks[0] == "deny")
    {
        if (check_params(_channel, toks, 2, "bansync deny <user>"))
            return true;

		aegis::snowflake userid = sd.ab.get_snowflake(toks[1], sd.guild);

		if (userid)
		{
			sd.ab.srem({ fmt::format("{}:perms", redis_key), std::to_string(userid) });
			allowed_to_sync.erase(userid);
			sd.ab.req_success(sd.msg.msg);
		}
		else
			sd.ab.req_fail(sd.msg.msg);

        return true;
    }

    if ((toks[0] == "add") || (toks[0] == "ban"))
    {
        if (check_params(_channel, toks, 2, "bansync add <user> [reason]"))
            return true;

        aegis::snowflake userid = sd.ab.get_snowflake(toks[1], sd.guild);

        std::string reason;
        if (toks.size() > 2)
            reason = std::string(toks[2].data());

        if (!userid)
        {
            _channel.create_message(fmt::format("Unable to parse user `{}`", toks[2]));
            return true;
        }

        //auto bans = sd.ab.get_array({ fmt::format("{}:bans", redis_key) });

        json obj;
		int64_t _time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        obj["time_added"] = std::to_string(_time);
		obj["added_by"] = std::to_string(member_id);
		obj["reason"] = reason;

        std::vector<std::string> vals;
        vals.emplace_back(fmt::format("{}:bans", redis_key));
        vals.emplace_back(std::to_string(userid));
        vals.emplace_back(obj.dump());
        sd.ab.hmset(vals);
		sd.ab.req_success(sd.msg.msg);

		active_bans.emplace(userid, bansync_ban{ member_id, _time, reason });

		g_data.send_message_default(fmt::format("BANSYNC Banned: <@{}> source[{}] for `{}`", userid, guild_id, reason));
		_guild.create_guild_ban(userid, 0, reason);
		//_channel.create_message(fmt::format("Would have banned: <@{}>", userid));
        for (auto& gd : other_servers)
        {
            auto g = sd.ab._bot->find_guild(gd.first);//nullptr if bot not in server
            if (!g) continue; //error
            auto bot_gd = sd.ab.find_guild(gd.first);//nullptr if bot not in server
            if (!bot_gd) continue; //error

			bot_gd->send_message_default(fmt::format("BANSYNC Banned: <@{}> source[{}] for `{}`", userid, guild_id, reason));

			g->create_guild_ban(userid, 0, reason);
        }
        return true;
    }

    if ((toks[0] == "rem") || (toks[0] == "remove") || (toks[0] == "del") || (toks[0] == "delete"))
    {
        if (check_params(_channel, toks, 2, "bansync remove <user>"))
            return true;

        aegis::snowflake userid = sd.ab.get_snowflake(toks[1], sd.guild);

        if (!userid)
        {
            _channel.create_message(fmt::format("Unable to parse user `{}`", toks[2]));
            return true;
        }

        sd.ab.hdel({ fmt::format("{}:bans", redis_key), std::to_string(userid ) });
        sd.ab.req_success(sd.msg.msg);

		auto it = active_bans.find(userid);
		if (it != active_bans.end())
			active_bans.erase(it);

		g_data.send_message_default(fmt::format("BANSYNC Unbanned: <@{}> source[{}]", userid, guild_id));
		_guild.remove_guild_ban(userid);
		//_channel.create_message(fmt::format("Would have banned: <@{}>", userid));
		for (auto& gd : other_servers)
		{
			auto g = sd.ab._bot->find_guild(gd.first);//nullptr if bot not in server
			if (!g) continue; //error
			auto bot_gd = sd.ab.find_guild(gd.first);//nullptr if bot not in server
			if (!bot_gd) continue; //error

			bot_gd->send_message_default(fmt::format("BANSYNC Unbanned: <@{}> source[{}]", userid, guild_id));

			g->remove_guild_ban(userid);
		}

        return true;
    }

	if (toks[0] == "link")
	{
		if (check_params(_channel, toks, 1, "link <server>"))
			return true;

		aegis::snowflake target_server = std::stoll(std::string{ toks[1] });
		auto g = sd.ab._bot->find_guild(target_server);
		if (!g)
		{
			_channel.create_message(fmt::format("I am not in that server `{}`", target_server));
			return true;
		}
		if (g->get_owner() == _member.get_id())
		{
			//is also owner of target server
			int64_t _time = std::chrono::system_clock::now().time_since_epoch().count();
			sd.ab.hset({ fmt::format("{}:servers", redis_key), std::to_string(target_server), std::to_string(_time) });
			other_servers.emplace(target_server, _time);
			_channel.create_message(fmt::format("Added server `{}` to synced banlist", target_server));
		}
		return true;
	}

	if (toks[0] == "unlink")
	{
		if (check_params(_channel, toks, 1, "unlink <server>"))
			return true;

		aegis::snowflake target_server = std::stoll(std::string{ toks[1] });
		auto g = sd.ab._bot->find_guild(target_server);
		if (!g)
		{
			_channel.create_message(fmt::format("I am not in that server `{}`", target_server));
			return true;
		}
		if (g->get_owner() == _member.get_id())
		{
			//is also owner of target server
			auto it = other_servers.find(target_server);
			if (it == other_servers.end())
				sd.ab.req_fail(sd.msg.msg);
			else
			{
				sd.ab.hdel({ fmt::format("{}:servers", redis_key), std::to_string(target_server) });
				other_servers.erase(it);
				_channel.create_message(fmt::format("Removed server `{}` from synced banlist", target_server));
			}
		}
		return true;
	}

	if (toks[0] == "servers")
	{
		std::stringstream w;
		w << "Synced servers\n```\n";
		for (auto & [k,v] : other_servers)
		{
			aegis::guild * g = sd.ab._bot->find_guild(k);
			if (!g)
				w << '[' << k << "]  \n";
			else
				w << '[' << k << "]  " << g->get_name() << '\n';
		}
		w << "```";

		_channel.create_message(w.str());

		return true;
	}

	return false;
}
