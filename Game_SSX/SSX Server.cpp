#include <server/server.h>

#include "network/ssx_protocol.h"

int main( int argc, char **argv )
{
    SSXProtocol protocol;
    aries_server_options options;
    options.lobby_port = 11000;
    options.buddy_port = 13505;
    options.database_path = "data/aries.db";
    options.config_path = "aries.ini";
    options.config_section = "ssx";
    if( argc > 1 ) options.config_path = argv[1];
    return RunAriesServer( protocol, options );
}
