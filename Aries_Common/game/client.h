#pragma once

#include "../misc/types.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <string>

enum enum_client_stage : uint8_t
{
    stage_connected = 0,
    stage_directory,
    stage_lobby,
    stage_authenticated,
    stage_persona
};

class aries_client
{
public:
    aries_client( sptr_socket socket, uint32_t client_id );

    sptr_socket socket;
    uint32_t client_id{ 0 };
    enum_client_stage stage{ stage_connected };
    int64_t account_id{ 0 };
    int64_t persona_id{ 0 };
    std::string account_name;
    std::string persona;
    std::string room_name;
    std::string machine_id;
    int32_t hwflag{ 0 };
    int32_t hwmask{ 0 };
    std::string lobby_key;
    std::array<uint8_t, 16> session_key{};
    bool buddy_authenticated{ false };
    bool buddy_ping_outstanding{ false };
    uint32_t buddy_ping_code{ 0 };
    uint32_t buddy_last_ping_ms{ 0 };
    std::chrono::steady_clock::time_point buddy_last_ping{};
    std::chrono::steady_clock::time_point buddy_ping_sent_at{};
    std::string buddy_resource;
    std::string buddy_show;
    std::string buddy_status;
    std::string buddy_product;
    bool lobby_ping_outstanding{ false };
    uint32_t lobby_ping_code{ 0 };
    uint32_t lobby_last_ping_ms{ 0 };
    std::chrono::steady_clock::time_point lobby_last_ping{};
    std::chrono::steady_clock::time_point lobby_ping_sent_at{};
    std::string reported_address;
    uint16_t reported_port{ 0 };
};
