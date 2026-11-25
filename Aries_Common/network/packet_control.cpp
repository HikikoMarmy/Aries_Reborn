#include "stdafx.h"
#include "../global_define.h"
#include "buddy_protocol.h"
#include "lobby_protocol.h"
#include "packet_control.h"
#include "socket_struct.h"

void aries_network::Process()
{
    sptr_socket socket;
    sptr_packet packet;

    if( false == g_socketMng->get_message( socket, packet ) ) return;

    switch( socket->socket_protocol )
    {
        case protocol_lobby: Protocol::Lobby::process_protocol( socket, packet ); break;
        case protocol_buddy: Protocol::Buddy::process_protocol( socket, packet ); break;
    }
}
