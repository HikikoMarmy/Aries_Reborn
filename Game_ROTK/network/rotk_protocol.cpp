#include <stdafx.h>
#include <global_define.h>
#include <game/client.h>
#include <network/aries_commands.h>
#include <network/aries_packet.h>
#include <network/network_util.h>
#include <network/socket_struct.h>

#include <set>
#include <sstream>

#include "rotk_protocol.h"

namespace
{
    std::string rotk_encode_unquoted_message_value( const std::string &value );
}

static std::string rotk_match_start_key( const std::string &left, const std::string &right );

ROTKProtocol::ROTKProtocol()
{
    metadata_.name = "The Lord of the Rings: The Return of the King";
    metadata_.product = "PS2_ROTK_2004";
    metadata_.sku = "SLUS_20770";
    metadata_.lobby_key_prefix = "ROTK-LOCAL";
    metadata_.news_message = "Welcome to the Return of the King private server.";
    metadata_.buddy_product = "ROTK-PS2-2004";
    metadata_.max_personas = 5;
    metadata_.session_key =
    {
        'R', 'O', 'T', 'K', '-', 'S', 'E', 'S',
        'S', 'I', 'O', 'N', '-', 'K', 'E', 'Y'
    };
    metadata_.directory_response
        .set_number( "DIRECT", 1 )
        .set_number( "SESS", 1 );

    create_system_rooms();
}

const aries_game_metadata &ROTKProtocol::metadata() const
{
    return metadata_;
}

bool ROTKProtocol::handle_lobby_packet( ptr_client &client, sptr_packet &packet )
{
    return handle_shared_packet( client, packet );
}

bool ROTKProtocol::handle_buddy_packet( ptr_client &client, sptr_packet &packet )
{
    return handle_shared_packet( client, packet );
}

void ROTKProtocol::lobby_disconnected( ptr_client &client )
{
    forget_client( client );
}

void ROTKProtocol::buddy_disconnected( ptr_client &client )
{
    forget_client( client );
}

void ROTKProtocol::persona_selected( ptr_client &client )
{
    send_lobby_snapshot( client );
}

bool ROTKProtocol::handle_shared_packet( ptr_client &client, sptr_packet &packet )
{
    switch( packet->get_command() )
    {
        case AriesCommand::sele: select( client, packet ); return true;
        case AriesCommand::room: room( client, packet ); return true;
        case AriesCommand::move: move( client, packet ); return true;
        case AriesCommand::peek: peek( client, packet ); return true;
        case AriesCommand::onln: online_stats( client, packet ); return true;
        case AriesCommand::rank: ranking_results( client, packet ); return true;
        case AriesCommand::cusr: custom_user( client, packet ); return true;
        case AriesCommand::quik: quick_match( client, packet ); return true;
        case AriesCommand::chal: challenge( client, packet ); return true;
        case AriesCommand::mesg: message( client, packet ); return true;
    }

    return false;
}


static std::string rotk_default_room_description( const std::string &room_name )
{
    if( room_name.rfind( "Coop.", 0 ) == 0 ) return "Cooperative Lobby";
    if( room_name.rfind( "Match.", 0 ) == 0 ) return "Match Lobby";
    if( room_name.rfind( "Stroke.", 0 ) == 0 ) return "Stroke Lobby";
    return "Return of the King Lobby";
}

static uint32_t rotk_clamp_max_players( int32_t requested )
{
    return static_cast<uint32_t>( std::clamp<int32_t>( requested <= 0 ? 4 : requested, 1, 50 ) );
}

static std::string rotk_clean_room_suffix( std::string value )
{
    auto first = value.find_first_not_of( " \t\r\n" );
    if( first == std::string::npos ) return {};
    auto last = value.find_last_not_of( " \t\r\n" );
    value = value.substr( first, last - first + 1 );

    for( auto &c : value )
    {
        if( c == ' ' || c == '\t' || c == '\r' || c == '\n' ) c = '_';
    }
    return value;
}

static const std::vector<std::string> &rotk_coop_lobby_names()
{
    // ROTK's inherited EA/Tiger lobby frontend filters visible co-op buckets by
    // the single-letter category between "Coop." and the displayed suffix.
    // Client-created rooms still use Coop.C.<name>, but the four built-in rows
    // need to live under the frontend's E/W/B/A buckets or the game-room screen
    // has nothing to resolve and reports an EA server/lobby failure.
    static const std::vector<std::string> names =
    {
        "Coop.E.Easy",
        "Coop.W.Normal",
        "Coop.B.Hard",
        "Coop.A.Expert"
    };
    return names;
}

static std::string persona_or_account( const ptr_client &client );

void ROTKProtocol::create_system_rooms()
{
    std::lock_guard lock( room_mutex_ );
    for( const auto &name : rotk_coop_lobby_names() )
    {
        rotk_room_state room;
        room.id = next_room_id_.fetch_add( 1 );
        room.name = name;
        room.description = rotk_clean_room_suffix( name.substr( 7 ) ) + " co-op lobby";
        room.max_players = 50;
        room.persistent = true;
        rooms_.insert( { room.name, room } );
    }
}

uint32_t ROTKProtocol::room_population( const std::string &room_name ) const
{
    uint32_t count = 0;
    for( const auto &other : g_clientMng->get_clients() )
    {
        if( nullptr == other || nullptr == other->socket ) continue;
        if( other->socket->disconnected ) continue;
        if( other->room_name == room_name ) ++count;
    }
    return count;
}

bool ROTKProtocol::get_room_state( const std::string &room_name, rotk_room_state &room )
{
    std::lock_guard lock( room_mutex_ );
    const auto iter = rooms_.find( room_name );
    if( iter == rooms_.end() ) return false;
    room = iter->second;
    return true;
}

std::vector<ROTKProtocol::rotk_room_state> ROTKProtocol::get_room_snapshot()
{
    std::lock_guard lock( room_mutex_ );
    std::vector<rotk_room_state> custom_rooms;
    std::vector<rotk_room_state> system_rooms;
    custom_rooms.reserve( rooms_.size() );
    system_rooms.reserve( rooms_.size() );

    for( const auto &[name, room] : rooms_ )
    {
        if( room.persistent ) continue;
        custom_rooms.push_back( room );
    }

    for( const auto &[name, room] : rooms_ )
    {
        if( false == room.persistent ) continue;
        system_rooms.push_back( room );
    }

    std::sort( custom_rooms.begin(), custom_rooms.end(), []( const auto &left, const auto &right )
    {
        return left.id < right.id;
    } );
    std::sort( system_rooms.begin(), system_rooms.end(), []( const auto &left, const auto &right )
    {
        return left.id < right.id;
    } );

    custom_rooms.insert( custom_rooms.end(), system_rooms.begin(), system_rooms.end() );
    return custom_rooms;
}

void ROTKProtocol::remove_empty_custom_rooms()
{
    std::vector<rotk_room_state> removed_rooms;

    {
        std::lock_guard lock( room_mutex_ );
        for( auto iter = rooms_.begin(); iter != rooms_.end(); )
        {
            if( false == iter->second.persistent && room_population( iter->first ) == 0 )
            {
                removed_rooms.push_back( iter->second );
                iter = rooms_.erase( iter );
            }
            else
            {
                ++iter;
            }
        }
    }

    for( const auto &room : removed_rooms )
    {
        logging.information( L"ROTK custom room removed NAME=%hs ID=%u", room.name.c_str(), room.id );
        broadcast_lobby_room_remove( room );
    }
}

void ROTKProtocol::leave_room( ptr_client &client )
{
    if( nullptr == client ) return;
    const auto room_name = client->room_name;
    if( room_name.empty() ) return;

    const auto previous_members = room_members( room_name );
    const auto persona = persona_or_account( client );
    broadcast_room_log_message( room_name, persona, "Left the room" );
    client->room_name.clear();

    for( const auto &viewer : previous_members )
    {
        if( nullptr == viewer || nullptr == viewer->socket || viewer->socket->disconnected ) continue;
        auto viewer_client = viewer;
        for( const auto &old_user : previous_members ) send_room_user_remove( viewer_client, old_user );
    }

    broadcast_room_population( room_name );
    broadcast_room_user_snapshot( room_name );
    remove_empty_custom_rooms();
}

void ROTKProtocol::remove_client_room_state( const ptr_client &client )
{
    if( nullptr == client ) return;
    const auto room_name = client->room_name;
    if( room_name.empty() ) return;

    const auto persona = persona_or_account( client );
    broadcast_room_log_message( room_name, persona, "Left the room" );
    client->room_name.clear();
    broadcast_room_user_remove( room_name, client );
    broadcast_room_population( room_name );
    broadcast_room_user_snapshot( room_name );
    remove_empty_custom_rooms();
}

