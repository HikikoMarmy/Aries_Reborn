#include "module_manager.h"
#include "loaded_module.h"

#include <algorithm>
#include <cctype>

ModuleManager::ModuleManager( std::filesystem::path base_directory )
    : base_directory_( std::move( base_directory ) )
{
    modules_directory_ = base_directory_ / "modules";
    runtime_directory_ = base_directory_ / "runtime";
    config_path_ = base_directory_ / "aries.ini";
    std::error_code ec;
    std::filesystem::create_directories( modules_directory_, ec );
    std::filesystem::create_directories( runtime_directory_, ec );
}

const std::vector<std::shared_ptr<LoadedModule>> &ModuleManager::modules() const
{
    return modules_;
}

std::shared_ptr<LoadedModule> ModuleManager::load_module( const std::filesystem::path &path, std::string &error )
{
    auto module = std::make_shared<LoadedModule>( path, runtime_directory_, config_path_ );
    if( false == module->load( error ) ) return {};

    if( contains_module_id( module->info().id ) )
    {
        std::string unload_error;
        module->unload( unload_error );
        error = std::string( "A module with id '" ) + module->info().id + "' is already installed";
        return {};
    }

    modules_.push_back( module );
    return module;
}

std::shared_ptr<LoadedModule> ModuleManager::install_module( const std::filesystem::path &source_path, std::string &error )
{
    std::error_code ec;
    if( source_path.empty() || false == std::filesystem::exists( source_path, ec ) )
    {
        error = "Module file does not exist";
        return {};
    }

    const auto destination = modules_directory_ / source_path.filename();
    if( std::filesystem::exists( destination, ec ) )
    {
        error = "A module with that filename is already installed";
        return {};
    }

    std::filesystem::copy_file( source_path, destination, std::filesystem::copy_options::none, ec );
    if( ec )
    {
        error = "Unable to copy the module into the modules directory: " + ec.message();
        return {};
    }

    auto module = load_module( destination, error );
    if( module == nullptr ) std::filesystem::remove( destination, ec );
    return module;
}

void ModuleManager::discover_modules( std::vector<std::string> &errors )
{
    std::error_code ec;
    for( const auto &entry : std::filesystem::directory_iterator( modules_directory_, ec ) )
    {
        if( ec ) break;
        if( false == entry.is_regular_file() ) continue;
#ifdef _WIN32
        if( entry.path().extension() != ".dll" ) continue;
#else
        if( entry.path().extension() != ".so" ) continue;
#endif
        std::string error;
        if( load_module( entry.path(), error ) == nullptr ) errors.push_back( entry.path().filename().string() + ": " + error );
    }
}

void ModuleManager::request_stop_all()
{
    for( const auto &module : modules_ )
    {
        const auto state = module->status().state;
        if( state == ARIES_MODULE_STATE_STARTING || state == ARIES_MODULE_STATE_RUNNING ) module->request_stop();
    }
}

bool ModuleManager::all_stopped() const
{
    for( const auto &module : modules_ )
    {
        const auto state = module->status().state;
        if( state == ARIES_MODULE_STATE_STARTING || state == ARIES_MODULE_STATE_RUNNING || state == ARIES_MODULE_STATE_STOPPING ) return false;
    }
    return true;
}

bool ModuleManager::unload_all( std::string &error )
{
    for( const auto &module : modules_ )
    {
        std::string module_error;
        if( false == module->unload( module_error ) )
        {
            error = module_error;
            return false;
        }
    }
    return true;
}

const std::filesystem::path &ModuleManager::modules_directory() const
{
    return modules_directory_;
}

const std::filesystem::path &ModuleManager::runtime_directory() const
{
    return runtime_directory_;
}

const std::filesystem::path &ModuleManager::config_path() const
{
    return config_path_;
}

bool ModuleManager::contains_module_id( const char *id ) const
{
    std::string wanted = id != nullptr ? id : "";
    std::transform( wanted.begin(), wanted.end(), wanted.begin(), []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
    for( const auto &module : modules_ )
    {
        std::string current = module->info().id;
        std::transform( current.begin(), current.end(), current.begin(), []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
        if( current == wanted ) return true;
    }
    return false;
}
