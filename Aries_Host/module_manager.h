#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

class LoadedModule;

class ModuleManager
{
public:
    ModuleManager( std::filesystem::path base_directory );

    const std::vector<std::shared_ptr<LoadedModule>> &modules() const;
    std::shared_ptr<LoadedModule> load_module( const std::filesystem::path &path, std::string &error );
    std::shared_ptr<LoadedModule> install_module( const std::filesystem::path &source_path, std::string &error );
    void discover_modules( std::vector<std::string> &errors );
    void request_stop_all();
    bool all_stopped() const;
    bool unload_all( std::string &error );
    const std::filesystem::path &modules_directory() const;
    const std::filesystem::path &runtime_directory() const;
    const std::filesystem::path &config_path() const;

private:
    bool contains_module_id( const char *id ) const;

    std::filesystem::path base_directory_;
    std::filesystem::path modules_directory_;
    std::filesystem::path runtime_directory_;
    std::filesystem::path config_path_;
    std::vector<std::shared_ptr<LoadedModule>> modules_;
};
