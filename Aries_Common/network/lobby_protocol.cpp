#include "stdafx.h"
#include "../global_define.h"
#include "../crypto/password_hash.h"
#include "../crypto/ssc2.h"
#include "aries_commands.h"
#include "aries_packet.h"
#include "game_protocol.h"
#include "lobby_protocol.h"
#include "network_util.h"
#include "socket_struct.h"

#include <random>

using LobbyFunction = void( * )( ptr_client &, sptr_packet & );

std::atomic<uint32_t> g_lobby_ping_sequence{ 1 };

const std::map<uint32_t, LobbyFunction> LOBBY_PROTOCOL =
{
    { AriesCommand::directory, &Protocol::Lobby::directory },
    { AriesCommand::addr, &Protocol::Lobby::address },
    { AriesCommand::skey, &Protocol::Lobby::session_key },
    { AriesCommand::sele, &Protocol::Lobby::select },
    { AriesCommand::auth, &Protocol::Lobby::authenticate },
    { AriesCommand::acct, &Protocol::Lobby::account },
    { AriesCommand::news, &Protocol::Lobby::news },
    { AriesCommand::pers, &Protocol::Lobby::persona },
    { AriesCommand::mesg, &Protocol::Lobby::generic_ack },
    { AriesCommand::chal, &Protocol::Lobby::generic_ack },
    { AriesCommand::rank, &Protocol::Lobby::generic_ack },
    { AriesCommand::snap, &Protocol::Lobby::generic_ack },
    { AriesCommand::user, &Protocol::Lobby::generic_ack },
    { AriesCommand::rept, &Protocol::Lobby::generic_ack },
    { AriesCommand::edit, &Protocol::Lobby::edit_account },
    { AriesCommand::cper, &Protocol::Lobby::create_persona },
    { AriesCommand::dper, &Protocol::Lobby::delete_persona },
    { AriesCommand::lost, &Protocol::Lobby::generic_ack },
    { AriesCommand::pass, &Protocol::Lobby::change_password },
    { AriesCommand::auxi, &Protocol::Lobby::generic_ack },
    { AriesCommand::lobby_ping, &Protocol::Lobby::ping }
};

void Protocol::Lobby::process_protocol( sptr_socket &socket, sptr_packet &packet )
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

    if( nullptr != g_gameProtocol && g_gameProtocol->handle_lobby_packet( client, packet ) ) return;

    const auto protocol_iter = LOBBY_PROTOCOL.find( packet->get_command() );
    if( protocol_iter == LOBBY_PROTOCOL.end() )
    {
        unknown( client, packet );
        return;
    }

    ( *protocol_iter->second )( client, packet );
}

void Protocol::Lobby::process_notice( sptr_socket &socket, sptr_packet &packet )
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
            logging.information( L"Lobby client %u connected", client->client_id );
            if( nullptr != g_gameProtocol ) g_gameProtocol->lobby_connected( client );
        } break;

        case 200:
        {
            if( g_clientMng->get_client( socket, client ) )
            {
                logging.information( L"Lobby client %u disconnected", client->client_id );
                if( nullptr != g_gameProtocol ) g_gameProtocol->lobby_disconnected( client );
            }
            g_clientMng->remove_client( socket );
        } break;
    }
}

void Protocol::Lobby::directory( ptr_client &client, sptr_packet &packet )
{
    client->stage = stage_directory;

    logging.information(
        L"Directory request PROD=%hs VERS=%hs LANG=%hs SLUS=%hs",
        packet->get_string( "PROD" ).c_str(),
        packet->get_string( "VERS" ).c_str(),
        packet->get_string( "LANG" ).c_str(),
        packet->get_string( "SLUS" ).c_str()
    );

    auto response = make_packet( AriesCommand::directory, 0 );
    if( nullptr != g_gameProtocol ) g_gameProtocol->metadata().directory_response.apply( *response );
    NET_SendMessage( client->socket, response );
}

void Protocol::Lobby::address( ptr_client &client, sptr_packet &packet )
{
    client->stage = stage_lobby;
    client->reported_address = packet->get_string( "ADDR" );
    client->reported_port = static_cast<uint16_t>( std::max< int32_t >( 0, packet->get_number( "PORT", 0 ) ) );
    if( false == client->reported_address.empty() )
    {
        logging.debug( L"Client address data: %hs:%u", client->reported_address.c_str(), client->reported_port );
    }
}

