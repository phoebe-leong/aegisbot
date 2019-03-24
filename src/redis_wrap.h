//
// redis_wrap.h
// ************
//
// Copyright (c) 2019 Sharon W (sharon at aegis dot gg)
//
// Distributed under the MIT License. (See accompanying file LICENSE)
// 

#include <redisclient/redisasyncclient.h>
#include <asio/io_context.hpp>
#include <asio/steady_timer.hpp>
#include <asio/ip/address.hpp>

class redis_subscriber
{
public:
    redis_subscriber(asio::io_context & _io)
        : _io_service(_io)
        , subscriber(_io)
        , connectSubscriberTimer(_io)
    {
    };
    ~redis_subscriber() = default;

    asio::io_context & _io_service;
    redisclient::RedisAsyncClient subscriber;
    asio::steady_timer connectSubscriberTimer;
    std::string channel_name;
    asio::ip::address ip_address;
    uint16_t port;
    std::function<void(const std::vector<char> &)> fn;

    void subscribe(asio::ip::address ip_address, uint16_t port, const std::string & channel_name, std::function<void(const std::vector<char> &)> fn)
    {
        this->channel_name = channel_name;
        this->ip_address = ip_address;
        this->port = port;
        this->fn = fn;
        subscriber.installErrorHandler(std::bind(&redis_subscriber::connectSubscriber, this));
        connectSubscriber();
    }

    void callLater(asio::steady_timer &timer,
                   void(redis_subscriber::*callback)())
    {
        timer.expires_after(std::chrono::seconds(1));
        timer.async_wait([callback, this](const asio::error_code &ec)
        {
            if (!ec)
            {
                (this->*callback)();
            }
        });
    }

    void connectSubscriber()
    {
        if (subscriber.state() == redisclient::RedisAsyncClient::State::Connected ||
            subscriber.state() == redisclient::RedisAsyncClient::State::Subscribed)
        {
            subscriber.disconnect();
        }

        asio::ip::tcp::endpoint endpoint(ip_address, port);
        subscriber.connect(endpoint, std::bind(&redis_subscriber::onSubscriberConnected, this, std::placeholders::_1, std::placeholders::_2));
    }

    void onSubscriberConnected(bool res, const std::string & msg)
    {
        if (!res)
        {
            callLater(connectSubscriberTimer, &redis_subscriber::connectSubscriber);
        }
        else
        {
            //subscriber.subscribe(channel_name, std::bind(&redis_subscriber::onMessage, this, std::placeholders::_1));
            subscriber.subscribe(channel_name, fn);
        }
    }

//     void onMessage(const std::vector<char> &buf)
//     {
//         std::string s(buf.begin(), buf.end());
//         //r_log->info(s.substr(0, s.size() - 2));
//     }
};

class redis_publisher
{
public:
    redis_publisher(asio::io_context & _io)
        : _io_service(_io)
        , subscriber(_io)
        , connectSubscriberTimer(_io)
    {
    };
    ~redis_publisher() = default;

    asio::io_context & _io_service;
    redisclient::RedisAsyncClient subscriber;
    asio::steady_timer connectSubscriberTimer;
    std::string channel_name;
    asio::ip::address ip_address;
    uint16_t port;

    void subscribe(asio::ip::address ip_address, uint16_t port, const std::string & channel_name)
    {
        this->channel_name = channel_name;
        this->ip_address = ip_address;
        this->port = port;
        subscriber.installErrorHandler(std::bind(&redis_publisher::connectSubscriber, this));
        connectSubscriber();
    }

    void callLater(asio::steady_timer &timer,
                   void(redis_publisher::*callback)())
    {
        timer.expires_after(std::chrono::seconds(1));
        timer.async_wait([callback, this](const asio::error_code &ec)
        {
            if (!ec)
            {
                (this->*callback)();
            }
        });
    }

    void connectSubscriber()
    {
        if (subscriber.state() == redisclient::RedisAsyncClient::State::Connected ||
            subscriber.state() == redisclient::RedisAsyncClient::State::Subscribed)
        {
            subscriber.disconnect();
        }

        asio::ip::tcp::endpoint endpoint(ip_address, port);
        subscriber.connect(endpoint, std::bind(&redis_publisher::onSubscriberConnected, this, std::placeholders::_1, std::placeholders::_2));
    }

    void onSubscriberConnected(bool res, const std::string & msg)
    {
        if (!res)
        {
            callLater(connectSubscriberTimer, &redis_publisher::connectSubscriber);
        }
        else
        {
            subscriber.subscribe(channel_name,
                                 std::bind(&redis_publisher::onMessage, this, std::placeholders::_1));
        }
    }

    void onMessage(const std::vector<char> &buf)
    {
        std::string s(buf.begin(), buf.end());
        //r_log->info(s.substr(0, s.size() - 2));
    }
};