void ROTKProtocol::select( ptr_client &client, sptr_packet & )
{
    auto response = make_packet( AriesCommand::sele, 0 );
    NET_SendMessage( client->socket, response );
    send_lobby_snapshot( client );
    send_room_user_clear( client );
    if( false == client->room_name.empty() ) send_room_user_snapshot( client, client->room_name );
}

void ROTKProtocol::send_lobby_room( ptr_client &client, const rotk_room_state &room )
{
    if( nullptr == client || nullptr == client->socket ) return;

    auto response = make_packet( AriesCommand::room_add, 0 );
    response->set_number( "I", room.id );
    response->set_string( "N", room.name );
    response->set_string( "H", "" );
    // ROTK decodes room flags with the same lobby flag alphabet used by
    // challenge messages.  The password/private bit is P (0x00010000).
    // Sending decimal 65536 leaves the frontend unaware that it should open
    // the room password prompt before it sends move PASS=... .
    response->set_string( "F", room.password.empty() ? "0" : "P" );
    response->set_number( "A", 0 );
    // ROTK uses T here as the number of active games inside this lobby bucket,
    // not the visible player count. Sending the room population made the header
    // render as a bogus "Online Games: 1/4" style value. Player counts are
    // supplied through peek/move COUNT and +usr/+pop instead.
    response->set_number( "T", 0 );
    response->set_number( "L", room.max_players );
    response->set_number( "P", room.password.empty() ? 0 : 1 );
    NET_SendMessage( client->socket, response );
}

struct rotk_profile_stats
{
    uint32_t level{ 1 };
    uint32_t online_games{ 0 };
    uint32_t disconnects{ 0 };
    uint32_t experience_points{ 0 };
    uint32_t kills{ 0 };
};

static std::vector<std::string> rotk_split_csv( const std::string &value )
{
    std::vector<std::string> output;
    std::string current;

    for( const auto c : value )
    {
        if( c == ',' )
        {
            output.push_back( current );
            current.clear();
        }
        else current.push_back( c );
    }

    output.push_back( current );
    return output;
}

static uint32_t rotk_parse_u32( const std::string &value, uint32_t fallback = 0 )
{
    if( value.empty() ) return fallback;

    try
    {
        return static_cast<uint32_t>( std::stoul( value, nullptr, 0 ) );
    }
    catch( ... )
    {
        return fallback;
    }
}

static std::vector<uint8_t> rotk_text_blob( const std::string &value )
{
    return std::vector<uint8_t>( value.begin(), value.end() );
}

static std::string rotk_blob_text( const std::vector<uint8_t> &blob )
{
    return std::string( blob.begin(), blob.end() );
}

static std::string rotk_make_profile_stat_blob( const rotk_profile_stats &stats )
{
    // ROTK's user profile screen reads these values positionally from the S
    // field, not from named top-level fields. A positional probe showed:
    //   S[1] = Online Games
    //   S[2] = Disconnects
    //   S[3] = Experience Points
    //   S[5] = Kills
    // S[4] did not affect the visible Voice field in the profile panel; voice
    // appears to come from VOIP/buddy presence/challenge data instead.
    std::ostringstream stream;
    stream << stats.online_games << ','
           << stats.disconnects << ','
           << stats.experience_points << ','
           << 0 << ','
           << stats.kills;
    return stream.str();
}

static rotk_profile_stats rotk_default_profile_stats()
{
    rotk_profile_stats stats;
    stats.level = 1;
    return stats;
}

static rotk_profile_stats rotk_load_profile_stats( int64_t persona_id, const std::string &title )
{
    auto stats = rotk_default_profile_stats();
    if( nullptr == g_database || persona_id <= 0 ) return stats;

    const auto blob = g_database->get_title_blob( persona_id, title, "profile_stats" );
    const auto text = rotk_blob_text( blob );
    const auto fields = rotk_split_csv( text );
    if( fields.size() >= 1 ) stats.level = std::max<uint32_t>( 1, rotk_parse_u32( fields[0], stats.level ) );
    if( fields.size() >= 2 ) stats.online_games = rotk_parse_u32( fields[1], stats.online_games );
    if( fields.size() >= 3 ) stats.disconnects = rotk_parse_u32( fields[2], stats.disconnects );
    if( fields.size() >= 4 ) stats.experience_points = rotk_parse_u32( fields[3], stats.experience_points );
    if( fields.size() >= 5 ) stats.kills = rotk_parse_u32( fields[4], stats.kills );
    return stats;
}

static void rotk_save_profile_stats( int64_t persona_id, const std::string &title, const rotk_profile_stats &stats )
{
    if( nullptr == g_database || persona_id <= 0 ) return;

    std::ostringstream stream;
    stream << stats.level << ','
           << stats.online_games << ','
           << stats.disconnects << ','
           << stats.experience_points << ','
           << stats.kills;
    g_database->set_title_blob( persona_id, title, "profile_stats", rotk_text_blob( stream.str() ) );
}

static void rotk_apply_result_report( rotk_profile_stats &stats, const std::string &report )
{
    const auto fields = rotk_split_csv( report );

    // ROTK reports compact REPT# CSV values after returning from a match.
    // The first slot behaves like a played-game delta. The final observed slot
    // maps well to the visible Kills column, but the middle fields are still
    // being mapped, so keep them out of the visible profile counters for now.
    const auto game_delta = fields.empty() ? 0 : rotk_parse_u32( fields[0], 0 );
    stats.online_games += game_delta == 0 && false == report.empty() ? 1 : game_delta;

    if( fields.size() >= 6 ) stats.kills += rotk_parse_u32( fields[5], 0 );
}

static void rotk_add_persona_stats( sptr_packet &packet, const rotk_profile_stats &stats = rotk_default_profile_stats(), uint32_t ping_ms = 0 )
{
    // F is rendered as the bracketed value after the persona name. The client
    // labels it nowhere in this panel, but it behaves like a level/rank value.
    packet->set_number( "F", stats.level );

    // These compact rank-ish fields are parsed by ROTK's lobby user records.
    // P/PNG appears to affect connection quality/orb/session validation, but
    // the value is not raw milliseconds. Keep it neutral until the encoding is
    // mapped; raw RTT values made the client bounce between quality states.
    (void)ping_ms;
    packet->set_number( "RI", 0 );
    packet->set_number( "RT", 0 );
    packet->set_number( "R", 0 );
    packet->set_number( "RF", 0 );
    packet->set_number( "P", 0 );
    packet->set_number( "A", 0 );
    packet->set_string( "S", rotk_make_profile_stat_blob( stats ) );
    packet->set_string( "X", "" );
    packet->set_number( "D", 0 );
}

void ROTKProtocol::send_room_user( ptr_client &client, const ptr_client &user )
{
    if( nullptr == client || nullptr == client->socket ) return;
    if( nullptr == user || nullptr == user->socket ) return;
    if( user->socket->disconnected ) return;

    const auto name = persona_or_account( user );
    if( name.empty() ) return;

    auto response = make_packet( AriesCommand::user_add, 0 );
    response->set_number( "I", user->client_id );
    response->set_string( "N", name );
    rotk_add_persona_stats( response, rotk_load_profile_stats( user->persona_id, metadata_.product ), user->lobby_last_ping_ms );
    NET_SendMessage( client->socket, response );
}

void ROTKProtocol::send_room_user_remove( ptr_client &client, const ptr_client &user )
{
    if( nullptr == client || nullptr == client->socket ) return;
    if( nullptr == user ) return;

    auto response = make_packet( AriesCommand::user_add, 0 );
    response->set_number( "I", user->client_id );
    NET_SendMessage( client->socket, response );
}

void ROTKProtocol::send_room_user_clear( ptr_client &client )
{
    if( nullptr == client || nullptr == client->socket ) return;

    std::set<uint32_t> removed_ids;
    for( const auto &other : g_clientMng->get_clients() )
    {
        if( nullptr == other ) continue;
        if( other->client_id == 0 ) continue;
        if( false == removed_ids.insert( other->client_id ).second ) continue;

        auto response = make_packet( AriesCommand::user_add, 0 );
        response->set_number( "I", other->client_id );
        NET_SendMessage( client->socket, response );
    }
}