void Protocol::Lobby::session_key( ptr_client &client, sptr_packet &packet )
{
    client->stage = stage_lobby;
    if( nullptr != g_gameProtocol ) client->session_key = g_gameProtocol->metadata().session_key;

    const auto public_key = packet->get_binary( "SKEY" );
    if( false == public_key.empty() )
    {
        const std::string public_key_text( public_key.begin(), public_key.end() );
        logging.debug( L"Client SKEY data: %hs", public_key_text.c_str() );
    }

    auto response = make_packet( AriesCommand::skey, 0 );
    response->set_binary( "SKEY", client->session_key.data(), client->session_key.size() );
    NET_SendMessage( client->socket, response );
}

void Protocol::Lobby::select( ptr_client &client, sptr_packet & )
{
    auto response = make_packet( AriesCommand::sele, 0 );
    NET_SendMessage( client->socket, response );
}

static std::string join_persona_names( const std::vector<aries_persona_record> &personas )
{
    std::string output;
    for( const auto &persona : personas )
    {
        if( false == output.empty() ) output.push_back( ',' );
        output += persona.name;
    }
    return output;
}

static std::string join_strings( const std::vector<std::string> &values )
{
    std::string output;
    for( const auto &value : values )
    {
        if( false == output.empty() ) output.push_back( ',' );
        output += value;
    }
    return output;
}

static bool ascii_equal_case( std::string_view left, std::string_view right )
{
    if( left.size() != right.size() ) return false;
    for( size_t i = 0; i < left.size(); ++i )
    {
        const auto a = static_cast<unsigned char>( left[i] );
        const auto b = static_cast<unsigned char>( right[i] );
        if( std::tolower( a ) != std::tolower( b ) ) return false;
    }
    return true;
}

static std::string make_persona_base( std::string_view requested )
{
    std::string output;
    output.reserve( requested.size() );
    for( const auto c : requested )
    {
        const auto value = static_cast<unsigned char>( c );
        if( std::isalnum( value ) ) output.push_back( static_cast<char>( value ) );
    }

    if( output.empty() ) output = "Player";
    return output;
}

static bool persona_name_exists( const std::string &name )
{
    if( nullptr == g_database || name.empty() ) return true;
    aries_persona_record persona;
    return g_database->get_persona_by_name( name, persona );
}

static std::vector<std::string> make_persona_alternatives( const std::string &requested_name, int32_t requested_count )
{
    const auto count = std::clamp<int32_t>( requested_count <= 0 ? 5 : requested_count, 1, 10 );
    constexpr size_t max_persona_length = 15;
    std::vector<std::string> alternatives;
    alternatives.reserve( static_cast<size_t>( count ) );

    const auto base = make_persona_base( requested_name );
    for( uint32_t suffix = 1; alternatives.size() < static_cast<size_t>( count ) && suffix < 100000; ++suffix )
    {
        auto suffix_text = std::to_string( suffix );
        auto prefix = base;
        if( prefix.size() + suffix_text.size() > max_persona_length )
        {
            prefix.resize( max_persona_length - std::min( max_persona_length, suffix_text.size() ) );
        }

        auto candidate = prefix + suffix_text;
        if( ascii_equal_case( candidate, requested_name ) ) continue;
        if( persona_name_exists( candidate ) ) continue;
        alternatives.push_back( std::move( candidate ) );
    }

    return alternatives;
}

static void send_duplicate_with_alternatives( ptr_client &client, uint32_t command, uint32_t code, const std::string &name, int32_t requested_count )
{
    auto response = make_packet( command, code );
    if( false == name.empty() ) response->set_string( "NAME", name );

    const auto alternatives = make_persona_alternatives( name, requested_count );
    response->set_number( "ALTS", static_cast<int64_t>( alternatives.size() ) );

    if( false == alternatives.empty() )
    {
        response->set_string( "OPTS", join_strings( alternatives ) );
    }


    NET_SendMessage( client->socket, response );
}

