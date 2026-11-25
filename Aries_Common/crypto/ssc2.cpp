#include "stdafx.h"
#include "ssc2.h"

namespace aries_crypto
{
    struct ssc2_state
    {
        std::array<uint8_t, 256> sbox{};
        uint8_t index{ 0 };
        uint32_t accumulator{ 0 };
    };

    constexpr std::array<uint32_t, 256> make_crc32_table()
    {
        std::array<uint32_t, 256> table{};
        for( uint32_t i = 0; i < table.size(); ++i )
        {
            uint32_t value = i;
            for( uint32_t bit = 0; bit < 8; ++bit )
            {
                value = ( value & 1 ) != 0 ? ( value >> 1 ) ^ 0xEDB88320u : value >> 1;
            }
            table[i] = value;
        }
        return table;
    }

    constexpr auto crc32_table = make_crc32_table();

    void ssc2_init( ssc2_state &state, const uint8_t *key, size_t key_length, uint32_t iterations )
    {
        for( size_t i = 0; i < state.sbox.size(); ++i ) state.sbox[i] = static_cast<uint8_t>( i );
        state.index = 0;
        state.accumulator = 0;

        if( nullptr == key || key_length == 0 || iterations == 0 ) return;

        uint32_t accumulator = 0;
        const size_t count = static_cast<size_t>( iterations ) * state.sbox.size();
        for( size_t i = 0; i < count; ++i )
        {
            uint8_t low = static_cast<uint8_t>( accumulator & 0xFF );
            const uint8_t si = state.sbox[i & 0xFF];
            accumulator >>= 8;
            low ^= key[i % key_length];
            accumulator ^= crc32_table[low];

            const uint8_t low2 = static_cast<uint8_t>( accumulator & 0xFF ) ^ si;
            accumulator >>= 8;
            accumulator ^= crc32_table[low2];

            const uint8_t j = static_cast<uint8_t>( accumulator & 0xFF );
            std::swap( state.sbox[i & 0xFF], state.sbox[j] );
        }
    }

    void ssc2_apply( ssc2_state &state, uint8_t *data, size_t size )
    {
        if( nullptr == data ) return;

        uint8_t index = state.index;
        uint32_t accumulator = state.accumulator;

        for( size_t offset = 0; offset < size; ++offset )
        {
            uint8_t low = static_cast<uint8_t>( accumulator & 0xFF );
            accumulator >>= 8;

            const uint8_t si = state.sbox[index];
            index = static_cast<uint8_t>( index + 1 );
            low ^= si;
            accumulator ^= crc32_table[low];

            const uint8_t old_i = state.sbox[index];
            const uint8_t j = static_cast<uint8_t>( accumulator & 0xFF );
            const uint8_t old_j = state.sbox[j];
            state.sbox[index] = old_j;
            state.sbox[j] = old_i;

            const uint8_t key_byte = state.sbox[static_cast<uint8_t>( old_i - old_j )];
            data[offset] ^= key_byte;
        }

        state.index = index;
        state.accumulator = accumulator;
    }

    std::string ssc2_decrypt_password( std::string_view value, const std::array<uint8_t, 16> &key )
    {
        if( value.empty() ) return {};
        if( value.front() == '~' ) value.remove_prefix( 1 );

        ssc2_state state;
        ssc2_init( state, key.data(), key.size(), 16 );

        std::string output;
        output.reserve( value.size() );
        uint8_t state_byte = 0;

        for( const unsigned char encrypted : value )
        {
            ssc2_apply( state, &state_byte, 1 );
            const int key_value = static_cast<int>( state_byte % 96 ) - 64;
            int decoded = static_cast<int>( encrypted ) - key_value;
            decoded %= 96;
            decoded += 32;

            if( static_cast<int8_t>( decoded ) == 127 ) break;
            output.push_back( static_cast<char>( static_cast<uint8_t>( decoded ) ) );
        }

        return output;
    }
}
