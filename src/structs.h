#pragma once

//#include <aegis/gateway/objects/permission_overwrite.hpp>
//#include <set>
#include <aegis/snowflake.hpp>
//#include <aegis/guild.hpp>
//#include <spdlog/fmt/fmt.h>
//#include <asio/steady_timer.hpp>

// using aegis::snowflake;
// using aegis::member;
// using aegis::channel;
// using aegis::guild;


struct s_tag_data
{
    enum tag_type
    {
        Text,
        Alias
    };

    int64_t use_count = 0;
    aegis::snowflake owner;
    aegis::snowflake owning_server;
    int64_t creation;
};
