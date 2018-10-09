//
// mod_help.cpp
// ************
//
// Copyright (c) 2018 Sharon W (sharon at aegis dot gg)
//
// Distributed under the MIT License. (See accompanying file LICENSE)
// 

#include "mod_help.h"

std::string mod_help::r_prefix = "help";

mod_help::mod_help(Guild & data, asio::io_context & _io)
    : Module(data, _io)
{
}

mod_help::~mod_help()
{
}

std::vector<std::string> mod_help::get_db_entries()
{
    return std::vector<std::string>();
}

void mod_help::do_stop()
{

}

bool mod_help::check_command(std::string cmd, shared_data & sd)
{
    auto it = commands.find(cmd);
    if (it == commands.end())
        return false;

    return it->second(sd);
}

void mod_help::remove()
{
    delete this;
}

void mod_help::load(AegisBot & bot)
{

}
