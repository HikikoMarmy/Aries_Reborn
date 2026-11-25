#include <stdafx.h>
#include <global_define.h>
#include <game/client.h>
#include <network/aries_commands.h>
#include <network/aries_packet.h>
#include <network/network_util.h>

#include "madden_protocol.h"

MaddenProtocol::MaddenProtocol()
{
    metadata_.name = "Madden NFL 2003";
    metadata_.product = "MADDEN2003";
    metadata_.sku = "SLUS_20529";
    metadata_.lobby_key_prefix = "MADDEN2003-LOCAL";
    metadata_.news_message = "Welcome to the Madden NFL 2003 private server.";
    metadata_.buddy_product = "MADDEN2003";
    metadata_.max_personas = 5;
    metadata_.session_key =
    {
        'M', 'A', 'D', 'D', 'E', 'N', '0', '3',
        '-', 'S', 'E', 'S', 'S', 'I', 'O', 'N'
    };
    metadata_.directory_response
        .set_number( "DIRECT", 1 )
        .set_number( "SESS", 1 );
}

const aries_game_metadata &MaddenProtocol::metadata() const
{
    return metadata_;
}

bool MaddenProtocol::handle_lobby_packet( ptr_client &client, sptr_packet &packet )
{
    if( packet->get_command() == AriesCommand::auth )
    {
        authenticate( client, packet );
        return true;
    }

    if( packet->get_command() == AriesCommand::acct )
    {
        account( client, packet );
        return true;
    }

    if( packet->get_command() == AriesCommand::cper )
    {
        create_persona( client, packet );
        return true;
    }

    if( packet->get_command() == AriesCommand::pers )
    {
        persona( client, packet );
        return true;
    }

    if( packet->get_command() == AriesCommand::news )
    {
        news( client, packet );
        return true;
    }

    return false;
}

void MaddenProtocol::authenticate( ptr_client &client, sptr_packet &packet )
{
    auto name = packet->get_string( "NAME" );
    if( name.empty() ) name = "Prof 1";

    client->account_id = 1;
    client->persona_id = 0;
    client->account_name = name;
    client->persona.clear();
    client->stage = stage_authenticated;

    logging.information( L"Madden profile accepted NAME=%hs", name.c_str() );

    auto response = make_packet( AriesCommand::auth, 0 );
    response->set_string( "PERSONAS", name );
    response->set_string( "MAIL", "madden@example.invalid" );
    response->set_string( "GEND", "M" );
    response->set_string( "BORN", "19700101" );
    response->set_string( "SPAM", "NN" );
    response->set_number( "TOS", 1 );
    NET_SendMessage( client->socket, response );
}

void MaddenProtocol::account( ptr_client &client, sptr_packet &packet )
{
    auto name = packet->get_string( "NAME" );
    if( name.empty() ) name = "Prof 1";

    client->account_id = 1;
    client->persona_id = 0;
    client->account_name = name;
    client->persona.clear();
    client->stage = stage_authenticated;

    logging.information( L"Madden account accepted NAME=%hs", name.c_str() );

    auto response = make_packet( AriesCommand::acct, 0 );
    NET_SendMessage( client->socket, response );
}

void MaddenProtocol::create_persona( ptr_client &client, sptr_packet &packet )
{
    auto name = packet->get_string( "PERS" );
    if( name.empty() ) name = client->account_name.empty() ? "Prof 1" : client->account_name;

    client->persona_id = 1;
    client->persona = name;
    client->stage = stage_persona;

    logging.information( L"Madden persona created PERS=%hs", name.c_str() );

    auto response = make_packet( AriesCommand::cper, 0 );
    NET_SendMessage( client->socket, response );
}

void MaddenProtocol::persona( ptr_client &client, sptr_packet &packet )
{
    auto name = packet->get_string( "PERS" );
    if( name.empty() ) name = client->account_name.empty() ? "Prof 1" : client->account_name;

    client->persona_id = 1;
    client->persona = name;
    client->lobby_key = metadata_.lobby_key_prefix + "-000001";
    client->stage = stage_persona;
    client->lobby_ping_outstanding = false;
    client->lobby_last_ping = std::chrono::steady_clock::now();

    logging.information( L"Madden persona selected PERS=%hs LKEY=%hs", client->persona.c_str(), client->lobby_key.c_str() );

    auto response = make_packet( AriesCommand::pers, 0 );
    response->set_string( "LKEY", client->lobby_key );
    response->set_string( "EX-GAME", g_configuration->service_ip );
    NET_SendMessage( client->socket, response );
}

void MaddenProtocol::news( ptr_client &client, sptr_packet &packet )
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
