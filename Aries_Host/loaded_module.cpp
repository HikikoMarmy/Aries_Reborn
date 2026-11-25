#include "loaded_module.h"
#include "ini_file.h"
#include "platform_util.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <random>
#include <sstream>
#include <utility>

static std::string path_utf8( const std::filesystem::path &path )
{
    const auto value = path.u8string();
    return std::string( reinterpret_cast<const char *>( value.data() ), value.size() );
}

LoadedModule::LoadedModule( std::filesystem::path installed_path, std::filesystem::path runtime_directory, std::filesystem::path config_path )
    : installed_path_( std::move( installed_path ) ), runtime_directory_( std::move( runtime_directory ) ), config_path_( std::move( config_path ) )
{
}

LoadedModule::~LoadedModule()
{
    if( library_ == nullptr ) return;
    const auto current = status().state;
    if( is_unload_safe( current ) )
    {
        std::string error;
        unload( error );
    }
}

bool LoadedModule::load( std::string &error )
{
    if( library_ != nullptr ) return true;
    if( false == load_shadow_copy( error ) ) return false;
    if( false == resolve_exports( error ) )
    {
        aries_unload_library( library_ );
        library_ = nullptr;
        std::error_code ec;
        std::filesystem::remove( runtime_path_, ec );
        runtime_path_.clear();
        return false;
    }

    if( get_api_version_() != ARIES_MODULE_API_VERSION )
    {
        error = "Module API version mismatch";
        aries_unload_library( library_ );
        library_ = nullptr;
        std::error_code ec;
        std::filesystem::remove( runtime_path_, ec );
        runtime_path_.clear();
        return false;
    }

    info_ = AriesModuleInfo{};
    if( false == get_info_( &info_ ) )
    {
        error = "Module did not return valid metadata";
        aries_unload_library( library_ );
        library_ = nullptr;
        std::error_code ec;
        std::filesystem::remove( runtime_path_, ec );
        runtime_path_.clear();
        return false;
    }

    AriesHostApi host{};
    host.context = this;
    host.log = &LoadedModule::host_log;
    handle_ = create_( &host );
    if( handle_ == nullptr )
    {
        error = "Module creation failed";
        aries_unload_library( library_ );
        library_ = nullptr;
        std::error_code ec;
        std::filesystem::remove( runtime_path_, ec );
        runtime_path_.clear();
        return false;
    }
    return true;
}

bool LoadedModule::unload( std::string &error )
{
    if( library_ == nullptr ) return true;
    const auto current = status().state;
    if( false == is_unload_safe( current ) )
    {
        error = "Module is not stopped; refusing to detach it";
        return false;
    }

    if( handle_ != nullptr )
    {
        destroy_( handle_ );
        handle_ = nullptr;
    }

    if( false == aries_unload_library( library_ ) )
    {
        error = aries_platform_library_error();
        return false;
    }

    library_ = nullptr;
    std::error_code ec;
    std::filesystem::remove( runtime_path_, ec );
    runtime_path_.clear();
    get_api_version_ = nullptr;
    get_info_ = nullptr;
    create_ = nullptr;
    start_ = nullptr;
    request_stop_ = nullptr;
    get_status_ = nullptr;
    destroy_ = nullptr;
    return true;
}

bool LoadedModule::start( const std::string &bind_ip, const std::string &service_ip, std::string &error )
{
    if( handle_ == nullptr || start_ == nullptr )
    {
        error = "Module is not loaded";
        return false;
    }

    AriesModuleStartArgs args{};
    copy_text( args.bind_ip, sizeof( args.bind_ip ), bind_ip );
    copy_text( args.service_ip, sizeof( args.service_ip ), service_ip );
    copy_text( args.config_path, sizeof( args.config_path ), path_utf8( config_path_ ) );
    if( false == start_( handle_, &args ) )
    {
        error = "Module refused to start";
        return false;
    }
    return true;
}

void LoadedModule::request_stop()
{
    if( handle_ != nullptr && request_stop_ != nullptr ) request_stop_( handle_ );
}

