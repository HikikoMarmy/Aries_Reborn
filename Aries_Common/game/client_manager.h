#pragma once

#include "client.h"
#include "../network/socket_struct.h"

#include <atomic>
#include <map>
#include <mutex>
#include <vector>

class AriesClientManager
{
public:
    bool spawn_new( sptr_socket &socket, ptr_client &ret );
    bool get_client( sptr_socket &socket, ptr_client &ret );
    void remove_client( sptr_socket &socket );
    void disconnect( ptr_client &client );
    std::vector<ptr_client> get_clients();
    bool find_lobby_by_key( const std::string &lobby_key, ptr_client &ret );
    bool find_online_persona( int64_t persona_id, ptr_client &ret );
    bool find_online_persona( int64_t persona_id, enum_socket_protocol protocol, ptr_client &ret );
    bool has_online_persona( int64_t persona_id );

private:
    std::atomic<uint32_t> next_client_id_{ 1 };
    std::map<const aries_socket *, ptr_client> client_map_;
    std::mutex mutex_;
};
