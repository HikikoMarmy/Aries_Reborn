#include <server/server.h>

#include "network/eon_protocol.h"

int main( int argc, char **argv )
{
    EONProtocol protocol;
    aries_server_options options;
    options.lobby_port = 11600;
    options.buddy_port = 11601;
    options.database_path = "data/aries.db";
    options.config_path = "aries.ini";
    options.config_section = "eon";
    if( argc > 1 ) options.config_path = argv[1];
    return RunAriesServer( protocol, options );
}
