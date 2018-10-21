//
// main.cpp
// ********
//
// Copyright (c) 2018 Sharon W (sharon at aegis dot gg)
//
// Distributed under the MIT License. (See accompanying file LICENSE)
// 


#include <aegis.hpp>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include "AegisBot.h"
#include "token.h"

int main(int argc, char * argv[])
{
    aegis::core bot(spdlog::level::level_enum::trace);
    bot.log->info("Created lib bot object");
    std::error_code ec;
    AegisBot commands(bot.get_io_context(), bot);
    bot.log->info("Starting bot main loop");
    bot.log->info("Setting up AegisBot");
    bot.run();
    commands.inject();
    commands.init();
    bot.yield();
    bot.log->info("Bot exiting");
    bot.log->info("Press any key to continue...");
    std::cin.ignore();
    return 0;
}


