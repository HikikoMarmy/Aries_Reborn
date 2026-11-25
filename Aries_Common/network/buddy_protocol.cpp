#include "stdafx.h"
#include "../global_define.h"
#include "aries_commands.h"
#include "aries_packet.h"
#include "buddy_protocol.h"
#include "game_protocol.h"
#include "network_util.h"
#include "socket_struct.h"

using BuddyFunction = void( * )( ptr_client &, sptr_packet & );

std::atomic<uint32_t> g_buddy_ping_sequence{ 1 };

const std::map<uint32_t, BuddyFunction> BUDDY_PROTOCOL =
{
    { AriesCommand::buddy_auth, &Protocol::Buddy::authenticate },
    { AriesCommand::buddy_ping, &Protocol::Buddy::ping },
    { AriesCommand::buddy_rost, &Protocol::Buddy::generic_ack },
    { AriesCommand::buddy_rget, &Protocol::Buddy::roster_get },
    { AriesCommand::buddy_radd, &Protocol::Buddy::roster_add },
    { AriesCommand::buddy_rdel, &Protocol::Buddy::roster_delete },
    { AriesCommand::buddy_send, &Protocol::Buddy::message_send },
    { AriesCommand::buddy_recv, &Protocol::Buddy::generic_ack },
    { AriesCommand::buddy_pset, &Protocol::Buddy::presence_set },
    { AriesCommand::buddy_disc, &Protocol::Buddy::disconnect }
};

static void send_presence( const ptr_client &target, const ptr_client &source, const std::string &show_override )
{
    if( nullptr == target || nullptr == source || nullptr == target->socket ) return;

    auto presence = make_packet( AriesCommand::buddy_pget, 0 );
    presence->set_string( "USER", source->persona );
    presence->set_string( "RSRC", source->buddy_resource );
    presence->set_string( "SHOW", show_override.empty() ? source->buddy_show : show_override );
    presence->set_string( "PROD", source->buddy_product );
    presence->set_string( "STAT", source->buddy_status );
    NET_SendMessage( target->socket, presence );
}

static void push_presence_to_watchers( const ptr_client &source, const std::string &show_override )
{
    if( nullptr == source || source->persona_id <= 0 || source->persona.empty() ) return;

    for( auto &watcher : g_clientMng->get_clients() )
    {
        if( nullptr == watcher || nullptr == watcher->socket ) continue;
        if( watcher.get() == source.get() ) continue;
        if( watcher->socket->socket_protocol != protocol_buddy ) continue;
        if( false == watcher->buddy_authenticated || watcher->persona_id <= 0 || watcher->socket->disconnected ) continue;

        const auto buddies = g_database->get_buddies( watcher->persona_id );
        const auto found = std::find_if( buddies.begin(), buddies.end(), [&]( const aries_buddy_record &entry )
        {
            return entry.persona_id == source->persona_id;
        } );
        if( found == buddies.end() ) continue;

        send_presence( watcher, source, show_override );
    }
}

void Protocol::Buddy::process_protocol( sptr_socket &socket, sptr_packet &packet )
{
    if( packet->is_notice() )
    {
        process_notice( socket, packet );
        return;
    }

    ptr_client client;
    if( false == g_clientMng->get_client( socket, client ) )
    {
        socket->disconnected = true;
        return;
    }

    const auto command_text = packet->command_string();
    logging.recv(
        L"%hs [0x%08X] Code: 0x%08X Length: %u <- %hs",
        command_text.c_str(),
        packet->get_command(),
        packet->get_code(),
        packet->get_length(),
        socket->ip_address.c_str()
    );

    if( g_configuration->log_packet_payloads && false == packet->payload().empty() )
    {
        logging.debug( L"Payload: %hs", packet->payload().c_str() );
    }

    if( g_configuration->log_packet_payloads ) logging.packet( packet->raw(), false );

    if( nullptr != g_gameProtocol && g_gameProtocol->handle_buddy_packet( client, packet ) ) return;

    const auto protocol_iter = BUDDY_PROTOCOL.find( packet->get_command() );
    if( protocol_iter == BUDDY_PROTOCOL.end() )
    {
        unknown( client, packet );
        return;
    }

    ( *protocol_iter->second )( client, packet );
}

