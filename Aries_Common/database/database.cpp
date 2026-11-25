#include "stdafx.h"
#include "database.h"

#include <sqlite3.h>

#ifdef _WIN32
#pragma comment( lib, "sqlite3.lib" )
#endif

AriesDatabase::AriesDatabase() = default;

AriesDatabase::~AriesDatabase()
{
    close();
}

bool AriesDatabase::initialize( const std::string &path )
{
    std::lock_guard lock( mutex_ );
    if( nullptr != database_ ) return true;

    const int result = sqlite3_open_v2( path.c_str(), &database_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr );
    if( result != SQLITE_OK )
    {
        set_sqlite_error();
        if( nullptr != database_ )
        {
            sqlite3_close( database_ );
            database_ = nullptr;
        }
        return false;
    }

    sqlite3_busy_timeout( database_, 10000 );
    if( false == execute_retry( "PRAGMA foreign_keys=ON;", 100, 50 ) ) return false;
    if( false == execute_retry( "PRAGMA journal_mode=WAL;", 100, 50 ) ) return false;
    if( false == execute_retry( "PRAGMA synchronous=NORMAL;", 100, 50 ) ) return false;
    return create_schema();
}

void AriesDatabase::close()
{
    std::lock_guard lock( mutex_ );
    if( nullptr == database_ ) return;
    sqlite3_close( database_ );
    database_ = nullptr;
}

bool AriesDatabase::is_open() const
{
    std::lock_guard lock( mutex_ );
    return nullptr != database_;
}

bool AriesDatabase::execute( const char *sql )
{
    char *error = nullptr;
    const int result = sqlite3_exec( database_, sql, nullptr, nullptr, &error );
    if( result == SQLITE_OK ) return true;

    if( nullptr != error )
    {
        set_error( error );
        sqlite3_free( error );
    }
    else
    {
        set_sqlite_error();
    }
    return false;
}

bool AriesDatabase::execute_retry( const char *sql, uint32_t attempts, uint32_t delay_ms )
{
    for( uint32_t attempt = 0; attempt < attempts; ++attempt )
    {
        if( execute( sql ) ) return true;

        const auto code = sqlite3_extended_errcode( database_ );
        if( code != SQLITE_BUSY && code != SQLITE_LOCKED && ( code & 0xFF ) != SQLITE_BUSY && ( code & 0xFF ) != SQLITE_LOCKED ) return false;
        if( attempt + 1 < attempts ) std::this_thread::sleep_for( std::chrono::milliseconds( delay_ms ) );
    }

    return false;
}

bool AriesDatabase::create_schema()
{
    static constexpr const char *schema = R"SQL(
CREATE TABLE IF NOT EXISTS accounts (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL COLLATE NOCASE UNIQUE,
    password_hash TEXT NOT NULL,
    password_salt TEXT NOT NULL,
    email TEXT NOT NULL DEFAULT '',
    birth_date TEXT NOT NULL DEFAULT '',
    gender TEXT NOT NULL DEFAULT '',
    spam TEXT NOT NULL DEFAULT 'NN',
    tos INTEGER NOT NULL DEFAULT 1,
    created_at INTEGER NOT NULL DEFAULT (unixepoch()),
    updated_at INTEGER NOT NULL DEFAULT (unixepoch()),
    last_login_at INTEGER,
    last_login_ip TEXT NOT NULL DEFAULT '',
    machine_id TEXT NOT NULL DEFAULT '',
    hwflag INTEGER NOT NULL DEFAULT 0,
    hwmask INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE IF NOT EXISTS personas (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    name TEXT NOT NULL COLLATE NOCASE UNIQUE,
    created_at INTEGER NOT NULL DEFAULT (unixepoch()),
    last_login_at INTEGER
);
CREATE INDEX IF NOT EXISTS idx_personas_account ON personas(account_id);
CREATE TABLE IF NOT EXISTS buddy_entries (
    owner_persona_id INTEGER NOT NULL REFERENCES personas(id) ON DELETE CASCADE,
    target_persona_id INTEGER NOT NULL REFERENCES personas(id) ON DELETE CASCADE,
    group_name TEXT NOT NULL DEFAULT '',
    created_at INTEGER NOT NULL DEFAULT (unixepoch()),
    PRIMARY KEY(owner_persona_id, target_persona_id),
    CHECK(owner_persona_id <> target_persona_id)
);
CREATE TABLE IF NOT EXISTS blocked_personas (
    owner_persona_id INTEGER NOT NULL REFERENCES personas(id) ON DELETE CASCADE,
    target_persona_id INTEGER NOT NULL REFERENCES personas(id) ON DELETE CASCADE,
    created_at INTEGER NOT NULL DEFAULT (unixepoch()),
    PRIMARY KEY(owner_persona_id, target_persona_id),
    CHECK(owner_persona_id <> target_persona_id)
);
CREATE TABLE IF NOT EXISTS login_history (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    persona_id INTEGER REFERENCES personas(id) ON DELETE SET NULL,
    ip_address TEXT NOT NULL DEFAULT '',
    machine_id TEXT NOT NULL DEFAULT '',
    hwflag INTEGER NOT NULL DEFAULT 0,
    hwmask INTEGER NOT NULL DEFAULT 0,
    created_at INTEGER NOT NULL DEFAULT (unixepoch())
);
CREATE INDEX IF NOT EXISTS idx_login_history_account ON login_history(account_id, created_at DESC);
CREATE TABLE IF NOT EXISTS title_data (
    persona_id INTEGER NOT NULL REFERENCES personas(id) ON DELETE CASCADE,
    title TEXT NOT NULL,
    data_key TEXT NOT NULL,
    data BLOB NOT NULL DEFAULT X'',
    updated_at INTEGER NOT NULL DEFAULT (unixepoch()),
    PRIMARY KEY(persona_id, title, data_key)
);
PRAGMA user_version=2;
)SQL";
    return execute_retry( schema, 100, 50 );
}

