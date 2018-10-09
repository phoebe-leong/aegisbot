//
// mod_auction.cpp
// ***************
//
// Copyright (c) 2018 Sharon W (sharon at aegis dot gg)
//
// Distributed under the MIT License. (See accompanying file LICENSE)
// 

#include "mod_auction.h"
#include "AegisBot.h"//TODO: this is only used for redis actions. make a redis class?
#include "Guild.h"
#include <aegis/channel.hpp>
#include <nlohmann/json.hpp>

using aegis::channel;

std::string mod_auction::r_prefix = "auction";

mod_auction::mod_auction(Guild & data, asio::io_context & _io)
    : Module(data, _io)
    , timer(_io)
{
    commands["reset"] = std::bind(&mod_auction::reset, this, std::placeholders::_1);
    commands["teamlist"] = std::bind(&mod_auction::teamlist, this, std::placeholders::_1);
    commands["register"] = std::bind(&mod_auction::_register, this, std::placeholders::_1);
    commands["state"] = std::bind(&mod_auction::state, this, std::placeholders::_1);
    commands["set"] = std::bind(&mod_auction::set, this, std::placeholders::_1);
    commands["start"] = std::bind(&mod_auction::start, this, std::placeholders::_1);
    commands["playerlist"] = std::bind(&mod_auction::playerlist, this, std::placeholders::_1);
    commands["addplayers"] = std::bind(&mod_auction::addplayers, this, std::placeholders::_1);
    commands["nom"] = std::bind(&mod_auction::nom, this, std::placeholders::_1);
    commands["pause"] = std::bind(&mod_auction::pause, this, std::placeholders::_1);
    commands["resume"] = std::bind(&mod_auction::resume, this, std::placeholders::_1);
    commands["bid"] = std::bind(&mod_auction::bid, this, std::placeholders::_1);
    commands["end"] = std::bind(&mod_auction::end, this, std::placeholders::_1);
    commands["setname"] = std::bind(&mod_auction::setname, this, std::placeholders::_1);
    commands["standings"] = std::bind(&mod_auction::standings, this, std::placeholders::_1);
    commands["retain"] = std::bind(&mod_auction::retain, this, std::placeholders::_1);
    commands["skip"] = std::bind(&mod_auction::skip, this, std::placeholders::_1);
    commands["setfunds"] = std::bind(&mod_auction::setfunds, this, std::placeholders::_1);
    commands["undobid"] = std::bind(&mod_auction::undobid, this, std::placeholders::_1);
    commands["auctionhelp"] = std::bind(&mod_auction::auctionhelp, this, std::placeholders::_1);
    commands["undobuy"] = std::bind(&mod_auction::undobuy, this, std::placeholders::_1);
    commands["withdraw"] = std::bind(&mod_auction::withdraw, this, std::placeholders::_1);
    commands["addfunds"] = std::bind(&mod_auction::addfunds, this, std::placeholders::_1);
    commands["removefunds"] = std::bind(&mod_auction::removefunds, this, std::placeholders::_1);
    commands["adminsetname"] = std::bind(&mod_auction::adminsetname, this, std::placeholders::_1);
    commands["addbidder"] = std::bind(&mod_auction::addbidder, this, std::placeholders::_1);
    commands["delbidder"] = std::bind(&mod_auction::delbidder, this, std::placeholders::_1);
}

mod_auction::~mod_auction()
{
}

std::vector<std::string> mod_auction::get_db_entries()
{
    return std::vector<std::string>();
}

void mod_auction::do_stop()
{
    timer.cancel();
}

void mod_auction::remove()
{
    delete this;
}

void mod_auction::load(AegisBot & bot)
{
    try
    {
        auto & mod_data = g_data._modules[modules::Auction];

        if (!mod_data->enabled && !mod_data->admin_override)
            return;//mod not enabled or overridden

        if (std::string t = bot.hget("auction", { "hostrole" }, g_data); !t.empty())
            hostrole = std::stoll(t);
        if (std::string t = bot.hget("auction", { "managerrole" }, g_data); !t.empty())
            managerrole = std::stoll(t);
        if (std::string t = bot.hget("auction", { "bidtime" }, g_data); !t.empty())
            bidtime = std::stoll(t);
        if (std::string t = bot.hget("auction", { "defaultfunds" }, g_data); !t.empty())
            defaultfunds = std::stoll(t);

    }
    catch (std::exception & e)
    {
        bot.bot.log->error("Exception loading auction module data for guild [{}] e[{}]", g_data.guild_id, e.what());
    }
}

const mod_auction::Team & mod_auction::getteam(const snowflake id) const
{
    for (auto & t : teams)
    {
        if (t.owner_id == id)
            return t;
        for (auto & b : t.bidders)
            if (b == id)
                return t;
    }
    throw std::runtime_error(fmt::format("Team with ownerid [{}] does not exist", id));
}

void mod_auction::timercontinuation(const asio::error_code & ec, channel & _channel)
{
    if (ec != asio::error::operation_aborted)
    {
        if (auctioninprogress)
        {
            if (!paused)
                _channel.create_message(fmt::format("Auction for player **{}** currently at [{}] by **{}**", currentnom, bids.back().second, teams[bids.back().first].teamname));
            else
            {
                timer.expires_after(std::chrono::milliseconds(4500));
                timer.async_wait(std::bind(&mod_auction::timercontinuation, this, std::placeholders::_1, std::ref(_channel)));
                return;
            }
            if (timeuntilstop <= std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count())
            {
                auto res = bids.back();
                //award player 

                teams[res.first].funds -= res.second;
                teams[res.first].players.emplace_back(currentnom, res.second);
                std::string won_player = currentnom;
                currentnom = "";

                undo_buy.player_bought = won_player;
                undo_buy.direction = direction;
                undo_buy.lastteam = lastteam;
                undo_buy.currentteam = currentteam;

                {
                    int withdrawn = 0;
                    for (auto & t : teams)
                    {
                        if (t.withdrawn)
                            withdrawn++;
                    }
                    if (teams.size() == withdrawn)
                    {
                        //Auction over
                        json jteams;
                        for (auto & t : teams)
                        {
                            std::stringstream players;
                            for (auto & p : t.players)
                                players << p.first << " (" << p.second << ")\n";
                            jteams.emplace_back(json({ { "name", fmt::format("{} ({})", t.teamname, t.funds) },{ "value", players.str().empty() ? "No players won yet" : players.str() },{ "inline", false } }));
                        }
                        if (jteams.empty())
                        {
                            jteams.emplace_back(json({ { "name", "Empty" },{ "value", "Empty" } }));
                        }
                        json t = {
                            { "title", "Current Standings" },
                        { "color", 12330144 },
                        { "fields", jteams },
                        { "footer",{ { "icon_url", "https://cdn.discordapp.com/emojis/289276304564420608.png" },{ "text", "Auction bot" } } }
                        };
                        _channel.create_message_embed("Auction has ended. Current standings:\n\n", t);
                        auctioninprogress = false;
                        return;
                    }
                }


                do
                {
                    lastteam = currentteam;

                    currentteam += direction;

                    if (currentteam > teams.size() - 1)
                        currentteam = static_cast<int32_t>(teams.size() - 1);
                    if (currentteam < 0)
                        currentteam = 0;

                    if (currentteam == lastteam)
                    {
                        if (direction == -1) direction = 1;
                        else if (direction == 1) direction = -1;
                    }

                    if (!teams[currentteam].withdrawn)
                        break;
                } while (true);


                json jteams;
                for (auto & t : teams)
                {
                    std::stringstream players;
                    for (auto & p : t.players)
                        players << p.first << " (" << p.second << ")\n";
                    jteams.push_back(json({ { "name", fmt::format("[{}] {} ({})", t.id, t.teamname, t.funds) },{ "value", players.str().empty() ? "No players purchased yet" : players.str() },{ "inline", false } }));
                }
                json t = {
                    { "title", "Current Standings" },
                { "color", 10599460 },
                { "fields", jteams },
                { "footer",{ { "icon_url", "https://cdn.discordapp.com/emojis/289276304564420608.png" },{ "text", "Auction bot" } } }
                };
                std::string prefix;
                if (!g_data.cmd_prefix_list.empty())
                    prefix = g_data.cmd_prefix_list[0];
                _channel.create_message_embed(fmt::format("Auction of player **{}** completed for [{}] awarded to **{}**\n<@{}> (**{}**) type `{}nom player name` to nominate another player.", won_player, res.second, teams[bids.back().first].teamname, teams[currentteam].owner_id, teams[currentteam].teamname, prefix), t);

                players[won_player] = false;


                //check for withdrawn
                for (auto & t : teams)
                {
                    if (!t.withdrawn)
                    {
                        if (t.funds < 3000)
                        {
                            //auto withdraw
                            _channel.create_message(fmt::format("[<@{}>] Your team **{}** has automatically withdrawn from the auction due to lack of funds.", t.owner_id, t.teamname));
                            t.withdrawn = true;
                        }
                    }
                }

                return;
            }
            timer.expires_after(std::chrono::milliseconds(4500));
            timer.async_wait(std::bind(&mod_auction::timercontinuation, this, std::placeholders::_1, std::ref(_channel)));
        }
    }
}

