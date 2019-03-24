//
// mod_tag.cpp
// ***********
//
// Copyright (c) 2019 Sharon W (sharon at aegis dot gg)
//
// Distributed under the MIT License. (See accompanying file LICENSE)
// 

#include "mod_perms.h"
#include "AegisBot.h"

std::string mod_perms::r_prefix = "perms";

mod_perms::mod_perms(Guild & data, asio::io_context & _io)
    : Module(data, _io)
{
    //commands["perms"] = std::bind(&mod_perms::perms, this, std::placeholders::_1);
}

mod_perms::~mod_perms()
{
}

std::vector<std::string> mod_perms::get_db_entries()
{
    return std::vector<std::string>();
}

void mod_perms::do_stop()
{

}

bool mod_perms::check_command(std::string cmd, shared_data & sd)
{
    auto it = commands.find(cmd);
    if (it == commands.end())
        return false;

    return it->second(sd);
}

void mod_perms::remove()
{
    delete this;
}

void mod_perms::load(AegisBot & bot)
{
//     try
//     {
//         auto & mod_data = g_data.enabled_modules[modules::Tags];
// 
//         mod_data.enabled = bot.to_bool(bot.hget({ fmt::format("{}:tag", g_data.redis_prefix), "enabled" }));
//         mod_data.admin_override = bot.to_bool(bot.hget({ fmt::format("{}:tag", g_data.redis_prefix), "admin_override" }));
// 
//         if (!mod_data.enabled && !mod_data.admin_override)
//             return;//mod not enabled or overridden
// 
//         if (auto r_taglist = bot.get_vector(fmt::format("{}:tag:list", g_data.redis_prefix)); !taglist.empty())
//         {
//             for (auto & tag_name : r_taglist)
//             {
//                 auto tag_data = bot.get_array(fmt::format("{}:tag:tags:{}", g_data.redis_prefix, tag_name));
//                 if (tag_data["type"] == "tag")
//                 {
//                     auto & added_tag = *dynamic_cast<tag_st*>(taglist.emplace(tag_name, std::make_unique<tag_st>()).first->second.get());
//                     added_tag.creation = std::stoull(tag_data["creation"]);
//                     added_tag.owner_name = tag_data["owner_name"];
//                     added_tag.owner_id = std::stoull(tag_data["owner_id"]);
//                     added_tag.usage_count = std::stoi(tag_data["usage_count"]);
//                     added_tag.content = tag_data["content"];
//                     bot.bot.log->info("Tag loaded: name({}) content({})", tag_name, tag_data["content"]);
//                 }
//                 else if (tag_data["type"] == "alias")
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
};

bool mod_perms::perms(shared_data & sd)
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


    if (toks[0] == "create")
    {
        if (check_params(_channel, toks, 3, "perms create <name> <content>"))
            return true;

        return true;
    }

  
    return false;
};