std::vector<ptr_client> ROTKProtocol::room_members( const std::string &room_name ) const
{
    std::vector<ptr_client> members;
    if( room_name.empty() ) return members;

    for( const auto &other : g_clientMng->get_clients() )
    {
        if( nullptr == other || nullptr == other->socket ) continue;
        if( other->socket->disconnected ) continue;
        if( other->room_name != room_name ) continue;
        members.push_back( other );
    }

    return members;
}

void ROTKProtocol::send_room_user_snapshot( ptr_client &client, const std::string &room_name )
{
    if( nullptr == client || room_name.empty() ) return;

    for( const auto &other : g_clientMng->get_clients() )
    {
        if( nullptr == other || nullptr == other->socket ) continue;
        if( other->socket->disconnected ) continue;
        if( other->room_name != room_name ) continue;
        send_room_user( client, other );
    }
}

void ROTKProtocol::broadcast_room_user_snapshot( const std::string &room_name )
{
    if( room_name.empty() ) return;

    for( const auto &other : room_members( room_name ) )
    {
        auto client = other;
        send_room_user_snapshot( client, room_name );
    }
}

void ROTKProtocol::broadcast_room_user_remove( const std::string &room_name, const ptr_client &user )
{
    if( room_name.empty() || nullptr == user ) return;

    for( const auto &other : room_members( room_name ) )
    {
        auto client = other;
        send_room_user_remove( client, user );
    }
}

void ROTKProtocol::send_room_log_message( ptr_client &client, const std::string &actor, const std::string &message )
{
    if( nullptr == client || nullptr == client->socket || client->socket->disconnected ) return;
    if( actor.empty() || message.empty() ) return;

    std::string payload;
    const auto encoded_message = rotk_encode_unquoted_message_value( message );
    payload.reserve( 8 + actor.size() + encoded_message.size() );
    payload += "N=";
    payload += actor;
    payload += "\nT=";
    payload += encoded_message;
    payload += "\n";

    auto response = make_packet( AriesCommand::message_add, 0, payload );
    NET_SendMessage( client->socket, response );
}

void ROTKProtocol::broadcast_room_log_message( const std::string &room_name, const std::string &actor, const std::string &message )
{
    if( room_name.empty() ) return;

    for( const auto &other : room_members( room_name ) )
    {
        auto client = other;
        send_room_log_message( client, actor, message );
    }
}

void ROTKProtocol::send_room_population( ptr_client &client, const std::string &room_name )
{
    if( nullptr == client || nullptr == client->socket ) return;
    if( room_name.empty() ) return;

    rotk_room_state room;
    if( false == get_room_state( room_name, room ) ) return;

    std::ostringstream population;
    population << room.id << ' ' << room_population( room_name );

    auto response = make_packet( AriesCommand::population, 0 );
    response->set_string( "Z", population.str() );
    NET_SendMessage( client->socket, response );
}

void ROTKProtocol::broadcast_room_population( const std::string &room_name )
{
    if( room_name.empty() ) return;

    for( const auto &other : g_clientMng->get_clients() )
    {
        if( nullptr == other || nullptr == other->socket ) continue;
        if( other->socket->disconnected ) continue;
        auto client = other;
        send_room_population( client, room_name );
    }
}

void ROTKProtocol::broadcast_lobby_room( const rotk_room_state &room )
{
    for( const auto &other : g_clientMng->get_clients() )
    {
        if( nullptr == other || nullptr == other->socket ) continue;
        if( other->socket->disconnected ) continue;
        auto other_client = other;
        send_lobby_room( other_client, room );
    }
}

void ROTKProtocol::send_lobby_room_remove( ptr_client &client, const rotk_room_state &room )
{
    if( nullptr == client || nullptr == client->socket ) return;

    // The room roster uses the same add/remove convention as +usr: an update
    // with only I=<id> removes that record from the frontend list.
    auto response = make_packet( AriesCommand::room_add, 0 );
    response->set_number( "I", room.id );
    NET_SendMessage( client->socket, response );
}

void ROTKProtocol::broadcast_lobby_room_remove( const rotk_room_state &room )
{
    for( const auto &other : g_clientMng->get_clients() )
    {
        if( nullptr == other || nullptr == other->socket ) continue;
        if( other->socket->disconnected ) continue;
        auto other_client = other;
        send_lobby_room_remove( other_client, room );
    }
}

void ROTKProtocol::send_lobby_snapshot( ptr_client &client )
{
    const auto rooms = get_room_snapshot();
    for( const auto &room : rooms ) send_lobby_room( client, room );
}

void ROTKProtocol::room( ptr_client &client, sptr_packet &packet )
{
    auto room_name = packet->get_string( "NAME" );
    if( room_name.empty() ) room_name = "Coop.C.Room_" + std::to_string( next_room_id_.load() );

    rotk_room_state room;
    room.id = next_room_id_.fetch_add( 1 );
    room.name = room_name;
    room.description = packet->get_string( "DESC", rotk_default_room_description( room_name ) );
    room.password = packet->get_string( "PASS" );
    room.max_players = rotk_clamp_max_players( packet->get_number( "MAX", 50 ) );
    room.persistent = false;

    {
        std::lock_guard lock( room_mutex_ );
        if( rooms_.contains( room.name ) )
        {
            auto response = make_packet( AriesCommand::room, MakeFourCC( 'd', 'u', 'p', 'l' ) );
            response->set_string( "NAME", room.name );
            NET_SendMessage( client->socket, response );
            return;
        }
        rooms_.insert( { room.name, room } );
    }

    leave_room( client );
    client->room_name = room.name;

    logging.information(
        L"ROTK room created NAME=%hs DESC=%hs PASS=%hs MAX=%u",
        room.name.c_str(),
        room.description.c_str(),
        room.password.empty() ? "" : "***",
        room.max_players
    );

    auto response = make_packet( AriesCommand::room, 0 );
    response->set_string( "NAME", room.name );
    response->set_number( "COUNT", room_population( room.name ) );
    NET_SendMessage( client->socket, response );
    send_room_user_clear( client );
    send_room_user_snapshot( client, room.name );
    broadcast_room_log_message( room.name, persona_or_account( client ), "Joined the room" );
    broadcast_lobby_room( room );
    broadcast_room_population( room.name );
    broadcast_room_user_snapshot( room.name );
}

void ROTKProtocol::move( ptr_client &client, sptr_packet &packet )
{
    const auto room_name = packet->get_string( "NAME" );
    if( room_name.empty() )
    {
        leave_room( client );
        send_room_user_clear( client );
        auto response = make_packet( AriesCommand::move, 0 );
        response->set_number( "COUNT", 0 );
        NET_SendMessage( client->socket, response );
        return;
    }

    rotk_room_state room;
    if( false == get_room_state( room_name, room ) )
    {
        auto response = make_packet( AriesCommand::move, 1 );
        NET_SendMessage( client->socket, response );
        return;
    }


    const auto already_in_target_room = client->room_name == room_name;
    const auto supplied_password = packet->get_string( "PASS" );
    if( false == room.password.empty() && false == already_in_target_room && room.password != supplied_password )
    {
        logging.warning(
            L"ROTK passworded room rejected NAME=%hs PERS=%hs HAS_PASS=%hs",
            room_name.c_str(),
            persona_or_account( client ).c_str(),
            false == supplied_password.empty() ? "yes" : "no"
        );

        // Online.elf's lobby error table maps code "pass" to "Password is
        // invalid".  Returning "auth" here is treated as an authorization
        // failure and the frontend can drop the connection instead of keeping
        // the password flow alive.
        auto response = make_packet( AriesCommand::move, MakeFourCC( 'p', 'a', 's', 's' ) );
        response->set_string( "NAME", room.name );
        response->set_number( "PASS", 1 );
        NET_SendMessage( client->socket, response );
        return;
    }

    if( false == already_in_target_room && room_population( room_name ) >= room.max_players )
    {
        auto response = make_packet( AriesCommand::move, MakeFourCC( 'f', 'u', 'l', 'l' ) );
        NET_SendMessage( client->socket, response );
        return;
    }

    const auto previous_room = client->room_name;
    if( false == previous_room.empty() && previous_room != room_name ) leave_room( client );
    client->room_name = room_name;

    auto response = make_packet( AriesCommand::move, 0 );
    response->set_number( "COUNT", room_population( room_name ) );
    NET_SendMessage( client->socket, response );

    send_room_user_clear( client );
    send_room_user_snapshot( client, room_name );
    broadcast_room_population( room_name );
    broadcast_lobby_room( room );
    broadcast_room_user_snapshot( room_name );
    if( false == already_in_target_room ) broadcast_room_log_message( room_name, persona_or_account( client ), "Joined the room" );
}

