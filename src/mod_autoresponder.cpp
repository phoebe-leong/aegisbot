//
// mod_autoresponder.cpp
// *********************
//
// Copyright (c) 2019 Sharon W (sharon at aegis dot gg)
//
// Distributed under the MIT License. (See accompanying file LICENSE)
// 

#include "mod_autoresponder.h"

std::string mod_autoresponder::r_prefix = "autoresponder";

mod_autoresponder::mod_autoresponder(Guild & data, asio::io_context & _io)
    : Module(data, _io)
{
}

mod_autoresponder::~mod_autoresponder()
{
}

std::vector<std::string> mod_autoresponder::get_db_entries()
{
    return std::vector<std::string>();
}

void mod_autoresponder::do_stop()
{

}

bool mod_autoresponder::check_command(std::string cmd, shared_data & sd)
{
    auto it = commands.find(cmd);
    if (it == commands.end())
        return false;

    return it->second(sd);
}

void mod_autoresponder::remove()
{
    delete this;
}

void mod_autoresponder::load(AegisBot & bot)
{

}
