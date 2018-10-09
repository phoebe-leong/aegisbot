//
// mod_tag.cpp
// ***********
//
// Copyright (c) 2018 Sharon W (sharon at aegis dot gg)
//
// Distributed under the MIT License. (See accompanying file LICENSE)
// 

#include "mod_tag.h"
#include "AegisBot.h"

std::string mod_tag::r_prefix = "tag";

mod_tag::mod_tag(Guild & data, asio::io_context & _io)
    : Module(data, _io)
{
    commands["tag"] = std::bind(&mod_tag::tag, this, std::placeholders::_1);
}

mod_tag::~mod_tag()
{
}

std::vector<std::string> mod_tag::get_db_entries()
{
    return std::vector<std::string>();
}

void mod_tag::do_stop()
{

}

bool mod_tag::check_command(std::string cmd, shared_data & sd)
{
    auto it = commands.find(cmd);
    if (it == commands.end())
        return false;

    return it->second(sd);
}

void mod_tag::remove()
{
    delete this;
}

void mod_tag::load(AegisBot & bot)
{
    try
    {
        auto & mod_data = g_data._modules[modules::Tags];

        if (!mod_data->enabled && !mod_data->admin_override)
            return;//mod not enabled or overridden

        if (auto r_taglist = bot.get_vector(fmt::format("{}:tag:tags", g_data.redis_prefix)); !r_taglist.empty())
        {
            for (auto & tag_name : r_taglist)
            {
                auto tag_data = bot.get_array(fmt::format("{}:tag:tags:{}", g_data.redis_prefix, tag_name));
                if (tag_data["type"] == "t")
                {
                    auto & added_tag = *dynamic_cast<tag_st*>(taglist.emplace(tag_name, std::make_unique<tag_st>()).first->second.get());
                    added_tag.creation = std::stoull(tag_data["creation"]);
                    added_tag.owner_name = tag_data["owner_name"];
                    added_tag.owner_id = std::stoull(tag_data["owner_id"]);
                    added_tag.usage_count = std::stoi(tag_data["usage_count"]);
                    added_tag.content = tag_data["content"];
                    //bot.bot.log->info("Tag loaded: name({}) content({})", tag_name, tag_data["content"]);
                }
                else if (tag_data["type"] == "a")
                {
                    auto & added_tag = *dynamic_cast<alias_tag_st*>(taglist.emplace(tag_name, std::make_unique<alias_tag_st>()).first->second.get());
                    added_tag.creation = std::stoull(tag_data["creation"]);
                    added_tag.owner_name = tag_data["owner_name"];
                    added_tag.owner_id = std::stoull(tag_data["owner_id"]);
                    added_tag.usage_count = std::stoi(tag_data["usage_count"]);
                    added_tag.alias_of = tag_data["alias_of"];
                    //bot.bot.log->info("Tag alias loaded: name({}) alias_of({})", tag_name, tag_data["alias_of"]);
                }
            }
        }
    }
    catch (std::exception & e)
    {
        bot.bot.log->error("Exception loading auction module data for guild [{}] e[{}]", g_data.guild_id, e.what());
    }
};

