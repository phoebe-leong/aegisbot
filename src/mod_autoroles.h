//
// mod_autoroles.h
// ***************
//
// Copyright (c) 2018 Sharon W (sharon at aegis dot gg)
//
// Distributed under the MIT License. (See accompanying file LICENSE)
// 

#pragma once

#include "Module.h"

class Guild;

class mod_autoroles : public Module
{
public:
    mod_autoroles(Guild&, asio::io_context&);
    virtual ~mod_autoroles();

    std::vector<std::string> get_db_entries() override;

    static std::string r_prefix;

    void do_stop() override;

    bool check_command(std::string, shared_data&) override;

    void remove() override;

    void load(AegisBot&) override;
};