bool mod_auction::check_command(std::string cmd, shared_data & sd)
{
    auto it = commands.find(cmd);
    if (it == commands.end())
        return false;

    if (paused && !sd._guild.member_has_role(sd.member_id, hostrole))
        return false;

    return it->second(sd);
}

bool mod_auction::reset(shared_data & sd)
{
    const snowflake & channel_id = sd.channel_id;
    const snowflake & guild_id = sd.guild_id;
    const snowflake & message_id = sd.message_id;
    const snowflake & member_id = sd.member_id;
    const snowflake & guild_owner_id = sd.guild_owner_id;

    std::string_view username = sd.username;

    member & _member = sd._member;
    channel & _channel = sd._channel;
    guild & _guild = sd._guild;
    std::string_view content = sd.content;

    Guild & g_data = sd.g_data;

    std::vector<std::string_view> & toks = sd.toks;

    bool is_guild_owner = (member_id == guild_owner_id || member_id == sd.ab.bot_owner_id);

    std::string request{ content };

    teams.clear();
    currentnom = "";
    currentteam = 0;
    players.clear();
    bids.clear();
    auctioninprogress = false;
    lastteam = 0;
    paused = false;

    timer.cancel();

    _channel.create_message("Auction reset.");
    return true;
}

bool mod_auction::teamlist(shared_data & sd)
{
    const snowflake & channel_id = sd.channel_id;
    const snowflake & guild_id = sd.guild_id;
    const snowflake & message_id = sd.message_id;
    const snowflake & member_id = sd.member_id;
    const snowflake & guild_owner_id = sd.guild_owner_id;

    std::string_view username = sd.username;

    member & _member = sd._member;
    channel & _channel = sd._channel;
    guild & _guild = sd._guild;
    std::string_view content = sd.content;

    Guild & g_data = sd.g_data;

    std::vector<std::string_view> & toks = sd.toks;

    bool is_guild_owner = (member_id == guild_owner_id || member_id == sd.ab.bot_owner_id);

    std::string request{ content };

    //////////////////////////////////////////////////////////////////////////

    int i = 0;
    std::stringstream ss;
    for (auto & t : teams)
    {
        ss << "Team[" << i++ << "]: " << t.teamname << '\n';
    }
    _channel.create_message(ss.str());
    return true;
}

bool mod_auction::_register(shared_data & sd)
{
    const snowflake & channel_id = sd.channel_id;
    const snowflake & guild_id = sd.guild_id;
    const snowflake & message_id = sd.message_id;
    const snowflake & member_id = sd.member_id;
    const snowflake & guild_owner_id = sd.guild_owner_id;

    std::string_view username = sd.username;

    member & _member = sd._member;
    channel & _channel = sd._channel;
    guild & _guild = sd._guild;
    std::string_view content = sd.content;

    Guild & g_data = sd.g_data;

    std::vector<std::string_view> & toks = sd.toks;

    bool is_guild_owner = (member_id == guild_owner_id || member_id == sd.ab.bot_owner_id);

    std::string request{ content };

    //////////////////////////////////////////////////////////////////////////

    if (!_guild.member_has_role(member_id, managerrole))
        return true;

    if (auctioninprogress)
    {
        _channel.create_message(fmt::format("[<@{}>] Auction in progress.", _member.get_id()));
        return true;
    }

    if (check_params(_channel, toks, 2))
        return true;

    std::string_view name = toks[1].data();

    for (auto & t : teams)
    {
        if (t.owner_id == _member.get_id())
        {
            _channel.create_message(fmt::format("[{}] Command failed. You are already registered", _member.get_username()));
            return true;
        }
    }
    mod_auction::Team t;
    t.funds = defaultfunds;
    t.owner = _member.get_username();
    t.owner_id = _member.get_id();
    t.id = static_cast<int32_t>(teams.size());
    t.teamname = name;
    _channel.create_message(fmt::format("[<@{}>] Registered for auction successfully. Team name **{}**", _member.get_id(), name));
    t.bidders.push_back(_member.get_id());
    teams.push_back(t);
    return true;
}

bool mod_auction::state(shared_data & sd)
{
    const snowflake & channel_id = sd.channel_id;
    const snowflake & guild_id = sd.guild_id;
    const snowflake & message_id = sd.message_id;
    const snowflake & member_id = sd.member_id;
    const snowflake & guild_owner_id = sd.guild_owner_id;

    std::string_view username = sd.username;

    member & _member = sd._member;
    channel & _channel = sd._channel;
    guild & _guild = sd._guild;
    std::string_view content = sd.content;

    Guild & g_data = sd.g_data;

    std::vector<std::string_view> & toks = sd.toks;

    bool is_guild_owner = (member_id == guild_owner_id || member_id == sd.ab.bot_owner_id);

    std::string request{ content };

    //////////////////////////////////////////////////////////////////////////

    if (!auctioninprogress)
    {
        _channel.create_message(fmt::format("[<@{}>] No auction.", _member.get_id()));
        return true;
    }

    json jteams;
    for (auto & t : teams)
    {
        std::stringstream players;
        for (auto & p : t.players)
            players << p.first << " (" << p.second << ")\n";
        jteams.push_back(json({ { "name", fmt::format("[{}] {} ({})", t.id, t.teamname, t.funds) },{ "value", players.str().empty() ? "No players purchased yet" : players.str() },{ "inline", false } }));
    }
    json t = {
        { "title", "Current Standings" },
        { "color", 10599460 },
        { "fields", jteams },
        { "footer",{ { "icon_url", "https://cdn.discordapp.com/emojis/289276304564420608.png" },{ "text", "Auction bot" } } }
    };
    std::string prefix;
    if (!g_data.cmd_prefix_list.empty())
        prefix = g_data.cmd_prefix_list[0];
    _channel.create_message_embed(fmt::format("<@{}> (**{}**) type `{}nom playername` to nominate another player.", teams[currentteam].owner_id, teams[currentteam].teamname, prefix), t);
    return true;
}

