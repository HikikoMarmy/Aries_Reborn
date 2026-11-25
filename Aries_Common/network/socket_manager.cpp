#include "stdafx.h"
#include "../global_define.h"
#include "aries_packet.h"
#include "socket_manager.h"

static uint32_t read_be32_socket( const uint8_t *data )
{
    return ( static_cast<uint32_t>( data[0] ) << 24 ) |
           ( static_cast<uint32_t>( data[1] ) << 16 ) |
           ( static_cast<uint32_t>( data[2] ) << 8 ) |
           static_cast<uint32_t>( data[3] );
}

static bool starts_with_ascii( const std::vector<uint8_t> &data, const char *value )
{
    const auto size = std::strlen( value );
    if( data.size() < size ) return false;
    return std::memcmp( data.data(), value, size ) == 0;
}

static bool looks_like_http_request( const std::vector<uint8_t> &data )
{
    return starts_with_ascii( data, "GET " ) ||
           starts_with_ascii( data, "HEAD " ) ||
           starts_with_ascii( data, "POST " );
}

static void handle_http_probe( const sptr_socket &socket )
{
    auto request = std::string( socket->read_data.begin(), socket->read_data.end() );
    const auto line_end = request.find_first_of( "\r\n" );
    if( line_end != std::string::npos ) request.resize( line_end );

    logging.warning( L"HTTP-style probe from %hs:%u [%ls]: %hs",
        socket->ip_address.c_str(),
        socket->port,
        socket->socket_protocol == protocol_lobby ? L"Lobby" : L"Buddy",
        request.c_str()
    );

    const std::string response =
        "HTTP/1.0 200 OK\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n";

    socket->write_data.assign( response.begin(), response.end() );
    socket->write_offset = 0;
    socket->read_data.clear();
}


CSocketManager::CSocketManager() = default;

CSocketManager::~CSocketManager()
{
    shutdown();
#ifdef _WIN32
    WSACleanup();
#endif
}

bool CSocketManager::Initialize()
{
#ifdef _WIN32
    WSADATA wsa_data{};
    if( WSAStartup( MAKEWORD( 2, 2 ), &wsa_data ) != 0 )
    {
        logging.error( L"WSAStartup failed" );
        return false;
    }
#endif

    if( g_configuration->lobby_port > 0 && false == open_tcp_listener( g_configuration->bind_ip, g_configuration->lobby_port, protocol_lobby ) ) return false;
    for( const auto port : g_configuration->additional_lobby_ports )
    {
        if( port == 0 || port == g_configuration->lobby_port || port == g_configuration->buddy_port ) continue;
        if( false == open_tcp_listener( g_configuration->bind_ip, port, protocol_lobby ) ) return false;
    }
    if( g_configuration->buddy_port > 0 && false == open_tcp_listener( g_configuration->bind_ip, g_configuration->buddy_port, protocol_buddy ) ) return false;

    run_ = true;
    io_thread_ = std::thread( &CSocketManager::socket_io_thread, this );
    return true;
}

bool CSocketManager::set_nonblocking( SOCKET fd )
{
#ifdef _WIN32
    u_long enabled = 1;
    return ioctlsocket( fd, FIONBIO, &enabled ) == 0;
#else
    const auto flags = fcntl( fd, F_GETFL, 0 );
    if( flags < 0 ) return false;
    return fcntl( fd, F_SETFL, flags | O_NONBLOCK ) == 0;
#endif
}

bool CSocketManager::open_tcp_listener( const std::string &ip, uint16_t port, enum_socket_protocol protocol )
{
    const auto fd = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
    if( fd == INVALID_SOCKET )
    {
        logging.error( L"Unable to create TCP listener on port %u", port );
        return false;
    }

    int reuse = 1;
#ifdef _WIN32
    setsockopt( fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>( &reuse ), sizeof( reuse ) );
#else
    setsockopt( fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ) );
#endif

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons( port );
    if( ip.empty() || ip == "0.0.0.0" ) address.sin_addr.s_addr = htonl( INADDR_ANY );
    else if( inet_pton( AF_INET, ip.c_str(), &address.sin_addr ) != 1 )
    {
        closesocket( fd );
        logging.error( L"Invalid bind address: %hs", ip.c_str() );
        return false;
    }

    if( bind( fd, reinterpret_cast<sockaddr *>( &address ), sizeof( address ) ) == SOCKET_ERROR )
    {
        logging.error( L"Unable to bind %hs:%u. Error: %d", ip.c_str(), port, WSAGetLastError() );
        closesocket( fd );
        return false;
    }

    if( listen( fd, SOMAXCONN ) == SOCKET_ERROR )
    {
        logging.error( L"Unable to listen on port %u. Error: %d", port, WSAGetLastError() );
        closesocket( fd );
        return false;
    }

    set_nonblocking( fd );

    auto listener = std::make_shared<aries_socket>();
    listener->fd = fd;
    listener->socket_protocol = protocol;
    listener->ip_address = ip;
    listener->port = port;
    listener->listener = true;

    {
        std::lock_guard lock( socket_mutex_ );
        socket_set_.insert( listener );
    }

    logging.information( L"Listening on %hs:%u [%ls]", ip.c_str(), port, protocol == protocol_lobby ? L"Lobby" : L"Buddy" );
    return true;
}

