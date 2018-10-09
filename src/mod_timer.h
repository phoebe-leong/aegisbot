//
// mod_automod.h
// *************
//
// Copyright (c) 2018 Sharon W (sharon at aegis dot gg)
//
// Distributed under the MIT License. (See accompanying file LICENSE)
// 

#pragma once

#include "Module.h"
#include <asio/steady_timer.hpp>

class Guild;

class mod_timer : public Module
{
public:
    mod_timer(Guild&, asio::io_context&);
    virtual ~mod_timer();

    std::vector<std::string> get_db_entries() override;

    static std::string r_prefix;

    void do_stop() override;

    bool check_command(std::string, shared_data&) override;

    void remove() override;

    void load(AegisBot&) override;

    asio::steady_timer timer;

    bool remind(shared_data&);

    //std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds>
    struct remind_st
    {
        int32_t id;
        snowflake owner_id;
        std::chrono::system_clock::time_point expiry_time;
        std::chrono::system_clock::time_point creation;
        std::string message;
    };

    std::unordered_map<std::string, std::unique_ptr<remind_st>> remindlist;

    std::chrono::milliseconds get_time(std::string s);

};
