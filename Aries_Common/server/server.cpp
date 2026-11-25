#include "stdafx.h"
#include "../global_define.h"
#include "../network/buddy_protocol.h"
#include "../network/game_protocol.h"
#include "../network/lobby_protocol.h"
#include "../network/packet_control.h"
#include "server.h"

#include <filesystem>

#ifdef _WIN32
#include <windows.h>

static BOOL WINAPI ConsoleHandler( DWORD event_type )
{
    if( event_type == CTRL_C_EVENT || event_type == CTRL_CLOSE_EVENT || event_type == CTRL_BREAK_EVENT )
    {
        g_shutdown_requested = true;
        return TRUE;
    }
    return FALSE;
}

class AriesConsoleHandlerGuard
{
public:
    explicit AriesConsoleHandlerGuard( bool enabled ) : enabled_( enabled )
    {
        if( enabled_ ) SetConsoleCtrlHandler( ConsoleHandler, TRUE );
    }

    ~AriesConsoleHandlerGuard()
    {
        if( enabled_ ) SetConsoleCtrlHandler( ConsoleHandler, FALSE );
    }

private:
    bool enabled_{ false };
};

static std::filesystem::path AriesExecutableDirectory()
{
    std::array<char, 32768> path{};
    const auto length = GetModuleFileNameA( nullptr, path.data(), static_cast<DWORD>( path.size() ) );
    if( length > 0 && length < path.size() )
    {
        return std::filesystem::path( std::string( path.data(), length ) ).parent_path();
    }
    return std::filesystem::current_path();
}
#else
#include <limits.h>
#include <unistd.h>

static std::filesystem::path AriesExecutableDirectory()
{
    std::array<char, PATH_MAX> path{};
    const auto length = readlink( "/proc/self/exe", path.data(), path.size() - 1 );
    if( length > 0 )
    {
        path[static_cast<size_t>( length )] = '\0';
        return std::filesystem::path( path.data() ).parent_path();
    }
    return std::filesystem::current_path();
}
#endif

static std::filesystem::path AriesRuntimePath( const std::filesystem::path &base_path, const std::string &value )
{
    std::filesystem::path path( value );
    if( path.is_relative() ) path = base_path / path;
    return path.lexically_normal();
}

int RunAriesServer( AriesGameProtocol &game_protocol, const aries_server_options &options )
{
#ifdef _WIN32
    AriesConsoleHandlerGuard console_handler( options.shutdown_requested == nullptr );
#endif

    std::atomic<bool> *shutdown_requested = options.shutdown_requested;
    if( shutdown_requested == nullptr )
    {
        g_shutdown_requested = false;
        shutdown_requested = &g_shutdown_requested;
    }

    g_gameProtocol = &game_protocol;
    g_configuration = std::make_unique<CConfiguration>();
    g_configuration->lobby_port = options.lobby_port;
    g_configuration->buddy_port = options.buddy_port;
    g_configuration->database_path = options.database_path;

    const auto executable_directory = AriesExecutableDirectory();
    const auto config_path = AriesRuntimePath( executable_directory, options.config_path );

    if( false == g_configuration->load( config_path.string(), options.config_section ) )
    {
        logging.warning( L"Config %hs not found. Using defaults.", config_path.string().c_str() );
    }

    if( false == options.bind_ip_override.empty() ) g_configuration->bind_ip = options.bind_ip_override;
    if( false == options.service_ip_override.empty() ) g_configuration->service_ip = options.service_ip_override;

    const auto database_path = AriesRuntimePath( executable_directory, g_configuration->database_path );
    std::error_code filesystem_error;
    if( false == database_path.parent_path().empty() )
    {
        std::filesystem::create_directories( database_path.parent_path(), filesystem_error );
        if( filesystem_error )
        {
            logging.error( L"Database directory creation failed: %hs", filesystem_error.message().c_str() );
            g_configuration.reset();
            g_gameProtocol = nullptr;
            return EXIT_FAILURE;
        }
    }
    g_configuration->database_path = database_path.string();

    g_database = std::make_unique<AriesDatabase>();
    if( false == g_database->initialize( g_configuration->database_path ) )
    {
        logging.error( L"Database initialization failed: %hs", g_database->last_error().c_str() );
        g_database.reset();
        g_configuration.reset();
        g_gameProtocol = nullptr;
        return EXIT_FAILURE;
    }

    g_clientMng = std::make_unique<AriesClientManager>();
    g_socketMng = std::make_unique<CSocketManager>();

    logging.information( L"%hs Server", game_protocol.metadata().name.c_str() );
    logging.information( L"Config: %hs [%hs]", config_path.string().c_str(), options.config_section.empty() ? "global" : options.config_section.c_str() );
    logging.information( L"ProtoAries Lobby: %hs:%u", g_configuration->bind_ip.c_str(), g_configuration->lobby_port );
    if( g_configuration->buddy_port > 0 ) logging.information( L"Buddy: %hs:%u", g_configuration->bind_ip.c_str(), g_configuration->buddy_port );
    logging.information( L"Advertised service IP: %hs", g_configuration->service_ip.c_str() );
    logging.information( L"Shared database: %hs", g_configuration->database_path.c_str() );

    if( false == g_socketMng->Initialize() )
    {
        logging.error( L"Server initialization failed" );
        g_socketMng.reset();
        g_clientMng.reset();
        g_database.reset();
        g_configuration.reset();
        g_gameProtocol = nullptr;
        return EXIT_FAILURE;
    }

    if( options.started_callback != nullptr ) options.started_callback( options.callback_context );

    while( false == shutdown_requested->load() )
    {
        for( int i = 0; i < 64; ++i ) aries_network::Process();
        Protocol::Lobby::update();
        Protocol::Buddy::update();
        game_protocol.update();
        std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
    }

    logging.information( L"Shutting down" );
    g_socketMng->shutdown();
    g_socketMng.reset();
    g_clientMng.reset();
    g_database.reset();
    g_configuration.reset();
    g_gameProtocol = nullptr;
    return EXIT_SUCCESS;
}
