#include <stdafx.h>
#include <global_define.h>
#include <network/aries_commands.h>
#include <network/aries_packet.h>
#include <network/network_util.h>
#include <network/socket_struct.h>

#include "eon_protocol.h"

static std::string eon_persona_or_account( const ptr_client &client )
{
    if( nullptr == client ) return {};
    if( false == client->persona.empty() ) return client->persona;
    return client->account_name;
}

EONProtocol::EONProtocol()
{
    metadata_.name = "007 Everything or Nothing";
    metadata_.product = "PS2-BOND-2004";
    metadata_.sku = "SLUS_20751";
    metadata_.lobby_key_prefix = "EON-LOCAL";
    metadata_.news_message = "Welcome to the 007 Everything or Nothing private server.";
    metadata_.buddy_product = "PS2-BOND-2004";
    metadata_.max_personas = 5;
    metadata_.session_key =
    {
        'E', 'O', 'N', '-', 'S', 'E', 'S', 'S',
        'I', 'O', 'N', '-', 'K', 'E', 'Y', '!'
    };
    metadata_.directory_response
        .set_number( "DIRECT", 1 )
        .set_number( "SESS", 1 );
}

const aries_game_metadata &EONProtocol::metadata() const
{
    return metadata_;
}

bool EONProtocol::handle_lobby_packet( ptr_client &client, sptr_packet &packet )
{
    switch( packet->get_command() )
    {
        case AriesCommand::sele: select( client, packet ); return true;
        case AriesCommand::room: room( client, packet ); return true;
        case AriesCommand::move: move( client, packet ); return true;
        case AriesCommand::peek: peek( client, packet ); return true;
        case AriesCommand::quik: quick_match( client, packet ); return true;
        case AriesCommand::mesg: message( client, packet ); return true;
        case AriesCommand::chal: challenge( client, packet ); return true;
        case AriesCommand::cusr: custom_user( client, packet ); return true;
        case AriesCommand::onln: online_stats( client, packet ); return true;
    }

    return false;
}

void EONProtocol::lobby_disconnected( ptr_client &client )
{
    const auto previous_room = nullptr != client ? client->room_name : std::string{};
    room_manager_.remove_client( client );
    broadcast_room_user_remove( previous_room, client );
    broadcast_room_user_snapshot( previous_room );
    broadcast_room_population( previous_room );
    forget_quick_match_request( client );
    forget_invites( client );
}

void EONProtocol::persona_selected( ptr_client &client )
{
    send_room_snapshot( client );
    send_room_user_clear( client );
}

void EONProtocol::select( ptr_client &client, sptr_packet & )
{
    auto response = make_packet( AriesCommand::sele, 0 );
    NET_SendMessage( client->socket, response );
    send_room_snapshot( client );
    send_room_user_clear( client );
}

void EONProtocol::send_room_update( ptr_client &client, const ptr_room &room )
{
    if( nullptr == client || nullptr == client->socket || nullptr == room ) return;

    auto response = make_packet( AriesCommand::room_add, 0 );
    response->set_number( "I", room->room_id );
    response->set_string( "N", room->name );
    response->set_string( "H", "" );
    response->set_string( "F", room->password.empty() ? "0" : "P" );
    response->set_number( "A", 0 );
    response->set_number( "T", static_cast<int64_t>( room->player_count() ) );
    response->set_number( "L", room->max_players );
    response->set_number( "P", 0 );
    NET_SendMessage( client->socket, response );
}

void EONProtocol::send_room_snapshot( ptr_client &client )
{
    const auto rooms = room_manager_.get_rooms();
    logging.information( L"Sending %zu lobby room record(s) to client=%u", rooms.size(), client->client_id );
    for( const auto &room : rooms ) send_room_update( client, room );
}