void AriesDatabase::set_error( const std::string &value )
{
    last_error_ = value;
}

void AriesDatabase::set_sqlite_error()
{
    last_error_ = nullptr != database_ ? sqlite3_errmsg( database_ ) : "SQLite database is not open";
}

const std::string &AriesDatabase::last_error() const
{
    return last_error_;
}

aries_database_result AriesDatabase::create_account(
    const std::string &name,
    const std::string &password_hash,
    const std::string &password_salt,
    const std::string &email,
    const std::string &birth_date,
    const std::string &gender,
    const std::string &spam,
    int32_t tos,
    int64_t &account_id )
{
    std::lock_guard lock( mutex_ );
    account_id = 0;
    if( nullptr == database_ || name.empty() || password_hash.empty() || password_salt.empty() ) return aries_database_result::invalid;

    sqlite3_stmt *statement = nullptr;
    static constexpr const char *sql =
        "INSERT INTO accounts(name,password_hash,password_salt,email,birth_date,gender,spam,tos) VALUES(?,?,?,?,?,?,?,?);";
    if( sqlite3_prepare_v2( database_, sql, -1, &statement, nullptr ) != SQLITE_OK )
    {
        set_sqlite_error();
        return aries_database_result::error;
    }

    sqlite3_bind_text( statement, 1, name.c_str(), -1, SQLITE_TRANSIENT );
    sqlite3_bind_text( statement, 2, password_hash.c_str(), -1, SQLITE_TRANSIENT );
    sqlite3_bind_text( statement, 3, password_salt.c_str(), -1, SQLITE_TRANSIENT );
    sqlite3_bind_text( statement, 4, email.c_str(), -1, SQLITE_TRANSIENT );
    sqlite3_bind_text( statement, 5, birth_date.c_str(), -1, SQLITE_TRANSIENT );
    sqlite3_bind_text( statement, 6, gender.c_str(), -1, SQLITE_TRANSIENT );
    sqlite3_bind_text( statement, 7, spam.c_str(), -1, SQLITE_TRANSIENT );
    sqlite3_bind_int( statement, 8, tos );

    const int result = sqlite3_step( statement );
    sqlite3_finalize( statement );
    if( result == SQLITE_CONSTRAINT || result == SQLITE_CONSTRAINT_UNIQUE ) return aries_database_result::duplicate;
    if( result != SQLITE_DONE )
    {
        set_sqlite_error();
        return aries_database_result::error;
    }

    account_id = sqlite3_last_insert_rowid( database_ );
    return aries_database_result::success;
}

