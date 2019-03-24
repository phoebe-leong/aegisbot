//
// main.cpp
// ********
//
// Copyright (c) 2019 Sharon W (sharon at aegis dot gg)
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
    //https://discordapp.com/channels/81384788765712384/381870553235193857/381870565658460170
    //std::cin.ignore();
    std::shared_ptr<asio::io_context> _io = std::make_shared<asio::io_context>();
    //aegis::internal::wrk = std::make_unique<aegis::asio_exec>(asio::make_work_guard(*_io));
    //aegis::core bot(_io);
    {
        AegisBot commands(*_io);
        {
            aegis::core bot(_io, spdlog::level::info);

            using asio_exec = asio::executor_work_guard<asio::io_context::executor_type>;
            std::unique_ptr<asio_exec> wrk = std::make_unique<asio_exec>(asio::make_work_guard(*_io));
            for (std::size_t i = 0; i < std::thread::hardware_concurrency(); ++i)
                bot.add_run_thread();

            commands.set_bot(bot);
#if !defined(WIN32)
            bot.log->info("\n\n\n\
             █████╗ ███████╗ ██████╗ ██╗███████╗██████╗  ██████╗ ████████╗\n\
            ██╔══██╗██╔════╝██╔════╝ ██║██╔════╝██╔══██╗██╔═══██╗╚══██╔══╝\n\
            ███████║█████╗  ██║  ███╗██║███████╗██████╔╝██║   ██║   ██║   \n\
            ██╔══██║██╔══╝  ██║   ██║██║╚════██║██╔══██╗██║   ██║   ██║   \n\
            ██║  ██║███████╗╚██████╔╝██║███████║██████╔╝╚██████╔╝   ██║   \n\
            ╚═╝  ╚═╝╚══════╝ ╚═════╝ ╚═╝╚══════╝╚═════╝  ╚═════╝    ╚═╝   \n\n\n\
            ");
#endif
            bot.log->info("Created lib bot object");
            bot.log->info("Starting bot main loop");
            bot.log->info("Setting up AegisBot");
            commands.inject();
            commands.init();
            commands.start_timers();
            bot.run();
            std::cin.ignore();
            bot.shutdown();
            //bot.yield();
            bot.log->info("Bot exiting");
            bot.log->info("Press any key to continue...");
        }
    }
    //_io.reset();
    std::cin.ignore();
    return 0;
}