void EONProtocol::send_room_user( ptr_client &client, const ptr_client &user )
{
    if( nullptr == client || nullptr == client->socket ) return;
    if( nullptr == user || nullptr == user->socket || user->socket->disconnected ) return;

    const auto name = eon_persona_or_account( user );
    if( name.empty() ) return;

    auto response = make_packet( AriesCommand::user_add, 0 );
    response->set_number( "I", user->client_id );
    response->set_string( "N", name );
    response->set_number( "F", 0 );
    response->set_number( "RI", 0 );
    response->set_number( "RT", 0 );
    response->set_number( "R", 0 );
    response->set_number( "RF", 0 );
    response->set_number( "P", 0 );
    response->set_number( "A", 0 );
    response->set_string( "S", "" );
    response->set_string( "X", "" );
    response->set_number( "D", 0 );

    ptr_room room;
    const auto population = room_manager_.get_room( user->room_name, room ) && nullptr != room
        ? static_cast<int64_t>( room->player_count() )
        : 0;
    response->set_number( "T", population );
    NET_SendMessage( client->socket, response );
}

void EONProtocol::send_room_user_remove( ptr_client &client, const ptr_client &user )
{
    if( nullptr == client || nullptr == client->socket || nullptr == user ) return;

    auto response = make_packet( AriesCommand::user_add, 0 );
    response->set_number( "I", user->client_id );
    NET_SendMessage( client->socket, response );
}

void EONProtocol::send_room_user_clear( ptr_client &client )
{
    if( nullptr == client || nullptr == client->socket ) return;

    for( const auto &other : g_clientMng->get_clients() )
    {
        if( nullptr == other || other->client_id == 0 ) continue;
        send_room_user_remove( client, other );
    }
}

std::vector<ptr_client> EONProtocol::room_members( const std::string &room_name ) const
{
    std::vector<ptr_client> members;
    if( room_name.empty() ) return members;

    for( const auto &other : g_clientMng->get_clients() )
    {
        if( nullptr == other || nullptr == other->socket ) continue;
        if( other->socket->socket_protocol != protocol_lobby ) continue;
        if( other->socket->disconnected || other->room_name != room_name ) continue;
        members.push_back( other );
    }

    return members;
}

void EONProtocol::send_room_user_snapshot( ptr_client &client, const std::string &room_name )
{
    if( nullptr == client || room_name.empty() ) return;
    for( const auto &member : room_members( room_name ) ) send_room_user( client, member );
}

void EONProtocol::broadcast_room_user_snapshot( const std::string &room_name )
{
    for( auto &member : room_members( room_name ) ) send_room_user_snapshot( member, room_name );
}

void EONProtocol::broadcast_room_user_remove( const std::string &room_name, const ptr_client &user )
{
    if( nullptr == user ) return;
    for( auto &member : room_members( room_name ) ) send_room_user_remove( member, user );
}

void EONProtocol::send_room_population( ptr_client &client, const std::string &room_name )
{
    if( nullptr == client || nullptr == client->socket || room_name.empty() ) return;

    ptr_room room;
    if( false == room_manager_.get_room( room_name, room ) || nullptr == room ) return;

    std::ostringstream value;
    value << room->room_id << ' ' << room->player_count();

    auto response = make_packet( AriesCommand::population, 0 );
    response->set_string( "Z", value.str() );
    NET_SendMessage( client->socket, response );
}

void EONProtocol::broadcast_room_population( const std::string &room_name )
{
    if( room_name.empty() ) return;

    for( auto &other : g_clientMng->get_clients() )
    {
        if( nullptr == other || nullptr == other->socket ) continue;
        if( other->socket->socket_protocol != protocol_lobby ) continue;
        if( other->socket->disconnected ) continue;
        send_room_population( other, room_name );
    }
}