void Protocol::Buddy::process_notice( sptr_socket &socket, sptr_packet &packet )
{
    ptr_client client;

    switch( packet->get_notice() )
    {
        case 1:
        {
            if( false == g_clientMng->spawn_new( socket, client ) )
            {
                socket->disconnected = true;
                return;
            }
            logging.information( L"Buddy client %u connected", client->client_id );
            if( nullptr != g_gameProtocol ) g_gameProtocol->buddy_connected( client );
        } break;

        case 200:
        {
            if( g_clientMng->get_client( socket, client ) )
            {
                logging.information( L"Buddy client %u disconnected", client->client_id );
                if( client->buddy_authenticated ) push_presence_to_watchers( client, "DISC" );
                if( nullptr != g_gameProtocol ) g_gameProtocol->buddy_disconnected( client );
            }
            g_clientMng->remove_client( socket );
        } break;
    }
}

static std::string buddy_user_name( std::string value )
{
    const auto slash = value.find( '/' );
    if( slash != std::string::npos ) value.resize( slash );
    const auto at = value.find( '@' );
    if( at != std::string::npos ) value.resize( at );
    return value;
}

static void send_buddy_error( ptr_client &client, uint32_t command )
{
    auto response = make_packet( command, MakeFourCC( 'e', 'r', 'r', 'r' ) );
    NET_SendMessage( client->socket, response );
}

void Protocol::Buddy::authenticate( ptr_client &client, sptr_packet &packet )
{
    const auto lobby_key = packet->get_string( "LKEY" );
    ptr_client lobby_client;
    if( lobby_key.empty() || false == g_clientMng->find_lobby_by_key( lobby_key, lobby_client ) )
    {
        logging.warning( L"Buddy auth rejected LKEY=%hs", lobby_key.c_str() );
        send_buddy_error( client, AriesCommand::buddy_auth );
        return;
    }

    client->account_id = lobby_client->account_id;
    client->persona_id = lobby_client->persona_id;
    client->account_name = lobby_client->account_name;
    client->persona = lobby_client->persona;
    client->lobby_key = lobby_key;
    client->buddy_authenticated = true;
    client->buddy_ping_outstanding = false;
    client->buddy_last_ping = std::chrono::steady_clock::now();

    logging.information(
        L"Buddy auth account=%lld PERS=%hs persona_id=%lld",
        static_cast<long long>( client->account_id ),
        client->persona.c_str(),
        static_cast<long long>( client->persona_id )
    );

    auto response = make_packet( AriesCommand::buddy_auth, 0 );
    NET_SendMessage( client->socket, response );
}

void Protocol::Buddy::ping( ptr_client &client, sptr_packet &packet )
{
    const auto sent_code = client->buddy_ping_code;
    const auto had_outstanding_ping = client->buddy_ping_outstanding;
    const auto now = std::chrono::steady_clock::now();

    client->buddy_ping_outstanding = false;
    client->buddy_last_ping = now;

    if( had_outstanding_ping )
    {
        if( client->buddy_ping_sent_at.time_since_epoch().count() != 0 )
        {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>( now - client->buddy_ping_sent_at ).count();
            client->buddy_last_ping_ms = static_cast<uint32_t>( std::clamp<int64_t>( elapsed, 0, 9999 ) );
        }

        return;
    }

    logging.debug(
        L"Unsolicited Buddy PING client=%u code=0x%08X",
        client->client_id,
        packet->get_code()
    );
}

