//
// mod_log.cpp
// ***********
//
// Copyright (c) 2018 Sharon W (sharon at aegis dot gg)
//
// Distributed under the MIT License. (See accompanying file LICENSE)
// 

#include "mod_log.h"

std::string mod_log::r_prefix = "log";

mod_log::mod_log(Guild & data, asio::io_context & _io)
    : Module(data, _io)
{
}

mod_log::~mod_log()
{
}

std::vector<std::string> mod_log::get_db_entries()
{
    return std::vector<std::string>();
}

void mod_log::do_stop()
{

}

bool mod_log::check_command(std::string cmd, shared_data & sd)
{
    auto it = commands.find(cmd);
    if (it == commands.end())
        return false;

    return it->second(sd);
}

void mod_log::remove()
{
    delete this;
}

void mod_log::load(AegisBot & bot)
{

}
