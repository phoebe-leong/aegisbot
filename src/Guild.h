//
// Guild.h
// *******
//
// Copyright (c) 2018 Sharon W (sharon at aegis dot gg)
//
// Distributed under the MIT License. (See accompanying file LICENSE)
// 

#pragma once

#include <string>
#include <vector>
#include <map>
#include <aegis/snowflake.hpp>
#include "mod_auction.h"
#include "mod_automod.h"
#include "mod_music.h"
#include "mod_autoresponder.h"
#include "mod_log.h"
#include "mod_autoroles.h"
#include "mod_tag.h"
#include "mod_moderation.h"
#include "mod_announcer.h"
//#include "mod_perms.h"

struct s_command_data
{
    enum perm_access
    {
        Deny = 0,
        Allow = 1
    };
    enum perm_type
    {
        User = 0,
        Role = 1,
        Internal = 2
    };
    bool enabled = true;
    perm_access default_access = Allow;
    perm_type access_type = User;
    std::unordered_map<aegis::snowflake, perm_access> role_perms;
    std::unordered_map<aegis::snowflake, perm_access> user_perms;
    std::unordered_map<aegis::snowflake, perm_access> int_perms;
};

enum modules
{
    Auction = 1,
    Music = 2,
    Announcer = 3,
    Autoresponder = 4,
    Autoroles = 5,
    Moderation = 6,
    Tags = 7,
    Log = 8,
    Automod = 9,
    Timer = 10,
    MAX_MODULES
};

struct s_rank
{
    //lower rank = higher priority
    int64_t rank_id = 0;
    int64_t rank_value = 0;
    std::string rank_name;
};

class Guild
{
public:
    Guild() = default;
    ~Guild() = default;

    aegis::snowflake guild_id;
    bool loaded = false;
    std::vector<std::string> cmd_prefix_list;
    std::string redis_prefix;/*< config:guild:id */
    bool cached = false;
    bool ignore_bots = false;
    bool ignore_users = false;
    aegis::snowflake mod_role;
    //uint16_t mod_role_position = 0;
    std::string nick;
    std::unordered_map<std::string, s_command_data> cmds;
//    std::unordered_map<modules, s_module_data_guild> enabled_modules;
    std::unordered_map<modules, Module*> _modules;
    std::unordered_map<int64_t, s_rank> ranks;
};
