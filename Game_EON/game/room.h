#pragma once

#include <misc/types.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class eon_room
{
public:
    eon_room( uint32_t room_id, std::string name, std::string description, std::string password, uint32_t max_players, bool persistent );

    bool add_player( const ptr_client &client );
    void remove_player( uint32_t client_id );
    size_t player_count() const;
    bool full() const;
    bool password_matches( const std::string &password_value ) const;

    uint32_t room_id{ 0 };
    std::string name;
    std::string description;
    std::string password;
    uint32_t max_players{ 2 };
    bool persistent{ false };
    std::vector<std::weak_ptr<aries_client>> players;
};

using ptr_room = std::shared_ptr<eon_room>;