bool AriesDatabase::get_account( const std::string &name, aries_account_record &account )
{
    std::lock_guard lock( mutex_ );
    if( nullptr == database_ ) return false;

    sqlite3_stmt *statement = nullptr;
    static constexpr const char *sql =
        "SELECT id,name,password_hash,password_salt,email,birth_date,gender,spam,tos FROM accounts WHERE name=? COLLATE NOCASE LIMIT 1;";
    if( sqlite3_prepare_v2( database_, sql, -1, &statement, nullptr ) != SQLITE_OK )
    {
        set_sqlite_error();
        return false;
    }
    sqlite3_bind_text( statement, 1, name.c_str(), -1, SQLITE_TRANSIENT );

    const int result = sqlite3_step( statement );
    if( result == SQLITE_ROW )
    {
        account.id = sqlite3_column_int64( statement, 0 );
        account.name = reinterpret_cast<const char *>( sqlite3_column_text( statement, 1 ) );
        account.password_hash = reinterpret_cast<const char *>( sqlite3_column_text( statement, 2 ) );
        account.password_salt = reinterpret_cast<const char *>( sqlite3_column_text( statement, 3 ) );
        account.email = reinterpret_cast<const char *>( sqlite3_column_text( statement, 4 ) );
        account.birth_date = reinterpret_cast<const char *>( sqlite3_column_text( statement, 5 ) );
        account.gender = reinterpret_cast<const char *>( sqlite3_column_text( statement, 6 ) );
        account.spam = reinterpret_cast<const char *>( sqlite3_column_text( statement, 7 ) );
        account.tos = sqlite3_column_int( statement, 8 );
        sqlite3_finalize( statement );
        return true;
    }

    if( result != SQLITE_DONE ) set_sqlite_error();
    sqlite3_finalize( statement );
    return false;
}

bool AriesDatabase::get_account( int64_t account_id, aries_account_record &account )
{
    std::lock_guard lock( mutex_ );
    if( nullptr == database_ ) return false;

    sqlite3_stmt *statement = nullptr;
    static constexpr const char *sql =
        "SELECT id,name,password_hash,password_salt,email,birth_date,gender,spam,tos FROM accounts WHERE id=? LIMIT 1;";
    if( sqlite3_prepare_v2( database_, sql, -1, &statement, nullptr ) != SQLITE_OK )
    {
        set_sqlite_error();
        return false;
    }
    sqlite3_bind_int64( statement, 1, account_id );

    const int result = sqlite3_step( statement );
    if( result == SQLITE_ROW )
    {
        account.id = sqlite3_column_int64( statement, 0 );
        account.name = reinterpret_cast<const char *>( sqlite3_column_text( statement, 1 ) );
        account.password_hash = reinterpret_cast<const char *>( sqlite3_column_text( statement, 2 ) );
        account.password_salt = reinterpret_cast<const char *>( sqlite3_column_text( statement, 3 ) );
        account.email = reinterpret_cast<const char *>( sqlite3_column_text( statement, 4 ) );
        account.birth_date = reinterpret_cast<const char *>( sqlite3_column_text( statement, 5 ) );
        account.gender = reinterpret_cast<const char *>( sqlite3_column_text( statement, 6 ) );
        account.spam = reinterpret_cast<const char *>( sqlite3_column_text( statement, 7 ) );
        account.tos = sqlite3_column_int( statement, 8 );
        sqlite3_finalize( statement );
        return true;
    }

    if( result != SQLITE_DONE ) set_sqlite_error();
    sqlite3_finalize( statement );
    return false;
}

bool AriesDatabase::update_account_profile(
    int64_t account_id,
    const std::string &email,
    const std::string &birth_date,
    const std::string &gender,
    const std::string &spam,
    int32_t tos )
{
    std::lock_guard lock( mutex_ );
    if( nullptr == database_ ) return false;

    sqlite3_stmt *statement = nullptr;
    static constexpr const char *sql =
        "UPDATE accounts SET email=?,birth_date=?,gender=?,spam=?,tos=?,updated_at=unixepoch() WHERE id=?;";
    if( sqlite3_prepare_v2( database_, sql, -1, &statement, nullptr ) != SQLITE_OK )
    {
        set_sqlite_error();
        return false;
    }
    sqlite3_bind_text( statement, 1, email.c_str(), -1, SQLITE_TRANSIENT );
    sqlite3_bind_text( statement, 2, birth_date.c_str(), -1, SQLITE_TRANSIENT );
    sqlite3_bind_text( statement, 3, gender.c_str(), -1, SQLITE_TRANSIENT );
    sqlite3_bind_text( statement, 4, spam.c_str(), -1, SQLITE_TRANSIENT );
    sqlite3_bind_int( statement, 5, tos );
    sqlite3_bind_int64( statement, 6, account_id );
    const bool success = sqlite3_step( statement ) == SQLITE_DONE;
    if( false == success ) set_sqlite_error();
    sqlite3_finalize( statement );
    return success;
}

