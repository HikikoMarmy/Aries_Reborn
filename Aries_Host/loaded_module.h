#pragma once

#include <aries_module_api.h>

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct AriesHostLogEntry
{
    AriesLogLevel level{ ARIES_LOG_INFO };
    std::string message;
};

class LoadedModule
{
public:
    LoadedModule( std::filesystem::path installed_path, std::filesystem::path runtime_directory, std::filesystem::path config_path );
    ~LoadedModule();

    bool load( std::string &error );
    bool unload( std::string &error );
    bool start( const std::string &bind_ip, const std::string &service_ip, std::string &error );
    void request_stop();
    AriesModuleStatus status() const;
    const AriesModuleInfo &info() const;
    const std::filesystem::path &installed_path() const;
    const std::filesystem::path &config_path() const;
    std::string config_section() const;
    std::filesystem::path log_file_path() const;
    std::vector<AriesHostLogEntry> take_logs();
    bool request_replacement( const std::filesystem::path &source_path, std::string &error );
    bool perform_maintenance( std::string &error );
    bool is_loaded() const;

private:
    static void host_log( void *context, const char *module_id, AriesLogLevel level, const char *message );
    bool load_shadow_copy( std::string &error );
    bool resolve_exports( std::string &error );
    bool perform_replacement( const std::filesystem::path &source_path, std::string &error );
    std::filesystem::path create_runtime_path( const std::filesystem::path &source, const char *prefix ) const;
    static bool is_unload_safe( AriesModuleState state );
    static void copy_text( char *destination, size_t destination_size, const std::string &source );

    std::filesystem::path installed_path_;
    std::filesystem::path runtime_directory_;
    std::filesystem::path config_path_;
    std::filesystem::path runtime_path_;
    void *library_{ nullptr };
    AriesModuleInfo info_{};
    AriesModuleHandle handle_{ nullptr };
    PFN_AriesModule_GetApiVersion get_api_version_{ nullptr };
    PFN_AriesModule_GetInfo get_info_{ nullptr };
    PFN_AriesModule_Create create_{ nullptr };
    PFN_AriesModule_Start start_{ nullptr };
    PFN_AriesModule_RequestStop request_stop_{ nullptr };
    PFN_AriesModule_GetStatus get_status_{ nullptr };
    PFN_AriesModule_Destroy destroy_{ nullptr };
    mutable std::mutex log_mutex_;
    std::vector<AriesHostLogEntry> pending_logs_;
    std::filesystem::path pending_replacement_;
};