static std::string make_lobby_key()
{
    static constexpr char hex[] = "0123456789abcdef";
    std::random_device random;
    std::string output = nullptr != g_gameProtocol ? g_gameProtocol->metadata().lobby_key_prefix : "ARIES";
    output.push_back( '-' );
    for( size_t i = 0; i < 12; ++i )
    {
        const uint8_t value = static_cast<uint8_t>( random() );
        output.push_back( hex[( value >> 4 ) & 0x0F] );
        output.push_back( hex[value & 0x0F] );
    }
    return output;
}

static void send_lobby_error( ptr_client &client, uint32_t command, uint32_t code, const std::string &name = {} )
{
    auto response = make_packet( command, code );
    if( false == name.empty() ) response->set_string( "NAME", name );
    NET_SendMessage( client->socket, response );
}

static void send_lobby_ping_now( ptr_client &client )
{
    if( nullptr == client || nullptr == client->socket ) return;
    if( client->socket->disconnected ) return;
    if( client->socket->socket_protocol != protocol_lobby ) return;
    if( client->lobby_ping_outstanding ) return;

    auto sequence = g_lobby_ping_sequence.fetch_add( 1 );
    if( sequence == 0 ) sequence = g_lobby_ping_sequence.fetch_add( 1 );

    const auto now = std::chrono::steady_clock::now();
    client->lobby_ping_code = sequence;
    client->lobby_ping_outstanding = true;
    client->lobby_last_ping = now;
    client->lobby_ping_sent_at = now;

    auto ping_packet = make_packet( AriesCommand::lobby_ping, sequence );
    NET_SendMessage( client->socket, ping_packet );
}

static bool load_client_account( ptr_client &client, aries_account_record &account )
{
    if( nullptr == client || nullptr == g_database || client->account_id <= 0 ) return false;
    return g_database->get_account( client->account_id, account );
}

void Protocol::Lobby::authenticate( ptr_client &client, sptr_packet &packet )
{
    static constexpr uint32_t error_code = MakeFourCC( 'e', 'r', 'r', 'r' );
    send_lobby_ping_now( client );

    const auto account_name = packet->get_string( "NAME" );
    const auto wire_password = packet->get_string( "PASS" );
    const auto password = aries_crypto::ssc2_decrypt_password( wire_password, client->session_key );

    if( account_name.empty() || password.empty() )
    {
        logging.warning( L"Login rejected NAME=%hs", account_name.c_str() );
        send_lobby_error( client, AriesCommand::auth, error_code );
        return;
    }

    aries_account_record account;
    const bool found_account = g_database->get_account( account_name, account );
    const bool password_matches = found_account && aries_crypto::verify_password( password, account.password_salt, account.password_hash );

    if( false == found_account )
    {
        if( nullptr == g_configuration || false == g_configuration->get_bool( "auto_provision_auth_accounts", false ) )
        {
            logging.warning( L"Login rejected NAME=%hs", account_name.c_str() );
            send_lobby_error( client, AriesCommand::auth, error_code );
            return;
        }

        const auto password_record = aries_crypto::hash_password( password );
        int64_t account_id = 0;
        const auto result = g_database->create_account(
            account_name,
            password_record.hash,
            password_record.salt,
            packet->get_string( "MAIL" ),
            packet->get_string( "BORN", "19700101" ),
            packet->get_string( "GEND", "M" ),
            packet->get_string( "SPAM", "NN" ),
            packet->get_number( "TOS", 1 ),
            account_id
        );

        if( result != aries_database_result::success || false == g_database->get_account( account_id, account ) )
        {
            logging.error( L"Login auto-provision failed NAME=%hs: %hs", account_name.c_str(), g_database->last_error().c_str() );
            send_lobby_error( client, AriesCommand::auth, error_code );
            return;
        }

        logging.warning( L"Login auto-provisioned account NAME=%hs ID=%lld", account.name.c_str(), static_cast<long long>( account.id ) );
    }
    else if( false == password_matches )
    {
        if( nullptr == g_configuration || false == g_configuration->get_bool( "auto_rehash_auth_password", false ) )
        {
            logging.warning( L"Login rejected NAME=%hs", account_name.c_str() );
            send_lobby_error( client, AriesCommand::auth, error_code );
            return;
        }

        const auto password_record = aries_crypto::hash_password( password );
        if( false == g_database->update_account_password( account.id, password_record.hash, password_record.salt ) ||
            false == g_database->get_account( account.id, account ) )
        {
            logging.error( L"Login password rehash failed NAME=%hs: %hs", account_name.c_str(), g_database->last_error().c_str() );
            send_lobby_error( client, AriesCommand::auth, error_code );
            return;
        }

        logging.warning( L"Login accepted after password rehash NAME=%hs ID=%lld", account.name.c_str(), static_cast<long long>( account.id ) );
    }

    client->account_id = account.id;
    client->persona_id = 0;
    client->account_name = account.name;
    client->persona.clear();
    client->lobby_key.clear();
    client->machine_id = packet->get_string( "MID" );
    client->hwflag = packet->get_number( "HWFLAG" );
    client->hwmask = packet->get_number( "HWMASK" );
    client->stage = stage_authenticated;

    const auto personas = g_database->get_personas( account.id );
    const auto persona_names = join_persona_names( personas );

    logging.information(
        L"Login NAME=%hs ID=%lld MID=%hs HWFLAG=%d HWMASK=%d PERSONAS=%zu",
        client->account_name.c_str(),
        static_cast<long long>( client->account_id ),
        client->machine_id.c_str(),
        client->hwflag,
        client->hwmask,
        personas.size()
    );

    g_database->record_login(
        client->account_id,
        0,
        nullptr != client->socket ? client->socket->ip_address : std::string{},
        client->machine_id,
        client->hwflag,
        client->hwmask
    );

    auto response = make_packet( AriesCommand::auth, 0 );
    response->set_string( "PERSONAS", persona_names );
    response->set_string( "MAIL", account.email );
    response->set_string( "GEND", account.gender );
    response->set_string( "BORN", account.birth_date );
    response->set_string( "SPAM", account.spam );
    response->set_number( "TOS", account.tos );
    NET_SendMessage( client->socket, response );
}

