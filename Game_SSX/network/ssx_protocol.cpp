#include <stdafx.h>
#include <global_define.h>
#include <network/aries_commands.h>
#include <network/aries_packet.h>
#include <network/network_util.h>

#include "ssx_protocol.h"

SSXProtocol::SSXProtocol()
{
    metadata_.name = "SSX 3";
    metadata_.product = "SSX-PS2-2004";
    metadata_.sku = "SLUS_20772";
    metadata_.lobby_key_prefix = "SSX-LOCAL";
    metadata_.news_message = "Welcome to the SSX 3 private server.";
    metadata_.buddy_product = "SSX-PS2-2004";
    metadata_.max_personas = 5;
    metadata_.session_key =
    {
        'S', 'S', 'X', '-', 'S', 'E', 'S', 'S',
        'I', 'O', 'N', '-', 'K', 'E', 'Y', '!'
    };
    metadata_.directory_response
        .set_number( "DIRECT", 1 )
        .set_number( "SESS", 1 );
}

const aries_game_metadata &SSXProtocol::metadata() const
{
    return metadata_;
}

bool SSXProtocol::handle_lobby_packet( ptr_client &client, sptr_packet &packet )
{
    if( packet->get_command() == AriesCommand::news )
    {
        news( client, packet );
        return true;
    }

    return false;
}

void SSXProtocol::news( ptr_client &client, sptr_packet &packet )
{
    const std::string name = packet->get_string( "NAME" );

    if( name == "0" )
    {
        auto response = make_packet( AriesCommand::news, AriesCommand::news0 );
        response->set_string( "BUDDYSERVERNAME", g_configuration->service_ip );
        response->set_number( "BUDDYPORT", g_configuration->buddy_port );
        response->set_string( "BUDDYRESOURCE", metadata_.buddy_product );
        response->set_number( "BUDDYMSGTIMEOUT", 0xD2F00 );
        NET_SendMessage( client->socket, response );
        return;
    }

    if( name == "1" )
    {
        auto response = make_packet( AriesCommand::news, AriesCommand::news1, metadata_.news_message );
        NET_SendMessage( client->socket, response );
        return;
    }

    auto response = make_packet( AriesCommand::news, 0 );
    NET_SendMessage( client->socket, response );
}
