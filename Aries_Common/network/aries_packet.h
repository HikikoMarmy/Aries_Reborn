#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class aries_packet
{
public:
    aries_packet();
    explicit aries_packet( std::vector<uint8_t> raw );
    aries_packet( uint32_t command, uint32_t code, std::string payload, bool null_terminate = true );

    uint32_t get_command() const;
    uint32_t get_code() const;
    uint32_t get_length() const;
    uint32_t get_notice() const;
    bool is_notice() const;

    const std::string &payload() const;
    const std::vector<uint8_t> &raw() const;

    bool has_field( std::string_view name ) const;
    std::string get_string( std::string_view name, std::string_view fallback = {} ) const;
    int32_t get_number( std::string_view name, int32_t fallback = 0 ) const;
    std::vector<uint8_t> get_binary( std::string_view name ) const;

    void set_string( std::string_view name, std::string_view value );
    void set_number( std::string_view name, int64_t value );
    void set_epoch( std::string_view name, uint32_t epoch );
    void set_binary( std::string_view name, const uint8_t *data, size_t size );
    void set_binary( std::string_view name, const std::vector<uint8_t> &data );
    void set_payload( std::string payload, bool null_terminate = true );

    std::string command_string() const;

    static std::shared_ptr<aries_packet> make_notice( uint32_t notice );

private:
    static uint32_t read_be32( const uint8_t *data );
    static void write_be32( uint8_t *data, uint32_t value );
    static bool equal_ascii_case( std::string_view left, std::string_view right );
    static std::string decode_value( std::string_view value );
    static std::string encode_value( std::string_view value );

    std::string find_field( std::string_view name, bool &found ) const;
    void rebuild();

    uint32_t command_{ 0 };
    uint32_t code_{ 0 };
    uint32_t notice_{ 0 };
    bool notice_packet_{ false };
    bool null_terminate_{ true };
    std::string payload_;
    std::vector<uint8_t> raw_;
};

std::shared_ptr<aries_packet> make_packet( uint32_t command, uint32_t code = 0 );
std::shared_ptr<aries_packet> make_packet( uint32_t command, uint32_t code, const std::string &payload );
