#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

struct aries_account_record
{
    int64_t id{ 0 };
    std::string name;
    std::string password_hash;
    std::string password_salt;
    std::string email;
    std::string birth_date;
    std::string gender;
    std::string spam;
    int32_t tos{ 1 };
};

struct aries_persona_record
{
    int64_t id{ 0 };
    int64_t account_id{ 0 };
    std::string name;
};

struct aries_buddy_record
{
    int64_t persona_id{ 0 };
    std::string name;
    std::string group_name;
};

enum class aries_database_result
{
    success,
    duplicate,
    not_found,
    limit_reached,
    invalid,
    error
};

class AriesDatabase
{
public:
    AriesDatabase();
    ~AriesDatabase();

    bool initialize( const std::string &path );
    void close();
    bool is_open() const;

    aries_database_result create_account(
        const std::string &name,
        const std::string &password_hash,
        const std::string &password_salt,
        const std::string &email,
        const std::string &birth_date,
        const std::string &gender,
        const std::string &spam,
        int32_t tos,
        int64_t &account_id
    );
    bool get_account( const std::string &name, aries_account_record &account );
    bool get_account( int64_t account_id, aries_account_record &account );
    bool update_account_profile(
        int64_t account_id,
        const std::string &email,
        const std::string &birth_date,
        const std::string &gender,
        const std::string &spam,
        int32_t tos
    );
    bool update_account_password( int64_t account_id, const std::string &password_hash, const std::string &password_salt );
    bool record_login( int64_t account_id, int64_t persona_id, const std::string &ip_address, const std::string &machine_id, int32_t hwflag, int32_t hwmask );

    std::vector<aries_persona_record> get_personas( int64_t account_id );
    bool get_persona( int64_t account_id, const std::string &name, aries_persona_record &persona );
    bool get_persona_by_name( const std::string &name, aries_persona_record &persona );
    aries_database_result create_persona( int64_t account_id, const std::string &name, uint32_t max_personas, aries_persona_record &persona );
    aries_database_result delete_persona( int64_t account_id, const std::string &name );
    bool touch_persona( int64_t persona_id );

    aries_database_result add_buddy( int64_t owner_persona_id, const std::string &target_name, const std::string &group_name );
    aries_database_result delete_buddy( int64_t owner_persona_id, const std::string &target_name );
    std::vector<aries_buddy_record> get_buddies( int64_t owner_persona_id );

    aries_database_result add_block( int64_t owner_persona_id, const std::string &target_name );
    aries_database_result delete_block( int64_t owner_persona_id, const std::string &target_name );
    std::vector<aries_buddy_record> get_blocks( int64_t owner_persona_id );

    std::vector<uint8_t> get_title_blob( int64_t persona_id, const std::string &title, const std::string &key );
    bool set_title_blob( int64_t persona_id, const std::string &title, const std::string &key, const std::vector<uint8_t> &blob );

    const std::string &last_error() const;

private:
    bool execute( const char *sql );
    bool execute_retry( const char *sql, uint32_t attempts, uint32_t delay_ms );
    bool create_schema();
    void set_error( const std::string &value );
    void set_sqlite_error();

    sqlite3 *database_{ nullptr };
    mutable std::mutex mutex_;
    std::string last_error_;
};
