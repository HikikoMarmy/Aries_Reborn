#include "stdafx.h"
#include "client.h"

aries_client::aries_client( sptr_socket socket_value, uint32_t client_id_value )
    : socket( std::move( socket_value ) ), client_id( client_id_value )
{
}
