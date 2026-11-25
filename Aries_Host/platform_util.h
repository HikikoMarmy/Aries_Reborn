#pragma once

#include <filesystem>
#include <string>

std::filesystem::path aries_executable_directory();
std::filesystem::path aries_open_module_file_dialog();
std::string aries_platform_library_error();
void *aries_load_library( const std::filesystem::path &path );
void *aries_find_symbol( void *library, const char *name );
bool aries_unload_library( void *library );
uint64_t aries_process_id();
