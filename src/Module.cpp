//
// Module.cpp
// **********
//
// Copyright (c) 2018 Sharon W (sharon at aegis dot gg)
//
// Distributed under the MIT License. (See accompanying file LICENSE)
// 

#include "Module.h"
#include <spdlog/fmt/fmt.h>

//TODO:
#include <aegis.hpp>

std::string Module::gen_random(const int len) const noexcept
{
    std::stringstream ss;
    static const char alphanum[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";

    for (int i = 0; i < len; ++i)
        ss << alphanum[rand() % (sizeof(alphanum) - 1)];
    return ss.str();
};

bool Module::check_params(aegis::channel & _channel, std::vector<std::string_view> & toks, size_t req, std::string example)
{
    size_t count = toks.size();
    if (count < req)
    {
        if (example.empty())
            _channel.create_message(fmt::format("Not enough parameters. Need {} given {}", req, count));
        else
            _channel.create_message(fmt::format("Not enough parameters. Need {} given {} - example: {}", req, count, example));
        return true;
    }
    return false;
};
