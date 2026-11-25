#include <aries_module_api.h>
#include <configuration.h>
#include <demangler/demangler_server.h>
#include <global_define.h>
#include <module/module_util.h>
#include <platform/socket_compat.h>

#include <array>
#include <atomic>
#include <chrono>
#include <codecvt>
#include <filesystem>
#include <locale>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

class DemanglerModuleRuntime
{
public:
    explicit DemanglerModuleRuntime( const AriesHostApi *host )
    {
        if( host != nullptr && host->api_version == ARIES_MODULE_API_VERSION ) host_ = *host;
        install_log_sink();
    }

    ~DemanglerModuleRuntime()
    {
        request_stop();
        if( worker_.joinable() ) worker_.join();
        logging.clear_sink();
    }

    bool start( const AriesModuleStartArgs *args )
    {
        const auto current = state_.load();
        if( current == ARIES_MODULE_STATE_STARTING || current == ARIES_MODULE_STATE_RUNNING || current == ARIES_MODULE_STATE_STOPPING ) return false;
        if( worker_.joinable() ) worker_.join();

        AriesModuleStartArgs copied{};
        if( args != nullptr ) copied = *args;
        if( copied.struct_size != sizeof( AriesModuleStartArgs ) ) copied = AriesModuleStartArgs{};

        stop_requested_.store( false );
        install_log_sink();
        set_state( ARIES_MODULE_STATE_STARTING, "Starting" );

        try
        {
            worker_ = std::thread( &DemanglerModuleRuntime::worker_main, this, copied );
        }
        catch( const std::exception &exception )
        {
            set_state( ARIES_MODULE_STATE_FAULTED, exception.what() );
            return false;
        }
        return true;
    }

    void request_stop()
    {
        const auto current = state_.load();
        if( current == ARIES_MODULE_STATE_STARTING || current == ARIES_MODULE_STATE_RUNNING )
        {
            set_state( ARIES_MODULE_STATE_STOPPING, "Stopping" );
            stop_requested_.store( true );
        }
    }

    bool get_status( AriesModuleStatus *status ) const
    {
        if( status == nullptr || status->struct_size != sizeof( AriesModuleStatus ) ) return false;
        status->state = state_.load();
        std::lock_guard lock( status_mutex_ );
        AriesModuleCopyText( status->message, sizeof( status->message ), status_message_.c_str() );
        return true;
    }

private:
    static std::filesystem::path executable_directory()
    {
#ifdef _WIN32
        std::array<char, 32768> path{};
        const auto length = GetModuleFileNameA( nullptr, path.data(), static_cast<DWORD>( path.size() ) );
        if( length > 0 && length < path.size() ) return std::filesystem::path( std::string( path.data(), length ) ).parent_path();
        return std::filesystem::current_path();
#else
        std::array<char, PATH_MAX> path{};
        const auto length = readlink( "/proc/self/exe", path.data(), path.size() - 1 );
        if( length > 0 )
        {
            path[static_cast<size_t>( length )] = '\0';
            return std::filesystem::path( path.data() ).parent_path();
        }
        return std::filesystem::current_path();
#endif
    }

    static std::string wide_to_utf8( const std::wstring &value )
    {
#ifdef _WIN32
        if( value.empty() ) return {};
        const auto size = WideCharToMultiByte( CP_UTF8, 0, value.data(), static_cast<int>( value.size() ), nullptr, 0, nullptr, nullptr );
        if( size <= 0 ) return {};
        std::string result( static_cast<size_t>( size ), '\0' );
        WideCharToMultiByte( CP_UTF8, 0, value.data(), static_cast<int>( value.size() ), result.data(), size, nullptr, nullptr );
        return result;
#else
        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
        return converter.to_bytes( value );
#endif
    }

    static AriesLogLevel map_level( const wchar_t *level )
    {
        if( level == nullptr ) return ARIES_LOG_INFO;
        if( std::wcscmp( level, L"DEBUG" ) == 0 ) return ARIES_LOG_DEBUG;
        if( std::wcscmp( level, L"WARN" ) == 0 ) return ARIES_LOG_WARNING;
        if( std::wcscmp( level, L"ERROR" ) == 0 ) return ARIES_LOG_ERROR;
        if( std::wcscmp( level, L"RECV" ) == 0 ) return ARIES_LOG_RECV;
        if( std::wcscmp( level, L"SEND" ) == 0 ) return ARIES_LOG_SEND;
        return ARIES_LOG_INFO;
    }

    void install_log_sink()
    {
        logging.set_sink( [this]( const wchar_t *level, const std::wstring &message )
        {
            if( host_.log == nullptr ) return;
            const auto utf8 = wide_to_utf8( message );
            host_.log( host_.context, "demangler", map_level( level ), utf8.c_str() );
        } );
    }

