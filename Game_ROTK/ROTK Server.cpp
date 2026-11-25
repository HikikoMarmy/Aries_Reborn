#include <server/server.h>

#include "network/rotk_protocol.h"

int main( int argc, char **argv )
{
    ROTKProtocol protocol;
    aries_server_options options;
    options.lobby_port = 11200;
    options.buddy_port = 11201;
    options.database_path = "data/aries.db";
    options.config_path = "aries.ini";
    options.config_section = "rotk";
    if( argc > 1 ) options.config_path = argv[1];
    return RunAriesServer( protocol, options );
}
