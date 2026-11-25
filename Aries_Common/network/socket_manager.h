#pragma once

#include "../common/threadsafe_queue.hpp"
#include "socket_struct.h"

#include <atomic>
#include <set>
#include <thread>
#include <utility>

using T_SOCKET_SET = std::set<sptr_socket>;

class CSocketManager
{
public:
    CSocketManager();
    ~CSocketManager();

    bool Initialize();
    bool open_tcp_listener( const std::string &ip, uint16_t port, enum_socket_protocol protocol );
    bool get_message( sptr_socket &socket, sptr_packet &packet );
    void shutdown();

private:
    void socket_io_thread();
    void accept_new_tcp( const sptr_socket &listener );
    void notify_socket( const sptr_socket &socket, uint32_t notice );
    void TCP_TryReadData( const sptr_socket &socket );
    void TCP_TryWriteData( const sptr_socket &socket );
    void extract_packets( const sptr_socket &socket );
    void close_disconnected();
    bool set_nonblocking( SOCKET fd );

    std::atomic<bool> run_{ false };
    std::thread io_thread_;
    T_SOCKET_SET socket_set_;
    std::mutex socket_mutex_;
    threadsafe_queue<std::pair<sptr_socket, sptr_packet>> read_queue_;
};