void Protocol::Buddy::roster_get( ptr_client &client, sptr_packet &packet )
{
    const auto id = packet->get_number( "ID", 0 );
    const auto list = packet->get_string( "LIST" );
    const bool include_presence = packet->get_string( "PRES" ) == "Y";

    logging.information( L"Buddy roster request PERS=%hs ID=%d LIST=%hs", client->persona.c_str(), id, list.c_str() );

    std::vector<aries_buddy_record> entries;
    if( client->buddy_authenticated && client->persona_id > 0 )
    {
        if( list == "B" ) entries = g_database->get_buddies( client->persona_id );
        else if( list == "I" ) entries = g_database->get_blocks( client->persona_id );
    }

    for( const auto &entry : entries )
    {
        auto roster = make_packet( AriesCommand::buddy_rost, 0 );
        roster->set_string( "USER", entry.name );
        roster->set_number( "ID", id );
        if( list == "B" ) roster->set_string( "GROUP", entry.group_name );

        NET_SendMessage( client->socket, roster );

        if( include_presence )
        {
            ptr_client online;
            if( g_clientMng->find_online_persona( entry.persona_id, online ) )
            {
                send_presence( client, online, online->buddy_show.empty() ? "CHAT" : std::string{} );
            }
        }
    }

    auto response = make_packet( AriesCommand::buddy_rget, 0 );
    response->set_number( "ID", id );
    response->set_number( "SIZE", static_cast<int64_t>( entries.size() ) );
    NET_SendMessage( client->socket, response );
}

void Protocol::Buddy::roster_add( ptr_client &client, sptr_packet &packet )
{
    if( false == client->buddy_authenticated || client->persona_id <= 0 )
    {
        send_buddy_error( client, AriesCommand::buddy_radd );
        return;
    }

    const auto list = packet->get_string( "LIST" );
    const auto user = buddy_user_name( packet->get_string( "USER" ) );
    const auto group = packet->get_string( "GROUP" );
    aries_database_result result = aries_database_result::invalid;

    if( list == "B" ) result = g_database->add_buddy( client->persona_id, user, group );
    else if( list == "I" ) result = g_database->add_block( client->persona_id, user );

    if( result != aries_database_result::success )
    {
        logging.warning( L"Buddy RADD rejected owner=%hs USER=%hs LIST=%hs result=%d", client->persona.c_str(), user.c_str(), list.c_str(), static_cast<int>( result ) );
        send_buddy_error( client, AriesCommand::buddy_radd );
        return;
    }

    logging.information( L"Buddy RADD owner=%hs USER=%hs LIST=%hs GROUP=%hs", client->persona.c_str(), user.c_str(), list.c_str(), group.c_str() );
    auto response = make_packet( AriesCommand::buddy_radd, 0 );
    NET_SendMessage( client->socket, response );
}

void Protocol::Buddy::roster_delete( ptr_client &client, sptr_packet &packet )
{
    if( false == client->buddy_authenticated || client->persona_id <= 0 )
    {
        send_buddy_error( client, AriesCommand::buddy_rdel );
        return;
    }

    const auto list = packet->get_string( "LIST" );
    const auto user = buddy_user_name( packet->get_string( "USER" ) );
    aries_database_result result = aries_database_result::invalid;

    if( list == "B" ) result = g_database->delete_buddy( client->persona_id, user );
    else if( list == "I" ) result = g_database->delete_block( client->persona_id, user );

    if( result != aries_database_result::success && result != aries_database_result::not_found )
    {
        send_buddy_error( client, AriesCommand::buddy_rdel );
        return;
    }

    logging.information( L"Buddy RDEL owner=%hs USER=%hs LIST=%hs", client->persona.c_str(), user.c_str(), list.c_str() );
    auto response = make_packet( AriesCommand::buddy_rdel, 0 );
    NET_SendMessage( client->socket, response );
}

void Protocol::Buddy::presence_set( ptr_client &client, sptr_packet &packet )
{
    client->buddy_resource = packet->get_string( "RSRC" );
    client->buddy_show = packet->get_string( "SHOW" );
    client->buddy_status = packet->get_string( "STAT" );
    const auto default_product = nullptr != g_gameProtocol ? g_gameProtocol->metadata().buddy_product : std::string{};
    client->buddy_product = packet->get_string( "PROD", default_product );

    logging.information(
        L"Buddy presence RSRC=%hs SHOW=%hs PROD=%hs",
        client->buddy_resource.c_str(),
        client->buddy_show.c_str(),
        client->buddy_product.c_str()
    );

    auto response = make_packet( AriesCommand::buddy_pset, 0 );
    NET_SendMessage( client->socket, response );
    push_presence_to_watchers( client, {} );
}

