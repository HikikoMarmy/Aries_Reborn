#include "stdafx.h"
#include "../global_define.h"
#include "../network/game_protocol.h"
#include "game_module_runtime.h"

#include <algorithm>
#include <cstring>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <codecvt>
#include <locale>
#endif

AriesGameModuleRuntime::AriesGameModuleRuntime(
    const AriesHostApi *host,
    std::string module_id,
    std::string display_name,
    std::string default_config,
    uint16_t lobby_port,
    uint16_t buddy_port,
    protocol_factory factory )
    : module_id_( std::move( module_id ) ),
      display_name_( std::move( display_name ) ),
      default_config_( std::move( default_config ) ),
      lobby_port_( lobby_port ),
      buddy_port_( buddy_port ),
      factory_( std::move( factory ) )
{
    if( host != nullptr && host->api_version == ARIES_MODULE_API_VERSION ) host_ = *host;
    install_log_sink();
}

AriesGameModuleRuntime::~AriesGameModuleRuntime()
{
    request_stop();
    if( worker_.joinable() ) worker_.join();
    logging.clear_sink();
}

bool AriesGameModuleRuntime::start( const AriesModuleStartArgs *args )
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
        worker_ = std::thread( &AriesGameModuleRuntime::worker_main, this, copied );
    }
    catch( const std::exception &exception )
    {
        set_state( ARIES_MODULE_STATE_FAULTED, exception.what() );
        return false;
    }

    return true;
}

void AriesGameModuleRuntime::request_stop()
{
    const auto current = state_.load();
    if( current == ARIES_MODULE_STATE_STARTING || current == ARIES_MODULE_STATE_RUNNING )
    {
        set_state( ARIES_MODULE_STATE_STOPPING, "Stopping" );
        stop_requested_.store( true );
    }
}

bool AriesGameModuleRuntime::get_status( AriesModuleStatus *status ) const
{
    if( status == nullptr || status->struct_size != sizeof( AriesModuleStatus ) ) return false;

    status->state = state_.load();
    std::lock_guard lock( status_mutex_ );
#ifdef _WIN32
    strncpy_s( status->message, status_message_.c_str(), _TRUNCATE );
#else
    std::strncpy( status->message, status_message_.c_str(), sizeof( status->message ) - 1 );
    status->message[sizeof( status->message ) - 1] = '\0';
#endif
    return true;
}

void AriesGameModuleRuntime::started_callback( void *context )
{
    auto *runtime = static_cast<AriesGameModuleRuntime *>( context );
    if( runtime == nullptr ) return;
    if( runtime->stop_requested_.load() )
    {
        runtime->set_state( ARIES_MODULE_STATE_STOPPING, "Stopping" );
        return;
    }
    runtime->set_state( ARIES_MODULE_STATE_RUNNING, "Running" );
}

void AriesGameModuleRuntime::worker_main( AriesModuleStartArgs args )
{
    auto protocol = factory_ ? factory_() : nullptr;
    if( protocol == nullptr )
    {
        set_state( ARIES_MODULE_STATE_FAULTED, "Unable to create game protocol" );
        return;
    }

    aries_server_options options;
    options.lobby_port = lobby_port_;
    options.buddy_port = buddy_port_;
    options.database_path = "data/aries.db";
    options.config_path = args.config_path[0] != '\0' ? args.config_path : default_config_;
    options.config_section = module_id_;
    if( args.bind_ip[0] != '\0' ) options.bind_ip_override = args.bind_ip;
    if( args.service_ip[0] != '\0' ) options.service_ip_override = args.service_ip;
    options.shutdown_requested = &stop_requested_;
    options.started_callback = &AriesGameModuleRuntime::started_callback;
    options.callback_context = this;

    const auto result = RunAriesServer( *protocol, options );

    if( result == EXIT_SUCCESS )
    {
        set_state( ARIES_MODULE_STATE_STOPPED, "Stopped" );
    }
    else
    {
        set_state( ARIES_MODULE_STATE_FAULTED, "Server exited with an error" );
    }
}

void AriesGameModuleRuntime::set_state( AriesModuleState state, const std::string &message )
{
    {
        std::lock_guard lock( status_mutex_ );
        status_message_ = message;
    }
    state_.store( state );
}

void AriesGameModuleRuntime::install_log_sink()
{
    logging.set_sink( [this]( const wchar_t *level, const std::wstring &message )
    {
        emit_log( level, message );
    } );
}

void AriesGameModuleRuntime::emit_log( const wchar_t *level, const std::wstring &message ) const
{
    if( host_.log == nullptr ) return;
    const auto utf8 = wide_to_utf8( message );
    host_.log( host_.context, module_id_.c_str(), map_log_level( level ), utf8.c_str() );
}

std::string AriesGameModuleRuntime::wide_to_utf8( const std::wstring &value )
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

AriesLogLevel AriesGameModuleRuntime::map_log_level( const wchar_t *level )
{
    if( level == nullptr ) return ARIES_LOG_INFO;
    if( std::wcscmp( level, L"DEBUG" ) == 0 ) return ARIES_LOG_DEBUG;
    if( std::wcscmp( level, L"WARN" ) == 0 ) return ARIES_LOG_WARNING;
    if( std::wcscmp( level, L"ERROR" ) == 0 ) return ARIES_LOG_ERROR;
    if( std::wcscmp( level, L"RECV" ) == 0 ) return ARIES_LOG_RECV;
    if( std::wcscmp( level, L"SEND" ) == 0 ) return ARIES_LOG_SEND;
    return ARIES_LOG_INFO;
}