bool AriesDatabase::update_account_password( int64_t account_id, const std::string &password_hash, const std::string &password_salt )
{
    std::lock_guard lock( mutex_ );
    if( nullptr == database_ ) return false;
    sqlite3_stmt *statement = nullptr;
    static constexpr const char *sql = "UPDATE accounts SET password_hash=?,password_salt=?,updated_at=unixepoch() WHERE id=?;";
    if( sqlite3_prepare_v2( database_, sql, -1, &statement, nullptr ) != SQLITE_OK ) return false;
    sqlite3_bind_text( statement, 1, password_hash.c_str(), -1, SQLITE_TRANSIENT );
    sqlite3_bind_text( statement, 2, password_salt.c_str(), -1, SQLITE_TRANSIENT );
    sqlite3_bind_int64( statement, 3, account_id );
    const bool success = sqlite3_step( statement ) == SQLITE_DONE;
    if( false == success ) set_sqlite_error();
    sqlite3_finalize( statement );
    return success;
}

bool AriesDatabase::record_login( int64_t account_id, int64_t persona_id, const std::string &ip_address, const std::string &machine_id, int32_t hwflag, int32_t hwmask )
{
    std::lock_guard lock( mutex_ );
    if( nullptr == database_ ) return false;
    if( false == execute( "BEGIN IMMEDIATE;" ) ) return false;

    sqlite3_stmt *update = nullptr;
    static constexpr const char *update_sql =
        "UPDATE accounts SET last_login_at=unixepoch(),last_login_ip=?,machine_id=?,hwflag=?,hwmask=? WHERE id=?;";
    bool success = sqlite3_prepare_v2( database_, update_sql, -1, &update, nullptr ) == SQLITE_OK;
    if( success )
    {
        sqlite3_bind_text( update, 1, ip_address.c_str(), -1, SQLITE_TRANSIENT );
        sqlite3_bind_text( update, 2, machine_id.c_str(), -1, SQLITE_TRANSIENT );
        sqlite3_bind_int( update, 3, hwflag );
        sqlite3_bind_int( update, 4, hwmask );
        sqlite3_bind_int64( update, 5, account_id );
        success = sqlite3_step( update ) == SQLITE_DONE;
    }
    sqlite3_finalize( update );

    sqlite3_stmt *insert = nullptr;
    static constexpr const char *insert_sql =
        "INSERT INTO login_history(account_id,persona_id,ip_address,machine_id,hwflag,hwmask) VALUES(?,?,?,?,?,?);";
    if( success ) success = sqlite3_prepare_v2( database_, insert_sql, -1, &insert, nullptr ) == SQLITE_OK;
    if( success )
    {
        sqlite3_bind_int64( insert, 1, account_id );
        if( persona_id > 0 ) sqlite3_bind_int64( insert, 2, persona_id ); else sqlite3_bind_null( insert, 2 );
        sqlite3_bind_text( insert, 3, ip_address.c_str(), -1, SQLITE_TRANSIENT );
        sqlite3_bind_text( insert, 4, machine_id.c_str(), -1, SQLITE_TRANSIENT );
        sqlite3_bind_int( insert, 5, hwflag );
        sqlite3_bind_int( insert, 6, hwmask );
        success = sqlite3_step( insert ) == SQLITE_DONE;
    }
    sqlite3_finalize( insert );

    if( success ) return execute( "COMMIT;" );
    set_sqlite_error();
    execute( "ROLLBACK;" );
    return false;
}

