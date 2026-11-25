#include "stdafx.h"
#include "game_metadata.h"
#include "../network/aries_packet.h"

aries_field_set &aries_field_set::set_string( std::string_view name, std::string_view value )
{
    fields_.push_back( { std::string( name ), std::string( value ) } );
    return *this;
}

aries_field_set &aries_field_set::set_number( std::string_view name, int64_t value )
{
    fields_.push_back( { std::string( name ), value } );
    return *this;
}

aries_field_set &aries_field_set::set_binary( std::string_view name, const uint8_t *data, size_t size )
{
    std::vector<uint8_t> value;
    if( nullptr != data && size > 0 ) value.assign( data, data + size );
    fields_.push_back( { std::string( name ), std::move( value ) } );
    return *this;
}

aries_field_set &aries_field_set::set_binary( std::string_view name, const std::vector<uint8_t> &value )
{
    fields_.push_back( { std::string( name ), value } );
    return *this;
}

void aries_field_set::apply( aries_packet &packet ) const
{
    for( const auto &field : fields_ )
    {
        if( const auto value = std::get_if<std::string>( &field.value ) ) packet.set_string( field.name, *value );
        else if( const auto value = std::get_if<int64_t>( &field.value ) ) packet.set_number( field.name, *value );
        else if( const auto value = std::get_if<std::vector<uint8_t>>( &field.value ) ) packet.set_binary( field.name, *value );
    }
}

bool aries_field_set::empty() const
{
    return fields_.empty();
}
