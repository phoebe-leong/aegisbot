//
// mod_automod.h
// *************
//
// Copyright (c) 2019 Sharon W (sharon at aegis dot gg)
//
// Distributed under the MIT License. (See accompanying file LICENSE)
// 

#pragma once

#include "Module.h"

class Guild;

class mod_automod : public Module
{
public:
    mod_automod(Guild&, asio::io_context&);
    virtual ~mod_automod();

    std::vector<std::string> get_db_entries() override;

    static std::string r_prefix;

    void do_stop() override;

    bool check_command(std::string, shared_data&) override;

    void remove() override;

    void load(AegisBot&) override;

    bool automod(shared_data&);

    bool process(shared_data&);

    enum class action_taken
    {
        Log,
        Warn,
        Delete,
        Kick,
        Ban
    };//461966371505307658 = juvenile

    aegis::snowflake log_target;

    std::unordered_map<std::string, action_taken> banned_keywords;

    std::vector<aegis::snowflake> ignored_roles;//merge into ignored_ids?
    std::vector<aegis::snowflake> ignored_users;

    std::string redis_key;
};
