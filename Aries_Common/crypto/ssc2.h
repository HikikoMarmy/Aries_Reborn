#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace aries_crypto
{
    std::string ssc2_decrypt_password( std::string_view value, const std::array<uint8_t, 16> &key );
}
