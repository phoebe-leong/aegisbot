//
// mod_announcer.cpp
// *****************
//
// Copyright (c) 2019 Sharon W (sharon at aegis dot gg)
//
// Distributed under the MIT License. (See accompanying file LICENSE)
// 

#include "mod_announcer.h"

std::string mod_announcer::r_prefix = "announcer";

mod_announcer::mod_announcer(Guild & data, asio::io_context & _io)
    : Module(data, _io)
{
}

mod_announcer::~mod_announcer()
{
}

std::vector<std::string> mod_announcer::get_db_entries()
{
    return std::vector<std::string>();
}

void mod_announcer::do_stop()
{

}

bool mod_announcer::check_command(std::string cmd, shared_data & sd)
{
    auto it = commands.find(cmd);
    if (it == commands.end())
        return false;

    return it->second(sd);
}

void mod_announcer::remove()
{
    delete this;
}

void mod_announcer::load(AegisBot & bot)
{

}
