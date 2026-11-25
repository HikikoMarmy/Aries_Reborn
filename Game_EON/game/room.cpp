#include "stdafx.h"
#include <game/client.h>
#include "room.h"

eon_room::eon_room( uint32_t room_id_value, std::string name_value, std::string description_value, std::string password_value, uint32_t max_players_value, bool persistent_value )
    : room_id( room_id_value ),
      name( std::move( name_value ) ),
      description( std::move( description_value ) ),
      password( std::move( password_value ) ),
      max_players( std::max< uint32_t >( 1, max_players_value ) ),
      persistent( persistent_value )
{
}

bool eon_room::add_player( const ptr_client &client )
{
    if( nullptr == client ) return false;

    players.erase(
        std::remove_if( players.begin(), players.end(), []( const auto &player ) { return player.expired(); } ),
        players.end()
    );

    for( const auto &player : players )
    {
        const auto value = player.lock();
        if( value && value->client_id == client->client_id ) return true;
    }

    if( players.size() >= max_players ) return false;
    players.push_back( client );
    return true;
}

void eon_room::remove_player( uint32_t client_id )
{
    players.erase(
        std::remove_if(
            players.begin(),
            players.end(),
            [client_id]( const auto &player )
            {
                const auto value = player.lock();
                return false == static_cast<bool>( value ) || value->client_id == client_id;
            }
        ),
        players.end()
    );
}

size_t eon_room::player_count() const
{
    size_t count = 0;
    for( const auto &player : players )
    {
        if( false == player.expired() ) ++count;
    }
    return count;
}

bool eon_room::full() const
{
    return player_count() >= max_players;
}

bool eon_room::password_matches( const std::string &password_value ) const
{
    return password.empty() || password == password_value;
}
