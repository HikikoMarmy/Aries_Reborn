#include <server/server.h>

#include "network/madden_protocol.h"

int main( int argc, char **argv )
{
    MaddenProtocol protocol;
    aries_server_options options;
    options.lobby_port = 9995;
    options.buddy_port = 9996;
    options.database_path = "data/aries.db";
    options.config_path = "aries.ini";
    options.config_section = "madden";
    if( argc > 1 ) options.config_path = argv[1];
    return RunAriesServer( protocol, options );
}
