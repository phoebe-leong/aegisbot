//
// mod_tag.cpp
// ***********
//
// Copyright (c) 2019 Sharon W (sharon at aegis dot gg)
//
// Distributed under the MIT License. (See accompanying file LICENSE)
// 

#include "mod_moderation.h"

std::string mod_moderation::r_prefix = "moderation";

mod_moderation::mod_moderation(Guild & data, asio::io_context & _io)
    : Module(data, _io)
{
}

mod_moderation::~mod_moderation()
{
}

std::vector<std::string> mod_moderation::get_db_entries()
{
    return std::vector<std::string>();
}

void mod_moderation::do_stop()
{

}

bool mod_moderation::check_command(std::string cmd, shared_data & sd)
{
    auto it = commands.find(cmd);
    if (it == commands.end())
        return false;

    return it->second(sd);
}

void mod_moderation::remove()
{
    delete this;
}

void mod_moderation::load(AegisBot & bot)
{

}