std::vector<aries_persona_record> AriesDatabase::get_personas( int64_t account_id )
{
    std::lock_guard lock( mutex_ );
    std::vector<aries_persona_record> output;
    if( nullptr == database_ ) return output;

    sqlite3_stmt *statement = nullptr;
    static constexpr const char *sql = "SELECT id,account_id,name FROM personas WHERE account_id=? ORDER BY id;";
    if( sqlite3_prepare_v2( database_, sql, -1, &statement, nullptr ) != SQLITE_OK ) return output;
    sqlite3_bind_int64( statement, 1, account_id );

    while( sqlite3_step( statement ) == SQLITE_ROW )
    {
        aries_persona_record persona;
        persona.id = sqlite3_column_int64( statement, 0 );
        persona.account_id = sqlite3_column_int64( statement, 1 );
        persona.name = reinterpret_cast<const char *>( sqlite3_column_text( statement, 2 ) );
        output.push_back( std::move( persona ) );
    }
    sqlite3_finalize( statement );
    return output;
}

bool AriesDatabase::get_persona( int64_t account_id, const std::string &name, aries_persona_record &persona )
{
    std::lock_guard lock( mutex_ );
    if( nullptr == database_ ) return false;
    sqlite3_stmt *statement = nullptr;
    static constexpr const char *sql = "SELECT id,account_id,name FROM personas WHERE account_id=? AND name=? COLLATE NOCASE LIMIT 1;";
    if( sqlite3_prepare_v2( database_, sql, -1, &statement, nullptr ) != SQLITE_OK ) return false;
    sqlite3_bind_int64( statement, 1, account_id );
    sqlite3_bind_text( statement, 2, name.c_str(), -1, SQLITE_TRANSIENT );
    const int result = sqlite3_step( statement );
    if( result == SQLITE_ROW )
    {
        persona.id = sqlite3_column_int64( statement, 0 );
        persona.account_id = sqlite3_column_int64( statement, 1 );
        persona.name = reinterpret_cast<const char *>( sqlite3_column_text( statement, 2 ) );
        sqlite3_finalize( statement );
        return true;
    }
    sqlite3_finalize( statement );
    return false;
}

bool AriesDatabase::get_persona_by_name( const std::string &name, aries_persona_record &persona )
{
    std::lock_guard lock( mutex_ );
    if( nullptr == database_ ) return false;
    sqlite3_stmt *statement = nullptr;
    static constexpr const char *sql = "SELECT id,account_id,name FROM personas WHERE name=? COLLATE NOCASE LIMIT 1;";
    if( sqlite3_prepare_v2( database_, sql, -1, &statement, nullptr ) != SQLITE_OK ) return false;
    sqlite3_bind_text( statement, 1, name.c_str(), -1, SQLITE_TRANSIENT );
    const int result = sqlite3_step( statement );
    if( result == SQLITE_ROW )
    {
        persona.id = sqlite3_column_int64( statement, 0 );
        persona.account_id = sqlite3_column_int64( statement, 1 );
        persona.name = reinterpret_cast<const char *>( sqlite3_column_text( statement, 2 ) );
        sqlite3_finalize( statement );
        return true;
    }
    sqlite3_finalize( statement );
    return false;
}

aries_database_result AriesDatabase::create_persona( int64_t account_id, const std::string &name, uint32_t max_personas, aries_persona_record &persona )
{
    std::lock_guard lock( mutex_ );
    if( nullptr == database_ || account_id <= 0 || name.empty() ) return aries_database_result::invalid;

    sqlite3_stmt *count_statement = nullptr;
    if( sqlite3_prepare_v2( database_, "SELECT COUNT(*) FROM personas WHERE account_id=?;", -1, &count_statement, nullptr ) != SQLITE_OK ) return aries_database_result::error;
    sqlite3_bind_int64( count_statement, 1, account_id );
    const int count_result = sqlite3_step( count_statement );
    const int count = count_result == SQLITE_ROW ? sqlite3_column_int( count_statement, 0 ) : static_cast<int>( max_personas );
    sqlite3_finalize( count_statement );
    if( max_personas > 0 && count >= static_cast<int>( max_personas ) ) return aries_database_result::limit_reached;

    sqlite3_stmt *statement = nullptr;
    static constexpr const char *sql = "INSERT INTO personas(account_id,name) VALUES(?,?);";
    if( sqlite3_prepare_v2( database_, sql, -1, &statement, nullptr ) != SQLITE_OK ) return aries_database_result::error;
    sqlite3_bind_int64( statement, 1, account_id );
    sqlite3_bind_text( statement, 2, name.c_str(), -1, SQLITE_TRANSIENT );
    const int result = sqlite3_step( statement );
    sqlite3_finalize( statement );
    if( result == SQLITE_CONSTRAINT || result == SQLITE_CONSTRAINT_UNIQUE ) return aries_database_result::duplicate;
    if( result != SQLITE_DONE )
    {
        set_sqlite_error();
        return aries_database_result::error;
    }

    persona.id = sqlite3_last_insert_rowid( database_ );
    persona.account_id = account_id;
    persona.name = name;

    return aries_database_result::success;
}

