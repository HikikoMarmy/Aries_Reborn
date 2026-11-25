#pragma once

#include <cstdint>
#include <cstdarg>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

class CLogManager
{
public:
    using sink_type = std::function<void( const wchar_t *level, const std::wstring &message )>;

    void set_sink( sink_type sink );
    void clear_sink();

    void information( const wchar_t *format, ... );
    void debug( const wchar_t *format, ... );
    void warning( const wchar_t *format, ... );
    void error( const wchar_t *format, ... );
    void recv( const wchar_t *format, ... );
    void send( const wchar_t *format, ... );
    void packet( const std::vector<uint8_t> &raw, bool outgoing );

private:
    enum class console_colour
    {
        normal,
        green,
        cyan
    };

    void write( const wchar_t *level, console_colour colour, const wchar_t *format, va_list args );
    void write_line( const wchar_t *level, console_colour colour, const std::wstring &message );
    std::mutex mutex_;
    sink_type sink_;
};