bool mod_auction::set(shared_data & sd)
{
    const snowflake & channel_id = sd.channel_id;
    const snowflake & guild_id = sd.guild_id;
    const snowflake & message_id = sd.message_id;
    const snowflake & member_id = sd.member_id;
    const snowflake & guild_owner_id = sd.guild_owner_id;

    std::string_view username = sd.username;

    member & _member = sd._member;
    channel & _channel = sd._channel;
    guild & _guild = sd._guild;
    std::string_view content = sd.content;

    Guild & g_data = sd.g_data;

    std::vector<std::string_view> & toks = sd.toks;

    bool is_guild_owner = (member_id == guild_owner_id || member_id == sd.ab.bot_owner_id);

    std::string request{ content };

    //////////////////////////////////////////////////////////////////////////

    if (!_guild.member_has_role(member_id, hostrole) && !is_guild_owner)
        return true;

    if (check_params(_channel, toks, 4, "set `option` value"))
        return true;

    if (toks[1] == "defaultfunds")
    {
        std::string bid{ toks[2] };
        if (bid.empty())
        {
            _channel.create_message(fmt::format("[<@{}>] Invalid command arguments.", _member.get_id()));
            return true;
        }
        int dbid = std::stoul(bid);
        for (auto & t : teams)
            t.funds = dbid;
        _channel.create_message(fmt::format("[<@{}>] Starting funds and all current teams set to [{}]", _member.get_id(), dbid));
        defaultfunds = std::stoul(bid);
        sd.ab.hset("auction", { "defaultfunds", bid }, g_data);
        return true;
    }

    if (toks[1] == "bidtime")
    {
        std::string bid{ toks[2] };
        if (bid.empty())
        {
            _channel.create_message(fmt::format("[<@{}>] Invalid command arguments.", _member.get_id()));
            return true;
        }
        bidtime = std::stoul(bid) * 1000;
        _channel.create_message(fmt::format("[<@{}>] Time between bids set to [{} seconds]", _member.get_id(), bidtime / 1000));
        sd.ab.hset("auction", { "bidtime", std::to_string(bidtime) }, g_data);
        return true;
    }

    if (toks[1] == "managerrole")
    {
        snowflake mgr_role{ sd.ab.get_snowflake(toks[2], _guild) };
        if (mgr_role == 0)
        {
            _channel.create_message(fmt::format("[<@{}>] Invalid command arguments.", _member.get_id()));
            return true;
        }
        managerrole = mgr_role;
        _channel.create_message(fmt::format("[<@{}>] Set manager role to [{}]", _member.get_id(), managerrole));
        sd.ab.hset("auction", { "managerrole", std::to_string(mgr_role) }, g_data);
        return true;
    }

    if (toks[1] == "hostrole")
    {
        snowflake host_role{ sd.ab.get_snowflake(toks[2], _guild) };
        if (host_role == 0)
        {
            _channel.create_message(fmt::format("[<@{}>] Invalid command arguments.", _member.get_id()));
            return true;
        }
        hostrole = host_role;
        _channel.create_message(fmt::format("[<@{}>] Set host role to [{}]", _member.get_id(), hostrole));
        sd.ab.hset("auction", { "hostrole", std::to_string(host_role) }, g_data);
        return true;
    }
    return false;
}

bool mod_auction::start(shared_data & sd)
{
    const snowflake & channel_id = sd.channel_id;
    const snowflake & guild_id = sd.guild_id;
    const snowflake & message_id = sd.message_id;
    const snowflake & member_id = sd.member_id;
    const snowflake & guild_owner_id = sd.guild_owner_id;

    std::string_view username = sd.username;

    member & _member = sd._member;
    channel & _channel = sd._channel;
    guild & _guild = sd._guild;
    std::string_view content = sd.content;

    Guild & g_data = sd.g_data;

    std::vector<std::string_view> & toks = sd.toks;

    bool is_guild_owner = (member_id == guild_owner_id || member_id == sd.ab.bot_owner_id);

    std::string request{ content };

    //////////////////////////////////////////////////////////////////////////

    if (!_guild.member_has_role(member_id, hostrole))
        return true;

    if (teams.empty())
    {
        _channel.create_message("Team list empty");
        return true;
    }
    currentteam = 0;
    std::string prefix;
    if (!g_data.cmd_prefix_list.empty())
        prefix = g_data.cmd_prefix_list[0];
    _channel.create_message(fmt::format("Auction has begun. First team to nominate **{}** type `{}nom player name`", teams[currentteam].teamname, prefix));
    auctioninprogress = true;
    return true;
}

bool mod_auction::playerlist(shared_data & sd)
{
    const snowflake & channel_id = sd.channel_id;
    const snowflake & guild_id = sd.guild_id;
    const snowflake & message_id = sd.message_id;
    const snowflake & member_id = sd.member_id;
    const snowflake & guild_owner_id = sd.guild_owner_id;

    std::string_view username = sd.username;

    member & _member = sd._member;
    channel & _channel = sd._channel;
    guild & _guild = sd._guild;
    std::string_view content = sd.content;

    Guild & g_data = sd.g_data;

    std::vector<std::string_view> & toks = sd.toks;

    bool is_guild_owner = (member_id == guild_owner_id || member_id == sd.ab.bot_owner_id);

    std::string request{ content };

    //////////////////////////////////////////////////////////////////////////

    std::vector<std::stringstream> outputs;
    for (auto & t : { 1,2,3,4,5,6 })
    {
        outputs.emplace_back();
    }

    json jteams;

    if (players.empty())
    {
        _channel.create_message("Player list empty.");
        return true;
    }

    int j = 0;
    for (auto & p : players)
    {
        if (p.second)//available 
            outputs[j++ % 6] << p.first << "\n";
    }
    for (auto & output : outputs)
    {
        jteams.emplace_back(json({ { "name", "Players" },{ "value", (!output.str().empty() ? output.str() : "_") },{ "inline", true } }));
    }
    json t = {
        { "title", "Current Players" },
    { "color", 10599460 },
    { "fields", jteams },
    { "footer",{ { "icon_url", "https://cdn.discordapp.com/emojis/289276304564420608.png" },{ "text", "Auction bot" } } }
    };
    auto apireply = _channel.create_message_embed("", t).get();
    sd.ab.bot.log->info("API REPLY {} : {} : {} : {}", apireply.reply_code, apireply.remaining, apireply.reset, apireply.retry);
    if (!apireply)
    {
        //success
        sd.ab.bot.log->info("SUCCESS");
    }
    return true;
}

bool mod_auction::addplayers(shared_data & sd)
{
    const snowflake & channel_id = sd.channel_id;
    const snowflake & guild_id = sd.guild_id;
    const snowflake & message_id = sd.message_id;
    const snowflake & member_id = sd.member_id;
    const snowflake & guild_owner_id = sd.guild_owner_id;

    std::string_view username = sd.username;

    member & _member = sd._member;
    channel & _channel = sd._channel;
    guild & _guild = sd._guild;
    std::string_view content = sd.content;

    Guild & g_data = sd.g_data;

    std::vector<std::string_view> & toks = sd.toks;

    bool is_guild_owner = (member_id == guild_owner_id || member_id == sd.ab.bot_owner_id);

    std::string request{ content };

    //////////////////////////////////////////////////////////////////////////

    if (!_guild.member_has_role(member_id, hostrole))
        return true;

    if (request.empty())
    {
        _channel.create_message("Player list empty.");
        return true;
    }
    std::string_view lst = request;
    lst.remove_prefix(toks[0].size() + 1);
    auto player_toks = sd.ab.lineize(std::string{ lst });

    if (players.size() + player_toks.size() > 300)
    {
        _channel.create_message(fmt::format("Too many entries! {} to be added would make {}", player_toks.size(), player_toks.size() + players.size()));
        return true;
    }

    int cnt = 0;
    for (auto & p : player_toks)
    {
        if (!p.empty())
            if (players.emplace(std::string(p), true).second)
                ++cnt;
    }
    _channel.create_message(fmt::format("Players added: {}", cnt));
    return true;
}