aries_database_result AriesDatabase::delete_persona( int64_t account_id, const std::string &name )
{
    std::lock_guard lock( mutex_ );
    if( nullptr == database_ || account_id <= 0 || name.empty() ) return aries_database_result::invalid;
    sqlite3_stmt *statement = nullptr;
    static constexpr const char *sql = "DELETE FROM personas WHERE account_id=? AND name=? COLLATE NOCASE;";
    if( sqlite3_prepare_v2( database_, sql, -1, &statement, nullptr ) != SQLITE_OK ) return aries_database_result::error;
    sqlite3_bind_int64( statement, 1, account_id );
    sqlite3_bind_text( statement, 2, name.c_str(), -1, SQLITE_TRANSIENT );
    const int result = sqlite3_step( statement );
    const int changes = sqlite3_changes( database_ );
    sqlite3_finalize( statement );
    if( result != SQLITE_DONE ) return aries_database_result::error;
    return changes > 0 ? aries_database_result::success : aries_database_result::not_found;
}

bool AriesDatabase::touch_persona( int64_t persona_id )
{
    std::lock_guard lock( mutex_ );
    if( nullptr == database_ || persona_id <= 0 ) return false;
    sqlite3_stmt *statement = nullptr;
    if( sqlite3_prepare_v2( database_, "UPDATE personas SET last_login_at=unixepoch() WHERE id=?;", -1, &statement, nullptr ) != SQLITE_OK ) return false;
    sqlite3_bind_int64( statement, 1, persona_id );
    const bool success = sqlite3_step( statement ) == SQLITE_DONE;
    sqlite3_finalize( statement );
    return success;
}

aries_database_result AriesDatabase::add_buddy( int64_t owner_persona_id, const std::string &target_name, const std::string &group_name )
{
    std::lock_guard lock( mutex_ );
    if( nullptr == database_ || owner_persona_id <= 0 || target_name.empty() ) return aries_database_result::invalid;

    sqlite3_stmt *target = nullptr;
    if( sqlite3_prepare_v2( database_, "SELECT id FROM personas WHERE name=? COLLATE NOCASE LIMIT 1;", -1, &target, nullptr ) != SQLITE_OK ) return aries_database_result::error;
    sqlite3_bind_text( target, 1, target_name.c_str(), -1, SQLITE_TRANSIENT );
    const int target_result = sqlite3_step( target );
    const int64_t target_id = target_result == SQLITE_ROW ? sqlite3_column_int64( target, 0 ) : 0;
    sqlite3_finalize( target );
    if( target_id <= 0 ) return aries_database_result::not_found;
    if( target_id == owner_persona_id ) return aries_database_result::invalid;

    sqlite3_stmt *statement = nullptr;
    static constexpr const char *sql =
        "INSERT INTO buddy_entries(owner_persona_id,target_persona_id,group_name) VALUES(?,?,?) "
        "ON CONFLICT(owner_persona_id,target_persona_id) DO UPDATE SET group_name=excluded.group_name;";
    if( sqlite3_prepare_v2( database_, sql, -1, &statement, nullptr ) != SQLITE_OK ) return aries_database_result::error;
    sqlite3_bind_int64( statement, 1, owner_persona_id );
    sqlite3_bind_int64( statement, 2, target_id );
    sqlite3_bind_text( statement, 3, group_name.c_str(), -1, SQLITE_TRANSIENT );
    const bool success = sqlite3_step( statement ) == SQLITE_DONE;
    sqlite3_finalize( statement );
    return success ? aries_database_result::success : aries_database_result::error;
}

