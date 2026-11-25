#include "stdafx.h"
#include "client_manager.h"
#include "../network/socket_struct.h"

bool AriesClientManager::spawn_new( sptr_socket &socket, ptr_client &ret )
{
    if( nullptr == socket || socket->fd == INVALID_SOCKET ) return false;

    std::lock_guard lock( mutex_ );
    if( client_map_.contains( socket.get() ) ) return false;

    ret = std::make_shared<aries_client>( socket, next_client_id_.fetch_add( 1 ) );
    client_map_.insert( { socket.get(), ret } );
    return true;
}

bool AriesClientManager::get_client( sptr_socket &socket, ptr_client &ret )
{
    if( nullptr == socket ) return false;
    std::lock_guard lock( mutex_ );
    const auto iter = client_map_.find( socket.get() );
    if( iter == client_map_.end() ) return false;
    ret = iter->second;
    return true;
}

void AriesClientManager::remove_client( sptr_socket &socket )
{
    if( nullptr == socket ) return;
    std::lock_guard lock( mutex_ );
    client_map_.erase( socket.get() );
}

void AriesClientManager::disconnect( ptr_client &client )
{
    if( nullptr == client || nullptr == client->socket ) return;
    client->socket->disconnected = true;
}

std::vector<ptr_client> AriesClientManager::get_clients()
{
    std::lock_guard lock( mutex_ );
    std::vector<ptr_client> clients;
    clients.reserve( client_map_.size() );
    for( const auto &entry : client_map_ )
    {
        clients.push_back( entry.second );
    }
    return clients;
}


bool AriesClientManager::find_lobby_by_key( const std::string &lobby_key, ptr_client &ret )
{
    if( lobby_key.empty() ) return false;
    std::lock_guard lock( mutex_ );
    for( const auto &entry : client_map_ )
    {
        const auto &client = entry.second;
        if( nullptr == client || nullptr == client->socket ) continue;
        if( client->socket->socket_protocol != protocol_lobby ) continue;
        if( client->lobby_key != lobby_key ) continue;
        ret = client;
        return true;
    }
    return false;
}

bool AriesClientManager::find_online_persona( int64_t persona_id, ptr_client &ret )
{
    return find_online_persona( persona_id, protocol_buddy, ret );
}

bool AriesClientManager::find_online_persona( int64_t persona_id, enum_socket_protocol protocol, ptr_client &ret )
{
    if( persona_id <= 0 ) return false;
    std::lock_guard lock( mutex_ );
    for( const auto &entry : client_map_ )
    {
        const auto &client = entry.second;
        if( nullptr == client || nullptr == client->socket ) continue;
        if( client->socket->socket_protocol != protocol ) continue;
        if( client->socket->disconnected ) continue;
        if( protocol == protocol_buddy && false == client->buddy_authenticated ) continue;
        if( client->persona_id != persona_id ) continue;
        ret = client;
        return true;
    }
    return false;
}

bool AriesClientManager::has_online_persona( int64_t persona_id )
{
    if( persona_id <= 0 ) return false;
    std::lock_guard lock( mutex_ );
    for( const auto &entry : client_map_ )
    {
        const auto &client = entry.second;
        if( nullptr == client || nullptr == client->socket ) continue;
        if( client->socket->disconnected ) continue;
        if( client->persona_id != persona_id ) continue;
        if( client->socket->socket_protocol == protocol_buddy && false == client->buddy_authenticated ) continue;
        return true;
    }
    return false;
}
