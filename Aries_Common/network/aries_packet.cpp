#include "stdafx.h"
#include "aries_packet.h"

aries_packet::aries_packet()
{
    rebuild();
}

aries_packet::aries_packet( std::vector<uint8_t> raw ) : raw_( std::move( raw ) )
{
    if( raw_.size() < 12 ) return;
    command_ = read_be32( raw_.data() );
    code_ = read_be32( raw_.data() + 4 );
    const auto declared_length = read_be32( raw_.data() + 8 );
    const auto packet_length = std::min<size_t>( declared_length, raw_.size() );
    if( packet_length > 12 )
    {
        payload_.assign( reinterpret_cast<const char *>( raw_.data() + 12 ), packet_length - 12 );
        if( false == payload_.empty() && payload_.back() == '\0' ) payload_.pop_back();
    }
}

aries_packet::aries_packet( uint32_t command, uint32_t code, std::string payload, bool null_terminate )
    : command_( command ), code_( code ), null_terminate_( null_terminate ), payload_( std::move( payload ) )
{
    rebuild();
}

uint32_t aries_packet::get_command() const
{
    return command_;
}

uint32_t aries_packet::get_code() const
{
    return code_;
}

uint32_t aries_packet::get_length() const
{
    return static_cast<uint32_t>( raw_.size() );
}

uint32_t aries_packet::get_notice() const
{
    return notice_;
}

bool aries_packet::is_notice() const
{
    return notice_packet_;
}

const std::string &aries_packet::payload() const
{
    return payload_;
}

const std::vector<uint8_t> &aries_packet::raw() const
{
    return raw_;
}

bool aries_packet::equal_ascii_case( std::string_view left, std::string_view right )
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

std::string aries_packet::decode_value( std::string_view value )
{
    std::string output;
    output.reserve( value.size() );

    for( size_t i = 0; i < value.size(); ++i )
    {
        if( value[i] == '%' && i + 2 < value.size() )
        {
            const auto hex = []( char c ) -> int
            {
                if( c >= '0' && c <= '9' ) return c - '0';
                if( c >= 'a' && c <= 'f' ) return c - 'a' + 10;
                if( c >= 'A' && c <= 'F' ) return c - 'A' + 10;
                return -1;
            };

            const auto high = hex( value[i + 1] );
            const auto low = hex( value[i + 2] );
            if( high >= 0 && low >= 0 )
            {
                output.push_back( static_cast<char>( ( high << 4 ) | low ) );
                i += 2;
                continue;
            }
        }
        output.push_back( value[i] );
    }

    return output;
}

std::string aries_packet::encode_value( std::string_view value )
{
    bool quote = false;
    for( const auto c : value )
    {
        if( static_cast<unsigned char>( c ) <= 0x20 || c == '"' || c == '%' )
        {
            quote = true;
            break;
        }
    }

    if( false == quote ) return std::string( value );

    static constexpr char hex[] = "0123456789ABCDEF";
    std::string output;
    output.push_back( '"' );
    for( const auto c : value )
    {
        const auto value_byte = static_cast<unsigned char>( c );
        if( value_byte <= 0x20 || c == '"' || c == '%' )
        {
            output.push_back( '%' );
            output.push_back( hex[( value_byte >> 4 ) & 0x0F] );
            output.push_back( hex[value_byte & 0x0F] );
        }
        else
        {
            output.push_back( c );
        }
    }
    output.push_back( '"' );
    return output;
}

std::string aries_packet::find_field( std::string_view name, bool &found ) const
{
    found = false;
    size_t position = 0;

    while( position < payload_.size() )
    {
        while( position < payload_.size() && static_cast<unsigned char>( payload_[position] ) <= 0x20 ) ++position;
        if( position >= payload_.size() ) break;

        const auto key_begin = position;
        while( position < payload_.size() && payload_[position] != '=' && static_cast<unsigned char>( payload_[position] ) > 0x20 ) ++position;
        if( position >= payload_.size() || payload_[position] != '=' )
        {
            while( position < payload_.size() && static_cast<unsigned char>( payload_[position] ) > 0x20 ) ++position;
            continue;
        }

        const std::string_view key( payload_.data() + key_begin, position - key_begin );
        ++position;

        std::string_view value;
        if( position < payload_.size() && payload_[position] == '"' )
        {
            ++position;
            const auto value_begin = position;
            while( position < payload_.size() && payload_[position] != '"' ) ++position;
            value = std::string_view( payload_.data() + value_begin, position - value_begin );
            if( position < payload_.size() ) ++position;
        }
        else
        {
            const auto value_begin = position;
            while( position < payload_.size() && static_cast<unsigned char>( payload_[position] ) > 0x20 ) ++position;
            value = std::string_view( payload_.data() + value_begin, position - value_begin );
        }

        if( equal_ascii_case( key, name ) )
        {
            found = true;
            return decode_value( value );
        }
    }

    return {};
}

bool aries_packet::has_field( std::string_view name ) const
{
    bool found = false;
    find_field( name, found );
    return found;
}

std::string aries_packet::get_string( std::string_view name, std::string_view fallback ) const
{
    bool found = false;
    auto value = find_field( name, found );
    if( found ) return value;
    return std::string( fallback );
}

int32_t aries_packet::get_number( std::string_view name, int32_t fallback ) const
{
    bool found = false;
    const auto value = find_field( name, found );
    if( false == found || value.empty() ) return fallback;

    try
    {
        return std::stoi( value, nullptr, 0 );
    }
    catch( ... )
    {
        return fallback;
    }
}

