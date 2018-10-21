//
// mod_autoresponder.cpp
// *********************
//
// Copyright (c) 2018 Sharon W (sharon at aegis dot gg)
//
// Distributed under the MIT License. (See accompanying file LICENSE)
// 

#include "mod_timer.h"
#include "AegisBot.h"//TODO: this is only used for redis actions. make a redis class?
#include "Guild.h"
#include <aegis/channel.hpp>
#include <nlohmann/json.hpp>

using aegis::snowflake;
using aegis::member;
using aegis::channel;
using aegis::guild;

std::string mod_timer::r_prefix = "timer";

mod_timer::mod_timer(Guild & data, asio::io_context & _io)
    : Module(data, _io)
    , timer(_io)
{
    commands["reset"] = std::bind(&mod_timer::remind, this, std::placeholders::_1);
}

mod_timer::~mod_timer()
{
}

std::vector<std::string> mod_timer::get_db_entries()
{
    return std::vector<std::string>();
}

void mod_timer::do_stop()
{
    timer.cancel();
}

bool mod_timer::check_command(std::string cmd, shared_data & sd)
{
    auto it = commands.find(cmd);
    if (it == commands.end())
        return false;

    return it->second(sd);
}

void mod_timer::remove()
{
    delete this;
}

void mod_timer::load(AegisBot & bot)
{
//     try
//     {
//         auto & mod_data = g_data._modules[modules::Timer];
// 
//         if (!mod_data->enabled && !mod_data->admin_override)
//             return;//mod not enabled or overridden
// 
//         if (auto r_taglist = bot.get_vector(fmt::format("{}:tag:tags", g_data.redis_prefix)); !r_taglist.empty())
//         {
//             for (auto & tag_name : r_taglist)
//             {
//                 auto tag_data = bot.get_array(fmt::format("{}:tag:tags:{}", g_data.redis_prefix, tag_name));
//                 if (tag_data["type"] == "t")
//                 {
//                     auto & added_tag = *dynamic_cast<tag_st*>(taglist.emplace(tag_name, std::make_unique<tag_st>()).first->second.get());
//                     added_tag.creation = std::stoull(tag_data["creation"]);
//                     added_tag.owner_name = tag_data["owner_name"];
//                     added_tag.owner_id = std::stoull(tag_data["owner_id"]);
//                     added_tag.usage_count = std::stoi(tag_data["usage_count"]);
//                     added_tag.content = tag_data["content"];
//                     bot.bot.log->info("Tag loaded: name({}) content({})", tag_name, tag_data["content"]);
//                 }
//                 else if (tag_data["type"] == "a")
//                 {
//                     auto & added_tag = *dynamic_cast<alias_tag_st*>(taglist.emplace(tag_name, std::make_unique<alias_tag_st>()).first->second.get());
//                     added_tag.creation = std::stoull(tag_data["creation"]);
//                     added_tag.owner_name = tag_data["owner_name"];
//                     added_tag.owner_id = std::stoull(tag_data["owner_id"]);
//                     added_tag.usage_count = std::stoi(tag_data["usage_count"]);
//                     added_tag.alias_of = tag_data["alias_of"];
//                     bot.bot.log->info("Tag alias loaded: name({}) alias_of({})", tag_name, tag_data["alias_of"]);
//                 }
//             }
//         }
//     }
//     catch (std::exception & e)
//     {
//         bot.bot.log->error("Exception loading auction module data for guild [{}] e[{}]", g_data.guild_id, e.what());
//     }
}

bool mod_timer::remind(shared_data & sd)
{
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

    bool is_guild_owner = (member_id == guild_owner_id || member_id == sd.ab.bot_owner_id);

    std::string request{ content };

    toks.erase(toks.begin());

    if (toks.size() == 0)
        return true;

//     if (toks[0] == "list")
//     {
// 
//     }
// 
//     {
//         if (check_params(_channel, toks, 2, "remind <time> <message>"))
//             return true;
// 
//         std::string time_info{ toks[1] };
// 
// 
// 
//         try
//         {
//             std::string time_in;
//             std::chrono::milliseconds time_calc = get_time(time_in);
//         }
//         catch (std::exception & e)
//         {
//             std::cout << e.what();
//         }
// 
// 
//         auto & added_tag = *dynamic_cast<tag_st*>(taglist.emplace(tag_name, std::make_unique<tag_st>()).first->second.get());
//         added_tag.creation = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
//         added_tag.owner_name = _member.get_full_name();
//         added_tag.owner_id = member_id;
//         added_tag.usage_count = 0;
//         added_tag.content = toks[2].data();
// 
//         std::vector<std::string> vals;
//         vals.emplace_back(fmt::format("{}:tag:tags:{}", g_data.redis_prefix, tag_name));
//         vals.emplace_back("type");
//         vals.emplace_back("t");
//         vals.emplace_back("creation");
//         vals.emplace_back(std::to_string(added_tag.creation));
//         vals.emplace_back("owner_name");
//         vals.emplace_back(added_tag.owner_name);
//         vals.emplace_back("owner_id");
//         vals.emplace_back(std::to_string(added_tag.owner_id));
//         vals.emplace_back("usage_count");
//         vals.emplace_back(std::to_string(added_tag.usage_count));
//         vals.emplace_back("content");
//         vals.emplace_back(added_tag.content);
//         sd.ab.hmset(vals);
//         sd.ab.sadd({ fmt::format("{}:tag:tags", g_data.redis_prefix), tag_name });
//         _channel.create_message(fmt::format("Tag `{}` created.", tag_name));
//         return true;
//     }
    return true;
}

std::chrono::milliseconds mod_timer::get_time(std::string s)
{
    std::chrono::milliseconds time_from_now(0);
    std::string out;
    auto iter = s.begin();

    for (auto c = s.begin(); c != s.end(); ++c)
    {
        out = "";
        if (*c == 's')
        {//seconds
            std::copy(iter, c, std::back_inserter(out));
            iter = c + 1;

            int64_t seconds = std::stol(out);
            if (seconds == 0)
                return std::chrono::milliseconds(0);
            time_from_now += std::chrono::seconds(seconds);
        }

        if (*c == 'm')
        {//minutes
            std::copy(iter, c, std::back_inserter(out));
            iter = c + 1;

            int64_t minutes = std::stol(out);
            if (minutes == 0)
                return std::chrono::milliseconds(0);
            time_from_now += std::chrono::minutes(minutes);
        }

        if (*c == 'h')
        {//hours
            std::copy(iter, c, std::back_inserter(out));
            iter = c + 1;

            int64_t hours = std::stol(out);
            if (hours == 0)
                return std::chrono::milliseconds(0);
            time_from_now += std::chrono::hours(hours);
        }

        if (*c == 'd')
        {//days
            std::copy(iter, c, std::back_inserter(out));
            iter = c + 1;

            int64_t days = std::stol(out);
            if (days == 0)
                return std::chrono::milliseconds(0);
            time_from_now += std::chrono::duration<int, std::ratio<3600 * 24>>(days);
        }
    }

    return time_from_now;
}
