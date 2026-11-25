#include "ini_file.h"

#include <fstream>

static std::string trim_ini( std::string value )
{
    const auto first = value.find_first_not_of( " \t\r\n" );
    if( first == std::string::npos ) return {};
    const auto last = value.find_last_not_of( " \t\r\n" );
    return value.substr( first, last - first + 1 );
}

bool IniFile::load( const std::filesystem::path &path )
{
    std::ifstream file( path );
    if( false == file.is_open() ) return false;

    sections_.clear();
    std::string section;
    std::string line;
    while( std::getline( file, line ) )
    {
        line = trim_ini( line );
        if( line.empty() || line.front() == ';' || line.front() == '#' ) continue;
        if( line.front() == '[' && line.back() == ']' )
        {
            section = trim_ini( line.substr( 1, line.size() - 2 ) );
            continue;
        }

        const auto equals = line.find( '=' );
        if( equals == std::string::npos ) continue;
        const auto key = trim_ini( line.substr( 0, equals ) );
        if( key.empty() ) continue;
        sections_[section][key] = trim_ini( line.substr( equals + 1 ) );
    }
    return true;
}

bool IniFile::save( const std::filesystem::path &path ) const
{
    std::ofstream file( path, std::ios::trunc );
    if( false == file.is_open() ) return false;

    const auto root = sections_.find( {} );
    if( root != sections_.end() )
    {
        for( const auto &[key, value] : root->second ) file << key << '=' << value << '\n';
        if( false == root->second.empty() ) file << '\n';
    }

    for( const auto &[section, values] : sections_ )
    {
        if( section.empty() ) continue;
        file << '[' << section << "]\n";
        for( const auto &[key, value] : values ) file << key << '=' << value << '\n';
        file << '\n';
    }
    return true;
}

std::string IniFile::get( const std::string &section, const std::string &key, const std::string &fallback ) const
{
    const auto section_iter = sections_.find( section );
    if( section_iter == sections_.end() ) return fallback;
    const auto value_iter = section_iter->second.find( key );
    return value_iter != section_iter->second.end() ? value_iter->second : fallback;
}

void IniFile::set( const std::string &section, const std::string &key, std::string value )
{
    sections_[section][key] = std::move( value );
}