bool mod_auction::nom(shared_data & sd)
{
    const snowflake & channel_id = sd.channel_id;
    const snowflake & guild_id = sd.guild_id;
    const snowflake & message_id = sd.message_id;
    const snowflake & member_id = sd.member_id;
    const snowflake & guild_owner_id = sd.guild_owner_id;

    std::string_view username = sd.username;

    member & _member = sd._member;
    channel & _channel = sd._channel;
    guild & _guild = sd._guild;
    std::string_view content = sd.content;

    Guild & g_data = sd.g_data;

    std::vector<std::string_view> & toks = sd.toks;

    bool is_guild_owner = (member_id == guild_owner_id || member_id == sd.ab.bot_owner_id);

    std::string request{ content };

    //////////////////////////////////////////////////////////////////////////

    if (check_params(_channel, toks, 2))
        return true;

    if (!auctioninprogress)
    {
        _channel.create_message(fmt::format("[<@{}>] No auction.", _member.get_id()));
        return true;
    }

    if (!currentnom.empty())
    {
        _channel.create_message(fmt::format("[<@{}>] Bid already in progress.", _member.get_id()));
        return true;
    }

    bool can_proceed = [&]() -> bool
    {

        if (_member.get_id() == teams[currentteam].owner_id)
            return true;
        else
        {
            for (auto & b : teams[currentteam].bidders)
                if (b == _member.get_id())
                    return true;
        }

        return false;

    }();

    if (!can_proceed)
        return true;

    std::string name = toks[1].data();
    try
    {
        for (auto & p : players)
        {
            if (p.first == name)
            {
                if (!p.second)
                {
                    _channel.create_message(fmt::format("[<@{}>] Player [{}] already bought.", _member.get_id(), name));
                    return true;
                }
                currentnom = p.first;
                bids.clear();
                bids.emplace_back(getteam(_member.get_id()).id, 3000);
                std::string prefix;
                if (!g_data.cmd_prefix_list.empty())
                    prefix = g_data.cmd_prefix_list[0];
                _channel.create_message(fmt::format("Auction started for player **{}** Current bid at [{}] To bid, type `{}bid value` Only increments of 500 allowed.", p.first, bids.back().second, prefix));
                timeuntilstop = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() + bidtime;
                timer.expires_after(std::chrono::milliseconds(4500));
                timer.async_wait(std::bind(&mod_auction::timercontinuation, this, std::placeholders::_1, std::ref(_channel)));
                return true;
            }
        }
        return true;
    }
    catch (...)
    {
        _channel.create_message(fmt::format("[<@{}>] Invalid command arguments.", _member.get_id()));
    }
    return true;
}

bool mod_auction::pause(shared_data & sd)
{
    const snowflake & channel_id = sd.channel_id;
    const snowflake & guild_id = sd.guild_id;
    const snowflake & message_id = sd.message_id;
    const snowflake & member_id = sd.member_id;
    const snowflake & guild_owner_id = sd.guild_owner_id;

    std::string_view username = sd.username;

    member & _member = sd._member;
    channel & _channel = sd._channel;
    guild & _guild = sd._guild;
    std::string_view content = sd.content;

    Guild & g_data = sd.g_data;

    std::vector<std::string_view> & toks = sd.toks;

    bool is_guild_owner = (member_id == guild_owner_id || member_id == sd.ab.bot_owner_id);

    std::string request{ content };

    //////////////////////////////////////////////////////////////////////////

    if (!_guild.member_has_role(member_id, hostrole))
        return true;

    if (paused)
    {
        _channel.create_message(fmt::format("[<@{}>] Auction already paused.", _member.get_id()));
        return true;
    }

    if (!auctioninprogress)
    {
        _channel.create_message(fmt::format("[<@{}>] No auction going on.", _member.get_id()));
        return true;
    }
    pausetimeleft = timeuntilstop - std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    _channel.create_message("Auction has been paused.");
    paused = true;
    return true;
}

bool mod_auction::resume(shared_data & sd)
{
    const snowflake & channel_id = sd.channel_id;
    const snowflake & guild_id = sd.guild_id;
    const snowflake & message_id = sd.message_id;
    const snowflake & member_id = sd.member_id;
    const snowflake & guild_owner_id = sd.guild_owner_id;

    std::string_view username = sd.username;

    member & _member = sd._member;
    channel & _channel = sd._channel;
    guild & _guild = sd._guild;
    std::string_view content = sd.content;

    Guild & g_data = sd.g_data;

    std::vector<std::string_view> & toks = sd.toks;

    bool is_guild_owner = (member_id == guild_owner_id || member_id == sd.ab.bot_owner_id);

    std::string request{ content };

    //////////////////////////////////////////////////////////////////////////

    if (!_guild.member_has_role(member_id, hostrole))
        return true;

    if (!paused)
    {
        _channel.create_message(fmt::format("[<@{}>] Auction not paused.", _member.get_id()));
        return true;
    }

    if (!auctioninprogress)
    {
        _channel.create_message(fmt::format("[<@{}>] No auction going on.", _member.get_id()));
        return true;
    }
    timeuntilstop += pausetimeleft;
    _channel.create_message("Auction has been resumed.");
    paused = false;
    return true;
}

bool mod_auction::bid(shared_data & sd)
{
    const snowflake & channel_id = sd.channel_id;
    const snowflake & guild_id = sd.guild_id;
    const snowflake & message_id = sd.message_id;
    const snowflake & member_id = sd.member_id;
    const snowflake & guild_owner_id = sd.guild_owner_id;

    std::string_view username = sd.username;

    member & _member = sd._member;
    channel & _channel = sd._channel;
    guild & _guild = sd._guild;
    std::string_view content = sd.content;

    Guild & g_data = sd.g_data;

    std::vector<std::string_view> & toks = sd.toks;

    bool is_guild_owner = (member_id == guild_owner_id || member_id == sd.ab.bot_owner_id);

    std::string request{ content };

    //////////////////////////////////////////////////////////////////////////

    if (check_params(_channel, toks, 2))
        return true;

    if (!auctioninprogress)
    {
        _channel.create_message(fmt::format("[<@{}>] No auction.", _member.get_id()));
        return true;
    }

    if (currentnom.empty())
    {
        _channel.create_message(fmt::format("[<@{}>] No nomination.", _member.get_id()));
        return true;
    }

    std::tuple<bool, std::optional<std::reference_wrapper<mod_auction::Team>>> can_proceed = [&]() -> std::tuple<bool, std::optional<std::reference_wrapper<mod_auction::Team>>>
    {

        for (auto & t : teams)
            for (auto & b : t.bidders)
                if (b == _member.get_id())
                {
                    if (t.withdrawn)
                        return { false,{} };
                    else
                        return { true, std::optional<std::reference_wrapper<mod_auction::Team>>(std::ref(t)) };
                }

        return { false,{} };

    }();

    if (!std::get<0>(can_proceed))
        return true;

    auto & t = std::get<1>(can_proceed).value().get();

    std::string sbid{ toks[1] };
    if (sbid.empty())
    {
        _channel.create_message(fmt::format("[<@{}>] Invalid command arguments.", _member.get_id()));
        return true;
    }
    int32_t bid = std::stoul(sbid);

    if (bid % 500 > 0)
    {
        _channel.create_message(fmt::format("[<@{}>] Bid not a multiple of 500 [{}].", _member.get_id(), bid));
        return true;
    }

    if (bid > bids.back().second)
    {
        if (bid > t.funds - ((10 - t.players.size() - 1) * 3000) || bid > t.funds)
        {
            _channel.create_message(fmt::format("[<@{}>] Bid is too high for your funds [{}] Max allowed [{}].", _member.get_id(), t.funds, t.funds - ((10 - t.players.size() - 1) * 3000)));
            return true;
        }

        bids.emplace_back(t.id, bid);
        //message->channel->sendMessage(Poco::format("DEBUG [<@%Lu>] Bid increased to [%d].", message->member->id, bids.back().second)); 
        timeuntilstop = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() + bidtime;
        return true;
    }
    else
    {
        //message->channel->sendMessage(Poco::format("DEBUG [<@%Lu>] Bid not large enough. Current price [%d].", message->member->id, bids.back().second)); 
        return true;
    }
    _channel.create_message(fmt::format("[<@{}>] You do not have a team to bid for.", _member.get_id()));
    return true;
}

