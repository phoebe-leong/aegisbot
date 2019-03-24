//
// mod_globalbanlist.cpp
// *********************
//
// Copyright (c) 2019 Sharon W (sharon at aegis dot gg)
//
// Distributed under the MIT License. (See accompanying file LICENSE)
// 

#include "mod_globalbanlist.h"

std::string mod_globalbanlist::r_prefix = "globalbanlist";

mod_globalbanlist::mod_globalbanlist(Guild & data, asio::io_context & _io)
    : Module(data, _io)
{
}

mod_globalbanlist::~mod_globalbanlist()
{
}

std::vector<std::string> mod_globalbanlist::get_db_entries()
{
    return std::vector<std::string>();
}

void mod_globalbanlist::do_stop()
{

}

bool mod_globalbanlist::check_command(std::string cmd, shared_data & sd)
{
    auto it = commands.find(cmd);
    if (it == commands.end())
        return false;

    return it->second(sd);
}

void mod_globalbanlist::remove()
{
    delete this;
}

void mod_globalbanlist::load(AegisBot & bot)
{

}
