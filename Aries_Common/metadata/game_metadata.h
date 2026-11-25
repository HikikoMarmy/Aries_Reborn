#pragma once

#include "../misc/types.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

struct aries_field
{
    std::string name;
    std::variant<std::string, int64_t, std::vector<uint8_t>> value;
};

class aries_field_set
{
public:
    aries_field_set &set_string( std::string_view name, std::string_view value );
    aries_field_set &set_number( std::string_view name, int64_t value );
    aries_field_set &set_binary( std::string_view name, const uint8_t *data, size_t size );
    aries_field_set &set_binary( std::string_view name, const std::vector<uint8_t> &value );
    void apply( aries_packet &packet ) const;
    bool empty() const;

private:
    std::vector<aries_field> fields_;
};

struct aries_game_metadata
{
    std::string name;
    std::string product;
    std::string sku;
    std::string lobby_key_prefix;
    std::string news_message;
    std::string buddy_product;
    uint32_t max_personas{ 5 };
    std::array<uint8_t, 16> session_key{};
    aries_field_set directory_response;
};
