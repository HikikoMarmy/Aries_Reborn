#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>

inline void AriesModuleCopyText( char *destination, size_t destination_size, const char *source )
{
    if( destination == nullptr || destination_size == 0 ) return;
    destination[0] = '\0';
    if( source == nullptr ) return;
    const auto length = std::min( destination_size - 1, std::strlen( source ) );
    std::memcpy( destination, source, length );
    destination[length] = '\0';
}