void ROTKProtocol::peek( ptr_client &client, sptr_packet &packet )
{
    const auto room_name = packet->get_string( "NAME", client->room_name );
    rotk_room_state room;
    if( room_name.empty() || false == get_room_state( room_name, room ) )
    {
        auto response = make_packet( AriesCommand::peek, 1 );
        NET_SendMessage( client->socket, response );
        return;
    }

    auto response = make_packet( AriesCommand::peek, 0 );
    response->set_string( "NAME", room.name );
    response->set_string( "DESC", room.description );
    response->set_number( "COUNT", room_population( room.name ) );
    response->set_number( "MAX", room.max_players );
    response->set_number( "PASS", room.password.empty() ? 0 : 1 );
    response->set_number( "PRIV", 0 );
    NET_SendMessage( client->socket, response );

    send_room_user_clear( client );
    send_room_user_snapshot( client, room.name );
}

void ROTKProtocol::online_stats( ptr_client &client, sptr_packet & )
{
    auto response = make_packet( AriesCommand::onln, 0 );
    rotk_add_persona_stats( response, rotk_load_profile_stats( client->persona_id, metadata_.product ), client->lobby_last_ping_ms );
    NET_SendMessage( client->socket, response );
}

void ROTKProtocol::ranking_results( ptr_client &client, sptr_packet &packet )
{
    const auto auth = packet->get_string( "AUTH" );
    const auto reporter = packet->get_string( "REPT", persona_or_account( client ) );

    std::vector<std::string> updated_names;
    for( int index = 0; index < 4; ++index )
    {
        const auto index_text = std::to_string( index );
        const auto name = packet->get_string( std::string( "NAME" ) + index_text );
        const auto report = packet->get_string( std::string( "REPT" ) + index_text );
        if( name.empty() && report.empty() ) continue;

        aries_persona_record persona;
        if( name.empty() || false == g_database->get_persona_by_name( name, persona ) )
        {
            logging.warning(
                L"ROTK rank result ignored unknown NAME%d=%hs REPT%d=%hs AUTH=%hs",
                index,
                name.c_str(),
                index,
                report.c_str(),
                auth.c_str()
            );
            continue;
        }

        const auto last_auth_key = std::string( "last_result_auth" );
        const auto previous_auth = rotk_blob_text( g_database->get_title_blob( persona.id, metadata_.product, last_auth_key ) );
        const bool duplicate = false == auth.empty() && previous_auth == auth;

        g_database->set_title_blob( persona.id, metadata_.product, "last_result", rotk_text_blob( packet->payload() ) );
        if( false == auth.empty() ) g_database->set_title_blob( persona.id, metadata_.product, last_auth_key, rotk_text_blob( auth ) );

        if( false == duplicate )
        {
            auto stats = rotk_load_profile_stats( persona.id, metadata_.product );
            rotk_apply_result_report( stats, report );
            rotk_save_profile_stats( persona.id, metadata_.product, stats );
            updated_names.push_back( name );
        }
    }

    auto response = make_packet( AriesCommand::rank, 0 );
    NET_SendMessage( client->socket, response );

    if( false == updated_names.empty() )
    {
        logging.information( L"ROTK rank profile counters updated count=%zu", updated_names.size() );
    }
}

void ROTKProtocol::custom_user( ptr_client &client, sptr_packet &packet )
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

    if( command == "ufpvt" )
    {
        auto response = make_packet( AriesCommand::cusr, 0 );
        response->set_string( "CMD", command );
        response->set_number( "PVT", 0 );
        response->set_number( "U", 0 );
        NET_SendMessage( client->socket, response );
        return;
    }

    auto response = make_packet( AriesCommand::cusr, 0 );
    if( false == command.empty() ) response->set_string( "CMD", command );
    NET_SendMessage( client->socket, response );
}

static std::string make_match_auth( uint32_t seed )
{
    std::array<uint8_t, 16> value{};
    uint32_t state = seed ^ 0x524F544Bu;
    for( size_t i = 0; i < value.size(); ++i )
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        value[i] = static_cast<uint8_t>( state & 0xFF );
    }

    static constexpr char hex[] = "0123456789abcdef";
    std::string output;
    output.reserve( value.size() * 2 );
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

static constexpr uint16_t ROTK_MATCH_PORT = 3658;

static uint16_t rotk_endpoint_port()
{
    return ROTK_MATCH_PORT;
}

static bool can_use_reported_addresses( const ptr_client &host, const ptr_client &opponent )
{
    if( nullptr == host || nullptr == opponent ) return false;
    if( nullptr == host->socket || nullptr == opponent->socket ) return false;
    if( host->reported_address.empty() || opponent->reported_address.empty() ) return false;
    return host->socket->ip_address == opponent->socket->ip_address;
}

static std::string persona_or_account( const ptr_client &client )
{
    if( nullptr == client ) return {};
    if( false == client->persona.empty() ) return client->persona;
    return client->account_name;
}

static int64_t rotk_match_request_key( const ptr_client &client )
{
    if( nullptr == client ) return 0;
    if( client->persona_id > 0 ) return client->persona_id;
    return -static_cast<int64_t>( client->client_id );
}

static bool rotk_has_other_live_persona_connection( const ptr_client &client )
{
    if( nullptr == client || client->persona_id <= 0 ) return false;

    for( const auto &other : g_clientMng->get_clients() )
    {
        if( nullptr == other || other.get() == client.get() || nullptr == other->socket ) continue;
        if( other->socket->disconnected ) continue;
        if( other->persona_id == client->persona_id ) return true;
    }

    return false;
}

void ROTKProtocol::quick_match_kind( ptr_client &client, sptr_packet &packet )
{
    match_requests_.erase( rotk_match_request_key( client ) );
    if( nullptr != client && client->persona_id > 0 ) pending_challenges_.erase( client->persona_id );

    logging.information(
        L"ROTK Quick Match request withdrawn client=%u persona=%hs KIND=%hs",
        client->client_id,
        persona_or_account( client ).c_str(),
        packet->get_string( "KIND" ).c_str()
    );

    auto response = make_packet( AriesCommand::quik, 0 );
    NET_SendMessage( client->socket, response );
}

void ROTKProtocol::quick_match( ptr_client &client, sptr_packet &packet )
{
    if( packet->has_field( "KIND" ) )
    {
        quick_match_kind( client, packet );
        return;
    }

    const auto persona = persona_or_account( client );
    if( persona.empty() )
    {
        auto response = make_packet( AriesCommand::quik, MakeFourCC( 'e', 'r', 'r', 'r' ) );
        NET_SendMessage( client->socket, response );
        return;
    }

    rotk_quick_match_request request;
    request.parameters = make_match_parameters( packet );
    match_requests_[rotk_match_request_key( client )] = request;

    logging.information(
        L"ROTK Quick Match request accepted persona=%hs stage=%u character=%u",
        persona.c_str(),
        request.parameters.stage,
        request.parameters.character
    );

    auto response = make_packet( AriesCommand::quik, 0 );
    response->set_number( "WAIT", 1 );
    NET_SendMessage( client->socket, response );

    ptr_client waiting_client;
    for( const auto &other : g_clientMng->get_clients() )
    {
        if( nullptr == other || nullptr == other->socket ) continue;
        if( other->socket->disconnected ) continue;
        if( other.get() == client.get() ) continue;
        if( other->persona_id == client->persona_id ) continue;
        const auto other_request_iter = match_requests_.find( rotk_match_request_key( other ) );
        if( other_request_iter == match_requests_.end() ) continue;
        if( other_request_iter->second.parameters.stage != request.parameters.stage ) continue;
        waiting_client = other;
        break;
    }

    if( nullptr != waiting_client ) send_quick_match_session( waiting_client, client );
}

static std::vector<uint32_t> parse_unsigned_csv( const std::string &text )
{
    std::vector<uint32_t> values;
    std::string current;

    for( const auto c : text )
    {
        if( c == ',' )
        {
            if( false == current.empty() )
            {
                try
                {
                    values.push_back( static_cast<uint32_t>( std::stoul( current, nullptr, 0 ) ) );
                }
                catch( ... )
                {
                    values.push_back( 0 );
                }
                current.clear();
            }
            continue;
        }

        if( static_cast<unsigned char>( c ) > 0x20 ) current.push_back( c );
    }

    if( false == current.empty() )
    {
        try
        {
            values.push_back( static_cast<uint32_t>( std::stoul( current, nullptr, 0 ) ) );
        }
        catch( ... )
        {
            values.push_back( 0 );
        }
    }

    return values;
}