void Protocol::Lobby::account( ptr_client &client, sptr_packet &packet )
{
    static constexpr uint32_t duplicate_code = MakeFourCC( 'd', 'u', 'p', 'l' );
    static constexpr uint32_t error_code = MakeFourCC( 'e', 'r', 'r', 'r' );
    send_lobby_ping_now( client );

    const auto name = packet->get_string( "NAME" );
    const auto wire_password = packet->get_string( "PASS" );
    const auto password = aries_crypto::ssc2_decrypt_password( wire_password, client->session_key );
    const auto email = packet->get_string( "MAIL" );
    const auto birth_date = packet->get_string( "BORN" );
    const auto gender = packet->get_string( "GEND" );
    const auto spam = packet->get_string( "SPAM", "NN" );
    const auto tos = packet->get_number( "TOS", 1 );

    if( name.empty() || password.empty() )
    {
        send_lobby_error( client, AriesCommand::acct, error_code );
        return;
    }

    const auto password_record = aries_crypto::hash_password( password );
    int64_t account_id = 0;
    const auto result = g_database->create_account(
        name,
        password_record.hash,
        password_record.salt,
        email,
        birth_date,
        gender,
        spam,
        tos,
        account_id
    );

    if( result == aries_database_result::duplicate )
    {
        logging.information( L"Registration duplicate NAME=%hs", name.c_str() );
        send_duplicate_with_alternatives( client, AriesCommand::acct, duplicate_code, name, packet->get_number( "ALTS", 5 ) );
        return;
    }

    if( result != aries_database_result::success )
    {
        logging.error( L"Registration database failure NAME=%hs: %hs", name.c_str(), g_database->last_error().c_str() );
        send_lobby_error( client, AriesCommand::acct, error_code );
        return;
    }

    client->account_id = account_id;
    client->persona_id = 0;
    client->account_name = name;
    client->persona.clear();
    client->stage = stage_authenticated;

    logging.information( L"Registered account NAME=%hs ID=%lld", name.c_str(), static_cast<long long>( account_id ) );
    auto response = make_packet( AriesCommand::acct, 0 );
    NET_SendMessage( client->socket, response );
}

