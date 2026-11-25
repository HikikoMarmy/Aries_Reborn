#include "stdafx.h"
#include "password_hash.h"

#include <random>

namespace aries_crypto
{
    struct sha256_context
    {
        std::array<uint8_t, 64> data{};
        uint32_t data_length{ 0 };
        uint64_t bit_length{ 0 };
        std::array<uint32_t, 8> state
        {
            0x6A09E667u, 0xBB67AE85u, 0x3C6EF372u, 0xA54FF53Au,
            0x510E527Fu, 0x9B05688Cu, 0x1F83D9ABu, 0x5BE0CD19u
        };
    };

    constexpr std::array<uint32_t, 64> sha256_constants
    {
        0x428A2F98u, 0x71374491u, 0xB5C0FBCFu, 0xE9B5DBA5u, 0x3956C25Bu, 0x59F111F1u, 0x923F82A4u, 0xAB1C5ED5u,
        0xD807AA98u, 0x12835B01u, 0x243185BEu, 0x550C7DC3u, 0x72BE5D74u, 0x80DEB1FEu, 0x9BDC06A7u, 0xC19BF174u,
        0xE49B69C1u, 0xEFBE4786u, 0x0FC19DC6u, 0x240CA1CCu, 0x2DE92C6Fu, 0x4A7484AAu, 0x5CB0A9DCu, 0x76F988DAu,
        0x983E5152u, 0xA831C66Du, 0xB00327C8u, 0xBF597FC7u, 0xC6E00BF3u, 0xD5A79147u, 0x06CA6351u, 0x14292967u,
        0x27B70A85u, 0x2E1B2138u, 0x4D2C6DFCu, 0x53380D13u, 0x650A7354u, 0x766A0ABBu, 0x81C2C92Eu, 0x92722C85u,
        0xA2BFE8A1u, 0xA81A664Bu, 0xC24B8B70u, 0xC76C51A3u, 0xD192E819u, 0xD6990624u, 0xF40E3585u, 0x106AA070u,
        0x19A4C116u, 0x1E376C08u, 0x2748774Cu, 0x34B0BCB5u, 0x391C0CB3u, 0x4ED8AA4Au, 0x5B9CCA4Fu, 0x682E6FF3u,
        0x748F82EEu, 0x78A5636Fu, 0x84C87814u, 0x8CC70208u, 0x90BEFFFAu, 0xA4506CEBu, 0xBEF9A3F7u, 0xC67178F2u
    };

    uint32_t rotate_right( uint32_t value, uint32_t amount )
    {
        return ( value >> amount ) | ( value << ( 32 - amount ) );
    }

