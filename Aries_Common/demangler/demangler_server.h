#pragma once

#include "../platform/socket_compat.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

class CDemanglerServer
{
public:
    CDemanglerServer();
    ~CDemanglerServer();

    bool start( const std::string &bind_ip, const std::string &public_ip, const std::vector<uint16_t> &ports, uint32_t probe_count, uint32_t session_timeout_seconds );
    void shutdown();

private:
    struct listener
    {
        uint16_t port{ 0 };
        SOCKET tcp_listener{ INVALID_SOCKET };
        SOCKET udp_socket{ INVALID_SOCKET };
    };

    struct participant
    {
        std::string key;
        std::string private_ip;
        uint16_t private_port{ 0 };
        std::string public_ip;
        uint16_t public_port{ 0 };
        uint16_t listener_port{ 0 };
        std::string tag;
        std::chrono::steady_clock::time_point last_seen{};
        bool mapped{ false };
    };

    struct session
    {
        std::string id;
        uint16_t listener_port{ 0 };
        std::vector<participant> participants;
        std::chrono::steady_clock::time_point last_seen{};
    };

    struct tag_binding
    {
        std::string session_key;
        std::string participant_key;
        uint16_t listener_port{ 0 };
    };

    bool open_tcp_listener( listener &entry );
    bool open_udp_socket( listener &entry );
    bool set_nonblocking( SOCKET fd );
    void io_thread();
    void accept_tcp_clients( listener &entry );
    void handle_tcp_client( SOCKET fd, const sockaddr_in &remote, uint16_t listener_port );
    void handle_udp_packets( listener &entry );
    bool read_http_request( SOCKET fd, std::string &request );
    bool send_http_response( SOCKET fd, const std::string &body );
    void handle_get_peer_address( SOCKET fd, const sockaddr_in &remote, const std::string &request_target, const std::string &headers, uint16_t listener_port );
    void handle_connection_status( SOCKET fd, const sockaddr_in &remote, const std::string &body, const std::string &headers, uint16_t listener_port );
    std::string build_probe_response( const participant &entry ) const;
    std::string build_success_response( const participant &peer ) const;
    std::string build_failure_response() const;
    participant &get_or_create_participant( session &session_entry, const std::string &session_key, const std::string &key, const std::string &private_ip, uint16_t private_port, uint16_t listener_port );
    participant *find_participant( session &session_entry, const std::string &key );
    participant *find_mapped_peer( session &session_entry, const std::string &key );
    std::string make_participant_key( const sockaddr_in &remote, const std::string &private_ip, uint16_t private_port ) const;
    std::string make_session_key( const std::string &session_id, uint16_t listener_port ) const;
    std::string make_tag();
    void cleanup_sessions();
    void remove_session_tags( const session &session_entry );

    std::atomic<bool> run_{ false };
    std::thread thread_;
    std::vector<listener> listeners_;
    std::string bind_ip_;
    std::string public_ip_;
    uint32_t probe_count_{ 3 };
    uint32_t session_timeout_seconds_{ 60 };
    uint64_t next_tag_{ 1 };
    std::unordered_map<std::string, session> sessions_;
    std::unordered_map<std::string, tag_binding> tags_;
};
