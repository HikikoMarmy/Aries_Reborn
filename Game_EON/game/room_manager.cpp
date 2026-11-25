#include "stdafx.h"
#include <game/client.h>
#include "room.h"
#include "room_manager.h"

EONRoomManager::EONRoomManager()
{
    create_system_rooms();
}

void EONRoomManager::create_system_rooms()
{
    const std::array<std::string, 4> names =
    {
        "Coop.E.Lobby 1",
        "Coop.W.Lobby 2",
        "Coop.B.Lobby 3",
        "Coop.A.Lobby 4"
    };

    uint32_t room_id = 1;
    for( const auto &name : names )
    {
        rooms_.insert( { name, std::make_shared<eon_room>( room_id++, name, "Cooperative Lobby", "", 50, true ) } );
    }
}

ptr_room EONRoomManager::create_room( ptr_client &client, const std::string &room_name, const std::string &description, const std::string &password, uint32_t max_players )
{
    if( nullptr == client ) return nullptr;

    std::lock_guard lock( mutex_ );

    const auto room_id = next_room_id_.fetch_add( 1 );
    const auto final_room_name = room_name.empty() ? "Coop.C.Room_" + std::to_string( room_id ) : room_name;
    if( rooms_.contains( final_room_name ) ) return nullptr;

    for( auto &[name, room] : rooms_ )
    {
        room->remove_player( client->client_id );
    }

    auto room = std::make_shared<eon_room>( room_id, final_room_name, description, password, max_players, false );
    room->add_player( client );
    rooms_.insert( { final_room_name, room } );
    client->room_name = final_room_name;
    remove_empty_rooms();
    return room;
}

bool EONRoomManager::get_room( const std::string &name, ptr_room &room )
{
    std::lock_guard lock( mutex_ );
    const auto iter = rooms_.find( name );
    if( iter == rooms_.end() ) return false;
    room = iter->second;
    return true;
}

bool EONRoomManager::join_room( ptr_client &client, const std::string &name, const std::string &password, ptr_room &room )
{
    if( nullptr == client ) return false;

    std::lock_guard lock( mutex_ );
    const auto iter = rooms_.find( name );
    if( iter == rooms_.end() ) return false;
    if( false == iter->second->password_matches( password ) ) return false;
    if( false == iter->second->add_player( client ) ) return false;

    for( auto &[other_name, other_room] : rooms_ )
    {
        if( other_name != name ) other_room->remove_player( client->client_id );
    }

    client->room_name = name;
    room = iter->second;
    remove_empty_rooms();
    return true;
}

std::vector<ptr_room> EONRoomManager::get_rooms()
{
    std::lock_guard lock( mutex_ );
    std::vector<ptr_room> result;
    result.reserve( rooms_.size() );
    for( const auto &[name, room] : rooms_ ) result.push_back( room );
    return result;
}

void EONRoomManager::leave_room( ptr_client &client )
{
    if( nullptr == client ) return;

    std::lock_guard lock( mutex_ );
    for( auto &[name, room] : rooms_ ) room->remove_player( client->client_id );
    client->room_name.clear();
    remove_empty_rooms();
}

void EONRoomManager::remove_client( ptr_client &client )
{
    leave_room( client );
}

void EONRoomManager::remove_empty_rooms()
{
    for( auto iter = rooms_.begin(); iter != rooms_.end(); )
    {
        if( false == iter->second->persistent && iter->second->player_count() == 0 ) iter = rooms_.erase( iter );
        else ++iter;
    }
}
