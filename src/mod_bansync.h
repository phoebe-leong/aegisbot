//
// mod_bansync.h
// *************
//
// Copyright (c) 2019 Sharon W (sharon at aegis dot gg)
//
// Distributed under the MIT License. (See accompanying file LICENSE)
// 

#pragma once

#include "Module.h"
#include <spdlog/spdlog.h>
#include <asio/steady_timer.hpp>
#include <set>

class Guild;

struct bansync_ban
{
	//aegis::snowflake user_id;
	aegis::snowflake added_by;
	int64_t time_added;
	std::string reason;
};

class mod_bansync : public Module
{
public:
    mod_bansync(Guild&, asio::io_context&);
    ~mod_bansync();

    std::vector<std::string> get_db_entries() override;

    static std::string r_prefix;
    std::string redis_key;
    std::shared_ptr<spdlog::logger> log;

    asio::steady_timer sync_timer;

    std::unordered_map<aegis::snowflake, int64_t> other_servers;

	std::set<aegis::snowflake> allowed_to_sync;

	std::unordered_map<aegis::snowflake, bansync_ban> active_bans;

    bool bansync(shared_data&);

    void do_stop() override;

    bool check_command(std::string, shared_data&) override;

    void remove() override;

    void load(AegisBot&) override;
};
