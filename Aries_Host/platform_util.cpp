#include "platform_util.h"

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

#include <array>
#include <system_error>

std::filesystem::path aries_executable_directory()
{
#ifdef _WIN32
    std::array<wchar_t, 32768> buffer{};
    const auto length = GetModuleFileNameW( nullptr, buffer.data(), static_cast<DWORD>( buffer.size() ) );
    if( length == 0 || length >= buffer.size() ) return std::filesystem::current_path();
    return std::filesystem::path( std::wstring( buffer.data(), length ) ).parent_path();
#else
    std::array<char, 4096> buffer{};
    const auto length = readlink( "/proc/self/exe", buffer.data(), buffer.size() - 1 );
    if( length <= 0 ) return std::filesystem::current_path();
    return std::filesystem::path( std::string( buffer.data(), static_cast<size_t>( length ) ) ).parent_path();
#endif
}

std::filesystem::path aries_open_module_file_dialog()
{
#ifdef _WIN32
    std::array<wchar_t, 32768> buffer{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof( dialog );
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>( buffer.size() );
    dialog.lpstrFilter = L"Aries modules (*.dll)\0*.dll\0All files (*.*)\0*.*\0";
    dialog.nFilterIndex = 1;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if( false == GetOpenFileNameW( &dialog ) ) return {};
    return std::filesystem::path( buffer.data() );
#else
    return {};
#endif
}

std::string aries_platform_library_error()
{
#ifdef _WIN32
    const auto code = GetLastError();
    if( code == 0 ) return {};
    LPSTR message = nullptr;
    const auto length = FormatMessageA( FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                        nullptr, code, 0, reinterpret_cast<LPSTR>( &message ), 0, nullptr );
    std::string result = length != 0 && message != nullptr ? std::string( message, length ) : std::string( "Windows error " ) + std::to_string( code );
    if( message != nullptr ) LocalFree( message );
    return result;
#else
    const auto *message = dlerror();
    return message != nullptr ? message : "Dynamic loader error";
#endif
}

void *aries_load_library( const std::filesystem::path &path )
{
#ifdef _WIN32
    return reinterpret_cast<void *>( LoadLibraryW( path.c_str() ) );
#else
    return dlopen( path.c_str(), RTLD_NOW | RTLD_LOCAL );
#endif
}

void *aries_find_symbol( void *library, const char *name )
{
    if( library == nullptr || name == nullptr ) return nullptr;
#ifdef _WIN32
    return reinterpret_cast<void *>( GetProcAddress( reinterpret_cast<HMODULE>( library ), name ) );
#else
    return dlsym( library, name );
#endif
}

bool aries_unload_library( void *library )
{
    if( library == nullptr ) return true;
#ifdef _WIN32
    return FreeLibrary( reinterpret_cast<HMODULE>( library ) ) != FALSE;
#else
    return dlclose( library ) == 0;
#endif
}

uint64_t aries_process_id()
{
#ifdef _WIN32
    return static_cast<uint64_t>( GetCurrentProcessId() );
#else
    return static_cast<uint64_t>( getpid() );
#endif
}
