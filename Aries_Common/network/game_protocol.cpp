#include "stdafx.h"
#include "game_protocol.h"

bool AriesGameProtocol::handle_lobby_packet( ptr_client &, sptr_packet & )
{
    return false;
}

bool AriesGameProtocol::handle_buddy_packet( ptr_client &, sptr_packet & )
{
    return false;
}

void AriesGameProtocol::lobby_connected( ptr_client & )
{
}

void AriesGameProtocol::lobby_disconnected( ptr_client & )
{
}

void AriesGameProtocol::persona_selected( ptr_client & )
{
}

void AriesGameProtocol::buddy_connected( ptr_client & )
{
}

void AriesGameProtocol::buddy_disconnected( ptr_client & )
{
}

void AriesGameProtocol::update()
{
}