std::vector<uint8_t> aries_packet::get_binary( std::string_view name ) const
{
    bool found = false;
    const auto value = find_field( name, found );
    if( false == found || value.size() < 3 || value.front() != '$' ) return {};

    const auto hex_value = []( char c ) -> int
    {
        if( c >= '0' && c <= '9' ) return c - '0';
        if( c >= 'a' && c <= 'f' ) return c - 'a' + 10;
        if( c >= 'A' && c <= 'F' ) return c - 'A' + 10;
        return -1;
    };

    std::vector<uint8_t> output;
    output.reserve( ( value.size() - 1 ) / 2 );

    for( size_t i = 1; i + 1 < value.size(); i += 2 )
    {
        const auto high = hex_value( value[i] );
        const auto low = hex_value( value[i + 1] );
        if( high < 0 || low < 0 ) return {};
        output.push_back( static_cast<uint8_t>( ( high << 4 ) | low ) );
    }

    return output;
}

void aries_packet::set_string( std::string_view name, std::string_view value )
{
    payload_.append( name );
    payload_.push_back( '=' );
    payload_.append( encode_value( value ) );
    payload_.push_back( '\n' );
    rebuild();
}

void aries_packet::set_number( std::string_view name, int64_t value )
{
    set_string( name, std::to_string( value ) );
}

void aries_packet::set_epoch( std::string_view name, uint32_t epoch )
{
    const auto time_value = static_cast<std::time_t>( epoch );
    std::tm time_parts{};
#ifdef _WIN32
    gmtime_s( &time_parts, &time_value );
#else
    gmtime_r( &time_value, &time_parts );
#endif

    payload_.append( name );
    payload_.push_back( '=' );
    payload_.append( std::to_string( time_parts.tm_year + 1900 ) );
    payload_.push_back( '.' );
    payload_.append( std::to_string( time_parts.tm_mon + 1 ) );
    payload_.push_back( '.' );
    payload_.append( std::to_string( time_parts.tm_mday ) );
    payload_.push_back( ' ' );
    payload_.append( std::to_string( time_parts.tm_hour ) );
    payload_.push_back( ':' );
    if( time_parts.tm_min < 10 ) payload_.push_back( '0' );
    payload_.append( std::to_string( time_parts.tm_min ) );
    payload_.push_back( ':' );
    if( time_parts.tm_sec < 10 ) payload_.push_back( '0' );
    payload_.append( std::to_string( time_parts.tm_sec ) );
    payload_.push_back( '\n' );
    rebuild();
}

void aries_packet::set_binary( std::string_view name, const uint8_t *data, size_t size )
{
    static constexpr char hex[] = "0123456789ABCDEF";

    payload_.append( name );
    payload_.append( "=$" );
    payload_.reserve( payload_.size() + size * 2 + 1 );

    for( size_t i = 0; i < size; ++i )
    {
        payload_.push_back( hex[( data[i] >> 4 ) & 0x0F] );
        payload_.push_back( hex[data[i] & 0x0F] );
    }

    payload_.push_back( '\n' );
    rebuild();
}

void aries_packet::set_binary( std::string_view name, const std::vector<uint8_t> &data )
{
    set_binary( name, data.data(), data.size() );
}

void aries_packet::set_payload( std::string payload, bool null_terminate )
{
    payload_ = std::move( payload );
    null_terminate_ = null_terminate;
    rebuild();
}

std::string aries_packet::command_string() const
{
    std::string command( 4, '.' );
    for( size_t i = 0; i < 4; ++i )
    {
        const auto c = static_cast<char>( ( command_ >> ( 24 - i * 8 ) ) & 0xFF );
        command[i] = c >= 0x20 && c <= 0x7E ? c : '.';
    }
    return command;
}

std::shared_ptr<aries_packet> aries_packet::make_notice( uint32_t notice )
{
    auto packet = std::make_shared<aries_packet>();
    packet->notice_packet_ = true;
    packet->notice_ = notice;
    packet->raw_.clear();
    return packet;
}

uint32_t aries_packet::read_be32( const uint8_t *data )
{
    return ( static_cast<uint32_t>( data[0] ) << 24 ) |
           ( static_cast<uint32_t>( data[1] ) << 16 ) |
           ( static_cast<uint32_t>( data[2] ) << 8 ) |
           static_cast<uint32_t>( data[3] );
}

void aries_packet::write_be32( uint8_t *data, uint32_t value )
{
    data[0] = static_cast<uint8_t>( value >> 24 );
    data[1] = static_cast<uint8_t>( value >> 16 );
    data[2] = static_cast<uint8_t>( value >> 8 );
    data[3] = static_cast<uint8_t>( value );
}

void aries_packet::rebuild()
{
    const auto payload_length = payload_.size() + ( null_terminate_ ? 1 : 0 );
    raw_.assign( 12 + payload_length, 0 );
    write_be32( raw_.data(), command_ );
    write_be32( raw_.data() + 4, code_ );
    write_be32( raw_.data() + 8, static_cast<uint32_t>( raw_.size() ) );
    if( false == payload_.empty() ) std::memcpy( raw_.data() + 12, payload_.data(), payload_.size() );
}

std::shared_ptr<aries_packet> make_packet( uint32_t command, uint32_t code )
{
    return std::make_shared<aries_packet>( command, code, std::string{} );
}

std::shared_ptr<aries_packet> make_packet( uint32_t command, uint32_t code, const std::string &payload )
{
    return std::make_shared<aries_packet>( command, code, payload );
}
