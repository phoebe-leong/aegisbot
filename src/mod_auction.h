//
// mod_auction.h
// *************
//
// Copyright (c) 2018 Sharon W (sharon at aegis dot gg)
//
// Distributed under the MIT License. (See accompanying file LICENSE)
// 

#pragma once

#include "Module.h"
#include <asio/steady_timer.hpp>
#include <set>

class Guild;

class mod_auction : public Module
{
public:
    mod_auction(Guild&, asio::io_context&);
    virtual ~mod_auction();

    std::vector<std::string> get_db_entries() override;

    static std::string r_prefix;

    void do_stop() override;

    bool check_command(std::string, shared_data&) override;

    void remove() override;

    void load(AegisBot&) override;


    struct Team
    {
        int32_t id;
        std::string owner;
        snowflake owner_id;
        std::vector<snowflake> bidders;
        std::string teamname;
        std::vector<std::pair<std::string, int>> players;
        int64_t funds = 0;
        bool withdrawn = false;
    };

    struct
    {
        int direction = 1;
        int32_t lastteam = 0;
        int32_t currentteam = 0;
        std::string player_bought;
    } undo_buy;

    std::unordered_map<std::string, bool> players;
    std::vector<Team> teams;
    int64_t defaultfunds = 0;
    int direction = 1;
    int32_t lastteam = 0;
    int32_t currentteam = 0;
    bool auctioninprogress = false;
    std::string currentnom;
    std::vector<std::pair<uint32_t, int>> bids;//team id, bid amount
    snowflake managerrole;
    snowflake hostrole;
    int64_t currentstandingsid = 0;
    bool paused = false;
    int64_t timeuntilstop = 0;

    std::set<snowflake> admins;
    asio::steady_timer timer;
    int64_t pausetimeleft = 0;

    int64_t bidtime = 30000;

    const Team & getteam(const snowflake id) const;

    void timercontinuation(const asio::error_code & ec, aegis::channel & _channel);

    bool reset(shared_data&);
    bool teamlist(shared_data&);
    bool _register(shared_data&);
    bool state(shared_data&);
    bool set(shared_data&);
    bool start(shared_data&);
    bool playerlist(shared_data&);
    bool addplayers(shared_data&);
    bool nom(shared_data&);
    bool pause(shared_data&);
    bool resume(shared_data&);
    bool bid(shared_data&);
    bool end(shared_data&);
    bool setname(shared_data&);
    bool standings(shared_data&);
    bool retain(shared_data&);
    bool skip(shared_data&);
    bool setfunds(shared_data&);
    bool undobid(shared_data&);
    bool auctionhelp(shared_data&);
    bool undobuy(shared_data&);
    bool withdraw(shared_data&);
    bool addfunds(shared_data&);
    bool removefunds(shared_data&);
    bool adminsetname(shared_data&);
    bool addbidder(shared_data&);
    bool delbidder(shared_data&);

};
