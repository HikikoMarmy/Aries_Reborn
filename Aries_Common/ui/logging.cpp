#include "stdafx.h"
#include "logging.h"

#include <cstdarg>

#ifdef _WIN32
#include <windows.h>
#endif

static std::wstring make_timestamp()
{
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t( now );
    std::tm local{};
#ifdef _WIN32
    localtime_s( &local, &time );
#else
    localtime_r( &time, &local );
#endif
    wchar_t buffer[32]{};
    std::wcsftime( buffer, std::size( buffer ), L"%H:%M:%S", &local );
    return buffer;
}


void CLogManager::set_sink( sink_type sink )
{
    std::lock_guard lock( mutex_ );
    sink_ = std::move( sink );
}

void CLogManager::clear_sink()
{
    std::lock_guard lock( mutex_ );
    sink_ = {};
}

void CLogManager::write_line( const wchar_t *level, console_colour colour, const std::wstring &message )
{
    std::lock_guard lock( mutex_ );

    if( sink_ )
    {
        sink_( level, message );
        return;
    }

#ifdef _WIN32
    const auto console = GetStdHandle( STD_OUTPUT_HANDLE );
    CONSOLE_SCREEN_BUFFER_INFO console_info{};
    WORD original_attributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    if( console != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo( console, &console_info ) )
    {
        original_attributes = console_info.wAttributes;
    }

    WORD attributes = original_attributes;
    if( colour == console_colour::green )
    {
        attributes = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
    }
    else if( colour == console_colour::cyan )
    {
        attributes = FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
    }

    if( console != INVALID_HANDLE_VALUE ) SetConsoleTextAttribute( console, attributes );
#else
    if( colour == console_colour::green ) std::wcout << L"\x1b[92m";
    else if( colour == console_colour::cyan ) std::wcout << L"\x1b[96m";
#endif

    std::wcout << L"[" << make_timestamp() << L"] [" << level << L"] " << message << std::endl;

#ifdef _WIN32
    if( console != INVALID_HANDLE_VALUE ) SetConsoleTextAttribute( console, original_attributes );
#else
    if( colour != console_colour::normal ) std::wcout << L"\x1b[0m";
#endif
}

void CLogManager::write( const wchar_t *level, console_colour colour, const wchar_t *format, va_list args )
{
    wchar_t message[4096]{};
#ifdef _WIN32
    _vsnwprintf_s( message, std::size( message ), _TRUNCATE, format, args );
#else
    vswprintf( message, std::size( message ), format, args );
#endif
    write_line( level, colour, message );
}

void CLogManager::information( const wchar_t *format, ... )
{
    va_list args;
    va_start( args, format );
    write( L"INFO", console_colour::normal, format, args );
    va_end( args );
}

void CLogManager::debug( const wchar_t *format, ... )
{
    va_list args;
    va_start( args, format );
    write( L"DEBUG", console_colour::normal, format, args );
    va_end( args );
}

void CLogManager::warning( const wchar_t *format, ... )
{
    va_list args;
    va_start( args, format );
    write( L"WARN", console_colour::normal, format, args );
    va_end( args );
}

void CLogManager::error( const wchar_t *format, ... )
{
    va_list args;
    va_start( args, format );
    write( L"ERROR", console_colour::normal, format, args );
    va_end( args );
}

void CLogManager::recv( const wchar_t *format, ... )
{
    va_list args;
    va_start( args, format );
    write( L"RECV", console_colour::green, format, args );
    va_end( args );
}

void CLogManager::send( const wchar_t *format, ... )
{
    va_list args;
    va_start( args, format );
    write( L"SEND", console_colour::cyan, format, args );
    va_end( args );
}

void CLogManager::packet( const std::vector<uint8_t> &raw, bool outgoing )
{
    std::ostringstream stream;
    stream << raw.size() << " bytes";

    for( size_t offset = 0; offset < raw.size(); offset += 16 )
    {
        stream << "\n" << std::hex << std::setw( 4 ) << std::setfill( '0' ) << offset << "  ";
        for( size_t i = 0; i < 16; ++i )
        {
            if( offset + i < raw.size() ) stream << std::setw( 2 ) << static_cast<unsigned>( raw[offset + i] ) << " ";
            else stream << "   ";
        }
        stream << " ";
        for( size_t i = 0; i < 16 && offset + i < raw.size(); ++i )
        {
            const auto c = raw[offset + i];
            stream << ( c >= 0x20 && c <= 0x7E ? static_cast<char>( c ) : '.' );
        }
    }

    const auto text = stream.str();
    const std::wstring wide( text.begin(), text.end() );
    write_line( outgoing ? L"SEND" : L"RECV", outgoing ? console_colour::cyan : console_colour::green, wide );
}
