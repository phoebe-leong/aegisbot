//
// mod_music.cpp
// *************
//
// Copyright (c) 2018 Sharon W (sharon at aegis dot gg)
//
// Distributed under the MIT License. (See accompanying file LICENSE)
// 

#include "mod_music.h"

std::string mod_music::r_prefix = "music";

mod_music::mod_music(Guild & data, asio::io_context & _io)
    : Module(data, _io)
{
}

mod_music::~mod_music()
{
}

std::vector<std::string> mod_music::get_db_entries()
{
    return std::vector<std::string>();
}

void mod_music::do_stop()
{

}

bool mod_music::check_command(std::string cmd, shared_data & sd)
{
    auto it = commands.find(cmd);
    if (it == commands.end())
        return false;

    return it->second(sd);
}

void mod_music::remove()
{
    delete this;
}

void mod_music::load(AegisBot & bot)
{

}
