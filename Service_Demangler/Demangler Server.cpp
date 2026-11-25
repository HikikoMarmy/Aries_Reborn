#include <configuration.h>
#include <demangler/demangler_server.h>
#include <global_define.h>
#include <platform/socket_compat.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <thread>

#ifdef _WIN32
#include <windows.h>

static std::filesystem::path DemanglerExecutableDirectory()
{
    std::array<char, 32768> path{};
    const auto length = GetModuleFileNameA( nullptr, path.data(), static_cast<DWORD>( path.size() ) );
    if( length > 0 && length < path.size() ) return std::filesystem::path( std::string( path.data(), length ) ).parent_path();
    return std::filesystem::current_path();
}

static BOOL WINAPI DemanglerConsoleHandler( DWORD event_type )
{
    if( event_type == CTRL_C_EVENT || event_type == CTRL_CLOSE_EVENT || event_type == CTRL_BREAK_EVENT )
    {
        g_shutdown_requested = true;
        return TRUE;
    }
    return FALSE;
}
#else
#include <limits.h>
#include <unistd.h>

static std::filesystem::path DemanglerExecutableDirectory()
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

int main( int argc, char **argv )
{
    std::filesystem::path config_path = argc > 1 ? std::filesystem::path( argv[1] ) : std::filesystem::path( "aries.ini" );
    if( config_path.is_relative() ) config_path = DemanglerExecutableDirectory() / config_path;
    config_path = config_path.lexically_normal();

    CConfiguration configuration;
    if( false == configuration.load( config_path.string(), "demangler" ) )
    {
        std::cout << config_path.string() << " not found. Using defaults." << std::endl;
    }

    if( false == configuration.demangler_enabled )
    {
        std::cout << "Demangler is disabled in " << config_path.string() << std::endl;
        return EXIT_SUCCESS;
    }

    const auto public_ip = configuration.demangler_ip.empty() ? configuration.service_ip : configuration.demangler_ip;

#ifdef _WIN32
    WSADATA wsa_data{};
    if( WSAStartup( MAKEWORD( 2, 2 ), &wsa_data ) != 0 )
    {
        std::cerr << "WSAStartup failed" << std::endl;
        return EXIT_FAILURE;
    }
    SetConsoleCtrlHandler( DemanglerConsoleHandler, TRUE );
#endif

    g_shutdown_requested = false;

    CDemanglerServer demangler;
    if( false == demangler.start(
        configuration.bind_ip,
        public_ip,
        configuration.demangler_ports,
        configuration.demangler_probe_count,
        configuration.demangler_session_timeout_seconds ) )
    {
#ifdef _WIN32
        WSACleanup();
#endif
        return EXIT_FAILURE;
    }

    std::cout << "Aries Reborn Demangler" << std::endl;
    std::cout << "Bind: " << configuration.bind_ip << std::endl;
    std::cout << "Public probe target: " << public_ip << std::endl;
    std::cout << "Ports:";
    for( const auto port : configuration.demangler_ports ) std::cout << ' ' << port;
    std::cout << std::endl;

    while( false == g_shutdown_requested )
    {
        std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
    }

    demangler.shutdown();

#ifdef _WIN32
    WSACleanup();
#endif

    return EXIT_SUCCESS;
}
