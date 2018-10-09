//
// mod_tag.h
// *********
//
// Copyright (c) 2018 Sharon W (sharon at aegis dot gg)
//
// Distributed under the MIT License. (See accompanying file LICENSE)
// 

#pragma once

#include "Module.h"

class Guild;

class mod_tag : public Module
{
public:
    mod_tag(Guild&, asio::io_context&);
    ~mod_tag();

    std::vector<std::string> get_db_entries() override;

    static std::string r_prefix;

    void do_stop() override;

    bool check_command(std::string, shared_data&) override;

    void remove() override;

    void load(AegisBot&) override;

    bool tag(shared_data&);

    enum class tag_type
    {
        Base,
        Tag,
        Alias
    };

    struct base_tag_st
    {
        base_tag_st() { type = tag_type::Base; }
        virtual ~base_tag_st() = default;

        tag_type type;
        int32_t id;
        snowflake owner_id;
        std::string owner_name;
        uint64_t creation;
        uint32_t usage_count;
    };

    struct alias_tag_st : public base_tag_st
    {
        alias_tag_st() { type = tag_type::Alias; }
        std::string alias_of;
    };

    struct tag_st : public base_tag_st
    {
        tag_st() { type = tag_type::Tag; }
        std::string content;
    };

    struct tag_perm_st
    {
        tag_perm_st() = default;
        ~tag_perm_st() = default;
        bool can_access = true;
        bool can_create = true;
        bool can_delete = false;
        bool can_manage = false;
    };

    std::unordered_map<std::string, std::unique_ptr<base_tag_st>> taglist;
    std::unordered_map<snowflake, tag_perm_st> permlist;
    std::vector<snowflake> blocklist;




};