void EONProtocol::room( ptr_client &client, sptr_packet &packet )
{
    const auto previous_room = client->room_name;
    const auto room_name = packet->get_string( "NAME" );
    const auto description = packet->get_string( "DESC" );
    const auto password = packet->get_string( "PASS" );
    const auto max_players = static_cast<uint32_t>( std::max< int32_t >( 1, packet->get_number( "MAX", 2 ) ) );
    const auto room = room_manager_.create_room( client, room_name, description, password, max_players );

    if( nullptr == room )
    {
        auto response = make_packet( AriesCommand::room, 1 );
        NET_SendMessage( client->socket, response );
        return;
    }

    logging.information(
        L"Create room NAME=%hs DESC=%hs MAX=%u",
        room->name.c_str(),
        room->description.c_str(),
        room->max_players
    );

    auto response = make_packet( AriesCommand::room, 0 );
    response->set_string( "NAME", room->name );
    response->set_number( "COUNT", static_cast<int64_t>( room->player_count() ) );
    NET_SendMessage( client->socket, response );

    broadcast_room_user_remove( previous_room, client );
    broadcast_room_user_snapshot( previous_room );
    send_room_user_clear( client );
    broadcast_room_user_snapshot( room->name );
    broadcast_room_population( previous_room );
    broadcast_room_population( room->name );
}

void EONProtocol::move( ptr_client &client, sptr_packet &packet )
{
    const auto previous_room = client->room_name;
    const auto target_room = packet->get_string( "NAME" );
    if( target_room.empty() )
    {
        room_manager_.leave_room( client );
        auto response = make_packet( AriesCommand::move, 0 );
        response->set_number( "COUNT", 0 );
        NET_SendMessage( client->socket, response );
        broadcast_room_user_remove( previous_room, client );
        broadcast_room_user_snapshot( previous_room );
        send_room_user_clear( client );
        broadcast_room_population( previous_room );
        return;
    }

    ptr_room room;
    if( false == room_manager_.join_room( client, target_room, packet->get_string( "PASS" ), room ) )
    {
        auto response = make_packet( AriesCommand::move, 1 );
        NET_SendMessage( client->socket, response );
        return;
    }

    auto response = make_packet( AriesCommand::move, 0 );
    response->set_number( "COUNT", static_cast<int64_t>( room->player_count() ) );
    NET_SendMessage( client->socket, response );

    if( previous_room != target_room )
    {
        broadcast_room_user_remove( previous_room, client );
        broadcast_room_user_snapshot( previous_room );
        broadcast_room_population( previous_room );
    }
    send_room_user_clear( client );
    broadcast_room_user_snapshot( target_room );
    broadcast_room_population( target_room );
}

void EONProtocol::peek( ptr_client &client, sptr_packet &packet )
{
    const auto room_name = packet->get_string( "NAME", client->room_name );
    ptr_room room;

    if( false == room_manager_.get_room( room_name, room ) )
    {
        auto response = make_packet( AriesCommand::peek, 1 );
        NET_SendMessage( client->socket, response );
        return;
    }

    auto response = make_packet( AriesCommand::peek, 0 );
    response->set_string( "NAME", room->name );
    response->set_string( "DESC", room->description );
    response->set_number( "COUNT", static_cast<int64_t>( room->player_count() ) );
    response->set_number( "MAX", room->max_players );
    NET_SendMessage( client->socket, response );

    send_room_user_clear( client );
    send_room_user_snapshot( client, room->name );
}

static std::string make_match_auth( uint32_t seed )
{
    std::array<uint8_t, 16> value{};
    uint32_t state = seed ^ 0x454F4E31u;
    for( size_t i = 0; i < value.size(); ++i )
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        value[i] = static_cast<uint8_t>( state & 0xFF );
    }

    static constexpr char hex[] = "0123456789abcdef";
    std::string output;
    output.reserve( 32 );
    for( const auto byte : value )
    {
        output.push_back( hex[( byte >> 4 ) & 0x0F] );
        output.push_back( hex[byte & 0x0F] );
    }
    return output;
}

static std::string observed_address( const ptr_client &client )
{
    if( nullptr == client || nullptr == client->socket ) return {};
    return client->socket->ip_address;
}

