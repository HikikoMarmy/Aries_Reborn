#pragma once

#include <atomic>
#include <cstdint>
#include <string>

class AriesGameProtocol;

struct aries_server_options
{
    uint16_t lobby_port{ 0 };
    uint16_t buddy_port{ 0 };
    std::string database_path{ "data/aries.db" };
    std::string config_path{ "aries.ini" };
    std::string config_section;
    std::string bind_ip_override;
    std::string service_ip_override;
    std::atomic<bool> *shutdown_requested{ nullptr };
    void ( *started_callback )( void *context ){ nullptr };
    void *callback_context{ nullptr };
};

int RunAriesServer( AriesGameProtocol &game_protocol, const aries_server_options &options );