void Protocol::Buddy::message_send( ptr_client &client, sptr_packet &packet )
{
    if( false == client->buddy_authenticated || client->persona_id <= 0 )
    {
        send_buddy_error( client, AriesCommand::buddy_send );
        return;
    }

    const auto user = buddy_user_name( packet->get_string( "USER" ) );
    const auto body = packet->get_string( "BODY" );
    aries_persona_record target_persona;
    ptr_client target_client;

    if( user.empty() || body.empty() || body.size() >= 64 || false == g_database->get_persona_by_name( user, target_persona ) ||
        false == g_clientMng->find_online_persona( target_persona.id, target_client ) )
    {
        logging.warning( L"Buddy SEND rejected from=%hs USER=%hs", client->persona.c_str(), user.c_str() );
        send_buddy_error( client, AriesCommand::buddy_send );
        return;
    }

    const auto blocks = g_database->get_blocks( target_persona.id );
    const auto blocked = std::find_if( blocks.begin(), blocks.end(), [&]( const aries_buddy_record &entry )
    {
        return entry.persona_id == client->persona_id;
    } );
    if( blocked != blocks.end() )
    {
        logging.warning( L"Buddy SEND blocked from=%hs to=%hs", client->persona.c_str(), target_persona.name.c_str() );
        send_buddy_error( client, AriesCommand::buddy_send );
        return;
    }

    auto incoming = make_packet( AriesCommand::buddy_recv, 0 );
    incoming->set_string( "USER", client->persona );
    incoming->set_string( "BODY", body );
    NET_SendMessage( target_client->socket, incoming );

    auto response = make_packet( AriesCommand::buddy_send, 0 );
    NET_SendMessage( client->socket, response );
    logging.information( L"Buddy SEND from=%hs to=%hs bytes=%zu", client->persona.c_str(), target_persona.name.c_str(), body.size() );
}

void Protocol::Buddy::disconnect( ptr_client &client, sptr_packet & )
{
    if( client->buddy_authenticated ) push_presence_to_watchers( client, "DISC" );
    auto response = make_packet( AriesCommand::buddy_disc, 0 );
    NET_SendMessage( client->socket, response );
    client->buddy_authenticated = false;
}

void Protocol::Buddy::update()
{
    if( nullptr == g_configuration || nullptr == g_clientMng ) return;
    if( g_configuration->buddy_ping_interval_seconds == 0 ) return;

    const auto now = std::chrono::steady_clock::now();
    const auto interval = std::chrono::seconds( g_configuration->buddy_ping_interval_seconds );

    for( auto &client : g_clientMng->get_clients() )
    {
        if( nullptr == client || nullptr == client->socket ) continue;
        if( client->socket->socket_protocol != protocol_buddy ) continue;
        if( false == client->buddy_authenticated ) continue;
        if( client->socket->disconnected ) continue;
        if( client->buddy_ping_outstanding ) continue;
        if( now - client->buddy_last_ping < interval ) continue;

        auto sequence = g_buddy_ping_sequence.fetch_add( 1 );
        if( sequence == 0 ) sequence = g_buddy_ping_sequence.fetch_add( 1 );

        client->buddy_ping_code = sequence;
        client->buddy_ping_outstanding = true;
        client->buddy_last_ping = now;
        client->buddy_ping_sent_at = now;

        auto ping_packet = make_packet( AriesCommand::buddy_ping, sequence );
        NET_SendMessage( client->socket, ping_packet );
    }
}

void Protocol::Buddy::generic_ack( ptr_client &client, sptr_packet &packet )
{
    auto response = make_packet( packet->get_command(), 0 );
    NET_SendMessage( client->socket, response );
}

void Protocol::Buddy::unknown( ptr_client &client, sptr_packet &packet )
{
    const auto command = packet->command_string();
    logging.warning( L"UNDEFINED BUDDY PROTOCOL: %hs [0x%08X]", command.c_str(), packet->get_command() );
    if( false == g_configuration->auto_ack_unknown ) return;
    logging.warning( L"AUTO ACK BUDDY PROTOCOL: %hs", command.c_str() );
    auto response = make_packet( packet->get_command(), 0 );
    NET_SendMessage( client->socket, response );
}