static std::vector<uint32_t> rotk_read_character_candidates( const sptr_packet &packet )
{
    std::vector<uint32_t> values;
    const auto binary = packet->get_binary( "CHAR" );

    if( false == binary.empty() )
    {
        values.reserve( binary.size() / 2 );
        for( size_t index = 0; index + 1 < binary.size(); index += 2 )
        {
            values.push_back( static_cast<uint32_t>( binary[index] ) |
                              ( static_cast<uint32_t>( binary[index + 1] ) << 8 ) );
        }
    }
    else
    {
        values = parse_unsigned_csv( packet->get_string( "CHAR" ) );
    }

    std::vector<uint32_t> candidates;
    for( const auto value : values )
    {
        if( value > 9 ) continue;
        if( std::find( candidates.begin(), candidates.end(), value ) != candidates.end() ) continue;
        candidates.push_back( value );
    }
    return candidates;
}

static bool rotk_try_get_embedded_text_number( const std::string &text, std::string_view key, int32_t &value )
{
    if( text.empty() || key.empty() ) return false;

    size_t line_begin = 0;
    while( line_begin < text.size() )
    {
        while( line_begin < text.size() && ( text[line_begin] == '\r' || text[line_begin] == '\n' ) ) ++line_begin;
        if( line_begin >= text.size() ) break;

        const auto line_end = text.find_first_of( "\r\n", line_begin );
        const auto line = text.substr( line_begin, line_end == std::string::npos ? std::string::npos : line_end - line_begin );
        const auto equals = line.find( '=' );
        if( equals != std::string::npos && equals == key.size() && std::string_view( line.data(), equals ) == key )
        {
            try
            {
                value = static_cast<int32_t>( std::stoll( line.substr( equals + 1 ), nullptr, 0 ) );
            }
            catch( ... )
            {
                value = 0;
            }
            return true;
        }

        if( line_end == std::string::npos ) break;
        line_begin = line_end + 1;
    }

    return false;
}

ROTKProtocol::rotk_match_parameters ROTKProtocol::make_match_parameters( sptr_packet &packet ) const
{
    rotk_match_parameters parameters;
    const auto embedded_text = packet->get_string( "TEXT" );

    const auto has_number = [&]( std::string_view key )
    {
        int32_t embedded_value = 0;
        return packet->has_field( key ) || rotk_try_get_embedded_text_number( embedded_text, key, embedded_value );
    };

    const auto read_number = [&]( std::string_view key, int32_t fallback )
    {
        if( packet->has_field( key ) ) return packet->get_number( key, fallback );

        int32_t embedded_value = 0;
        if( rotk_try_get_embedded_text_number( embedded_text, key, embedded_value ) ) return embedded_value;
        return fallback;
    };

    parameters.character_candidates = rotk_read_character_candidates( packet );
    if( false == parameters.character_candidates.empty() ) parameters.character = parameters.character_candidates.front();

    if( has_number( "GAME" ) ) parameters.stage = std::clamp<uint32_t>( static_cast<uint32_t>( std::max<int32_t>( 0, read_number( "GAME", static_cast<int32_t>( parameters.stage ) ) ) ), 0u, 14u );
    if( has_number( "COURSE" ) ) parameters.stage = std::clamp<uint32_t>( static_cast<uint32_t>( std::max<int32_t>( 0, read_number( "COURSE", static_cast<int32_t>( parameters.stage ) ) ) ), 0u, 14u );
    if( has_number( "COUR" ) ) parameters.stage = std::clamp<uint32_t>( static_cast<uint32_t>( std::max<int32_t>( 0, read_number( "COUR", static_cast<int32_t>( parameters.stage ) ) ) ), 0u, 14u );
    if( has_number( "GLFR" ) )
    {
        parameters.character = std::clamp<uint32_t>( static_cast<uint32_t>( std::max<int32_t>( 0, read_number( "GLFR", static_cast<int32_t>( parameters.character ) ) ) ), 0u, 9u );
        parameters.character_candidates = { parameters.character };
    }
    if( has_number( "VOIP" ) ) parameters.voip = static_cast<uint32_t>( std::max<int32_t>( 0, read_number( "VOIP", static_cast<int32_t>( parameters.voip ) ) ) );
    if( has_number( "SHOT" ) ) parameters.shot = static_cast<uint32_t>( std::max<int32_t>( 0, read_number( "SHOT", static_cast<int32_t>( parameters.shot ) ) ) );
    if( has_number( "MFLG" ) ) parameters.mode_flags = static_cast<uint32_t>( std::max<int32_t>( 0, read_number( "MFLG", static_cast<int32_t>( parameters.mode_flags ) ) ) );
    if( has_number( "CFLG" ) ) parameters.challenge_flags = static_cast<uint32_t>( std::max<int32_t>( 0, read_number( "CFLG", static_cast<int32_t>( parameters.challenge_flags ) ) ) );

    if( parameters.shot == 0 ) parameters.shot = 10;
    if( parameters.mode_flags == 0 ) parameters.mode_flags = 0x2;
    if( parameters.challenge_flags == 0 ) parameters.challenge_flags = 0x08012464;

    return parameters;
}


