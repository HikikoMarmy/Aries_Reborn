#pragma once

#include <cstdint>

constexpr uint32_t MakeFourCC( char a, char b, char c, char d )
{
    return ( static_cast<uint32_t>( static_cast<uint8_t>( a ) ) << 24 ) |
           ( static_cast<uint32_t>( static_cast<uint8_t>( b ) ) << 16 ) |
           ( static_cast<uint32_t>( static_cast<uint8_t>( c ) ) << 8 ) |
           static_cast<uint32_t>( static_cast<uint8_t>( d ) );
}

namespace AriesCommand
{
    constexpr uint32_t directory = MakeFourCC( '@', 'd', 'i', 'r' );
    constexpr uint32_t addr = MakeFourCC( 'a', 'd', 'd', 'r' );
    constexpr uint32_t skey = MakeFourCC( 's', 'k', 'e', 'y' );
    constexpr uint32_t sele = MakeFourCC( 's', 'e', 'l', 'e' );
    constexpr uint32_t auth = MakeFourCC( 'a', 'u', 't', 'h' );
    constexpr uint32_t acct = MakeFourCC( 'a', 'c', 'c', 't' );
    constexpr uint32_t pers = MakeFourCC( 'p', 'e', 'r', 's' );
    constexpr uint32_t news = MakeFourCC( 'n', 'e', 'w', 's' );
    constexpr uint32_t news0 = MakeFourCC( 'n', 'e', 'w', '0' );
    constexpr uint32_t news1 = MakeFourCC( 'n', 'e', 'w', '1' );
    constexpr uint32_t room = MakeFourCC( 'r', 'o', 'o', 'm' );
    constexpr uint32_t move = MakeFourCC( 'm', 'o', 'v', 'e' );
    constexpr uint32_t peek = MakeFourCC( 'p', 'e', 'e', 'k' );
    constexpr uint32_t mesg = MakeFourCC( 'm', 'e', 's', 'g' );
    constexpr uint32_t quik = MakeFourCC( 'q', 'u', 'i', 'k' );
    constexpr uint32_t chal = MakeFourCC( 'c', 'h', 'a', 'l' );
    constexpr uint32_t rank = MakeFourCC( 'r', 'a', 'n', 'k' );
    constexpr uint32_t snap = MakeFourCC( 's', 'n', 'a', 'p' );
    constexpr uint32_t user = MakeFourCC( 'u', 's', 'e', 'r' );
    constexpr uint32_t cusr = MakeFourCC( 'c', 'u', 's', 'r' );
    constexpr uint32_t onln = MakeFourCC( 'o', 'n', 'l', 'n' );
    constexpr uint32_t rept = MakeFourCC( 'r', 'e', 'p', 't' );
    constexpr uint32_t edit = MakeFourCC( 'e', 'd', 'i', 't' );
    constexpr uint32_t cper = MakeFourCC( 'c', 'p', 'e', 'r' );
    constexpr uint32_t dper = MakeFourCC( 'd', 'p', 'e', 'r' );
    constexpr uint32_t lost = MakeFourCC( 'l', 'o', 's', 't' );
    constexpr uint32_t pass = MakeFourCC( 'p', 'a', 's', 's' );
    constexpr uint32_t auxi = MakeFourCC( 'a', 'u', 'x', 'i' );
    constexpr uint32_t user_add = MakeFourCC( '+', 'u', 's', 'r' );
    constexpr uint32_t room_add = MakeFourCC( '+', 'r', 'o', 'm' );
    constexpr uint32_t message_add = MakeFourCC( '+', 'm', 's', 'g' );
    constexpr uint32_t population = MakeFourCC( '+', 'p', 'o', 'p' );
    constexpr uint32_t session = MakeFourCC( '+', 's', 'e', 's' );
    constexpr uint32_t lobby_ping = MakeFourCC( '~', 'p', 'n', 'g' );

    constexpr uint32_t buddy_auth = MakeFourCC( 'A', 'U', 'T', 'H' );
    constexpr uint32_t buddy_ping = MakeFourCC( 'P', 'I', 'N', 'G' );
    constexpr uint32_t buddy_rost = MakeFourCC( 'R', 'O', 'S', 'T' );
    constexpr uint32_t buddy_rget = MakeFourCC( 'R', 'G', 'E', 'T' );
    constexpr uint32_t buddy_radd = MakeFourCC( 'R', 'A', 'D', 'D' );
    constexpr uint32_t buddy_rdel = MakeFourCC( 'R', 'D', 'E', 'L' );
    constexpr uint32_t buddy_send = MakeFourCC( 'S', 'E', 'N', 'D' );
    constexpr uint32_t buddy_recv = MakeFourCC( 'R', 'E', 'C', 'V' );
    constexpr uint32_t buddy_pset = MakeFourCC( 'P', 'S', 'E', 'T' );
    constexpr uint32_t buddy_pget = MakeFourCC( 'P', 'G', 'E', 'T' );
    constexpr uint32_t buddy_disc = MakeFourCC( 'D', 'I', 'S', 'C' );
}