bool mod_auction::end(shared_data & sd)
{
    const snowflake & channel_id = sd.channel_id;
    const snowflake & guild_id = sd.guild_id;
    const snowflake & message_id = sd.message_id;
    const snowflake & member_id = sd.member_id;
    const snowflake & guild_owner_id = sd.guild_owner_id;

    std::string_view username = sd.username;

    member & _member = sd._member;
    channel & _channel = sd._channel;
    guild & _guild = sd._guild;
    std::string_view content = sd.content;

    Guild & g_data = sd.g_data;

    std::vector<std::string_view> & toks = sd.toks;

    bool is_guild_owner = (member_id == guild_owner_id || member_id == sd.ab.bot_owner_id);

    std::string request{ content };

    //////////////////////////////////////////////////////////////////////////

    if (!_guild.member_has_role(member_id, hostrole))
        return true;

    if (!auctioninprogress)
    {
        _channel.create_message(fmt::format("[<@{}>] No auction going on.", _member.get_id()));
        return true;
    }

    currentteam = 0;
    json jteams;
    for (auto & t : teams)
    {
        std::stringstream players;
        for (auto & p : t.players)
            players << p.first << " (" << p.second << ")\n";
        jteams.emplace_back(json({ { "name", fmt::format("{} ({})", t.teamname, t.funds) },{ "value", players.str().empty() ? "No players won yet" : players.str() },{ "inline", false } }));
    }
    if (jteams.empty())
    {
        jteams.emplace_back(json({ { "name", "Empty" },{ "value", "Empty" } }));
    }
    json t = {
        { "title", "Current Standings" },
        { "color", 12330144 },
        { "fields", jteams },
        { "footer",{ { "icon_url", "https://cdn.discordapp.com/emojis/289276304564420608.png" },{ "text", "Auction bot" } } }
    };
    _channel.create_message_embed("Auction has ended. Current standings:\n\n", t);
    auctioninprogress = false;
    return true;
}

bool mod_auction::setname(shared_data & sd) 
{
    const snowflake & channel_id = sd.channel_id;
    const snowflake & guild_id = sd.guild_id;
    const snowflake & message_id = sd.message_id;
    const snowflake & member_id = sd.member_id;
    const snowflake & guild_owner_id = sd.guild_owner_id;

    std::string_view username = sd.username;

    member & _member = sd._member;
    channel & _channel = sd._channel;
    guild & _guild = sd._guild;
    std::string_view content = sd.content;

    Guild & g_data = sd.g_data;

    std::vector<std::string_view> & toks = sd.toks;

    bool is_guild_owner = (member_id == guild_owner_id || member_id == sd.ab.bot_owner_id);

    std::string request{ content };

    //////////////////////////////////////////////////////////////////////////

    if (!_guild.member_has_role(member_id, managerrole))
        return true;

    if (check_params(_channel, toks, 2))
        return true;
    std::string_view name = toks[1].data();
    for (auto & t : teams)
    {
        if (t.owner_id == _member.get_id())
        {
            t.teamname = name;
            _channel.create_message(fmt::format("[<@{}>] Name set successfully. **{}**", _member.get_id(), name));
            return true;
        }
    }
    std::string prefix;
    if (!g_data.cmd_prefix_list.empty())
        prefix = g_data.cmd_prefix_list[0];
    _channel.create_message(fmt::format("[<@{}>] You are not registered yet. Register for the auction with `{}register`", _member.get_id(), prefix));
    return true;
}

bool mod_auction::standings(shared_data & sd)
{
    const snowflake & channel_id = sd.channel_id;
    const snowflake & guild_id = sd.guild_id;
    const snowflake & message_id = sd.message_id;
    const snowflake & member_id = sd.member_id;
    const snowflake & guild_owner_id = sd.guild_owner_id;

    std::string_view username = sd.username;

    member & _member = sd._member;
    channel & _channel = sd._channel;
    guild & _guild = sd._guild;
    std::string_view content = sd.content;

    Guild & g_data = sd.g_data;

    std::vector<std::string_view> & toks = sd.toks;

    bool is_guild_owner = (member_id == guild_owner_id || member_id == sd.ab.bot_owner_id);

    std::string request{ content };

    //////////////////////////////////////////////////////////////////////////

    if (!_guild.member_has_role(member_id, managerrole) && !_guild.member_has_role(member_id, hostrole))
        return true;

    json jteams;
    for (auto & t : teams)
    {
        std::stringstream players;
        for (auto & p : t.players)
            players << p.first << " (" << p.second << ")\n";
        jteams.push_back(json({ { "name", fmt::format("[{}] {} ({})", t.id + 1, t.teamname, t.funds) },{ "value", players.str().empty() ? "No players purchased yet" : players.str() },{ "inline", false } }));
    }
    json t = {
        { "title", "Current Standings" },
    { "color", 10599460 },
    { "fields", jteams },
    { "footer",{ { "icon_url", "https://cdn.discordapp.com/emojis/289276304564420608.png" },{ "text", "Auction bot" } } }
    };
    _channel.create_message_embed("", t);
    return true;
}

bool mod_auction::retain(shared_data & sd)
{
    const snowflake & channel_id = sd.channel_id;
    const snowflake & guild_id = sd.guild_id;
    const snowflake & message_id = sd.message_id;
    const snowflake & member_id = sd.member_id;
    const snowflake & guild_owner_id = sd.guild_owner_id;

    std::string_view username = sd.username;

    member & _member = sd._member;
    channel & _channel = sd._channel;
    guild & _guild = sd._guild;
    std::string_view content = sd.content;

    Guild & g_data = sd.g_data;

    std::vector<std::string_view> & toks = sd.toks;

    bool is_guild_owner = (member_id == guild_owner_id || member_id == sd.ab.bot_owner_id);

    std::string request{ content };

    //////////////////////////////////////////////////////////////////////////

    if (!_guild.member_has_role(member_id, hostrole))
        return true;

    try
    {
        if (check_params(_channel, toks, 4, "retain teamindex fundstoremove playername"))
            return true;

        std::string_view playername = toks[3].data();
        int teamindex = std::stoul(std::string{ toks[1] });
        int funds = std::stoul(std::string{ toks[2] });

        if (teamindex > teams.size() || teamindex < 0)
        {
            _channel.create_message(fmt::format("[<@{}>] Team does not exist.", _member.get_id()));
            return true;
        }


        for (auto & p : players)
        {
            if (p.first == playername)
            {
                p.second = false;
                teams[teamindex].funds -= funds;
                teams[teamindex].players.emplace_back(playername, funds);
                _channel.create_message(fmt::format("[<@{}>] Player **{}** retained to team **{}** for [{}]", _member.get_id(), playername, teams[teamindex].teamname, funds));
                return true;
            }
        }

    }
    catch (...)
    {
        _channel.create_message(fmt::format("[<@{}>] Invalid command arguments.", _member.get_id()));
    }
    return true;
}

