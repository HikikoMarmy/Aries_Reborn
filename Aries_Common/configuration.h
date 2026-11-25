#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class CConfiguration
{
public:
    bool load( const std::string &path, std::string_view section = {} );

    std::string get_string( std::string_view key, std::string_view fallback = {} ) const;
    int64_t get_number( std::string_view key, int64_t fallback = 0 ) const;
    bool get_bool( std::string_view key, bool fallback = false ) const;

    std::string bind_ip{ "127.0.0.1" };
    std::string service_ip{ "127.0.0.1" };
    uint16_t lobby_port{ 0 };
    uint16_t buddy_port{ 0 };
    std::vector<uint16_t> additional_lobby_ports;
    uint32_t max_packet_size{ 1024 * 1024 };
    std::string database_path{ "data/aries.db" };
    std::string log_directory{ "logs" };
    bool auto_ack_unknown{ true };
    bool log_packet_payloads{ false };
    uint32_t buddy_ping_interval_seconds{ 30 };
    uint32_t lobby_ping_interval_seconds{ 10 };
    bool demangler_enabled{ false };
    std::string demangler_ip;
    std::vector<uint16_t> demangler_ports{ 3658 };
    uint32_t demangler_probe_count{ 3 };
    uint32_t demangler_session_timeout_seconds{ 60 };

private:
    void apply_value( const std::string &key, const std::string &value );

    std::unordered_map<std::string, std::string> values_;
};