AriesModuleStatus LoadedModule::status() const
{
    AriesModuleStatus result{};
    if( handle_ == nullptr || get_status_ == nullptr )
    {
        result.state = ARIES_MODULE_STATE_STOPPED;
        copy_text( result.message, sizeof( result.message ), "Not loaded" );
        return result;
    }
    if( false == get_status_( handle_, &result ) )
    {
        result.state = ARIES_MODULE_STATE_FAULTED;
        copy_text( result.message, sizeof( result.message ), "Unable to query module" );
    }
    return result;
}

const AriesModuleInfo &LoadedModule::info() const
{
    return info_;
}

const std::filesystem::path &LoadedModule::installed_path() const
{
    return installed_path_;
}

const std::filesystem::path &LoadedModule::config_path() const
{
    return config_path_;
}

std::string LoadedModule::config_section() const
{
    return info_.id;
}

std::filesystem::path LoadedModule::log_file_path() const
{
    IniFile settings;
    settings.load( config_path_ );
    auto directory = settings.get( "global", "log_directory", "logs" );
    std::filesystem::path directory_path( directory );
    if( directory_path.is_relative() ) directory_path = config_path_.parent_path() / directory_path;
    std::error_code ec;
    std::filesystem::create_directories( directory_path, ec );
    return directory_path / ( config_section() + ".log" );
}

std::vector<AriesHostLogEntry> LoadedModule::take_logs()
{
    std::lock_guard lock( log_mutex_ );
    std::vector<AriesHostLogEntry> result;
    result.swap( pending_logs_ );
    return result;
}

bool LoadedModule::request_replacement( const std::filesystem::path &source_path, std::string &error )
{
    std::error_code ec;
    if( source_path.empty() || false == std::filesystem::exists( source_path, ec ) )
    {
        error = "Replacement module does not exist";
        return false;
    }

    const auto current = status().state;
    if( current == ARIES_MODULE_STATE_STARTING || current == ARIES_MODULE_STATE_RUNNING || current == ARIES_MODULE_STATE_STOPPING )
    {
        const auto staged_path = create_runtime_path( source_path, "pending_" );
        std::filesystem::remove( staged_path, ec );
        std::filesystem::copy_file( source_path, staged_path, std::filesystem::copy_options::overwrite_existing, ec );
        if( ec )
        {
            error = "Unable to stage replacement module: " + ec.message();
            return false;
        }
        if( false == pending_replacement_.empty() ) std::filesystem::remove( pending_replacement_, ec );
        pending_replacement_ = staged_path;
        if( current != ARIES_MODULE_STATE_STOPPING ) request_stop();
        return true;
    }
    return perform_replacement( source_path, error );
}

bool LoadedModule::perform_maintenance( std::string &error )
{
    if( pending_replacement_.empty() ) return false;
    const auto current = status().state;
    if( false == is_unload_safe( current ) ) return false;

    const auto source = pending_replacement_;
    pending_replacement_.clear();
    const auto result = perform_replacement( source, error );
    std::error_code ec;
    std::filesystem::remove( source, ec );
    return result;
}

bool LoadedModule::is_loaded() const
{
    return library_ != nullptr && handle_ != nullptr;
}

void LoadedModule::host_log( void *context, const char *, AriesLogLevel level, const char *message )
{
    auto *module = static_cast<LoadedModule *>( context );
    if( module == nullptr ) return;
    std::lock_guard lock( module->log_mutex_ );
    module->pending_logs_.push_back( { level, message != nullptr ? message : "" } );
}

bool LoadedModule::load_shadow_copy( std::string &error )
{
    std::error_code ec;
    std::filesystem::create_directories( runtime_directory_, ec );
    if( ec )
    {
        error = "Unable to create runtime module directory: " + ec.message();
        return false;
    }

    runtime_path_ = create_runtime_path( installed_path_, "" );
    std::filesystem::remove( runtime_path_, ec );
    ec.clear();
    std::filesystem::copy_file( installed_path_, runtime_path_, std::filesystem::copy_options::overwrite_existing, ec );
    if( ec )
    {
        error = "Unable to create runtime copy of module: " + ec.message();
        runtime_path_.clear();
        return false;
    }

    library_ = aries_load_library( runtime_path_ );
    if( library_ == nullptr )
    {
        error = aries_platform_library_error();
        std::filesystem::remove( runtime_path_, ec );
        runtime_path_.clear();
        return false;
    }
    return true;
}