bool mod_auction::skip(shared_data & sd)
{
    const snowflake & channel_id = sd.channel_id;
    const snowflake & guild_id = sd.guild_id;
    const snowflake & message_id = sd.message_id;
    const snowflake & member_id = sd.member_id;
    const snowflake & guild_owner_id = sd.guild_owner_id;

    std::string_view username = sd.username;

    member & _member = sd._member;
    channel & _channel = sd._channel;
    guild & _guild = sd._guild;
    std::string_view content = sd.content;

    Guild & g_data = sd.g_data;

    std::vector<std::string_view> & toks = sd.toks;

    bool is_guild_owner = (member_id == guild_owner_id || member_id == sd.ab.bot_owner_id);

    std::string request{ content };

    //////////////////////////////////////////////////////////////////////////

    if (!_guild.member_has_role(member_id, hostrole))
        return true;

    if (!auctioninprogress)
    {
        _channel.create_message(fmt::format("[<@{}>] No auction going on.", _member.get_id()));
        return true;
    }

    {
        int withdrawn = 0;
        for (auto & t : teams)
        {
            if (t.withdrawn)
                withdrawn++;
        }
        if (teams.size() == withdrawn)
        {
            //Auction over
            json jteams;
            for (auto & t : teams)
            {
                std::stringstream players;
                for (auto & p : t.players)
                    players << p.first << " (" << p.second << ")\n";
                jteams.emplace_back(json({ { "name", fmt::format("{} ({})", t.teamname, t.funds) },{ "value", players.str().empty() ? "No players won yet" : players.str() },{ "inline", false } }));
            }
            if (jteams.empty())
            {
                jteams.emplace_back(json({ { "name", "Empty" },{ "value", "Empty" } }));
            }
            json t = {
                { "title", "Current Standings" },
            { "color", 12330144 },
            { "fields", jteams },
            { "footer",{ { "icon_url", "https://cdn.discordapp.com/emojis/289276304564420608.png" },{ "text", "Auction bot" } } }
            };
            _channel.create_message_embed("Auction has ended. Current standings:\n\n", t);
            auctioninprogress = false;
            return true;
        }
    }

    int safety = 0;
    std::string last_team = teams[currentteam].teamname;
    do
    {
        lastteam = currentteam;

        currentteam += direction;

        if (currentteam > teams.size() - 1)
            currentteam = static_cast<int32_t>(teams.size() - 1);
        if (currentteam < 0)
            currentteam = 0;

        if (currentteam == lastteam)
        {
            if (direction == -1) direction = 1;
            else if (direction == 1) direction = -1;
        }

        if (!teams[currentteam].withdrawn)
            break;
        safety++;
        if (safety > 30)
        {
            _channel.create_message("Infinite loop detected");
            return true;
        }
    } while (true);

    std::string prefix;
    if (!g_data.cmd_prefix_list.empty())
        prefix = g_data.cmd_prefix_list[0];
    _channel.create_message(fmt::format("**{}** has been skipped. **{}** is next.\n<@{}> (**{}**) type `{}nom player name` to nominate another player.", last_team, teams[currentteam].teamname, teams[currentteam].owner_id, teams[currentteam].teamname, prefix));
    return true;
}

bool mod_auction::setfunds(shared_data & sd)
{
    const snowflake & channel_id = sd.channel_id;
    const snowflake & guild_id = sd.guild_id;
    const snowflake & message_id = sd.message_id;
    const snowflake & member_id = sd.member_id;
    const snowflake & guild_owner_id = sd.guild_owner_id;

    std::string_view username = sd.username;

    member & _member = sd._member;
    channel & _channel = sd._channel;
    guild & _guild = sd._guild;
    std::string_view content = sd.content;

    Guild & g_data = sd.g_data;

    std::vector<std::string_view> & toks = sd.toks;

    bool is_guild_owner = (member_id == guild_owner_id || member_id == sd.ab.bot_owner_id);

    std::string request{ content };

    //////////////////////////////////////////////////////////////////////////

    if (!_guild.member_has_role(member_id, hostrole))
        return true;

    if (check_params(_channel, toks, 3))
        return true;
    try
    {
        std::string team{ toks[1] };

        if (team.empty())
        {
            _channel.create_message(fmt::format("[<@{}>] Invalid command arguments.", _member.get_id()));
            return true;
        }

        if (teams.size() < std::stoul(team) + 1 || std::stoul(team) < 0)
        {
            _channel.create_message(fmt::format("[<@{}>] Invalid team.", _member.get_id()));
            return true;
        }

        std::string funds{ toks[2] };

        if (funds.empty())
        {
            _channel.create_message(fmt::format("[<@{}>] Invalid command arguments.", _member.get_id()));
            return true;
        }


        teams[std::stoul(team)].funds = std::stoul(funds);
        _channel.create_message(fmt::format("[<@{}>] Funds for **{}** set successfully. [{}]", _member.get_id(), teams[std::stoul(team)].teamname, teams[std::stoul(team)].funds));
    }
    catch (...)
    {
        _channel.create_message(fmt::format("[<@{}>] Invalid command arguments.", _member.get_id()));
    }
    return true;
}

bool mod_auction::undobid(shared_data & sd)
{
    const snowflake & channel_id = sd.channel_id;
    const snowflake & guild_id = sd.guild_id;
    const snowflake & message_id = sd.message_id;
    const snowflake & member_id = sd.member_id;
    const snowflake & guild_owner_id = sd.guild_owner_id;

    std::string_view username = sd.username;

    member & _member = sd._member;
    channel & _channel = sd._channel;
    guild & _guild = sd._guild;
    std::string_view content = sd.content;

    Guild & g_data = sd.g_data;

    std::vector<std::string_view> & toks = sd.toks;

    bool is_guild_owner = (member_id == guild_owner_id || member_id == sd.ab.bot_owner_id);

    std::string request{ content };

    //////////////////////////////////////////////////////////////////////////

    if (!_guild.member_has_role(member_id, hostrole))
        return true;

    if (!auctioninprogress)
    {
        _channel.create_message(fmt::format("[<@{}>] No auction going on.", _member.get_id()));
        return true;
    }

    if (bids.size() > 1)
    {
        bids.pop_back();
        _channel.create_message(fmt::format("[<@{}>] Undo bid successful. Current Bid [{}] by **{}**.", _member.get_id(), bids.back().second, teams[bids.back().first].teamname));
        return true;
    }
    else
    {
        _channel.create_message(fmt::format("[<@{}>] There is no last bidder.", _member.get_id()));
        return true;
    }
}

bool mod_auction::auctionhelp(shared_data & sd)
{
    const snowflake & channel_id = sd.channel_id;
    const snowflake & guild_id = sd.guild_id;
    const snowflake & message_id = sd.message_id;
    const snowflake & member_id = sd.member_id;
    const snowflake & guild_owner_id = sd.guild_owner_id;

    std::string_view username = sd.username;

    member & _member = sd._member;
    channel & _channel = sd._channel;
    guild & _guild = sd._guild;
    std::string_view content = sd.content;

    Guild & g_data = sd.g_data;

    std::vector<std::string_view> & toks = sd.toks;

    bool is_guild_owner = (member_id == guild_owner_id || member_id == sd.ab.bot_owner_id);

    std::string request{ content };

    //////////////////////////////////////////////////////////////////////////

    json admincommands = json({ { "name", "Admin only" },{ "value", "start, end, defaultfunds, pause, resume, setfunds, bidtime, fsetname, retain, skip" },{ "inline", true } });
    json usercommands = json({ { "name", "Manager" },{ "value", "register, playerlist, nom, b, bid, setname, standings, undobid" },{ "inline", true } });
    json t = {
        { "title", "Commands" },
        { "color", 10599460 },
        { "fields",{ admincommands, usercommands } },
        { "footer",{ { "icon_url", "https://cdn.discordapp.com/emojis/289276304564420608.png" },{ "text", "Auction bot" } } }
    };
    _channel.create_message_embed("", t);
    return true;
}