bool CSocketManager::get_message( sptr_socket &socket, sptr_packet &packet )
{
    std::pair<sptr_socket, sptr_packet> value;
    if( false == read_queue_.try_pop( value ) ) return false;
    socket = std::move( value.first );
    packet = std::move( value.second );
    return true;
}

void CSocketManager::shutdown()
{
    run_ = false;
    if( io_thread_.joinable() ) io_thread_.join();

    std::lock_guard lock( socket_mutex_ );
    for( const auto &socket : socket_set_ )
    {
        if( socket->fd != INVALID_SOCKET )
        {
            ::shutdown( socket->fd, SD_BOTH );
            closesocket( socket->fd );
            socket->fd = INVALID_SOCKET;
        }
    }
    socket_set_.clear();
}

void CSocketManager::notify_socket( const sptr_socket &socket, uint32_t notice )
{
    read_queue_.push( std::make_pair( socket, aries_packet::make_notice( notice ) ) );
}

void CSocketManager::accept_new_tcp( const sptr_socket &listener )
{
    for( ;; )
    {
        sockaddr_in remote{};
#ifdef _WIN32
        int remote_size = sizeof( remote );
#else
        socklen_t remote_size = sizeof( remote );
#endif
        const auto fd = accept( listener->fd, reinterpret_cast<sockaddr *>( &remote ), &remote_size );
        if( fd == INVALID_SOCKET )
        {
            const auto error = WSAGetLastError();
            if( error != WSAEWOULDBLOCK ) logging.error( L"accept failed. Error: %d", error );
            break;
        }

        set_nonblocking( fd );

        char address_buffer[INET_ADDRSTRLEN]{};
        inet_ntop( AF_INET, &remote.sin_addr, address_buffer, sizeof( address_buffer ) );

        auto client = std::make_shared<aries_socket>();
        client->fd = fd;
        client->socket_protocol = listener->socket_protocol;
        client->ip_address = address_buffer;
        client->port = ntohs( remote.sin_port );

        {
            std::lock_guard lock( socket_mutex_ );
            socket_set_.insert( client );
        }

        logging.information(
            L"Connection from %hs:%u [%ls]",
            client->ip_address.c_str(),
            client->port,
            client->socket_protocol == protocol_lobby ? L"Lobby" : L"Buddy"
        );
        notify_socket( client, 1 );
    }
}

void CSocketManager::extract_packets( const sptr_socket &socket )
{
    while( socket->read_data.size() >= 12 )
    {
        if( looks_like_http_request( socket->read_data ) )
        {
            handle_http_probe( socket );
            return;
        }

        const auto packet_length = read_be32_socket( socket->read_data.data() + 8 );
        if( packet_length < 12 || packet_length > g_configuration->max_packet_size )
        {
            logging.error( L"Invalid Aries packet length %u from %hs", packet_length, socket->ip_address.c_str() );
            socket->disconnected = true;
            return;
        }

        if( socket->read_data.size() < packet_length ) return;

        std::vector<uint8_t> raw(
            socket->read_data.begin(),
            socket->read_data.begin() + static_cast<std::ptrdiff_t>( packet_length )
        );
        socket->read_data.erase(
            socket->read_data.begin(),
            socket->read_data.begin() + static_cast<std::ptrdiff_t>( packet_length )
        );

        read_queue_.push( std::make_pair( socket, std::make_shared<aries_packet>( std::move( raw ) ) ) );
    }
}

void CSocketManager::TCP_TryReadData( const sptr_socket &socket )
{
    std::array<uint8_t, 8192> buffer{};

    for( ;; )
    {
#ifdef _WIN32
        const auto received = recv( socket->fd, reinterpret_cast<char *>( buffer.data() ), static_cast<int>( buffer.size() ), 0 );
#else
        const auto received = recv( socket->fd, buffer.data(), buffer.size(), 0 );
#endif
        if( received > 0 )
        {
            socket->read_data.insert( socket->read_data.end(), buffer.begin(), buffer.begin() + received );
            extract_packets( socket );
            if( socket->disconnected ) return;
            continue;
        }

        if( received == 0 )
        {
            logging.information(
                L"TCP peer closed connection %hs:%u [%ls]",
                socket->ip_address.c_str(),
                socket->port,
                socket->socket_protocol == protocol_lobby ? L"Lobby" : L"Buddy"
            );
            socket->disconnected = true;
            return;
        }

        const auto error = WSAGetLastError();
        if( error == WSAEWOULDBLOCK ) return;
#ifdef _WIN32
        if( error == WSAECONNRESET )
        {
            logging.warning(
                L"TCP peer reset connection %hs:%u [%ls]",
                socket->ip_address.c_str(),
                socket->port,
                socket->socket_protocol == protocol_lobby ? L"Lobby" : L"Buddy"
            );
            socket->disconnected = true;
            return;
        }
#else
        if( error == EAGAIN || error == EWOULDBLOCK ) return;
#endif
        logging.error( L"recv failed for %hs. Error: %d", socket->ip_address.c_str(), error );
        socket->disconnected = true;
        return;
    }
}

