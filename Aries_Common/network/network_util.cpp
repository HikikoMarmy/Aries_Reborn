#include "stdafx.h"
#include "../global_define.h"
#include "aries_packet.h"
#include "network_util.h"
#include "socket_struct.h"

void NET_SendMessage( sptr_socket &socket, const sptr_packet &packet )
{
    if( nullptr == socket || nullptr == packet || socket->disconnected ) return;

    const auto command = packet->command_string();
    logging.send(
        L"%hs [0x%08X] Code: 0x%08X Length: %u -> %hs",
        command.c_str(),
        packet->get_command(),
        packet->get_code(),
        packet->get_length(),
        socket->ip_address.c_str()
    );

    if( g_configuration->log_packet_payloads && false == packet->payload().empty() )
    {
        logging.debug( L"Payload: %hs", packet->payload().c_str() );
    }

    if( g_configuration->log_packet_payloads ) logging.packet( packet->raw(), true );
    socket->send_queue.push( packet );
}