namespace
{
    std::string rotk_encode_unquoted_message_value( const std::string &value )
    {
        static constexpr char hex[] = "0123456789abcdef";

        std::string output;
        output.reserve( value.size() );

        for( const char c : value )
        {
            const auto byte = static_cast<unsigned char>( c );

            // Retail challenge bodies are carried as one unquoted value:
            // T=challenge%0aMYIP%3d...%0a
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

    sptr_packet rotk_make_challenge_message_packet( const std::string &from_persona, const std::string &text )
    {
        const auto encoded_text = rotk_encode_unquoted_message_value( text );

        std::string payload;
        payload.reserve( 32 + from_persona.size() + encoded_text.size() );

        // Online.elf parses +msg through N/T/F.  F uses the lobby flag alphabet:
        //   P = 0x00010000 -> private message path
        //   3 = 0x40000000 -> challenge payload path
        // Without F=P3, the same packet is only shown in the room message log.
        payload += "N=";
        payload += from_persona;
        payload += "\nT=";
        payload += encoded_text;
        payload += "\nF=P3\n";

        return make_packet( AriesCommand::message_add, 0, payload );
    }

    sptr_packet rotk_make_chat_message_packet( const std::string &from_persona, const std::string &text )
    {
        const auto encoded_text = rotk_encode_unquoted_message_value( text );

        std::string payload;
        payload.reserve( 8 + from_persona.size() + encoded_text.size() );

        // Plain room/system chat uses the same N/T envelope as the message-log
        // line we found with join/leave. Do not send TEXT/PERS here: ROTK
        // accepts that packet, but it does not render in the side chat log.
        payload += "N=";
        payload += from_persona;
        payload += "\nT=";
        payload += encoded_text;
        payload += "\n";

        return make_packet( AriesCommand::message_add, 0, payload );
    }
}

void ROTKProtocol::send_pending_challenge( ptr_client &client, const rotk_pending_challenge &challenge )
{
    if( nullptr == client || nullptr == client->socket ) return;

    std::ostringstream text;
    text << "challenge\n";
    text << "MYIP=" << challenge.remote_address << "\n";
    text << "COUR=" << challenge.parameters.stage << "\n";
    text << "GLFR=" << challenge.parameters.character << "\n";
    text << "VOIP=" << challenge.parameters.voip << "\n";
    text << "SHOT=" << challenge.parameters.shot << "\n";
    text << "MFLG=" << challenge.parameters.mode_flags << "\n";
    text << "CFLG=" << challenge.parameters.challenge_flags << "\n";
    text << "PTS=0\n";
    text << "RANK=0\n";
    text << "STUS=0\n";
    text << "PNG=0\n";
    text << "WIN=0\n";
    text << "LOSS=0\n";
    text << "TIE=0\n";
    text << "INC=0\n";

    auto response = rotk_make_challenge_message_packet( challenge.opponent, text.str() );
    NET_SendMessage( client->socket, response );
}


void ROTKProtocol::challenge( ptr_client &client, sptr_packet &packet )
{
    const auto text = packet->get_string( "TEXT" );
    const auto persona = persona_or_account( client );
    const auto target_name = packet->get_string( "PERS", packet->get_string( "OPPO" ) );
    const auto pending_iter = client->persona_id > 0 ? pending_challenges_.find( client->persona_id ) : pending_challenges_.end();

    if( false == target_name.empty() && target_name != "*" )
    {
        logging.information( L"ROTK challenge request FROM=%hs TO=%hs", persona.c_str(), target_name.c_str() );

        aries_persona_record target_persona;
        if( nullptr == g_database || false == g_database->get_persona_by_name( target_name, target_persona ) )
        {
            logging.warning( L"ROTK challenge target persona not found FROM=%hs TO=%hs", persona.c_str(), target_name.c_str() );
            auto response = make_packet( AriesCommand::chal, MakeFourCC( 'n', 'o', 'u', 's' ) );
            NET_SendMessage( client->socket, response );
            return;
        }

        ptr_client target_client;
        if( false == g_clientMng->find_online_persona( target_persona.id, protocol_lobby, target_client ) )
        {
            g_clientMng->find_online_persona( target_persona.id, protocol_buddy, target_client );
        }

        if( nullptr == target_client || nullptr == target_client->socket || target_client->socket->disconnected )
        {
            logging.warning( L"ROTK challenge target not online FROM=%hs TO=%hs", persona.c_str(), target_name.c_str() );
            auto response = make_packet( AriesCommand::chal, MakeFourCC( 'n', 'o', 'u', 's' ) );
            NET_SendMessage( client->socket, response );
            return;
        }

        if( target_client.get() == client.get() || client->persona_id == target_persona.id )
        {
            logging.warning( L"ROTK challenge target is self FROM=%hs TO=%hs", persona.c_str(), target_name.c_str() );
            auto response = make_packet( AriesCommand::chal, MakeFourCC( 's', 'e', 'l', 'f' ) );
            NET_SendMessage( client->socket, response );
            return;
        }

        if( text.empty() && packet->has_field( "HOST" ) )
        {
            const auto now = static_cast<uint32_t>( std::time( nullptr ) );
            for( auto iter = recent_session_starts_.begin(); iter != recent_session_starts_.end(); )
            {
                if( now - iter->second > 15 ) iter = recent_session_starts_.erase( iter );
                else ++iter;
            }

            const auto start_key = rotk_match_start_key( persona, target_persona.name );
            const auto recent_iter = recent_session_starts_.find( start_key );
            if( recent_iter != recent_session_starts_.end() && now - recent_iter->second <= 5 )
            {
                auto response = make_packet( AriesCommand::chal, 0 );
                NET_SendMessage( client->socket, response );
                return;
            }

            recent_session_starts_[start_key] = now;

            if( match_requests_.find( rotk_match_request_key( client ) ) == match_requests_.end() )
            {
                rotk_quick_match_request request;
                request.parameters = make_match_parameters( packet );
                match_requests_[rotk_match_request_key( client )] = request;
            }

            if( match_requests_.find( rotk_match_request_key( target_client ) ) == match_requests_.end() )
            {
                rotk_quick_match_request request;
                request.parameters = make_match_parameters( packet );
                match_requests_[rotk_match_request_key( target_client )] = request;
            }

            const auto sender_is_host = packet->get_number( "HOST", 0 ) != 0;
            auto host_client = sender_is_host ? client : target_client;
            auto opponent_client = sender_is_host ? target_client : client;
            send_quick_match_session( host_client, opponent_client );

            auto response = make_packet( AriesCommand::chal, 0 );
            NET_SendMessage( client->socket, response );
            return;
        }

        const auto parameters = make_match_parameters( packet );
        rotk_quick_match_request host_request;
        host_request.parameters = parameters;
        match_requests_[rotk_match_request_key( client )] = host_request;
        match_requests_[rotk_match_request_key( target_client )] = host_request;

        send_quick_match_session( client, target_client );

        auto response = make_packet( AriesCommand::chal, 0 );
        NET_SendMessage( client->socket, response );
        return;
    }

    if( packet->get_string( "PERS" ) == "*" && pending_iter != pending_challenges_.end() )
    {
        send_pending_challenge( client, pending_iter->second );
        return;
    }

    if( text == "accept" || text == "decline" || text == "busy" || text == "revoke" )
    {
        auto response = make_packet( AriesCommand::chal, 0 );
        NET_SendMessage( client->socket, response );
        return;
    }

    if( pending_iter != pending_challenges_.end() )
    {
        send_pending_challenge( client, pending_iter->second );
        return;
    }

    auto response = make_packet( AriesCommand::chal, 0 );
    NET_SendMessage( client->socket, response );
}


static bool rotk_find_persona_record( const std::string &persona, aries_persona_record &record )
{
    if( persona.empty() || nullptr == g_database ) return false;
    return g_database->get_persona_by_name( persona, record );
}

static ptr_client rotk_find_online_persona_client( int64_t persona_id, enum_socket_protocol preferred_protocol = protocol_lobby )
{
    ptr_client client;
    if( nullptr == g_clientMng || persona_id <= 0 ) return nullptr;
    if( g_clientMng->find_online_persona( persona_id, preferred_protocol, client ) ) return client;

    const auto fallback_protocol = preferred_protocol == protocol_lobby ? protocol_buddy : protocol_lobby;
    if( g_clientMng->find_online_persona( persona_id, fallback_protocol, client ) ) return client;
    return nullptr;
}

static ptr_client rotk_find_online_persona_client( const std::string &persona, aries_persona_record &record, enum_socket_protocol preferred_protocol = protocol_lobby )
{
    if( false == rotk_find_persona_record( persona, record ) ) return nullptr;
    return rotk_find_online_persona_client( record.id, preferred_protocol );
}

static std::vector<ptr_client> rotk_find_online_persona_channels( int64_t persona_id )
{
    std::vector<ptr_client> channels;
    if( nullptr == g_clientMng || persona_id <= 0 ) return channels;

    ptr_client lobby;
    if( g_clientMng->find_online_persona( persona_id, protocol_lobby, lobby ) && nullptr != lobby && nullptr != lobby->socket && false == lobby->socket->disconnected )
    {
        channels.push_back( lobby );
    }

    ptr_client buddy;
    if( g_clientMng->find_online_persona( persona_id, protocol_buddy, buddy ) && nullptr != buddy && nullptr != buddy->socket && false == buddy->socket->disconnected )
    {
        if( channels.empty() || channels.front().get() != buddy.get() ) channels.push_back( buddy );
    }

    return channels;
}


static std::vector<uint8_t> rotk_make_session_p1( uint16_t level, uint16_t player_one_character, uint16_t player_two_character )
{
    const std::array<uint16_t, 3> values{ level, player_one_character, player_two_character };
    std::vector<uint8_t> output;
    output.reserve( values.size() * sizeof( uint16_t ) );

    for( const auto value : values )
    {
        output.push_back( static_cast<uint8_t>( value & 0xFF ) );
        output.push_back( static_cast<uint8_t>( ( value >> 8 ) & 0xFF ) );
    }

    return output;
}

static uint16_t rotk_session_character_id( uint32_t character )
{
    return static_cast<uint16_t>( std::clamp<uint32_t>( character, 0u, 9u ) );
}

static std::pair<uint16_t, uint16_t> rotk_select_session_characters(
    const std::vector<uint32_t> &host_candidates,
    uint32_t host_fallback,
    const std::vector<uint32_t> &opponent_candidates,
    uint32_t opponent_fallback )
{
    const auto host_character = host_candidates.empty() ? rotk_session_character_id( host_fallback ) : rotk_session_character_id( host_candidates.front() );
    auto opponent_character = opponent_candidates.empty() ? rotk_session_character_id( opponent_fallback ) : rotk_session_character_id( opponent_candidates.front() );

    for( const auto candidate : opponent_candidates )
    {
        const auto character = rotk_session_character_id( candidate );
        if( character == host_character ) continue;
        opponent_character = character;
        break;
    }

    return { host_character, opponent_character };
}

static std::string rotk_endpoint_address_for_pair( const ptr_client &source, const ptr_client &target )
{
    if( nullptr == source || nullptr == source->socket ) return {};
    if( nullptr != target && nullptr != target->socket && false == source->reported_address.empty() && source->socket->ip_address == target->socket->ip_address ) return source->reported_address;
    return source->socket->ip_address;
}

static uint16_t rotk_endpoint_fallback_port_for_pair( const ptr_client &source, const ptr_client &target )
{
    if( nullptr == source || nullptr == source->socket ) return 0;
    if( nullptr != target && nullptr != target->socket && false == source->reported_address.empty() && source->reported_port != 0 && source->socket->ip_address == target->socket->ip_address ) return source->reported_port;
    return source->socket->port;
}

static void rotk_set_forwarded_message_endpoints( sptr_packet &packet, const ptr_client &source, const ptr_client &target )
{
    const auto source_address = rotk_endpoint_address_for_pair( source, target );
    const auto target_address = rotk_endpoint_address_for_pair( target, source );
    const auto source_port = rotk_endpoint_port();
    const auto target_port = rotk_endpoint_port();

    if( false == source_address.empty() )
    {
        packet->set_string( "ADDR", source_address );
        packet->set_string( "MYIP", source_address );
    }
    if( source_port != 0 ) packet->set_number( "PORT", source_port );

    if( false == target_address.empty() ) packet->set_string( "FROM", target_address );
    if( target_port != 0 ) packet->set_number( "FROMPORT", target_port );
}

static std::string rotk_control_message_kind( const std::string &text )
{
    const auto separator = text.find_first_of( "\r\n" );
    const auto kind = separator == std::string::npos ? text : text.substr( 0, separator );
    if( kind == "challenge" || kind == "accept" || kind == "decline" || kind == "busy" || kind == "revoke" ) return kind;
    return {};
}

static std::map<std::string, std::string> rotk_parse_embedded_control_fields( const std::string &text )
{
    std::map<std::string, std::string> fields;
    auto line_begin = text.find_first_of( "\r\n" );
    if( line_begin == std::string::npos ) return fields;

    while( line_begin < text.size() )
    {
        while( line_begin < text.size() && ( text[line_begin] == '\r' || text[line_begin] == '\n' ) ) ++line_begin;
        if( line_begin >= text.size() ) break;

        const auto line_end = text.find_first_of( "\r\n", line_begin );
        const auto line = text.substr( line_begin, line_end == std::string::npos ? std::string::npos : line_end - line_begin );
        const auto equals = line.find( '=' );
        if( equals != std::string::npos && equals != 0 ) fields[line.substr( 0, equals )] = line.substr( equals + 1 );

        if( line_end == std::string::npos ) break;
        line_begin = line_end + 1;
    }

    return fields;
}

static uint32_t rotk_read_embedded_u32( const std::map<std::string, std::string> &fields, const char *key, uint32_t fallback )
{
    const auto iter = fields.find( key );
    if( iter == fields.end() || iter->second.empty() ) return fallback;

    try
    {
        return static_cast<uint32_t>( std::stoul( iter->second, nullptr, 0 ) );
    }
    catch( ... )
    {
        return fallback;
    }
}

static std::string rotk_match_start_key( const std::string &left, const std::string &right )
{
    return left < right ? left + "|" + right : right + "|" + left;
}

static std::string rotk_rewrite_embedded_control_field( const std::string &text, std::string_view key, const std::string &value )
{
    if( text.empty() || key.empty() || value.empty() ) return text;

    std::string output;
    output.reserve( text.size() + value.size() );

    bool replaced = false;
    size_t line_begin = 0;
    while( line_begin < text.size() )
    {
        const auto line_end = text.find_first_of( "\r\n", line_begin );
        const auto line = text.substr( line_begin, line_end == std::string::npos ? std::string::npos : line_end - line_begin );
        const auto equals = line.find( '=' );

        if( equals != std::string::npos && equals == key.size() && std::string_view( line.data(), equals ) == key )
        {
            output.append( key.data(), key.size() );
            output.push_back( '=' );
            output += value;
            replaced = true;
        }
        else
        {
            output += line;
        }

        if( line_end == std::string::npos ) break;

        if( text[line_end] == '\r' && line_end + 1 < text.size() && text[line_end + 1] == '\n' )
        {
            output += "\r\n";
            line_begin = line_end + 2;
        }
        else
        {
            output.push_back( text[line_end] );
            line_begin = line_end + 1;
        }
    }

    return replaced ? output : text;
}

static void rotk_copy_embedded_control_fields( sptr_packet &target, const std::string &text )
{
    auto line_begin = text.find_first_of( "\r\n" );
    if( line_begin == std::string::npos ) return;

    while( line_begin < text.size() )
    {
        while( line_begin < text.size() && ( text[line_begin] == '\r' || text[line_begin] == '\n' ) ) ++line_begin;
        if( line_begin >= text.size() ) break;

        const auto line_end = text.find_first_of( "\r\n", line_begin );
        const auto line = text.substr( line_begin, line_end == std::string::npos ? std::string::npos : line_end - line_begin );
        const auto equals = line.find( '=' );
        if( equals != std::string::npos && equals != 0 )
        {
            const auto key = line.substr( 0, equals );
            const auto value = line.substr( equals + 1 );
            if( false == key.empty() && false == target->has_field( key ) ) target->set_string( key, value );
        }

        if( line_end == std::string::npos ) break;
        line_begin = line_end + 1;
    }
}


void ROTKProtocol::apply_embedded_match_parameters( rotk_match_parameters &parameters, const std::string &text ) const
{
    const auto fields = rotk_parse_embedded_control_fields( text );
    if( fields.empty() ) return;

    parameters.stage = std::clamp<uint32_t>( rotk_read_embedded_u32( fields, "GAME", rotk_read_embedded_u32( fields, "COURSE", rotk_read_embedded_u32( fields, "COUR", parameters.stage ) ) ), 0u, 14u );
    parameters.character = std::clamp<uint32_t>( rotk_read_embedded_u32( fields, "GLFR", parameters.character ), 0u, 9u );
    if( fields.contains( "GLFR" ) ) parameters.character_candidates = { parameters.character };
    parameters.voip = rotk_read_embedded_u32( fields, "VOIP", parameters.voip );
    parameters.shot = rotk_read_embedded_u32( fields, "SHOT", parameters.shot );
    parameters.mode_flags = rotk_read_embedded_u32( fields, "MFLG", parameters.mode_flags );
    parameters.challenge_flags = rotk_read_embedded_u32( fields, "CFLG", parameters.challenge_flags );

    if( parameters.shot == 0 ) parameters.shot = 10;
    if( parameters.mode_flags == 0 ) parameters.mode_flags = 0x2;
    if( parameters.challenge_flags == 0 ) parameters.challenge_flags = 0x08012464;
}

void ROTKProtocol::message( ptr_client &client, sptr_packet &packet )
{
    if( nullptr == client || nullptr == client->socket ) return;

    const auto text = packet->get_string( "TEXT" );
    const auto control_kind = rotk_control_message_kind( text );
    const auto source_name = persona_or_account( client );
    const auto target_name = packet->get_string( "PERS", packet->get_string( "OPPO", packet->get_string( "PRIV" ) ) );

    if( false == control_kind.empty() && false == target_name.empty() && target_name != source_name )
    {
        aries_persona_record target_persona;
        auto target_client = rotk_find_online_persona_client( target_name, target_persona, protocol_lobby );
        if( nullptr == target_client || nullptr == target_client->socket || target_client->socket->disconnected )
        {
            logging.warning( L"ROTK control message target not online PERS=%hs TEXT=%hs", target_name.c_str(), control_kind.c_str() );
        }
        else if( control_kind == "challenge" )
        {
            auto parameters = make_match_parameters( packet );
            apply_embedded_match_parameters( parameters, text );
            auto sequence = match_sequence_.fetch_add( 1 );
            if( sequence == 0 ) sequence = match_sequence_.fetch_add( 1 );

            const auto epoch = static_cast<uint32_t>( std::time( nullptr ) );
            auto seed = ( epoch ^ ( sequence * 0x9E3779B9u ) ) & 0x7FFFFFFFu;
            if( seed == 0 ) seed = 1;

            const auto auth = make_match_auth( seed );
            std::ostringstream session_stream;
            session_stream << source_name << '-' << target_persona.name << '-'
                           << std::hex << std::setw( 8 ) << std::setfill( '0' ) << epoch;
            const auto session_name = session_stream.str();

            rotk_pending_challenge challenge;
            challenge.self = target_persona.name;
            challenge.host = source_name;
            challenge.opponent = source_name;
            challenge.remote_address = rotk_endpoint_address_for_pair( client, target_client );
            challenge.local_address = rotk_endpoint_address_for_pair( target_client, client );
            challenge.remote_port = rotk_endpoint_port();
            challenge.local_port = rotk_endpoint_port();
            challenge.auth = auth;
            challenge.session = session_name;
            challenge.parameters = parameters;
            challenge.seed = seed;
            challenge.epoch = epoch;

            pending_challenges_[target_persona.id] = challenge;

            // Keep the original challenge parameters around for the later chal
            // HOST=0/start request that the client emits after the popup is accepted.
            rotk_quick_match_request host_request;
            host_request.parameters = parameters;
            match_requests_[rotk_match_request_key( client )] = host_request;
            match_requests_[rotk_match_request_key( target_client )] = host_request;

            const auto channels = rotk_find_online_persona_channels( target_persona.id );
            for( auto channel : channels )
            {
                auto live_challenge = challenge;
                live_challenge.local_address = rotk_endpoint_address_for_pair( channel, client );
                live_challenge.local_port = rotk_endpoint_port();
                send_pending_challenge( channel, live_challenge );
            }

            logging.information( L"ROTK invite sent FROM=%hs TO=%hs", source_name.c_str(), target_persona.name.c_str() );
        }
        else
        {
            // accept/busy/decline/revoke are replies to an existing popup, not a
            // new invite. The previous probe treated every control word as a new
            // challenge and caused both clients to bounce busy/challenge forever.
            const auto channels = rotk_find_online_persona_channels( target_persona.id );
            auto reply_text = text.empty() ? control_kind : text;

            if( control_kind == "accept" )
            {
                auto reply_parameters = make_match_parameters( packet );
                apply_embedded_match_parameters( reply_parameters, reply_text );

                if( client->persona_id > 0 )
                {
                    auto &request = match_requests_[rotk_match_request_key( client )];
                    request.parameters.character = reply_parameters.character;
                    request.parameters.character_candidates = reply_parameters.character_candidates;
                    request.parameters.voip = reply_parameters.voip;
                }
            }

            // Accept replies carry their own MYIP line.  The game may consult
            // that before or alongside +ses, so rewrite it just like the
            // original challenge body instead of leaking the sender's LAN/dev
            // address to an external peer.
            reply_text = rotk_rewrite_embedded_control_field( reply_text, "MYIP", rotk_endpoint_address_for_pair( client, target_client ) );

            for( auto channel : channels )
            {
                auto forwarded = rotk_make_challenge_message_packet( source_name, reply_text );
                NET_SendMessage( channel->socket, forwarded );
            }

            if( client->persona_id > 0 ) pending_challenges_.erase( client->persona_id );
            if( target_persona.id > 0 ) pending_challenges_.erase( target_persona.id );

            logging.information( L"ROTK invite reply TEXT=%hs FROM=%hs TO=%hs", control_kind.c_str(), source_name.c_str(), target_persona.name.c_str() );
        }
    }
    else if( false == target_name.empty() )
    {
        aries_persona_record target_persona;
        auto target_client = rotk_find_online_persona_client( target_name, target_persona, protocol_lobby );
        if( nullptr == target_client || nullptr == target_client->socket || target_client->socket->disconnected )
        {
            logging.warning( L"ROTK message target not online PERS=%hs TEXT=%hs", target_name.c_str(), text.c_str() );
        }
        else if( target_client.get() != client.get() )
        {
            auto forwarded = rotk_make_chat_message_packet( source_name, text );
            NET_SendMessage( target_client->socket, forwarded );
        }
    }
    else if( false == client->room_name.empty() )
    {
        for( const auto &other : g_clientMng->get_clients() )
        {
            if( nullptr == other || nullptr == other->socket ) continue;
            if( other->socket->disconnected ) continue;
            if( other->room_name != client->room_name ) continue;
            auto forwarded = rotk_make_chat_message_packet( source_name, text );
            NET_SendMessage( other->socket, forwarded );
        }
    }

    auto response = make_packet( AriesCommand::mesg, 0 );
    NET_SendMessage( client->socket, response );
}

void ROTKProtocol::send_quick_match_session( ptr_client &host, ptr_client &opponent )
{
    if( nullptr == host || nullptr == opponent ) return;
    if( nullptr == host->socket || nullptr == opponent->socket ) return;

    const auto host_persona = persona_or_account( host );
    const auto opponent_persona = persona_or_account( opponent );
    if( host_persona.empty() || opponent_persona.empty() ) return;
    if( host_persona == opponent_persona ) return;

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
    const auto public_match_port = rotk_endpoint_port();
    const auto host_port = public_match_port;
    const auto opponent_port = public_match_port;
    std::ostringstream session_stream;
    session_stream << host_persona << '-' << opponent_persona << '-'
                   << std::hex << std::setw( 8 ) << std::setfill( '0' ) << epoch;
    const auto session_name = session_stream.str();

    const auto host_request_iter = match_requests_.find( rotk_match_request_key( host ) );
    const auto opponent_request_iter = match_requests_.find( rotk_match_request_key( opponent ) );

    const auto host_parameters = host_request_iter != match_requests_.end() ? host_request_iter->second.parameters : rotk_match_parameters{};
    const auto opponent_parameters = opponent_request_iter != match_requests_.end() ? opponent_request_iter->second.parameters : host_parameters;

    rotk_pending_challenge host_challenge;
    host_challenge.self = host_persona;
    host_challenge.host = host_persona;
    host_challenge.opponent = opponent_persona;
    host_challenge.remote_address = opponent_address;
    host_challenge.local_address = host_address;
    host_challenge.remote_port = opponent_port;
    host_challenge.local_port = host_port;
    host_challenge.auth = auth;
    host_challenge.session = session_name;
    host_challenge.parameters = opponent_parameters;
    host_challenge.seed = seed;
    host_challenge.epoch = epoch;

    rotk_pending_challenge opponent_challenge;
    opponent_challenge.self = opponent_persona;
    opponent_challenge.host = host_persona;
    opponent_challenge.opponent = host_persona;
    opponent_challenge.remote_address = host_address;
    opponent_challenge.local_address = opponent_address;
    opponent_challenge.remote_port = host_port;
    opponent_challenge.local_port = opponent_port;
    opponent_challenge.auth = auth;
    opponent_challenge.session = session_name;
    opponent_challenge.parameters = host_parameters;
    opponent_challenge.seed = seed;
    opponent_challenge.epoch = epoch;

    const auto session_level = static_cast<uint16_t>( std::clamp<uint32_t>( host_parameters.stage, 0u, 14u ) );
    const auto session_characters = rotk_select_session_characters(
        host_parameters.character_candidates,
        host_parameters.character,
        opponent_parameters.character_candidates,
        opponent_parameters.character );
    const auto session_p1 = rotk_make_session_p1( session_level, session_characters.first, session_characters.second );

    auto send_session = [&]( ptr_client &recipient, const rotk_pending_challenge &challenge )
    {
        auto session = make_packet( AriesCommand::session, 0 );
        session->set_string( "NAME", "Quick Match" );
        session->set_string( "SELF", challenge.self );
        session->set_string( "HOST", host_persona );
        session->set_string( "OPPO", challenge.opponent );
        session->set_binary( "P1", session_p1 );
        session->set_string( "P2", "" );
        session->set_string( "P3", "" );
        session->set_string( "P4", "" );
        session->set_string( "AUTH", auth );
        session->set_string( "ADDR", challenge.remote_address );
        session->set_number( "PORT", challenge.remote_port );
        session->set_string( "FROM", challenge.local_address );
        session->set_number( "FROMPORT", challenge.local_port );
        session->set_number( "SEED", seed );
        session->set_epoch( "WHEN", epoch );
        session->set_string( "SESS", session_name );
        NET_SendMessage( recipient->socket, session );
    };

    match_requests_.erase( rotk_match_request_key( host ) );
    match_requests_.erase( rotk_match_request_key( opponent ) );
    if( host->persona_id > 0 ) pending_challenges_.erase( host->persona_id );
    if( opponent->persona_id > 0 ) pending_challenges_.erase( opponent->persona_id );

    logging.information(
        L"ROTK match started SESS=%hs HOST=%hs OPPO=%hs stage=%u chars=%u/%u port=%u",
        session_name.c_str(),
        host_persona.c_str(),
        opponent_persona.c_str(),
        host_parameters.stage,
        session_characters.first,
        session_characters.second,
        public_match_port
    );

    send_session( host, host_challenge );
    send_session( opponent, opponent_challenge );
}

void ROTKProtocol::forget_client( const ptr_client &client )
{
    if( nullptr == client ) return;
    if( false == rotk_has_other_live_persona_connection( client ) )
    {
        match_requests_.erase( rotk_match_request_key( client ) );
    }

    // Pending invites are keyed by persona, not by the transient lobby/buddy
    // socket client id. A normal reconnect or extra buddy socket close should
    // not silently delete an invite before the lobby chal PERS=* poll sees it.
    remove_client_room_state( client );
}