void Protocol::Lobby::create_persona( ptr_client &client, sptr_packet &packet )
{
    static constexpr uint32_t duplicate_code = MakeFourCC( 'd', 'u', 'p', 'l' );
    static constexpr uint32_t error_code = MakeFourCC( 'e', 'r', 'r', 'r' );
    const auto name = packet->get_string( "PERS" );

    if( client->account_id <= 0 || name.empty() )
    {
        send_lobby_error( client, AriesCommand::cper, error_code );
        return;
    }

    const auto max_personas = nullptr != g_gameProtocol ? g_gameProtocol->metadata().max_personas : 5;
    aries_persona_record persona;
    const auto result = g_database->create_persona( client->account_id, name, max_personas, persona );
    if( result == aries_database_result::duplicate )
    {
        const auto alternatives = make_persona_alternatives( name, packet->get_number( "ALTS", 5 ) );
        logging.information(
            L"Persona duplicate account=%lld PERS=%hs ALTS=%hs",
            static_cast<long long>( client->account_id ),
            name.c_str(),
            join_strings( alternatives ).c_str()
        );
        send_duplicate_with_alternatives( client, AriesCommand::cper, duplicate_code, name, packet->get_number( "ALTS", 5 ) );
        return;
    }
    if( result != aries_database_result::success )
    {
        logging.warning( L"Persona creation rejected account=%lld PERS=%hs result=%d", static_cast<long long>( client->account_id ), name.c_str(), static_cast<int>( result ) );
        send_lobby_error( client, AriesCommand::cper, error_code );
        return;
    }

    logging.information( L"Created persona account=%lld PERS=%hs ID=%lld", static_cast<long long>( client->account_id ), persona.name.c_str(), static_cast<long long>( persona.id ) );
    auto response = make_packet( AriesCommand::cper, 0 );
    NET_SendMessage( client->socket, response );
}

void Protocol::Lobby::delete_persona( ptr_client &client, sptr_packet &packet )
{
    static constexpr uint32_t error_code = MakeFourCC( 'e', 'r', 'r', 'r' );
    const auto name = packet->get_string( "PERS" );
    if( client->account_id <= 0 || name.empty() )
    {
        send_lobby_error( client, AriesCommand::dper, error_code );
        return;
    }

    const auto result = g_database->delete_persona( client->account_id, name );
    if( result != aries_database_result::success )
    {
        send_lobby_error( client, AriesCommand::dper, error_code );
        return;
    }

    if( client->persona == name )
    {
        client->persona.clear();
        client->persona_id = 0;
        client->lobby_key.clear();
        client->stage = stage_authenticated;
    }

    logging.information( L"Deleted persona account=%lld PERS=%hs", static_cast<long long>( client->account_id ), name.c_str() );
    auto response = make_packet( AriesCommand::dper, 0 );
    NET_SendMessage( client->socket, response );
}

void Protocol::Lobby::change_password( ptr_client &client, sptr_packet &packet )
{
    static constexpr uint32_t error_code = MakeFourCC( 'e', 'r', 'r', 'r' );
    aries_account_record account;
    if( false == load_client_account( client, account ) )
    {
        send_lobby_error( client, AriesCommand::pass, error_code );
        return;
    }

    const auto old_password = aries_crypto::ssc2_decrypt_password( packet->get_string( "PASS" ), client->session_key );
    const auto new_password = packet->get_string( "CHNG" );
    if( old_password.empty() || new_password.empty() || false == aries_crypto::verify_password( old_password, account.password_salt, account.password_hash ) )
    {
        send_lobby_error( client, AriesCommand::pass, error_code );
        return;
    }

    const auto password_record = aries_crypto::hash_password( new_password );
    if( false == g_database->update_account_password( client->account_id, password_record.hash, password_record.salt ) )
    {
        send_lobby_error( client, AriesCommand::pass, error_code );
        return;
    }

    logging.information( L"Password changed account=%lld", static_cast<long long>( client->account_id ) );
    auto response = make_packet( AriesCommand::pass, 0 );
    NET_SendMessage( client->socket, response );
}