void CSocketManager::TCP_TryWriteData( const sptr_socket &socket )
{
    for( ;; )
    {
        if( socket->write_data.empty() )
        {
            sptr_packet packet;
            if( false == socket->send_queue.try_pop( packet ) ) return;
            socket->write_data = packet->raw();
            socket->write_offset = 0;
        }

        const auto remaining = socket->write_data.size() - socket->write_offset;
#ifdef _WIN32
        const auto sent = send(
            socket->fd,
            reinterpret_cast<const char *>( socket->write_data.data() + socket->write_offset ),
            static_cast<int>( remaining ),
            0
        );
#else
        const auto sent = send( socket->fd, socket->write_data.data() + socket->write_offset, remaining, 0 );
#endif
        if( sent > 0 )
        {
            socket->write_offset += static_cast<size_t>( sent );
            if( socket->write_offset >= socket->write_data.size() )
            {
                socket->write_data.clear();
                socket->write_offset = 0;
                continue;
            }
            return;
        }

        if( sent == 0 ) return;

        const auto error = WSAGetLastError();
        if( error == WSAEWOULDBLOCK ) return;
#ifndef _WIN32
        if( error == EAGAIN || error == EWOULDBLOCK ) return;
#endif
        logging.error( L"send failed for %hs. Error: %d", socket->ip_address.c_str(), error );
        socket->disconnected = true;
        return;
    }
}

void CSocketManager::close_disconnected()
{
    std::lock_guard lock( socket_mutex_ );
    for( auto iter = socket_set_.begin(); iter != socket_set_.end(); )
    {
        const auto &socket = *iter;
        if( false == socket->disconnected || socket->listener )
        {
            ++iter;
            continue;
        }

        if( false == socket->disconnect_notified.exchange( true ) ) notify_socket( socket, 200 );
        if( socket->fd != INVALID_SOCKET )
        {
            logging.debug(
                L"Closing local TCP socket %hs:%u [%ls]",
                socket->ip_address.c_str(),
                socket->port,
                socket->socket_protocol == protocol_lobby ? L"Lobby" : L"Buddy"
            );
            ::shutdown( socket->fd, SD_BOTH );
            closesocket( socket->fd );
            socket->fd = INVALID_SOCKET;
        }
        iter = socket_set_.erase( iter );
    }
}

void CSocketManager::socket_io_thread()
{
    while( run_ )
    {
        fd_set read_set;
        fd_set write_set;
        FD_ZERO( &read_set );
        FD_ZERO( &write_set );

        SOCKET max_fd = 0;
        std::vector<sptr_socket> sockets;
        {
            std::lock_guard lock( socket_mutex_ );
            sockets.assign( socket_set_.begin(), socket_set_.end() );
        }

        for( const auto &socket : sockets )
        {
            if( socket->fd == INVALID_SOCKET || socket->disconnected ) continue;
            FD_SET( socket->fd, &read_set );
            if( false == socket->listener && ( false == socket->write_data.empty() || false == socket->send_queue.empty() ) )
            {
                FD_SET( socket->fd, &write_set );
            }
            if( socket->fd > max_fd ) max_fd = socket->fd;
        }

        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 10000;

#ifdef _WIN32
        const auto ready = select( 0, &read_set, &write_set, nullptr, &timeout );
#else
        const auto ready = select( max_fd + 1, &read_set, &write_set, nullptr, &timeout );
#endif
        if( ready == SOCKET_ERROR )
        {
            const auto error = WSAGetLastError();
            logging.error( L"select failed. Error: %d", error );
            std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
            continue;
        }

        if( ready > 0 )
        {
            for( const auto &socket : sockets )
            {
                if( socket->fd == INVALID_SOCKET || socket->disconnected ) continue;
                if( FD_ISSET( socket->fd, &read_set ) )
                {
                    if( socket->listener ) accept_new_tcp( socket );
                    else TCP_TryReadData( socket );
                }
                if( false == socket->listener && false == socket->disconnected && FD_ISSET( socket->fd, &write_set ) )
                {
                    TCP_TryWriteData( socket );
                }
            }
        }

        close_disconnected();
    }
}
