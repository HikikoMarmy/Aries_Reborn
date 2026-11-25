#pragma once

#include <filesystem>
#include <map>
#include <string>

class IniFile
{
public:
    bool load( const std::filesystem::path &path );
    bool save( const std::filesystem::path &path ) const;

    std::string get( const std::string &section, const std::string &key, const std::string &fallback = {} ) const;
    void set( const std::string &section, const std::string &key, std::string value );

private:
    std::map<std::string, std::map<std::string, std::string>> sections_;
};