    void set_state( AriesModuleState state, const std::string &message )
    {
        {
            std::lock_guard lock( status_mutex_ );
            status_message_ = message;
        }
        state_.store( state );
    }

    void worker_main( AriesModuleStartArgs args )
    {
        auto config_path = args.config_path[0] != '\0' ? std::filesystem::path( args.config_path ) : std::filesystem::path( "aries.ini" );
        if( config_path.is_relative() ) config_path = executable_directory() / config_path;
        config_path = config_path.lexically_normal();

        CConfiguration configuration;
        configuration.load( config_path.string(), "demangler" );
        if( args.bind_ip[0] != '\0' ) configuration.bind_ip = args.bind_ip;
        if( args.service_ip[0] != '\0' )
        {
            configuration.service_ip = args.service_ip;
            configuration.demangler_ip.clear();
        }

        const auto public_ip = configuration.demangler_ip.empty() ? configuration.service_ip : configuration.demangler_ip;

#ifdef _WIN32
        WSADATA wsa_data{};
        if( WSAStartup( MAKEWORD( 2, 2 ), &wsa_data ) != 0 )
        {
            set_state( ARIES_MODULE_STATE_FAULTED, "WSAStartup failed" );
            return;
        }
#endif

        CDemanglerServer server;
        if( false == server.start(
            configuration.bind_ip,
            public_ip,
            configuration.demangler_ports,
            configuration.demangler_probe_count,
            configuration.demangler_session_timeout_seconds ) )
        {
#ifdef _WIN32
            WSACleanup();
#endif
            set_state( ARIES_MODULE_STATE_FAULTED, "Unable to start Demangler" );
            return;
        }

        if( stop_requested_.load() ) set_state( ARIES_MODULE_STATE_STOPPING, "Stopping" );
        else set_state( ARIES_MODULE_STATE_RUNNING, "Running" );

        while( false == stop_requested_.load() ) std::this_thread::sleep_for( std::chrono::milliseconds( 25 ) );

        set_state( ARIES_MODULE_STATE_STOPPING, "Stopping" );
        server.shutdown();

#ifdef _WIN32
        WSACleanup();
#endif

        set_state( ARIES_MODULE_STATE_STOPPED, "Stopped" );
    }

    AriesHostApi host_{};
    std::atomic<AriesModuleState> state_{ ARIES_MODULE_STATE_STOPPED };
    std::atomic<bool> stop_requested_{ false };
    mutable std::mutex status_mutex_;
    std::string status_message_{ "Stopped" };
    std::thread worker_;
};

ARIES_MODULE_EXPORT uint32_t AriesModule_GetApiVersion()
{
    return ARIES_MODULE_API_VERSION;
}

ARIES_MODULE_EXPORT bool AriesModule_GetInfo( AriesModuleInfo *info )
{
    if( info == nullptr || info->struct_size != sizeof( AriesModuleInfo ) ) return false;
    info->kind = ARIES_MODULE_KIND_SERVICE;
    AriesModuleCopyText( info->id, sizeof( info->id ), "demangler" );
    AriesModuleCopyText( info->display_name, sizeof( info->display_name ), "Demangler" );
    AriesModuleCopyText( info->description, sizeof( info->description ), "Shared DirtySock Direct/Mangle rendezvous service" );
    AriesModuleCopyText( info->default_config, sizeof( info->default_config ), "aries.ini" );
    return true;
}

ARIES_MODULE_EXPORT AriesModuleHandle AriesModule_Create( const AriesHostApi *host )
{
    return new DemanglerModuleRuntime( host );
}

ARIES_MODULE_EXPORT bool AriesModule_Start( AriesModuleHandle module, const AriesModuleStartArgs *args )
{
    if( module == nullptr ) return false;
    return static_cast<DemanglerModuleRuntime *>( module )->start( args );
}

ARIES_MODULE_EXPORT void AriesModule_RequestStop( AriesModuleHandle module )
{
    if( module == nullptr ) return;
    static_cast<DemanglerModuleRuntime *>( module )->request_stop();
}

ARIES_MODULE_EXPORT bool AriesModule_GetStatus( AriesModuleHandle module, AriesModuleStatus *status )
{
    if( module == nullptr ) return false;
    return static_cast<DemanglerModuleRuntime *>( module )->get_status( status );
}

ARIES_MODULE_EXPORT void AriesModule_Destroy( AriesModuleHandle module )
{
    delete static_cast<DemanglerModuleRuntime *>( module );
}