bool mod_tag::tag(shared_data & sd)
{
#pragma region stuff
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

    if (toks.empty())
        return true;

    if (member_id != _guild.get_owner())
    {
        auto bit = std::find(blocklist.begin(), blocklist.end(), member_id);
        if (bit != blocklist.end())
            return true;//blocked
    }

#pragma endregion

    if (toks[0] == "create")
    {
        if (check_params(_channel, toks, 3, "tag create <name> <content>"))
            return true;

        if (member_id != _guild.get_owner())
        {
            //not tag owner, check delete perms
            auto it = permlist.find(member_id);
            if (it != permlist.end())
                if (it->second.can_create == false)
                {
                    sd.ab.req_permission(sd.msg.msg);
                    return true;//no permission to create
                }
        }

        if (member_id != _guild.get_owner())
        {
            //not guild owner

        }

        std::string tag_name{ toks[1] };

        auto it = taglist.find(tag_name);
        if (it != taglist.end())
        {
            _channel.create_message("Tag already exists.");
            return true;
        }

        auto & added_tag = *dynamic_cast<tag_st*>(taglist.emplace(tag_name, std::make_unique<tag_st>()).first->second.get());
        added_tag.creation = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        added_tag.owner_name = _member.get_full_name();
        added_tag.owner_id = member_id;
        added_tag.usage_count = 0;
        added_tag.content = toks[2].data();

        std::vector<std::string> vals;
        vals.emplace_back(fmt::format("{}:tag:tags:{}", g_data.redis_prefix, tag_name));
        vals.emplace_back("type");
        vals.emplace_back("t");
        vals.emplace_back("creation");
        vals.emplace_back(std::to_string(added_tag.creation));
        vals.emplace_back("owner_name");
        vals.emplace_back(added_tag.owner_name);
        vals.emplace_back("owner_id");
        vals.emplace_back(std::to_string(added_tag.owner_id));
        vals.emplace_back("usage_count");
        vals.emplace_back(std::to_string(added_tag.usage_count));
        vals.emplace_back("content");
        vals.emplace_back(added_tag.content);
        sd.ab.hmset(vals);
        sd.ab.sadd({ fmt::format("{}:tag:tags", g_data.redis_prefix), tag_name });
        _channel.create_message(fmt::format("Tag `{}` created.", tag_name));
        return true;
    }

    if (toks[0] == "alias")
    {
        if (check_params(_channel, toks, 3, "tag alias <name> <target>"))
            return true;

        if (member_id != _guild.get_owner())
        {
            //not tag owner, check delete perms
            auto it = permlist.find(member_id);
            if (it == permlist.end())
                if (it->second.can_create == false)
                {
                    sd.ab.req_permission(sd.msg.msg);
                    return true;//no permission to create
                }
        }

        std::string tag_name{ toks[1] };
        std::string target_name{ toks[2].data() };

        {
            auto it = taglist.find(tag_name);
            if (it != taglist.end())
            {
                _channel.create_message("Tag already exists.");
                return true;
            }
        }

        {
            auto it = taglist.find(target_name);
            if (it == taglist.end())
            {
                _channel.create_message("Target does not exist.");
                return true;
            }
            if (it->second->type == tag_type::Alias)
            {
                auto it = taglist.find(target_name);
                if (it == taglist.end())
                {
                    _channel.create_message("Target alias broken. Original tag not found.");
                    return true;
                }
                target_name = it->first;
            }
        }

        auto & added_tag = *dynamic_cast<alias_tag_st*>(taglist.emplace(tag_name, std::make_unique<alias_tag_st>()).first->second.get());
        added_tag.creation = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        added_tag.owner_name = _member.get_full_name();
        added_tag.owner_id = member_id;
        added_tag.usage_count = 0;
        added_tag.alias_of = toks[2].data();

        std::vector<std::string> vals;
        vals.emplace_back(fmt::format("{}:tag:tags:{}", g_data.redis_prefix, tag_name));
        vals.emplace_back("type");
        vals.emplace_back("a");
        vals.emplace_back("creation");
        vals.emplace_back(std::to_string(added_tag.creation));
        vals.emplace_back("owner_name");
        vals.emplace_back(added_tag.owner_name);
        vals.emplace_back("owner_id");
        vals.emplace_back(std::to_string(added_tag.owner_id));
        vals.emplace_back("usage_count");
        vals.emplace_back(std::to_string(added_tag.usage_count));
        vals.emplace_back("alias_of");
        vals.emplace_back(added_tag.alias_of);
        sd.ab.hmset(vals);
        _channel.create_message(fmt::format("Tag alias `{}` pointing to `{}` created.", tag_name, added_tag.alias_of));
        return true;
    }

    if (toks[0] == "delete")
    {
        if (check_params(_channel, toks, 2, "tag delete <name>"))
            return true;

        std::string tag_name{ toks[1] };

        auto it = taglist.find(tag_name);
        if (it == taglist.end())
        {
            _channel.create_message("Tag not found.");
            return true;
        }

        if (member_id != _guild.get_owner())
        {
            //not guild owner
            if (member_id != it->second->owner_id)
            {
                //not tag owner, check delete perms
                auto it = permlist.find(member_id);
                if (it == permlist.end() || it->second.can_delete == false)
                {
                    sd.ab.req_permission(sd.msg.msg);
                    return true;//no permission to delete
                }
            }
        }

        taglist.erase(it);

        //delete entry from list and remove hash map
        sd.ab.srem({ fmt::format("{}:tag:tags", g_data.redis_prefix), tag_name });
        sd.ab.del(fmt::format("{}:tag:tags:{}", g_data.redis_prefix, tag_name));

        _channel.create_message("Tag deleted.");
        return true;
    }

    if (toks[0] == "info")
    {
        if (check_params(_channel, toks, 2, "tag info <name>"))
            return true;

        std::string tag_name{ toks[1] };

        auto it = taglist.find(tag_name);
        if (it == taglist.end())
        {
            _channel.create_message("Tag not found.");
            return true;
        }
        if (it->second->type == tag_type::Tag)
        {
            const auto & tag_found = *dynamic_cast<tag_st*>(it->second.get());
            using namespace aegis::gateway::objects;

            embed e;
//             e.set_color(0x448800);
//             e.set_description(fmt::format("Tag `{}`", tag_name));
//             e.add_field("Owner", tag_found.owner_name, true);
//             e.add_field("Uses", std::to_string(tag_found.usage_count), true);
//             e.add_field(AegisBot::ZWSP, AegisBot::ZWSP, true);
            auto dura = std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds>(std::chrono::milliseconds(tag_found.creation));
            auto timet = std::chrono::system_clock::to_time_t(dura);
            char mstr[100];
            if (std::strftime(mstr, sizeof(mstr), "%F %T %z", std::localtime(&timet)))
            {
                footer ftr;
                ftr.text = fmt::format("Tag created: {}", mstr);
                //e.set_footer(ftr);
            }
            //e.add_field("Rank", std::to_string(tag_found.creation));

            _channel.create_message_embed("", e);
        }
        else if (it->second->type == tag_type::Alias)
        {
            const auto & tag_found = *dynamic_cast<alias_tag_st*>(it->second.get());
            using namespace aegis::gateway::objects;

            embed e;
//             e.set_color(0xDD0055);
//             e.set_description(fmt::format("Alias `{}`", tag_name));
//             e.add_field("Owner", tag_found.owner_name, true);
//             e.add_field("Uses", std::to_string(tag_found.usage_count), true);
//             e.add_field("Target", tag_found.alias_of, true);
            auto dura = std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds>(std::chrono::milliseconds(tag_found.creation));
            auto timet = std::chrono::system_clock::to_time_t(dura);
            char mstr[100];
            if (std::strftime(mstr, sizeof(mstr), "%F %T %z", std::localtime(&timet)))
            {
                footer ftr;
                ftr.text = fmt::format("Tag created: {}", mstr);
                //e.set_footer(ftr);
            }

            _channel.create_message_embed("", e);
        }
        else
        {

        }
        return true;
    }


    //perms
    if (toks[0] == "perms")
    {
        //if member is owner, bypass perm check
        if (member_id != _guild.get_owner())
        {
            auto it = permlist.find(member_id);
            if (it == permlist.end() || it->second.can_manage == false)
            {
                sd.ab.req_permission(sd.msg.msg);
                return true;//no permission to manage
            }
        }

        if (check_params(_channel, toks, 3, "tag perms <add|del> <name> [create|delete|manage]"))
            return true;

        if (toks[1] == "add")
        {
            if (check_params(_channel, toks, 4, "tag perms add <name> <create|delete|manage>"))
                return true;

            std::string target_name{ toks[2] };

            snowflake target_user = sd.ab.get_snowflake(target_name, _guild);

            if (!target_user)
            {
                _channel.create_message("Unable to find member.");
                return true;
            }

            std::string perm{ toks[3] };

            if (toks.size() == 4)
            {
                //single perm or single word list create|delete

                auto & tar_perm = permlist[target_user];

                //search for existence of perms
                if (perm == "create")
                    tar_perm.can_create = true;
                else if (perm == "delete")
                    tar_perm.can_delete = true;
                else if (perm == "manage")
                {
                    if (member_id == _guild.get_owner())
                        tar_perm.can_manage = true;
                }
                else
                {
                    auto n = perm.find("create");
                    if (n != std::string::npos) tar_perm.can_create = true;
                    n = perm.find("delete");
                    if (n != std::string::npos) tar_perm.can_delete = true;
                    if (member_id == _guild.get_owner())
                    {
                        n = perm.find("manage");
                        if (n != std::string::npos) tar_perm.can_manage = true;
                    }
                }
                _channel.create_message("Member tag perms updated.");
            }
            else if (toks.size() >= 5)
            {
                //multi word perms

                auto & tar_perm = permlist[target_user];

                //search for existence of perms
                toks.erase(toks.begin());
                toks.erase(toks.begin());
                toks.erase(toks.begin());
                for (auto & t : toks)
                {
                    if (t == "create")
                        tar_perm.can_create = true;
                    else if (t == "delete")
                        tar_perm.can_delete = true;
                    else if (t == "manage")
                    {
                        if (member_id == _guild.get_owner())
                            tar_perm.can_manage = true;
                    }
                    else
                    {
                        auto n = t.find("create");
                        if (n != std::string::npos) tar_perm.can_create = true;
                        n = t.find("delete");
                        if (n != std::string::npos) tar_perm.can_delete = true;
                        if (member_id == _guild.get_owner())
                        {
                            n = t.find("manage");
                            if (n != std::string::npos) tar_perm.can_manage = true;
                        }
                    }
                }
                _channel.create_message("Member tag perms updated.");
            }
            return true;
        }
        else if ((toks[1] == "del") || (toks[1] == "delete") || (toks[1] == "rem") || (toks[1] == "remove"))
        {
            std::string target_name{ toks[2] };

            snowflake target_user = sd.ab.get_snowflake(target_name, _guild);


            if (toks.size() == 3)
            {
                auto it = permlist.find(target_user);
                if (it == permlist.end())
                    return true;
                permlist.erase(it);
                _channel.create_message("Member tag perms reset.");
                return true;
            }
            else if (toks.size() == 4)
            {
                //single perm or single word list create|delete
                std::string perm{ toks[3] };

                auto & tar_perm = permlist[target_user];

                //search for existence of perms
                if (perm == "create")
                    tar_perm.can_create = false;
                else if (perm == "delete")
                    tar_perm.can_delete = false;
                else if (perm == "manage")
                {
                    if (member_id == _guild.get_owner())
                        tar_perm.can_manage = false;
                }
                else
                {
                    auto n = perm.find("create");
                    if (n != std::string::npos) tar_perm.can_create = false;
                    n = perm.find("delete");
                    if (n != std::string::npos) tar_perm.can_delete = false;
                    if (member_id == _guild.get_owner())
                    {
                        n = perm.find("manage");
                        if (n != std::string::npos) tar_perm.can_manage = false;
                    }
                }
                _channel.create_message("Member tag perms updated.");
            }
            else if (toks.size() >= 5)
            {
                //multi word perms
                std::string perm{ toks[3] };
               
                auto & tar_perm = permlist[target_user];

                //search for existence of perms
                toks.erase(toks.begin());
                toks.erase(toks.begin());
                toks.erase(toks.begin());
                for (auto & t : toks)
                {
                    if (t == "create")
                        tar_perm.can_create = false;
                    else if (t == "delete")
                        tar_perm.can_delete = false;
                    else if (t == "manage")
                    {
                        if (member_id == _guild.get_owner())
                            tar_perm.can_manage = false;
                    }
                    else
                    {
                        auto n = t.find("create");
                        if (n != std::string::npos) tar_perm.can_create = false;
                        n = t.find("delete");
                        if (n != std::string::npos) tar_perm.can_delete = false;
                        if (member_id == _guild.get_owner())
                        {
                            n = t.find("manage");
                            if (n != std::string::npos) tar_perm.can_manage = false;
                        }
                    }
                }
                _channel.create_message("Member tag perms updated.");
            }
            return true;
        }
        else
        {

        }
        return true;
    }

    if (toks[0] == "block")
    {
        if (check_params(_channel, toks, 1, "tag block <name>"))
            return true;

        if (member_id != _guild.get_owner())
        {
            //not guild owner
            auto it = permlist.find(member_id);
            if (it == permlist.end() || it->second.can_manage == false)
            {
                sd.ab.req_permission(sd.msg.msg);
                return true;//no permission to manage
            }
        }

        snowflake target_user = sd.ab.get_snowflake(toks[1], _guild);

        if (!target_user)
        {
            _channel.create_message("Unable to find member.");
            return true;
        }

        blocklist.push_back(target_user);
        sd.ab.sadd({ fmt::format("{}:tag:blocklist", g_data.redis_prefix), std::to_string(target_user) });
        sd.ab.req_success(sd.msg.msg);
        return true;
    }

    if (toks[0] == "unblock")
    {
        if (check_params(_channel, toks, 1, "tag perms <name>"))
            return true;

        if (member_id != _guild.get_owner())
        {
            //not guild owner
            auto it = permlist.find(member_id);
            if (it == permlist.end() || it->second.can_manage == false)
            {
                sd.ab.req_permission(sd.msg.msg);
                return true;//no permission to manage
            }
        }

        snowflake target_user = sd.ab.get_snowflake(toks[1], _guild);

        if (!target_user)
        {
            _channel.create_message("Unable to find member.");
            return true;
        }

        auto it = std::find(blocklist.begin(), blocklist.end(), target_user);
        if (it == blocklist.end())
            return true;
        blocklist.erase(it);
        sd.ab.srem({ fmt::format("{}:tag:blocklist", g_data.redis_prefix), std::to_string(target_user) });
        sd.ab.req_success(sd.msg.msg);
        return true;
    }


    //check for tags

    std::string tag_name{ toks[0] };

    auto it = taglist.find(tag_name);
    if (it == taglist.end())
    {
        //do fuzzy search or display potential results?
        _channel.create_message("Tag not found.");
        return true;
    }

    auto & tag_found = *it->second;

    if (tag_found.type == tag_type::Tag)
    {
        auto & tag_found = *dynamic_cast<tag_st*>(it->second.get());
        sd.ab.basic_action("HINCRBY", { fmt::format("{}:tag:tags:{}", g_data.redis_prefix, tag_name), "usage_count", "1" });
        ++tag_found.usage_count;
        _channel.create_message(tag_found.content);
    }
    else if (tag_found.type == tag_type::Alias)
    {
        auto & tag_found = *dynamic_cast<alias_tag_st*>(it->second.get());

        auto it = taglist.find(tag_found.alias_of);
        if (it == taglist.end())
        {
            //error. alias broken
            _channel.create_message("Alias broken. Original tag not found.");
            return true;
        }
        sd.ab.basic_action("HINCRBY", { fmt::format("{}:tag:tags:{}", g_data.redis_prefix, tag_name), "usage_count", "1" });
        ++tag_found.usage_count;

        {
            auto & tag_found = *dynamic_cast<tag_st*>(it->second.get());

            sd.ab.basic_action("HINCRBY", { fmt::format("{}:tag:tags:{}", g_data.redis_prefix, tag_name), "usage_count", "1" });
            ++tag_found.usage_count;
            _channel.create_message(tag_found.content);
        }
    }

    return true;
};
