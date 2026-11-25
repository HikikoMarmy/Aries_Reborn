#pragma once

#include "../common/threadsafe_queue.hpp"
#include "../misc/types.h"
#include "../platform/socket_compat.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

enum enum_socket_protocol : uint8_t
{
    protocol_lobby = 0,
    protocol_buddy = 1
};

struct aries_socket
{
    SOCKET fd{ INVALID_SOCKET };
    enum_socket_protocol socket_protocol{ protocol_lobby };
    std::string ip_address;
    uint16_t port{ 0 };
    bool listener{ false };
    std::atomic<bool> disconnected{ false };
    std::atomic<bool> disconnect_notified{ false };
    std::vector<uint8_t> read_data;
    threadsafe_queue<sptr_packet> send_queue;
    std::vector<uint8_t> write_data;
    size_t write_offset{ 0 };
};