static bool can_use_reported_addresses( const ptr_client &host, const ptr_client &opponent )
{
    if( nullptr == host || nullptr == opponent ) return false;
    if( nullptr == host->socket || nullptr == opponent->socket ) return false;
    if( host->reported_address.empty() || opponent->reported_address.empty() ) return false;
    return host->socket->ip_address == opponent->socket->ip_address;
}

static int64_t eon_match_request_key( const ptr_client &client )
{
    if( nullptr == client ) return 0;
    if( client->persona_id > 0 ) return client->persona_id;
    return -static_cast<int64_t>( client->client_id );
}

void EONProtocol::forget_quick_match_request( const ptr_client &client )
{
    if( nullptr == client ) return;

    const auto key = eon_match_request_key( client );
    const auto iter = quick_match_requests_.find( key );
    if( iter == quick_match_requests_.end() ) return;
    if( iter->second.client_id != client->client_id ) return;
    quick_match_requests_.erase( iter );
}

static ptr_client eon_find_lobby_persona( const std::string &persona )
{
    if( persona.empty() || nullptr == g_clientMng ) return nullptr;

    for( const auto &other : g_clientMng->get_clients() )
    {
        if( nullptr == other || nullptr == other->socket ) continue;
        if( other->socket->socket_protocol != protocol_lobby || other->socket->disconnected ) continue;
        if( eon_persona_or_account( other ) == persona ) return other;
    }

    return nullptr;
}

static std::string eon_control_kind( const std::string &text )
{
    const auto end = text.find_first_of( "\r\n" );
    return end == std::string::npos ? text : text.substr( 0, end );
}

static int32_t eon_control_number( const std::string &text, const std::string &name, int32_t fallback = 0 )
{
    size_t begin = 0;
    while( begin < text.size() )
    {
        const auto end = text.find_first_of( "\r\n", begin );
        const auto line = text.substr( begin, end == std::string::npos ? std::string::npos : end - begin );
        const auto equals = line.find( '=' );
        if( equals != std::string::npos && line.substr( 0, equals ) == name )
        {
            try
            {
                return std::stoi( line.substr( equals + 1 ) );
            }
            catch( ... )
            {
                return fallback;
            }
        }

        if( end == std::string::npos ) break;
        begin = end + 1;
        while( begin < text.size() && ( text[begin] == '\r' || text[begin] == '\n' ) ) ++begin;
    }

    return fallback;
}

static std::string eon_encode_message_value( const std::string &value )
{
    static constexpr char hex[] = "0123456789abcdef";
    std::string output;
    output.reserve( value.size() );

    for( const char c : value )
    {
        const auto byte = static_cast<unsigned char>( c );
        if( byte <= 0x20 || c == '%' || c == '"' || c == '=' )
        {
            output.push_back( '%' );
            output.push_back( hex[( byte >> 4 ) & 0x0F] );
            output.push_back( hex[byte & 0x0F] );
        }
        else
        {
            output.push_back( c );
        }
    }

    return output;
}

static sptr_packet eon_make_challenge_message( const std::string &from, const std::string &text )
{
    const auto encoded = eon_encode_message_value( text );
    std::string payload;
    payload.reserve( from.size() + encoded.size() + 16 );
    payload += "N=";
    payload += from;
    payload += "\nT=";
    payload += encoded;
    payload += "\nF=P3\n";
    return make_packet( AriesCommand::message_add, 0, payload );
}

void EONProtocol::forget_invites( const ptr_client &client )
{
    if( nullptr == client ) return;
    const auto key = eon_match_request_key( client );

    for( auto iter = pending_invites_.begin(); iter != pending_invites_.end(); )
    {
        if( iter->second.inviter_key == key || iter->second.target_key == key ) iter = pending_invites_.erase( iter );
        else ++iter;
    }
}

