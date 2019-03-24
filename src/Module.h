//
// Module.h
// ********
//
// Copyright (c) 2019 Sharon W (sharon at aegis dot gg)
//
// Distributed under the MIT License. (See accompanying file LICENSE)
// 

#pragma once

//#include <aegis/fwd.hpp>
#include <aegis/snowflake.hpp>
//#include <asio/io_context.hpp>
#include <string>
#include <cctype>
#include <vector>
#include <unordered_map>
#include <functional>
#include <algorithm>

namespace asio
{
class io_context;
}
class Guild;
class AegisBot;
struct shared_data;

struct s_module_data
{
    std::string name;//module name
    std::string prefix;//db prefix for this mod
    bool enabled = false;//is mod enabled globally
};

class Module
{
public:

    Module(Guild & data, asio::io_context & _io)
        : g_data(data)
        , _io_service(_io)
    {

    }

    virtual ~Module() = default;

    virtual std::vector<std::string> get_db_entries() = 0;
    
    virtual void do_stop() = 0;

    virtual bool check_command(std::string, shared_data&) = 0;

    virtual void remove() = 0;

    virtual void load(AegisBot&) = 0;

    virtual void member_leave() {};

    // return true if params are not enough
    bool check_params(aegis::channel & _channel, std::vector<std::string_view> & toks, size_t req, std::string example = "");
    std::string gen_random(const int len) const noexcept;

    std::string to_lower(const std::string & str)
    {
        std::string lower(str);
        std::transform(lower.begin(), lower.end(), lower.begin(), [](const unsigned char c) { return std::tolower(c); });
        return lower;
    }

    std::string to_lower(std::string_view str)
    {
        return to_lower(std::string(str));
    }

    Guild & g_data;

    asio::io_context & _io_service;

    std::unordered_map<std::string, std::function<bool(shared_data&)>> commands;

    bool enabled = false;//is mod enabled by server
    bool admin_enabled = false;//if true, admin enabled this mod
    bool admin_override = false;//disables changes to enabled by servers
    bool default_enabled = false;//is mod automatically enabled

    std::function<bool(shared_data&)> message_process;
};
