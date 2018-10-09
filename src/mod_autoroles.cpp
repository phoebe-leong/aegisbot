//
// mod_autoroles.cpp
// *****************
//
// Copyright (c) 2018 Sharon W (sharon at aegis dot gg)
//
// Distributed under the MIT License. (See accompanying file LICENSE)
// 

#include "mod_autoroles.h"

std::string mod_autoroles::r_prefix = "autoroles";

mod_autoroles::mod_autoroles(Guild & data, asio::io_context & _io)
    : Module(data, _io)
{
}

mod_autoroles::~mod_autoroles()
{
}

std::vector<std::string> mod_autoroles::get_db_entries()
{
    return std::vector<std::string>();
}

void mod_autoroles::do_stop()
{

}

bool mod_autoroles::check_command(std::string cmd, shared_data & sd)
{
    auto it = commands.find(cmd);
    if (it == commands.end())
        return false;

    return it->second(sd);
}

void mod_autoroles::remove()
{
    delete this;
}

void mod_autoroles::load(AegisBot & bot)
{

}
