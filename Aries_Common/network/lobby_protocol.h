#pragma once

#include "../misc/types.h"

namespace Protocol::Lobby
{
    void process_protocol( sptr_socket &socket, sptr_packet &packet );
    void process_notice( sptr_socket &socket, sptr_packet &packet );

    void directory( ptr_client &client, sptr_packet &packet );
    void address( ptr_client &client, sptr_packet &packet );
    void session_key( ptr_client &client, sptr_packet &packet );
    void select( ptr_client &client, sptr_packet &packet );
    void authenticate( ptr_client &client, sptr_packet &packet );
    void account( ptr_client &client, sptr_packet &packet );
    void create_persona( ptr_client &client, sptr_packet &packet );
    void delete_persona( ptr_client &client, sptr_packet &packet );
    void change_password( ptr_client &client, sptr_packet &packet );
    void edit_account( ptr_client &client, sptr_packet &packet );
    void news( ptr_client &client, sptr_packet &packet );
    void persona( ptr_client &client, sptr_packet &packet );
    void ping( ptr_client &client, sptr_packet &packet );
    void update();
    void generic_ack( ptr_client &client, sptr_packet &packet );
    void unknown( ptr_client &client, sptr_packet &packet );
}
