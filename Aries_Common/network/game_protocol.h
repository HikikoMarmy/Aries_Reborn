#pragma once

#include "../metadata/game_metadata.h"
#include "../misc/types.h"

class AriesGameProtocol
{
public:
    virtual ~AriesGameProtocol() = default;

    virtual const aries_game_metadata &metadata() const = 0;
    virtual bool handle_lobby_packet( ptr_client &client, sptr_packet &packet );
    virtual bool handle_buddy_packet( ptr_client &client, sptr_packet &packet );
    virtual void lobby_connected( ptr_client &client );
    virtual void lobby_disconnected( ptr_client &client );
    virtual void persona_selected( ptr_client &client );
    virtual void buddy_connected( ptr_client &client );
    virtual void buddy_disconnected( ptr_client &client );
    virtual void update();
};