aries_database_result AriesDatabase::delete_buddy( int64_t owner_persona_id, const std::string &target_name )
{
    std::lock_guard lock( mutex_ );
    if( nullptr == database_ ) return aries_database_result::error;
    sqlite3_stmt *statement = nullptr;
    static constexpr const char *sql =
        "DELETE FROM buddy_entries WHERE owner_persona_id=? AND target_persona_id=(SELECT id FROM personas WHERE name=? COLLATE NOCASE LIMIT 1);";
    if( sqlite3_prepare_v2( database_, sql, -1, &statement, nullptr ) != SQLITE_OK ) return aries_database_result::error;
    sqlite3_bind_int64( statement, 1, owner_persona_id );
    sqlite3_bind_text( statement, 2, target_name.c_str(), -1, SQLITE_TRANSIENT );
    const int result = sqlite3_step( statement );
    const int changes = sqlite3_changes( database_ );
    sqlite3_finalize( statement );
    if( result != SQLITE_DONE ) return aries_database_result::error;
    return changes > 0 ? aries_database_result::success : aries_database_result::not_found;
}

std::vector<aries_buddy_record> AriesDatabase::get_buddies( int64_t owner_persona_id )
{
    std::lock_guard lock( mutex_ );
    std::vector<aries_buddy_record> output;
    if( nullptr == database_ ) return output;
    sqlite3_stmt *statement = nullptr;
    static constexpr const char *sql =
        "SELECT p.id,p.name,b.group_name FROM buddy_entries b JOIN personas p ON p.id=b.target_persona_id "
        "WHERE b.owner_persona_id=? ORDER BY p.name COLLATE NOCASE;";
    if( sqlite3_prepare_v2( database_, sql, -1, &statement, nullptr ) != SQLITE_OK ) return output;
    sqlite3_bind_int64( statement, 1, owner_persona_id );
    while( sqlite3_step( statement ) == SQLITE_ROW )
    {
        aries_buddy_record record;
        record.persona_id = sqlite3_column_int64( statement, 0 );
        record.name = reinterpret_cast<const char *>( sqlite3_column_text( statement, 1 ) );
        record.group_name = reinterpret_cast<const char *>( sqlite3_column_text( statement, 2 ) );
        output.push_back( std::move( record ) );
    }
    sqlite3_finalize( statement );
    return output;
}

aries_database_result AriesDatabase::add_block( int64_t owner_persona_id, const std::string &target_name )
{
    std::lock_guard lock( mutex_ );
    if( nullptr == database_ || owner_persona_id <= 0 || target_name.empty() ) return aries_database_result::invalid;
    sqlite3_stmt *statement = nullptr;
    static constexpr const char *sql =
        "INSERT OR IGNORE INTO blocked_personas(owner_persona_id,target_persona_id) "
        "SELECT ?,id FROM personas WHERE name=? COLLATE NOCASE AND id<>?;";
    if( sqlite3_prepare_v2( database_, sql, -1, &statement, nullptr ) != SQLITE_OK ) return aries_database_result::error;
    sqlite3_bind_int64( statement, 1, owner_persona_id );
    sqlite3_bind_text( statement, 2, target_name.c_str(), -1, SQLITE_TRANSIENT );
    sqlite3_bind_int64( statement, 3, owner_persona_id );
    const int result = sqlite3_step( statement );
    const int changes = sqlite3_changes( database_ );
    sqlite3_finalize( statement );
    if( result != SQLITE_DONE ) return aries_database_result::error;
    return changes > 0 ? aries_database_result::success : aries_database_result::not_found;
}

aries_database_result AriesDatabase::delete_block( int64_t owner_persona_id, const std::string &target_name )
{
    std::lock_guard lock( mutex_ );
    if( nullptr == database_ ) return aries_database_result::error;
    sqlite3_stmt *statement = nullptr;
    static constexpr const char *sql =
        "DELETE FROM blocked_personas WHERE owner_persona_id=? AND target_persona_id=(SELECT id FROM personas WHERE name=? COLLATE NOCASE LIMIT 1);";
    if( sqlite3_prepare_v2( database_, sql, -1, &statement, nullptr ) != SQLITE_OK ) return aries_database_result::error;
    sqlite3_bind_int64( statement, 1, owner_persona_id );
    sqlite3_bind_text( statement, 2, target_name.c_str(), -1, SQLITE_TRANSIENT );
    const int result = sqlite3_step( statement );
    const int changes = sqlite3_changes( database_ );
    sqlite3_finalize( statement );
    if( result != SQLITE_DONE ) return aries_database_result::error;
    return changes > 0 ? aries_database_result::success : aries_database_result::not_found;
}

