//
// Guild.cpp
// *********
//
// Copyright (c) 2018 Sharon W (sharon at aegis dot gg)
//
// Distributed under the MIT License. (See accompanying file LICENSE)
// 

#include "Guild.h"

// Guild::Guild()
// {
// }
// 
// Guild::~Guild()
// {
// }

bool Guild::is_channel_ignored(aegis::snowflake channel_id)
{
    auto it = std::find(ignored_channels.begin(), ignored_channels.end(), channel_id);
    if (it == ignored_channels.end())
        return false;
    return *it;
}
