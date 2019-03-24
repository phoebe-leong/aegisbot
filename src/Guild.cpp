//
// Guild.cpp
// *********
//
// Copyright (c) 2019 Sharon W (sharon at aegis dot gg)
//
// Distributed under the MIT License. (See accompanying file LICENSE)
// 

#include "Guild.h"
#include "AegisBot.h"
#include <aegis/guild.hpp>
#include <aegis/channel.hpp>

// Guild::Guild()
// {
// }
// 
// Guild::~Guild()
// {
// }

bool Guild::is_channel_ignored(aegis::snowflake channel_id)
{
    auto it = std::find(ignored_channels.begin(), ignored_channels.end(), channel_id);
    if (it == ignored_channels.end())
        return false;
    return *it;
}

bool Guild::send_message_default(std::string msg)
{
	aegis::channel* c = nullptr;
	if (log_channel)
		c = bot._bot->find_channel(log_channel);
	if (!c && default_channel)
		c = bot._bot->find_channel(default_channel);
	if (!c)
		c = get_default_channel();
	if (!c)
		return false;
	
	c->create_message(msg);
	return true;
}

aegis::channel* Guild::get_default_channel()
{
	auto g = bot._bot->find_guild(guild_id);
	if (!g) return nullptr;//guild not found
	for (auto [k,v] : g->get_channels())
		if (v->get_type()
			== aegis::channel_type::Text
			&& std::abs(v->get_id() - guild_id) < 10000)
			return v; //high chance of being a default channel
	return nullptr;
}

