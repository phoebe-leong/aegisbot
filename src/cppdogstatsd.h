//
// cppdogstatsd.h
// **************
//
// Copyright (c) 2018 Sharon W (sharon at aegis dot gg)
//
// Distributed under the MIT License. (See accompanying file LICENSE)
// 



#pragma once

#include <chrono>
#include <string>
#include <asio/io_context.hpp>
#include <asio/basic_datagram_socket.hpp>
#include <asio/ip/udp.hpp>
#include <mutex>


namespace cppdogstatsd
{
using namespace std::chrono_literals;
using namespace std::literals;

using asio::ip::udp;

enum MessageType
{
    Count,
    Gauge,
    Timer,
    Set
};

class Dogstatsd
{
public:
    Dogstatsd(asio::io_context & _io, std::string appName = "", std::string host = "127.0.0.1", std::string port = "8126")
        : _io_service(_io)
        , _app_name(appName)
        , _receiver_endpoint(asio::ip::make_address(host), std::stoi(port))
        , _socket(_io_service, udp::endpoint(udp::v4(), 0))
    {
    }

    Dogstatsd(const Dogstatsd &&) = delete;

    ~Dogstatsd()
    {

    }

    void setApp(std::string appName) { _app_name = appName; }

    template<MessageType E = Count, typename T>
    void Metric(std::string name, T value, double sample_rate = 0.0, std::vector<std::string> keys = {})
    {
#if defined(_DEBUG) && defined(_WIN32)
        return;
#endif
        std::lock_guard<std::mutex> l(m);
        std::stringstream ss;
        ss << _app_name << '.';
        ss << name << ':';
        ss << value << '|';
        if constexpr (E == Count)
            ss << "c|";
        else if constexpr (E == Gauge)
            ss << "g|";
        else if constexpr (E == Timer)
            ss << "ms|";
        else if constexpr (E == Set)
            ss << "s|";
        if (sample_rate != 0.0)
            ss << '@' << sample_rate << '|';
        if (keys.size() != 0)
        {
            ss << "#";
            for (auto & k : keys)
                ss << k << ',';
        }
        std::string s = ss.str();
        s = s.erase(s.size() - 1);
        _socket.async_send_to(asio::buffer(s), _receiver_endpoint,
                              [](const asio::error_code & error, std::size_t bytes_transferred)
        {
            if (error)
                std::cout << "error with dogstatsd\n";
        });
    }

    //template<MessageType E, typename T>
    //void Metric(std::string name, T value, double sample_rate = 0.0, std::vector<std::string> keys = {});

private:
    asio::io_context & _io_service;
    std::string _app_name;
    asio::ip::udp::endpoint _receiver_endpoint;
    udp::socket _socket;
    std::mutex m;
};

}