void Protocol::Lobby::edit_account( ptr_client &client, sptr_packet &packet )
{
    static constexpr uint32_t error_code = MakeFourCC( 'e', 'r', 'r', 'r' );
    aries_account_record account;
    if( false == load_client_account( client, account ) )
    {
        send_lobby_error( client, AriesCommand::edit, error_code );
        return;
    }

    std::optional<aries_crypto::password_record> password_record;
    if( packet->has_field( "PASS" ) || packet->has_field( "CHNG" ) )
    {
        if( false == packet->has_field( "PASS" ) || false == packet->has_field( "CHNG" ) )
        {
            send_lobby_error( client, AriesCommand::edit, error_code );
            return;
        }

        const auto old_password = aries_crypto::ssc2_decrypt_password( packet->get_string( "PASS" ), client->session_key );
        const auto new_password = packet->get_string( "CHNG" );
        if( old_password.empty() || new_password.empty() || false == aries_crypto::verify_password( old_password, account.password_salt, account.password_hash ) )
        {
            send_lobby_error( client, AriesCommand::edit, error_code );
            return;
        }
        password_record = aries_crypto::hash_password( new_password );
    }

    const auto email = packet->get_string( "MAIL", account.email );
    const auto birth_date = packet->get_string( "BORN", account.birth_date );
    const auto gender = packet->get_string( "GEND", account.gender );
    const auto spam = packet->get_string( "SPAM", account.spam );
    const auto tos = packet->get_number( "TOS", account.tos );

    if( false == g_database->update_account_profile( client->account_id, email, birth_date, gender, spam, tos ) )
    {
        send_lobby_error( client, AriesCommand::edit, error_code );
        return;
    }

    if( password_record.has_value() &&
        false == g_database->update_account_password( client->account_id, password_record->hash, password_record->salt ) )
    {
        send_lobby_error( client, AriesCommand::edit, error_code );
        return;
    }

    logging.information( L"Account info updated account=%lld", static_cast<long long>( client->account_id ) );
    auto response = make_packet( AriesCommand::edit, 0 );
    NET_SendMessage( client->socket, response );
}


static std::string decode_news_message_markup( std::string message )
{
    std::string decoded;
    decoded.reserve( message.size() );

    for( size_t index = 0; index < message.size(); )
    {
        if( message[index] == '\\' && index + 1 < message.size() )
        {
            const auto next = message[index + 1];
            if( next == 'n' || next == 'N' )
            {
                decoded.push_back( '\n' );
                index += 2;
                continue;
            }
            if( next == 'r' || next == 'R' )
            {
                decoded.push_back( '\r' );
                index += 2;
                continue;
            }
        }

        if( message.compare( index, 4, "<br>" ) == 0 || message.compare( index, 4, "<BR>" ) == 0 )
        {
            decoded.push_back( '\n' );
            index += 4;
            continue;
        }

        if( message.compare( index, 5, "<br/>" ) == 0 || message.compare( index, 5, "<BR/>" ) == 0 )
        {
            decoded.push_back( '\n' );
            index += 5;
            continue;
        }

        if( message.compare( index, 6, "<br />" ) == 0 || message.compare( index, 6, "<BR />" ) == 0 )
        {
            decoded.push_back( '\n' );
            index += 6;
            continue;
        }

        decoded.push_back( message[index++] );
    }

    return decoded;
}

void Protocol::Lobby::news( ptr_client &client, sptr_packet &packet )
{
    const auto name = packet->get_string( "NAME" );
    if( name == "0" )
    {
        const auto buddy_address = g_configuration->service_ip + ":" + std::to_string( g_configuration->buddy_port );
        auto response = make_packet( AriesCommand::news, 0, buddy_address );
        NET_SendMessage( client->socket, response );
        return;
    }

    if( name == "1" )
    {
        logging.information( L"News message requested" );
        auto message = decode_news_message_markup( nullptr != g_gameProtocol ? g_gameProtocol->metadata().news_message : std::string{} );
        if( false == message.empty() && message.back() != '\n' ) message += '\n';
        auto response = make_packet( AriesCommand::news, 0, message );
        NET_SendMessage( client->socket, response );
        return;
    }

    auto response = make_packet( AriesCommand::news, 0, std::string{} );
    NET_SendMessage( client->socket, response );
}