void EONProtocol::message( ptr_client &client, sptr_packet &packet )
{
    if( nullptr == client || nullptr == client->socket ) return;

    const auto text = packet->get_string( "TEXT" );
    const auto target_name = packet->get_string( "PRIV" );
    const auto source_name = eon_persona_or_account( client );
    const auto attr = packet->get_number( "ATTR", 0 );

    if( attr == 3 && false == text.empty() && false == target_name.empty() && target_name != source_name )
    {
        auto target = eon_find_lobby_persona( target_name );
        if( nullptr != target && nullptr != target->socket && false == target->socket->disconnected )
        {
            const auto source_key = eon_match_request_key( client );
            const auto target_key = eon_match_request_key( target );
            const auto kind = eon_control_kind( text );

            if( kind == "challenge" )
            {
                pending_invites_.erase( { target_key, source_key } );

                eon_pending_invite invite;
                invite.inviter_key = source_key;
                invite.target_key = target_key;
                invite.inviter = source_name;
                invite.target = target_name;
                invite.game_type = static_cast<uint32_t>( std::max( 0, eon_control_number( text, "TYPE", 0 ) ) );
                invite.inviter_character = static_cast<uint32_t>( std::max( 0, eon_control_number( text, "GLFR", 0 ) ) );
                pending_invites_[{ source_key, target_key }] = invite;
            }
            else if( kind == "accept" )
            {
                const auto iter = pending_invites_.find( { target_key, source_key } );
                if( iter != pending_invites_.end() )
                {
                    iter->second.target_character = static_cast<uint32_t>( std::max( 0, eon_control_number( text, "GLFR", 0 ) ) );
                }
            }
            else if( kind == "decline" || kind == "busy" || kind == "revoke" )
            {
                pending_invites_.erase( { source_key, target_key } );
                pending_invites_.erase( { target_key, source_key } );
            }

            auto forwarded = eon_make_challenge_message( source_name, text );
            NET_SendMessage( target->socket, forwarded );

            logging.information(
                L"EON invite message TEXT=%hs FROM=%hs TO=%hs",
                kind.c_str(),
                source_name.c_str(),
                target_name.c_str()
            );
        }
        else
        {
            logging.warning( L"EON invite target not online FROM=%hs TO=%hs", source_name.c_str(), target_name.c_str() );
        }
    }

    auto response = make_packet( AriesCommand::mesg, 0 );
    NET_SendMessage( client->socket, response );
}

void EONProtocol::challenge( ptr_client &client, sptr_packet &packet )
{
    if( nullptr == client || nullptr == client->socket ) return;

    const auto persona = eon_persona_or_account( client );
    const auto target_name = packet->get_string( "PERS" );

    logging.information(
        L"EON challenge request client=%u persona=%hs PERS=%hs HOST=%d",
        client->client_id,
        persona.c_str(),
        target_name.c_str(),
        packet->get_number( "HOST", -1 )
    );

    if( target_name == "*" )
    {
        forget_invites( client );
        auto response = make_packet( AriesCommand::chal, 0 );
        response->set_string( "MODE", "idle" );
        NET_SendMessage( client->socket, response );
        return;
    }

    auto target = eon_find_lobby_persona( target_name );
    if( nullptr == target )
    {
        auto response = make_packet( AriesCommand::chal, 0 );
        response->set_string( "MODE", "idle" );
        NET_SendMessage( client->socket, response );
        return;
    }

    const auto source_key = eon_match_request_key( client );
    const auto target_key = eon_match_request_key( target );
    auto iter = pending_invites_.find( { source_key, target_key } );
    if( iter == pending_invites_.end() ) iter = pending_invites_.find( { target_key, source_key } );

    if( iter == pending_invites_.end() )
    {
        auto response = make_packet( AriesCommand::chal, 0 );
        response->set_string( "MODE", "idle" );
        NET_SendMessage( client->socket, response );
        return;
    }

    auto &invite = iter->second;
    if( source_key == invite.inviter_key ) invite.inviter_ready = true;
    if( source_key == invite.target_key ) invite.target_ready = true;

    auto response = make_packet( AriesCommand::chal, 0 );
    response->set_string( "MODE", "chal" );
    NET_SendMessage( client->socket, response );

    if( false == invite.inviter_ready || false == invite.target_ready ) return;

    auto host = eon_find_lobby_persona( invite.inviter );
    auto opponent = eon_find_lobby_persona( invite.target );
    const auto completed = invite;
    pending_invites_.erase( iter );

    if( nullptr == host || nullptr == opponent ) return;
    send_invite_session( host, opponent, completed );
}

