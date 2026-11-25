#pragma once

#include "../misc/types.h"

namespace Protocol::Buddy
{
    void process_protocol( sptr_socket &socket, sptr_packet &packet );
    void process_notice( sptr_socket &socket, sptr_packet &packet );

    void authenticate( ptr_client &client, sptr_packet &packet );
    void ping( ptr_client &client, sptr_packet &packet );
    void roster_get( ptr_client &client, sptr_packet &packet );
    void roster_add( ptr_client &client, sptr_packet &packet );
    void roster_delete( ptr_client &client, sptr_packet &packet );
    void presence_set( ptr_client &client, sptr_packet &packet );
    void message_send( ptr_client &client, sptr_packet &packet );
    void disconnect( ptr_client &client, sptr_packet &packet );
    void update();
    void generic_ack( ptr_client &client, sptr_packet &packet );
    void unknown( ptr_client &client, sptr_packet &packet );
}