void Protocol::Lobby::persona( ptr_client &client, sptr_packet &packet )
{
    static constexpr uint32_t error_code = MakeFourCC( 'e', 'r', 'r', 'r' );
    const auto persona_name = packet->get_string( "PERS" );
    aries_persona_record persona;

    if( client->account_id <= 0 || persona_name.empty() || false == g_database->get_persona( client->account_id, persona_name, persona ) )
    {
        logging.warning( L"Persona selection rejected account=%lld PERS=%hs", static_cast<long long>( client->account_id ), persona_name.c_str() );
        send_lobby_error( client, AriesCommand::pers, error_code );
        return;
    }

    client->persona_id = persona.id;
    client->persona = persona.name;
    client->lobby_key = make_lobby_key();
    client->stage = stage_persona;
    client->lobby_ping_outstanding = false;
    client->lobby_last_ping = std::chrono::steady_clock::now();
    g_database->touch_persona( client->persona_id );
    g_database->record_login(
        client->account_id,
        client->persona_id,
        nullptr != client->socket ? client->socket->ip_address : std::string{},
        client->machine_id,
        client->hwflag,
        client->hwmask
    );

    logging.information(
        L"Persona set account=%lld PERS=%hs ID=%lld LKEY=%hs",
        static_cast<long long>( client->account_id ),
        client->persona.c_str(),
        static_cast<long long>( client->persona_id ),
        client->lobby_key.c_str()
    );

    auto response = make_packet( AriesCommand::pers, 0 );
    response->set_string( "LKEY", client->lobby_key );
    response->set_string( "EX-GAME", g_configuration->service_ip );
    NET_SendMessage( client->socket, response );

    if( nullptr != g_gameProtocol ) g_gameProtocol->persona_selected( client );
}

void Protocol::Lobby::ping( ptr_client &client, sptr_packet &packet )
{
    const auto sent_code = client->lobby_ping_code;
    const auto had_outstanding_ping = client->lobby_ping_outstanding;
    const auto now = std::chrono::steady_clock::now();

    client->lobby_ping_outstanding = false;
    client->lobby_last_ping = now;

    if( had_outstanding_ping )
    {
        if( client->lobby_ping_sent_at.time_since_epoch().count() != 0 )
        {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>( now - client->lobby_ping_sent_at ).count();
            client->lobby_last_ping_ms = static_cast<uint32_t>( std::clamp<int64_t>( elapsed, 0, 9999 ) );
        }

        return;
    }

    logging.debug(
        L"Unsolicited Lobby ~png client=%u code=0x%08X TIME=%d",
        client->client_id,
        packet->get_code(),
        packet->get_number( "TIME", -1 )
    );
}

void Protocol::Lobby::update()
{
    if( nullptr == g_configuration || nullptr == g_clientMng ) return;
    if( g_configuration->lobby_ping_interval_seconds == 0 ) return;

    const auto now = std::chrono::steady_clock::now();
    const auto interval = std::chrono::seconds( g_configuration->lobby_ping_interval_seconds );

    for( auto &client : g_clientMng->get_clients() )
    {
        if( nullptr == client || nullptr == client->socket ) continue;
        if( client->socket->socket_protocol != protocol_lobby ) continue;
        if( client->stage < stage_lobby ) continue;
        if( client->socket->disconnected ) continue;
        if( client->lobby_ping_outstanding ) continue;
        if( now - client->lobby_last_ping < interval ) continue;

        send_lobby_ping_now( client );
    }
}

void Protocol::Lobby::generic_ack( ptr_client &client, sptr_packet &packet )
{
    auto response = make_packet( packet->get_command(), 0 );
    NET_SendMessage( client->socket, response );
}

void Protocol::Lobby::unknown( ptr_client &client, sptr_packet &packet )
{
    const auto command = packet->command_string();
    logging.warning( L"UNDEFINED LOBBY PROTOCOL: %hs [0x%08X]", command.c_str(), packet->get_command() );
    if( false == g_configuration->auto_ack_unknown ) return;
    if( static_cast<uint8_t>( packet->get_command() >> 24 ) == static_cast<uint8_t>( '+' ) ) return;

    logging.warning( L"AUTO ACK LOBBY PROTOCOL: %hs", command.c_str() );
    auto response = make_packet( packet->get_command(), 0 );
    NET_SendMessage( client->socket, response );
}