static std::vector<uint8_t> eon_make_session_p1( uint32_t game_type, uint32_t player_one_character, uint32_t player_two_character )
{
    const std::array<uint32_t, 3> values{ game_type, player_one_character, player_two_character };
    std::vector<uint8_t> output;
    output.reserve( 12 );

    for( const auto value : values )
    {
        output.push_back( static_cast<uint8_t>( value & 0xFF ) );
        output.push_back( static_cast<uint8_t>( ( value >> 8 ) & 0xFF ) );
        output.push_back( static_cast<uint8_t>( ( value >> 16 ) & 0xFF ) );
        output.push_back( static_cast<uint8_t>( ( value >> 24 ) & 0xFF ) );
    }

    return output;
}

void EONProtocol::send_invite_session( ptr_client &host, ptr_client &opponent, const eon_pending_invite &invite )
{
    if( nullptr == host || nullptr == opponent || nullptr == host->socket || nullptr == opponent->socket ) return;

    const auto host_persona = eon_persona_or_account( host );
    const auto opponent_persona = eon_persona_or_account( opponent );
    if( host_persona.empty() || opponent_persona.empty() || host_persona == opponent_persona ) return;

    auto sequence = match_sequence_.fetch_add( 1 );
    if( sequence == 0 ) sequence = match_sequence_.fetch_add( 1 );

    const auto epoch = static_cast<uint32_t>( std::time( nullptr ) );
    auto seed = ( epoch ^ ( sequence * 0x9E3779B9u ) ) & 0x7FFFFFFFu;
    if( seed == 0 ) seed = 1;

    const auto auth = make_match_auth( seed );
    const auto host_observed_address = observed_address( host );
    const auto opponent_observed_address = observed_address( opponent );
    const auto use_reported_addresses = can_use_reported_addresses( host, opponent );
    const auto host_address = use_reported_addresses ? host->reported_address : host_observed_address;
    const auto opponent_address = use_reported_addresses ? opponent->reported_address : opponent_observed_address;
    if( host_address.empty() || opponent_address.empty() ) return;

    std::ostringstream session_stream;
    session_stream << host_persona << '-' << opponent_persona << '-'
                   << std::hex << std::setw( 8 ) << std::setfill( '0' ) << epoch;
    const auto session_name = session_stream.str();
    const auto p1 = eon_make_session_p1( invite.game_type, invite.inviter_character, invite.target_character );

    auto send_session = [&]( ptr_client &recipient, const std::string &self, const std::string &peer, const std::string &remote, const std::string &local )
    {
        auto session = make_packet( AriesCommand::session, 0 );
        session->set_string( "NAME", "Challenge" );
        session->set_string( "SELF", self );
        session->set_string( "HOST", host_persona );
        session->set_string( "OPPO", peer );
        session->set_binary( "P1", p1 );
        session->set_string( "P2", "" );
        session->set_string( "P3", "" );
        session->set_string( "P4", "" );
        session->set_string( "AUTH", auth );
        session->set_string( "ADDR", remote );
        session->set_string( "FROM", local );
        session->set_number( "SEED", seed );
        session->set_epoch( "WHEN", epoch );
        session->set_string( "SESS", session_name );
        NET_SendMessage( recipient->socket, session );
    };

    logging.information(
        L"EON invite match started SESS=%hs HOST=%hs OPPO=%hs TYPE=%u GLFR=%u/%u",
        session_name.c_str(),
        host_persona.c_str(),
        opponent_persona.c_str(),
        invite.game_type,
        invite.inviter_character,
        invite.target_character
    );

    send_session( host, host_persona, opponent_persona, opponent_address, host_address );
    send_session( opponent, opponent_persona, host_persona, host_address, opponent_address );
}