std::vector<aries_buddy_record> AriesDatabase::get_blocks( int64_t owner_persona_id )
{
    std::lock_guard lock( mutex_ );
    std::vector<aries_buddy_record> output;
    if( nullptr == database_ ) return output;
    sqlite3_stmt *statement = nullptr;
    static constexpr const char *sql =
        "SELECT p.id,p.name FROM blocked_personas b JOIN personas p ON p.id=b.target_persona_id "
        "WHERE b.owner_persona_id=? ORDER BY p.name COLLATE NOCASE;";
    if( sqlite3_prepare_v2( database_, sql, -1, &statement, nullptr ) != SQLITE_OK ) return output;
    sqlite3_bind_int64( statement, 1, owner_persona_id );
    while( sqlite3_step( statement ) == SQLITE_ROW )
    {
        aries_buddy_record record;
        record.persona_id = sqlite3_column_int64( statement, 0 );
        record.name = reinterpret_cast<const char *>( sqlite3_column_text( statement, 1 ) );
        output.push_back( std::move( record ) );
    }
    sqlite3_finalize( statement );
    return output;
}

std::vector<uint8_t> AriesDatabase::get_title_blob( int64_t persona_id, const std::string &title, const std::string &key )
{
    std::lock_guard lock( mutex_ );
    std::vector<uint8_t> output;
    if( nullptr == database_ || persona_id <= 0 || title.empty() || key.empty() ) return output;

    sqlite3_stmt *statement = nullptr;
    static constexpr const char *sql = "SELECT data FROM title_data WHERE persona_id=? AND title=? AND data_key=? LIMIT 1;";
    if( sqlite3_prepare_v2( database_, sql, -1, &statement, nullptr ) != SQLITE_OK ) return output;
    sqlite3_bind_int64( statement, 1, persona_id );
    sqlite3_bind_text( statement, 2, title.c_str(), -1, SQLITE_TRANSIENT );
    sqlite3_bind_text( statement, 3, key.c_str(), -1, SQLITE_TRANSIENT );

    if( sqlite3_step( statement ) == SQLITE_ROW )
    {
        const auto *data = static_cast<const uint8_t *>( sqlite3_column_blob( statement, 0 ) );
        const auto size = sqlite3_column_bytes( statement, 0 );
        if( nullptr != data && size > 0 ) output.assign( data, data + size );
    }
    sqlite3_finalize( statement );
    return output;
}

bool AriesDatabase::set_title_blob( int64_t persona_id, const std::string &title, const std::string &key, const std::vector<uint8_t> &blob )
{
    std::lock_guard lock( mutex_ );
    if( nullptr == database_ || persona_id <= 0 || title.empty() || key.empty() ) return false;

    sqlite3_stmt *statement = nullptr;
    static constexpr const char *sql =
        "INSERT INTO title_data(persona_id,title,data_key,data,updated_at) VALUES(?,?,?,?,unixepoch()) "
        "ON CONFLICT(persona_id,title,data_key) DO UPDATE SET data=excluded.data,updated_at=unixepoch();";
    if( sqlite3_prepare_v2( database_, sql, -1, &statement, nullptr ) != SQLITE_OK )
    {
        set_sqlite_error();
        return false;
    }

    sqlite3_bind_int64( statement, 1, persona_id );
    sqlite3_bind_text( statement, 2, title.c_str(), -1, SQLITE_TRANSIENT );
    sqlite3_bind_text( statement, 3, key.c_str(), -1, SQLITE_TRANSIENT );
    if( blob.empty() ) sqlite3_bind_zeroblob( statement, 4, 0 );
    else sqlite3_bind_blob( statement, 4, blob.data(), static_cast<int>( blob.size() ), SQLITE_TRANSIENT );
    const bool success = sqlite3_step( statement ) == SQLITE_DONE;
    if( false == success ) set_sqlite_error();
    sqlite3_finalize( statement );
    return success;
}
