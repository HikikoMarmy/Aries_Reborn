#pragma once

#include <string>
#include <string_view>

namespace aries_crypto
{
    struct password_record
    {
        std::string salt;
        std::string hash;
    };

    password_record hash_password( std::string_view password );
    bool verify_password( std::string_view password, std::string_view salt_hex, std::string_view hash_hex );
}