void EONProtocol::send_quick_match_session( ptr_client &host, ptr_client &opponent )
{
    if( nullptr == host || nullptr == opponent ) return;
    if( nullptr == host->socket || nullptr == opponent->socket ) return;

    const auto host_persona = eon_persona_or_account( host );
    const auto opponent_persona = eon_persona_or_account( opponent );
    if( host_persona.empty() || opponent_persona.empty() )
    {
        logging.warning( L"EON Quick Match cannot start because a client has no persona" );
        return;
    }

    if( host_persona == opponent_persona )
    {
        logging.warning( L"EON Quick Match rejected duplicate persona %hs", host_persona.c_str() );
        return;
    }

    const auto host_request_iter = quick_match_requests_.find( eon_match_request_key( host ) );
    const auto opponent_request_iter = quick_match_requests_.find( eon_match_request_key( opponent ) );
    if( host_request_iter == quick_match_requests_.end() || opponent_request_iter == quick_match_requests_.end() ) return;
    if( host_request_iter->second.criteria != opponent_request_iter->second.criteria ) return;
    const auto criteria = host_request_iter->second.criteria;

    auto sequence = match_sequence_.fetch_add( 1 );
    if( sequence == 0 ) sequence = match_sequence_.fetch_add( 1 );

    const auto epoch = static_cast<uint32_t>( std::time( nullptr ) );
    auto seed = ( epoch ^ ( sequence * 0x9E3779B9u ) ) & 0x7FFFFFFFu;
    if( seed == 0 ) seed = 1;
    const auto auth = make_match_auth( seed );

    const auto host_observed_address = observed_address( host );
    const auto opponent_observed_address = observed_address( opponent );
    const auto use_reported_addresses = can_use_reported_addresses( host, opponent );
    const auto host_address = use_reported_addresses ? host->reported_address : host_observed_address;
    const auto opponent_address = use_reported_addresses ? opponent->reported_address : opponent_observed_address;

    if( host_address.empty() || opponent_address.empty() )
    {
        logging.warning( L"EON Quick Match cannot start because a peer address is missing" );
        return;
    }

    std::ostringstream session_stream;
    session_stream << host_persona << '-' << opponent_persona << '-'
                   << std::hex << std::setw( 8 ) << std::setfill( '0' ) << epoch;
    const auto session_name = session_stream.str();

    auto send_session = [&]( ptr_client &recipient, const std::string &self, const std::string &peer )
    {
        auto session = make_packet( AriesCommand::session, 0 );
        session->set_string( "NAME", "Quick Match" );
        session->set_string( "SELF", self );
        session->set_string( "HOST", host_persona );
        session->set_string( "OPPO", peer );
        session->set_string( "P1", "" );
        session->set_string( "P2", "" );
        session->set_string( "P3", "" );
        session->set_string( "P4", "" );
        session->set_string( "AUTH", auth );
        session->set_string( "ADDR", opponent_address );
        session->set_string( "FROM", host_address );
        session->set_number( "SEED", seed );
        session->set_epoch( "WHEN", epoch );
        session->set_string( "SESS", session_name );
        NET_SendMessage( recipient->socket, session );
    };

    forget_quick_match_request( host );
    forget_quick_match_request( opponent );

    logging.information(
        L"EON match started SESS=%hs HOST=%hs OPPO=%hs RQ=%hs",
        session_name.c_str(),
        host_persona.c_str(),
        opponent_persona.c_str(),
        criteria.c_str()
    );
    logging.information(
        L"EON endpoints host=%hs opponent=%hs source=%ls",
        host_address.c_str(),
        opponent_address.c_str(),
        use_reported_addresses ? L"reported LAN" : L"observed public"
    );

    send_session( host, host_persona, opponent_persona );
    send_session( opponent, opponent_persona, host_persona );
}

