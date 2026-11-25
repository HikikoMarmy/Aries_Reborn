#include "stdafx.h"
#include "configuration.h"

static std::string trim_copy( std::string value )
{
    const auto first = value.find_first_not_of( " \t\r\n" );
    if( first == std::string::npos ) return {};
    const auto last = value.find_last_not_of( " \t\r\n" );
    return value.substr( first, last - first + 1 );
}

static bool parse_bool( const std::string &value, bool fallback )
{
    std::string lower = value;
    std::transform( lower.begin(), lower.end(), lower.begin(), []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
    if( lower == "1" || lower == "true" || lower == "yes" || lower == "on" ) return true;
    if( lower == "0" || lower == "false" || lower == "no" || lower == "off" ) return false;
    return fallback;
}

static std::vector<uint16_t> parse_ports( const std::string &value )
{
    std::vector<uint16_t> ports;
    std::stringstream stream( value );
    std::string token;

    while( std::getline( stream, token, ',' ) )
    {
        token = trim_copy( token );
        if( token.empty() ) continue;

        try
        {
            const auto port = std::stoul( token );
            if( port == 0 || port > 65535 ) continue;
            ports.push_back( static_cast<uint16_t>( port ) );
        }
        catch( ... )
        {
        }
    }

    return ports;
}

void CConfiguration::apply_value( const std::string &key, const std::string &value )
{
    values_[key] = value;

    try
    {
        if( key == "bind_ip" ) bind_ip = value;
        else if( key == "service_ip" ) service_ip = value;
        else if( key == "lobby_port" ) lobby_port = static_cast<uint16_t>( std::stoul( value ) );
        else if( key == "buddy_port" ) buddy_port = static_cast<uint16_t>( std::stoul( value ) );
        else if( key == "additional_lobby_ports" ) additional_lobby_ports = parse_ports( value );
        else if( key == "max_packet_size" ) max_packet_size = static_cast<uint32_t>( std::stoul( value ) );
        else if( key == "database_path" ) database_path = value;
        else if( key == "log_directory" ) log_directory = value;
        else if( key == "auto_ack_unknown" ) auto_ack_unknown = parse_bool( value, auto_ack_unknown );
        else if( key == "log_packet_payloads" ) log_packet_payloads = parse_bool( value, log_packet_payloads );
        else if( key == "buddy_ping_interval_seconds" ) buddy_ping_interval_seconds = static_cast<uint32_t>( std::stoul( value ) );
        else if( key == "lobby_ping_interval_seconds" ) lobby_ping_interval_seconds = static_cast<uint32_t>( std::stoul( value ) );
        else if( key == "demangler_enabled" ) demangler_enabled = parse_bool( value, demangler_enabled );
        else if( key == "demangler_ip" ) demangler_ip = value;
        else if( key == "ports" || key == "demangler_ports" )
        {
            const auto ports = parse_ports( value );
            if( false == ports.empty() ) demangler_ports = ports;
        }
        else if( key == "demangler_port" )
        {
            const auto port = static_cast<uint16_t>( std::stoul( value ) );
            if( port != 0 ) demangler_ports = { port };
        }
        else if( key == "demangler_probe_count" ) demangler_probe_count = static_cast<uint32_t>( std::stoul( value ) );
        else if( key == "demangler_session_timeout_seconds" ) demangler_session_timeout_seconds = static_cast<uint32_t>( std::stoul( value ) );
    }
    catch( ... )
    {
    }
}

bool CConfiguration::load( const std::string &path, std::string_view section )
{
    std::ifstream file( path );
    if( false == file.is_open() ) return false;

    values_.clear();

    std::unordered_map<std::string, std::string> global_values;
    std::unordered_map<std::string, std::string> section_values;
    std::unordered_map<std::string, std::string> legacy_values;
    std::string current_section;
    const std::string wanted_section( section );

    std::string line;
    while( std::getline( file, line ) )
    {
        line = trim_copy( line );
        if( line.empty() ) continue;
        if( line.front() == ';' || line.front() == '#' ) continue;

        if( line.front() == '[' && line.back() == ']' )
        {
            current_section = trim_copy( line.substr( 1, line.size() - 2 ) );
            continue;
        }

        const auto equals = line.find( '=' );
        if( equals == std::string::npos ) continue;

        const auto key = trim_copy( line.substr( 0, equals ) );
        const auto value = trim_copy( line.substr( equals + 1 ) );
        if( key.empty() ) continue;

        if( wanted_section.empty() )
        {
            legacy_values[key] = value;
            continue;
        }

        if( current_section.empty() || current_section == "global" ) global_values[key] = value;
        if( current_section == wanted_section ) section_values[key] = value;
    }

    if( wanted_section.empty() )
    {
        for( const auto &[key, value] : legacy_values ) apply_value( key, value );
        return true;
    }

    for( const auto &[key, value] : global_values ) apply_value( key, value );
    for( const auto &[key, value] : section_values ) apply_value( key, value );
    return true;
}

std::string CConfiguration::get_string( std::string_view key, std::string_view fallback ) const
{
    const auto iter = values_.find( std::string( key ) );
    if( iter == values_.end() ) return std::string( fallback );
    return iter->second;
}

int64_t CConfiguration::get_number( std::string_view key, int64_t fallback ) const
{
    const auto value = get_string( key );
    if( value.empty() ) return fallback;

    try
    {
        return std::stoll( value, nullptr, 0 );
    }
    catch( ... )
    {
        return fallback;
    }
}

bool CConfiguration::get_bool( std::string_view key, bool fallback ) const
{
    const auto value = get_string( key );
    if( value.empty() ) return fallback;
    return parse_bool( value, fallback );
}