    void sha256_transform( sha256_context &context, const uint8_t *data )
    {
        std::array<uint32_t, 64> words{};
        for( size_t i = 0; i < 16; ++i )
        {
            words[i] = ( static_cast<uint32_t>( data[i * 4] ) << 24 ) |
                       ( static_cast<uint32_t>( data[i * 4 + 1] ) << 16 ) |
                       ( static_cast<uint32_t>( data[i * 4 + 2] ) << 8 ) |
                       static_cast<uint32_t>( data[i * 4 + 3] );
        }

        for( size_t i = 16; i < words.size(); ++i )
        {
            const uint32_t s0 = rotate_right( words[i - 15], 7 ) ^ rotate_right( words[i - 15], 18 ) ^ ( words[i - 15] >> 3 );
            const uint32_t s1 = rotate_right( words[i - 2], 17 ) ^ rotate_right( words[i - 2], 19 ) ^ ( words[i - 2] >> 10 );
            words[i] = words[i - 16] + s0 + words[i - 7] + s1;
        }

        uint32_t a = context.state[0];
        uint32_t b = context.state[1];
        uint32_t c = context.state[2];
        uint32_t d = context.state[3];
        uint32_t e = context.state[4];
        uint32_t f = context.state[5];
        uint32_t g = context.state[6];
        uint32_t h = context.state[7];

        for( size_t i = 0; i < words.size(); ++i )
        {
            const uint32_t s1 = rotate_right( e, 6 ) ^ rotate_right( e, 11 ) ^ rotate_right( e, 25 );
            const uint32_t choice = ( e & f ) ^ ( ( ~e ) & g );
            const uint32_t temp1 = h + s1 + choice + sha256_constants[i] + words[i];
            const uint32_t s0 = rotate_right( a, 2 ) ^ rotate_right( a, 13 ) ^ rotate_right( a, 22 );
            const uint32_t majority = ( a & b ) ^ ( a & c ) ^ ( b & c );
            const uint32_t temp2 = s0 + majority;

            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        context.state[0] += a;
        context.state[1] += b;
        context.state[2] += c;
        context.state[3] += d;
        context.state[4] += e;
        context.state[5] += f;
        context.state[6] += g;
        context.state[7] += h;
    }

    void sha256_update( sha256_context &context, const uint8_t *data, size_t size )
    {
        for( size_t i = 0; i < size; ++i )
        {
            context.data[context.data_length++] = data[i];
            if( context.data_length == 64 )
            {
                sha256_transform( context, context.data.data() );
                context.bit_length += 512;
                context.data_length = 0;
            }
        }
    }

    std::array<uint8_t, 32> sha256_final( sha256_context &context )
    {
        uint32_t i = context.data_length;
        context.data[i++] = 0x80;

        if( i > 56 )
        {
            while( i < 64 ) context.data[i++] = 0;
            sha256_transform( context, context.data.data() );
            i = 0;
        }

        while( i < 56 ) context.data[i++] = 0;
        context.bit_length += static_cast<uint64_t>( context.data_length ) * 8;

        for( uint32_t shift = 0; shift < 8; ++shift )
        {
            context.data[63 - shift] = static_cast<uint8_t>( context.bit_length >> ( shift * 8 ) );
        }
        sha256_transform( context, context.data.data() );

        std::array<uint8_t, 32> output{};
        for( size_t word = 0; word < context.state.size(); ++word )
        {
            output[word * 4] = static_cast<uint8_t>( context.state[word] >> 24 );
            output[word * 4 + 1] = static_cast<uint8_t>( context.state[word] >> 16 );
            output[word * 4 + 2] = static_cast<uint8_t>( context.state[word] >> 8 );
            output[word * 4 + 3] = static_cast<uint8_t>( context.state[word] );
        }
        return output;
    }

    std::array<uint8_t, 32> sha256( const uint8_t *data, size_t size )
    {
        sha256_context context;
        sha256_update( context, data, size );
        return sha256_final( context );
    }

    std::array<uint8_t, 32> hmac_sha256( const uint8_t *key, size_t key_size, const uint8_t *data, size_t data_size )
    {
        std::array<uint8_t, 64> normalized_key{};
        if( key_size > normalized_key.size() )
        {
            const auto key_hash = sha256( key, key_size );
            std::copy( key_hash.begin(), key_hash.end(), normalized_key.begin() );
        }
        else if( key_size != 0 )
        {
            std::copy( key, key + key_size, normalized_key.begin() );
        }

        std::array<uint8_t, 64> inner_pad{};
        std::array<uint8_t, 64> outer_pad{};
        for( size_t i = 0; i < normalized_key.size(); ++i )
        {
            inner_pad[i] = normalized_key[i] ^ 0x36;
            outer_pad[i] = normalized_key[i] ^ 0x5C;
        }

        sha256_context inner;
        sha256_update( inner, inner_pad.data(), inner_pad.size() );
        sha256_update( inner, data, data_size );
        const auto inner_hash = sha256_final( inner );

        sha256_context outer;
        sha256_update( outer, outer_pad.data(), outer_pad.size() );
        sha256_update( outer, inner_hash.data(), inner_hash.size() );
        return sha256_final( outer );
    }

    std::array<uint8_t, 32> pbkdf2_sha256( std::string_view password, const uint8_t *salt, size_t salt_size, uint32_t iterations )
    {
        std::vector<uint8_t> input( salt, salt + salt_size );
        input.push_back( 0 );
        input.push_back( 0 );
        input.push_back( 0 );
        input.push_back( 1 );

        const auto *password_bytes = reinterpret_cast<const uint8_t *>( password.data() );
        auto block = hmac_sha256( password_bytes, password.size(), input.data(), input.size() );
        auto output = block;

        for( uint32_t iteration = 1; iteration < iterations; ++iteration )
        {
            block = hmac_sha256( password_bytes, password.size(), block.data(), block.size() );
            for( size_t i = 0; i < output.size(); ++i ) output[i] ^= block[i];
        }

        return output;
    }

    std::string bytes_to_hex( const uint8_t *data, size_t size )
    {
        static constexpr char hex[] = "0123456789abcdef";
        std::string output;
        output.reserve( size * 2 );
        for( size_t i = 0; i < size; ++i )
        {
            output.push_back( hex[( data[i] >> 4 ) & 0x0F] );
            output.push_back( hex[data[i] & 0x0F] );
        }
        return output;
    }

    std::vector<uint8_t> hex_to_bytes( std::string_view value )
    {
        if( value.size() % 2 != 0 ) return {};

        const auto nibble = []( char c ) -> int
        {
            if( c >= '0' && c <= '9' ) return c - '0';
            if( c >= 'a' && c <= 'f' ) return c - 'a' + 10;
            if( c >= 'A' && c <= 'F' ) return c - 'A' + 10;
            return -1;
        };

        std::vector<uint8_t> output;
        output.reserve( value.size() / 2 );
        for( size_t i = 0; i < value.size(); i += 2 )
        {
            const int high = nibble( value[i] );
            const int low = nibble( value[i + 1] );
            if( high < 0 || low < 0 ) return {};
            output.push_back( static_cast<uint8_t>( ( high << 4 ) | low ) );
        }
        return output;
    }

    bool constant_time_equal( const uint8_t *left, const uint8_t *right, size_t size )
    {
        uint8_t difference = 0;
        for( size_t i = 0; i < size; ++i ) difference |= left[i] ^ right[i];
        return difference == 0;
    }

    password_record hash_password( std::string_view password )
    {
        constexpr uint32_t iterations = 100000;
        std::array<uint8_t, 16> salt{};
        std::random_device random;
        for( auto &byte : salt ) byte = static_cast<uint8_t>( random() );

        const auto hash = pbkdf2_sha256( password, salt.data(), salt.size(), iterations );
        return { bytes_to_hex( salt.data(), salt.size() ), bytes_to_hex( hash.data(), hash.size() ) };
    }

    bool verify_password( std::string_view password, std::string_view salt_hex, std::string_view hash_hex )
    {
        constexpr uint32_t iterations = 100000;
        const auto salt = hex_to_bytes( salt_hex );
        const auto expected = hex_to_bytes( hash_hex );
        if( salt.empty() || expected.size() != 32 ) return false;

        const auto actual = pbkdf2_sha256( password, salt.data(), salt.size(), iterations );
        return constant_time_equal( actual.data(), expected.data(), expected.size() );
    }
}