bool mod_auction::undobuy(shared_data & sd)
{
    const snowflake & channel_id = sd.channel_id;
    const snowflake & guild_id = sd.guild_id;
    const snowflake & message_id = sd.message_id;
    const snowflake & member_id = sd.member_id;
    const snowflake & guild_owner_id = sd.guild_owner_id;

    std::string_view username = sd.username;

    member & _member = sd._member;
    channel & _channel = sd._channel;
    guild & _guild = sd._guild;
    std::string_view content = sd.content;

    Guild & g_data = sd.g_data;

    std::vector<std::string_view> & toks = sd.toks;

    bool is_guild_owner = (member_id == guild_owner_id || member_id == sd.ab.bot_owner_id);

    std::string request{ content };

    //////////////////////////////////////////////////////////////////////////

    if (!_guild.member_has_role(member_id, hostrole))
        return true;

    if (undo_buy.player_bought.empty())
    {
        _channel.create_message(fmt::format("[<@{}>] No player bought.", _member.get_id()));
        return true;
    }


    auto res = bids.back();

    for (auto it = teams[res.first].players.begin(); it != teams[res.first].players.end(); ++it)
    {
        if (it->first == undo_buy.player_bought)
        {
            //player to undo
            players[undo_buy.player_bought] = true;
            teams[res.first].funds += res.second;
            teams[res.first].players.erase(it);
            direction = undo_buy.direction;
            lastteam = undo_buy.lastteam;
            currentteam = undo_buy.currentteam;

            _channel.create_message(fmt::format("[<@{}>] Purchase of player **{}** reversed. {} credited back {}.", _member.get_id(), undo_buy.player_bought, teams[res.first].teamname, res.second));

            undo_buy.player_bought = "";
            undo_buy.direction = 0;
            undo_buy.lastteam = 0;
            undo_buy.currentteam = 0;

            return true;
        }
    }

    _channel.create_message(fmt::format("[<@{}>] Unable to undo.", _member.get_id()));
    return true;
}

bool mod_auction::withdraw(shared_data & sd)
{
    const snowflake & channel_id = sd.channel_id;
    const snowflake & guild_id = sd.guild_id;
    const snowflake & message_id = sd.message_id;
    const snowflake & member_id = sd.member_id;
    const snowflake & guild_owner_id = sd.guild_owner_id;

    std::string_view username = sd.username;

    member & _member = sd._member;
    channel & _channel = sd._channel;
    guild & _guild = sd._guild;
    std::string_view content = sd.content;

    Guild & g_data = sd.g_data;

    std::vector<std::string_view> & toks = sd.toks;

    bool is_guild_owner = (member_id == guild_owner_id || member_id == sd.ab.bot_owner_id);

    std::string request{ content };

    //////////////////////////////////////////////////////////////////////////

    if (!_guild.member_has_role(member_id, managerrole))
        return true;

    if (!auctioninprogress)
    {
        _channel.create_message(fmt::format("[<@{}>] No auction going on.", _member.get_id()));
        return true;
    }
    for (auto & t : teams)
    {
        for (auto & b : t.bidders)
        {
            if (b == _member.get_id())
            {
                if (t.players.size() < 10)
                {
                    _channel.create_message(fmt::format("[<@{}>] Unable to withdraw. Need at least 10 players. Your players: **{}**.", _member.get_id(), t.players.size()));
                    return true;
                }
                _channel.create_message(fmt::format("[<@{}>] Your team **{}** has withdrawn from the auction.", _member.get_id(), t.teamname));
                t.withdrawn = true;


                {
                    int withdrawn = 0;
                    for (auto & t : teams)
                    {
                        if (t.withdrawn)
                            withdrawn++;
                    }
                    if (teams.size() == withdrawn)
                    {
                        //Auction over
                        json jteams;
                        for (auto & t : teams)
                        {
                            std::stringstream players;
                            for (auto & p : t.players)
                                players << p.first << " (" << p.second << ")\n";
                            jteams.emplace_back(json({ { "name", fmt::format("{} ({})", t.teamname, t.funds) },{ "value", players.str().empty() ? "No players won yet" : players.str() },{ "inline", false } }));
                        }
                        if (jteams.empty())
                        {
                            jteams.emplace_back(json({ { "name", "Empty" },{ "value", "Empty" } }));
                        }
                        json t = {
                            { "title", "Current Standings" },
                        { "color", 12330144 },
                        { "fields", jteams },
                        { "footer",{ { "icon_url", "https://cdn.discordapp.com/emojis/289276304564420608.png" },{ "text", "Auction bot" } } }
                        };
                        _channel.create_message_embed("Auction has ended. Current standings:\n\n", t);
                        auctioninprogress = false;
                        return true;
                    }
                }
                return true;
            }
        }
    }
    return true;
}

bool mod_auction::addfunds(shared_data & sd)
{
    const snowflake & channel_id = sd.channel_id;
    const snowflake & guild_id = sd.guild_id;
    const snowflake & message_id = sd.message_id;
    const snowflake & member_id = sd.member_id;
    const snowflake & guild_owner_id = sd.guild_owner_id;

    std::string_view username = sd.username;

    member & _member = sd._member;
    channel & _channel = sd._channel;
    guild & _guild = sd._guild;
    std::string_view content = sd.content;

    Guild & g_data = sd.g_data;

    std::vector<std::string_view> & toks = sd.toks;

    bool is_guild_owner = (member_id == guild_owner_id || member_id == sd.ab.bot_owner_id);

    std::string request{ content };

    //////////////////////////////////////////////////////////////////////////

    if (!_guild.member_has_role(member_id, hostrole))
        return true;

    if (check_params(_channel, toks, 3))
        return true;
    try
    {
        std::string team{ toks[1] };

        if (team.empty())
        {
            _channel.create_message(fmt::format("[<@{}>] Invalid command arguments.", _member.get_id()));
            return true;
        }

        if (teams.size() < std::stoul(team) + 1 || std::stoul(team) < 0)
        {
            _channel.create_message(fmt::format("[<@{}>] Invalid team.", _member.get_id()));
            return true;
        }

        std::string funds{ toks[2] };

        if (funds.empty())
        {
            _channel.create_message(fmt::format("[<@{}>] Invalid command arguments.", _member.get_id()));
            return true;
        }

        teams[std::stoul(team)].funds += std::stoul(funds);
        _channel.create_message(fmt::format("[<@{}>] Funds for **{}** set successfully. +{} [{}]", _member.get_id(), teams[std::stoul(team)].teamname, std::stoul(funds), teams[std::stoul(team)].funds));
    }
    catch (...)
    {
        _channel.create_message(fmt::format("[<@{}>] Invalid command arguments.", _member.get_id()));
    }
    return true;
}

bool mod_auction::removefunds(shared_data & sd)
{
    const snowflake & channel_id = sd.channel_id;
    const snowflake & guild_id = sd.guild_id;
    const snowflake & message_id = sd.message_id;
    const snowflake & member_id = sd.member_id;
    const snowflake & guild_owner_id = sd.guild_owner_id;

    std::string_view username = sd.username;

    member & _member = sd._member;
    channel & _channel = sd._channel;
    guild & _guild = sd._guild;
    std::string_view content = sd.content;

    Guild & g_data = sd.g_data;

    std::vector<std::string_view> & toks = sd.toks;

    bool is_guild_owner = (member_id == guild_owner_id || member_id == sd.ab.bot_owner_id);

    std::string request{ content };

    //////////////////////////////////////////////////////////////////////////

    if (!_guild.member_has_role(member_id, hostrole))
        return true;

    if (check_params(_channel, toks, 3))
        return true;
    try
    {
        std::string team{ toks[1] };

        if (team.empty())
        {
            _channel.create_message(fmt::format("[<@{}>] Invalid command arguments.", _member.get_id()));
            return true;
        }

        if (teams.size() < std::stoul(team) + 1 || std::stoul(team) < 0)
        {
            _channel.create_message(fmt::format("[<@{}>] Invalid team.", _member.get_id()));
            return true;
        }

        std::string funds{ toks[2] };

        if (funds.empty())
        {
            _channel.create_message(fmt::format("[<@{}>] Invalid command arguments.", _member.get_id()));
            return true;
        }

        teams[std::stoul(team)].funds -= std::stoul(funds);
        _channel.create_message(fmt::format("[<@{}>] Funds for **{}** set successfully. -{} [{}]", _member.get_id(), teams[std::stoul(team)].teamname, std::stoul(funds), teams[std::stoul(team)].funds));
    }
    catch (...)
    {
        _channel.create_message(fmt::format("[<@{}>] Invalid command arguments.", _member.get_id()));
    }
    return true;
}