bool LoadedModule::resolve_exports( std::string &error )
{
    get_api_version_ = reinterpret_cast<PFN_AriesModule_GetApiVersion>( aries_find_symbol( library_, "AriesModule_GetApiVersion" ) );
    get_info_ = reinterpret_cast<PFN_AriesModule_GetInfo>( aries_find_symbol( library_, "AriesModule_GetInfo" ) );
    create_ = reinterpret_cast<PFN_AriesModule_Create>( aries_find_symbol( library_, "AriesModule_Create" ) );
    start_ = reinterpret_cast<PFN_AriesModule_Start>( aries_find_symbol( library_, "AriesModule_Start" ) );
    request_stop_ = reinterpret_cast<PFN_AriesModule_RequestStop>( aries_find_symbol( library_, "AriesModule_RequestStop" ) );
    get_status_ = reinterpret_cast<PFN_AriesModule_GetStatus>( aries_find_symbol( library_, "AriesModule_GetStatus" ) );
    destroy_ = reinterpret_cast<PFN_AriesModule_Destroy>( aries_find_symbol( library_, "AriesModule_Destroy" ) );
    if( get_api_version_ == nullptr || get_info_ == nullptr || create_ == nullptr || start_ == nullptr || request_stop_ == nullptr || get_status_ == nullptr || destroy_ == nullptr )
    {
        error = "Module is missing one or more Aries module exports";
        return false;
    }
    return true;
}

bool LoadedModule::perform_replacement( const std::filesystem::path &source_path, std::string &error )
{
    std::error_code ec;
    const auto source_canonical = std::filesystem::weakly_canonical( source_path, ec );
    ec.clear();
    const auto installed_canonical = std::filesystem::weakly_canonical( installed_path_, ec );
    if( false == source_canonical.empty() && source_canonical == installed_canonical )
    {
        error = "The selected module is already installed";
        return false;
    }
    if( false == unload( error ) ) return false;

    const auto backup = installed_path_.string() + ".backup";
    std::filesystem::remove( backup, ec );
    ec.clear();
    if( std::filesystem::exists( installed_path_, ec ) )
    {
        std::filesystem::copy_file( installed_path_, backup, std::filesystem::copy_options::overwrite_existing, ec );
        if( ec )
        {
            error = "Unable to create module backup: " + ec.message();
            return false;
        }
    }

    std::filesystem::remove( installed_path_, ec );
    ec.clear();
    std::filesystem::copy_file( source_path, installed_path_, std::filesystem::copy_options::overwrite_existing, ec );
    if( ec )
    {
        std::filesystem::copy_file( backup, installed_path_, std::filesystem::copy_options::overwrite_existing, ec );
        error = "Unable to install replacement module";
        return false;
    }

    if( false == load( error ) )
    {
        std::filesystem::remove( installed_path_, ec );
        std::filesystem::copy_file( backup, installed_path_, std::filesystem::copy_options::overwrite_existing, ec );
        std::string restore_error;
        load( restore_error );
        return false;
    }
    std::filesystem::remove( backup, ec );
    return true;
}

std::filesystem::path LoadedModule::create_runtime_path( const std::filesystem::path &source, const char *prefix ) const
{
    const auto now = static_cast<uint64_t>( std::chrono::high_resolution_clock::now().time_since_epoch().count() );
    std::random_device random;
    std::ostringstream token;
    token << aries_process_id() << '_' << std::hex << now << '_' << random();
    return runtime_directory_ / ( std::string( prefix != nullptr ? prefix : "" ) + source.stem().string() + "_" + token.str() + source.extension().string() );
}

bool LoadedModule::is_unload_safe( AriesModuleState state )
{
    return state == ARIES_MODULE_STATE_STOPPED || state == ARIES_MODULE_STATE_FAULTED;
}

void LoadedModule::copy_text( char *destination, size_t destination_size, const std::string &source )
{
    if( destination == nullptr || destination_size == 0 ) return;
    const auto length = std::min( destination_size - 1, source.size() );
    if( length != 0 ) std::memcpy( destination, source.data(), length );
    destination[length] = '\0';
}