void EONProtocol::quick_match( ptr_client &client, sptr_packet &packet )
{
    if( packet->has_field( "KIND" ) )
    {
        forget_quick_match_request( client );
        logging.information(
            L"EON Quick Match request withdrawn client=%u persona=%hs KIND=%hs",
            client->client_id,
            eon_persona_or_account( client ).c_str(),
            packet->get_string( "KIND" ).c_str()
        );

        auto response = make_packet( AriesCommand::quik, 0 );
        NET_SendMessage( client->socket, response );
        return;
    }

    const auto persona = eon_persona_or_account( client );
    if( persona.empty() )
    {
        auto response = make_packet( AriesCommand::quik, MakeFourCC( 'e', 'r', 'r', 'r' ) );
        NET_SendMessage( client->socket, response );
        return;
    }

    eon_quick_match_request request;
    request.client_id = client->client_id;
    request.criteria = packet->get_string( "RQ" );
    if( false == packet->has_field( "RQ" ) )
    {
        logging.warning( L"EON Quick Match rejected invalid RQ client=%u persona=%hs", client->client_id, persona.c_str() );
        auto response = make_packet( AriesCommand::quik, MakeFourCC( 'e', 'r', 'r', 'r' ) );
        NET_SendMessage( client->socket, response );
        return;
    }
    quick_match_requests_[eon_match_request_key( client )] = request;

    logging.information(
        L"EON Quick Match request accepted client=%u persona=%hs RQ=%hs",
        client->client_id,
        persona.c_str(),
        request.criteria.c_str()
    );

    auto response = make_packet( AriesCommand::quik, 0 );
    NET_SendMessage( client->socket, response );

    ptr_client waiting_client;
    uint32_t waiting_clients = 0;
    for( const auto &other : g_clientMng->get_clients() )
    {
        if( nullptr == other || nullptr == other->socket ) continue;
        if( other->socket->socket_protocol != protocol_lobby ) continue;
        if( other->socket->disconnected ) continue;

        const auto key = eon_match_request_key( other );
        const auto request_iter = quick_match_requests_.find( key );
        if( request_iter == quick_match_requests_.end() ) continue;
        if( request_iter->second.client_id != other->client_id ) continue;

        ++waiting_clients;
        if( other.get() == client.get() ) continue;
        if( client->persona_id > 0 && other->persona_id == client->persona_id ) continue;
        if( request_iter->second.criteria != request.criteria ) continue;
        if( nullptr == waiting_client ) waiting_client = other;
    }

    logging.information( L"EON Quick Match queue now has %u client(s)", waiting_clients );
    if( nullptr != waiting_client ) send_quick_match_session( waiting_client, client );
}

void EONProtocol::custom_user( ptr_client &client, sptr_packet &packet )
{
    const auto command = packet->get_string( "CMD" );

    if( command == "myrnk" )
    {
        auto rankings = g_database->get_title_blob( client->persona_id, metadata_.product, "rank" );
        if( rankings.size() != 0x48 ) rankings.assign( 0x48, 0 );
        auto response = make_packet( AriesCommand::cusr, 0 );
        response->set_binary( "RNKRS", rankings );
        NET_SendMessage( client->socket, response );
        return;
    }

    auto response = make_packet( AriesCommand::cusr, 0 );
    NET_SendMessage( client->socket, response );
}

void EONProtocol::online_stats( ptr_client &client, sptr_packet & )
{
    auto response = make_packet( AriesCommand::onln, 0 );
    response->set_string( "S", "" );
    response->set_number( "R", 0 );
    response->set_number( "P", 0 );
    NET_SendMessage( client->socket, response );
}