bool mod_auction::adminsetname(shared_data & sd)
{
    const snowflake & channel_id = sd.channel_id;
    const snowflake & guild_id = sd.guild_id;
    const snowflake & message_id = sd.message_id;
    const snowflake & member_id = sd.member_id;
    const snowflake & guild_owner_id = sd.guild_owner_id;

    std::string_view username = sd.username;

    member & _member = sd._member;
    channel & _channel = sd._channel;
    guild & _guild = sd._guild;
    std::string_view content = sd.content;

    Guild & g_data = sd.g_data;

    std::vector<std::string_view> & toks = sd.toks;

    bool is_guild_owner = (member_id == guild_owner_id || member_id == sd.ab.bot_owner_id);

    std::string request{ content };

    //////////////////////////////////////////////////////////////////////////

    if (!_guild.member_has_role(member_id, hostrole))
        return true;

    if (check_params(_channel, toks, 3))
        return true;
    try
    {
        std::string team{ toks[1] };

        if (team.empty())
        {
            _channel.create_message(fmt::format("[<@{}>] Invalid command arguments.", _member.get_id()));
            return true;
        }

        if (teams.size() < std::stoul(team) + 1 || std::stoul(team) < 0)
        {
            _channel.create_message(fmt::format("[<@{}>] Invalid team.", _member.get_id()));
            return true;
        }

        std::string name{ toks[2].data() };

        if (name.empty())
        {
            _channel.create_message(fmt::format("[<@{}>] Invalid command arguments.", _member.get_id()));
            return true;
        }

        std::string oldname = teams[std::stoul(team)].teamname;
        teams[std::stoul(team)].teamname = name;
        _channel.create_message(fmt::format("[<@{}>] Name successfully changed from **{}** to **{}**", _member.get_id(), oldname, teams[std::stoul(team)].teamname));
    }
    catch (...)
    {
        _channel.create_message(fmt::format("[<@{}>] Invalid command arguments.", _member.get_id()));
    }
    return true;
}

bool mod_auction::addbidder(shared_data & sd)
{
    const snowflake & channel_id = sd.channel_id;
    const snowflake & guild_id = sd.guild_id;
    const snowflake & message_id = sd.message_id;
    const snowflake & member_id = sd.member_id;
    const snowflake & guild_owner_id = sd.guild_owner_id;

    std::string_view username = sd.username;

    member & _member = sd._member;
    channel & _channel = sd._channel;
    guild & _guild = sd._guild;
    std::string_view content = sd.content;

    Guild & g_data = sd.g_data;

    std::vector<std::string_view> & toks = sd.toks;

    bool is_guild_owner = (member_id == guild_owner_id || member_id == sd.ab.bot_owner_id);

    std::string request{ content };

    //////////////////////////////////////////////////////////////////////////

    if (!_guild.member_has_role(member_id, managerrole) && !_guild.member_has_role(member_id, hostrole))
        return true;

    if (_guild.member_has_role(member_id, hostrole))
    {
        if (check_params(_channel, toks, 3, "addbidder team# user"))
            return true;

        uint32_t team = std::stoul(std::string(toks[1]));
        snowflake add_bidder = sd.ab.get_snowflake(toks[2], _guild);

        if (teams.size() < team + 1 || team < 0)
        {
            _channel.create_message(fmt::format("[<@{}>] Invalid team.", _member.get_id()));
            return true;
        }

        auto & t = teams[team];

        if (t.withdrawn)
            return true;
        t.bidders.emplace_back(add_bidder);
        _channel.create_message(fmt::format("[<@{}>] Added [<@{}>] to **{}**'s list of bidders.", _member.get_id(), add_bidder, t.teamname));
        return true;
    }

    if (check_params(_channel, toks, 2))
        return true;
    std::string bidder{ toks[1] };
    if (bidder.empty())
    {
        _channel.create_message(fmt::format("[<@{}>] Invalid command arguments.", _member.get_id()));
        return true;
    }

    snowflake add_bidder = sd.ab.get_snowflake(bidder, _guild);

    for (auto & t : teams)
    {
        if (t.owner_id == _member.get_id())
        {
            if (t.withdrawn)
                return true;
            t.bidders.emplace_back(add_bidder);
            _channel.create_message(fmt::format("[<@{}>] Added [<@{}>] to **{}**'s list of bidders.", _member.get_id(), add_bidder, t.teamname));
            return true;
        }
    }
    _channel.create_message(fmt::format("[<@{}>] You are not the manager of a team.", _member.get_id()));
    return true;
}

bool mod_auction::delbidder(shared_data & sd)
{
    const snowflake & channel_id = sd.channel_id;
    const snowflake & guild_id = sd.guild_id;
    const snowflake & message_id = sd.message_id;
    const snowflake & member_id = sd.member_id;
    const snowflake & guild_owner_id = sd.guild_owner_id;

    std::string_view username = sd.username;

    member & _member = sd._member;
    channel & _channel = sd._channel;
    guild & _guild = sd._guild;
    std::string_view content = sd.content;

    Guild & g_data = sd.g_data;

    std::vector<std::string_view> & toks = sd.toks;

    bool is_guild_owner = (member_id == guild_owner_id || member_id == sd.ab.bot_owner_id);

    std::string request{ content };

    //////////////////////////////////////////////////////////////////////////

    if (!_guild.member_has_role(member_id, managerrole) && !_guild.member_has_role(member_id, hostrole))
        return true;

    if (_guild.member_has_role(member_id, hostrole))
    {
        if (check_params(_channel, toks, 3, "delbidder team# user"))
            return true;

        uint32_t team = std::stoul(std::string(toks[1]));
        snowflake rembidder = sd.ab.get_snowflake(toks[2], _guild);

        if (teams.size() < team + 1 || team < 0)
        {
            _channel.create_message(fmt::format("[<@{}>] Invalid team.", _member.get_id()));
            return true;
        }

        auto & t = teams[team];

        if (t.withdrawn)
            return true;

        for (auto it = t.bidders.begin(); it != t.bidders.end(); ++it)
        {
            if (*it == rembidder)
            {
                t.bidders.erase(it);
                _channel.create_message(fmt::format("[<@{}>] Removed [<@{}>] from **{}**'s list of bidders.", _member.get_id(), rembidder, t.teamname));
                return true;
            }
        }
        _channel.create_message(fmt::format("[<@{}>] Bidder does not exist.", _member.get_id()));
        return true;
    }

    if (check_params(_channel, toks, 2))
        return true;
    std::string bidder{ toks[1] };
    if (bidder.empty())
    {
        _channel.create_message(fmt::format("[<@{}>] Invalid command arguments.", _member.get_id()));
        return true;
    }

    snowflake rembidder = sd.ab.get_snowflake(bidder, _guild);

    for (auto & t : teams)
    {
        if (t.owner_id == _member.get_id())
        {
            if (t.withdrawn)
                return true;
            for (auto it = t.bidders.begin(); it != t.bidders.end(); ++it)
            {
                if (*it == rembidder)
                {
                    t.bidders.erase(it);
                    _channel.create_message(fmt::format("[<@{}>] Removed [<@{}>] from **{}**'s list of bidders.", _member.get_id(), rembidder, t.teamname));
                    return true;
                }
            }
        }
    }
    _channel.create_message(fmt::format("[<@{}>] You are not the manager of a team.", _member.get_id()));
    return true;
}
