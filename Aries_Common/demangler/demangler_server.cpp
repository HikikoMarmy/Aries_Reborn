#include "stdafx.h"
#include "../global_define.h"
#include "demangler_server.h"

static std::string demangler_address_text( const sockaddr_in &address )
{
    char buffer[INET_ADDRSTRLEN]{};
    if( nullptr == inet_ntop( AF_INET, &address.sin_addr, buffer, sizeof( buffer ) ) ) return {};
    return buffer;
}

static std::string demangler_trim_copy( std::string value )
{
    const auto first = value.find_first_not_of( " \t\r\n" );
    if( first == std::string::npos ) return {};
    const auto last = value.find_last_not_of( " \t\r\n" );
    return value.substr( first, last - first + 1 );
}

static std::string demangler_get_header( const std::string &headers, const std::string &name )
{
    std::istringstream stream( headers );
    std::string line;
    while( std::getline( stream, line ) )
    {
        if( false == line.empty() && line.back() == '\r' ) line.pop_back();
        const auto colon = line.find( ':' );
        if( colon == std::string::npos ) continue;
        const auto key = demangler_trim_copy( line.substr( 0, colon ) );
        if( key != name ) continue;
        return demangler_trim_copy( line.substr( colon + 1 ) );
    }
    return {};
}

static std::string demangler_get_parameter( const std::string &value, const std::string &name )
{
    size_t start = 0;
    while( start <= value.size() )
    {
        const auto end = value.find( '&', start );
        const auto token = value.substr( start, end == std::string::npos ? std::string::npos : end - start );
        const auto equals = token.find( '=' );
        if( equals != std::string::npos && token.substr( 0, equals ) == name ) return token.substr( equals + 1 );
        if( end == std::string::npos ) break;
        start = end + 1;
    }
    return {};
}

static std::string demangler_get_session_id( const std::string &headers )
{
    const auto cookie = demangler_get_header( headers, "Cookie" );
    size_t start = 0;
    while( start <= cookie.size() )
    {
        const auto end = cookie.find( ';', start );
        const auto token = demangler_trim_copy( cookie.substr( start, end == std::string::npos ? std::string::npos : end - start ) );
        const auto equals = token.find( '=' );
        if( equals != std::string::npos && token.substr( 0, equals ) == "sessionID" ) return token.substr( equals + 1 );
        if( end == std::string::npos ) break;
        start = end + 1;
    }
    return {};
}

static uint16_t demangler_parse_port( const std::string &value )
{
    if( value.empty() ) return 0;
    try
    {
        const auto port = std::stoul( value );
        if( port == 0 || port > 65535 ) return 0;
        return static_cast<uint16_t>( port );
    }
    catch( ... )
    {
        return 0;
    }
}

static size_t demangler_http_header_end( const std::string &request )
{
    const auto crlf = request.find( "\r\n\r\n" );
    if( crlf != std::string::npos ) return crlf + 4;
    const auto lf = request.find( "\n\n" );
    if( lf != std::string::npos ) return lf + 2;
    return std::string::npos;
}

static bool demangler_is_private_ipv4( const std::string &ip )
{
    in_addr address{};
    if( inet_pton( AF_INET, ip.c_str(), &address ) != 1 ) return false;
    const auto value = ntohl( address.s_addr );
    const auto first = ( value >> 24 ) & 0xFFu;
    const auto second = ( value >> 16 ) & 0xFFu;
    if( first == 10 ) return true;
    if( first == 127 ) return true;
    if( first == 192 && second == 168 ) return true;
    if( first == 172 && second >= 16 && second <= 31 ) return true;
    return false;
}

CDemanglerServer::CDemanglerServer() = default;

CDemanglerServer::~CDemanglerServer()
{
    shutdown();
}

bool CDemanglerServer::start( const std::string &bind_ip, const std::string &public_ip, const std::vector<uint16_t> &ports, uint32_t probe_count, uint32_t session_timeout_seconds )
{
    if( run_ ) return true;
    if( ports.empty() || public_ip.empty() ) return false;

    sockaddr_in public_address{};
    if( inet_pton( AF_INET, public_ip.c_str(), &public_address.sin_addr ) != 1 )
    {
        logging.error( L"Demangler requires a numeric public IPv4 address, got %hs", public_ip.c_str() );
        return false;
    }

    bind_ip_ = bind_ip;
    public_ip_ = public_ip;
    probe_count_ = std::max<uint32_t>( 1, probe_count );
    session_timeout_seconds_ = std::max<uint32_t>( 10, session_timeout_seconds );

    std::vector<uint16_t> unique_ports;
    for( const auto port : ports )
    {
        if( port == 0 ) continue;
        if( std::find( unique_ports.begin(), unique_ports.end(), port ) != unique_ports.end() ) continue;
        unique_ports.push_back( port );
    }
    if( unique_ports.empty() ) return false;

    for( const auto port : unique_ports )
    {
        listener entry;
        entry.port = port;
        if( false == open_tcp_listener( entry ) || false == open_udp_socket( entry ) )
        {
            if( entry.tcp_listener != INVALID_SOCKET )
            {
                ::shutdown( entry.tcp_listener, SD_BOTH );
                closesocket( entry.tcp_listener );
            }
            if( entry.udp_socket != INVALID_SOCKET ) closesocket( entry.udp_socket );
            shutdown();
            return false;
        }
        listeners_.push_back( entry );
    }

    run_ = true;
    thread_ = std::thread( &CDemanglerServer::io_thread, this );
    for( const auto &entry : listeners_ )
    {
        logging.information( L"Demangler TCP/UDP listening on %hs:%u, probe target %hs:%u", bind_ip_.c_str(), entry.port, public_ip_.c_str(), entry.port );
    }
    if( demangler_is_private_ipv4( public_ip_ ) )
    {
        logging.warning( L"Demangler probe target %hs is private; external clients require demangler_ip or service_ip to be a public IPv4 address", public_ip_.c_str() );
    }
    return true;
}

void CDemanglerServer::shutdown()
{
    run_ = false;
    if( thread_.joinable() ) thread_.join();

    for( auto &entry : listeners_ )
    {
        if( entry.tcp_listener != INVALID_SOCKET )
        {
            ::shutdown( entry.tcp_listener, SD_BOTH );
            closesocket( entry.tcp_listener );
            entry.tcp_listener = INVALID_SOCKET;
        }
        if( entry.udp_socket != INVALID_SOCKET )
        {
            closesocket( entry.udp_socket );
            entry.udp_socket = INVALID_SOCKET;
        }
    }

    listeners_.clear();
    sessions_.clear();
    tags_.clear();
}

bool CDemanglerServer::set_nonblocking( SOCKET fd )
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

bool CDemanglerServer::open_tcp_listener( listener &entry )
{
    entry.tcp_listener = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
    if( entry.tcp_listener == INVALID_SOCKET )
    {
        logging.error( L"Unable to create demangler TCP socket for port %u", entry.port );
        return false;
    }

    int reuse = 1;
#ifdef _WIN32
    setsockopt( entry.tcp_listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>( &reuse ), sizeof( reuse ) );
#else
    setsockopt( entry.tcp_listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ) );
#endif

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons( entry.port );
    if( bind_ip_.empty() || bind_ip_ == "0.0.0.0" ) address.sin_addr.s_addr = htonl( INADDR_ANY );
    else if( inet_pton( AF_INET, bind_ip_.c_str(), &address.sin_addr ) != 1 )
    {
        logging.error( L"Invalid demangler bind address: %hs", bind_ip_.c_str() );
        return false;
    }

    if( bind( entry.tcp_listener, reinterpret_cast<sockaddr *>( &address ), sizeof( address ) ) == SOCKET_ERROR )
    {
        logging.error( L"Unable to bind demangler TCP %hs:%u. Error: %d", bind_ip_.c_str(), entry.port, WSAGetLastError() );
        return false;
    }

    if( listen( entry.tcp_listener, SOMAXCONN ) == SOCKET_ERROR )
    {
        logging.error( L"Unable to listen on demangler TCP port %u. Error: %d", entry.port, WSAGetLastError() );
        return false;
    }

    return set_nonblocking( entry.tcp_listener );
}

bool CDemanglerServer::open_udp_socket( listener &entry )
{
    entry.udp_socket = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
    if( entry.udp_socket == INVALID_SOCKET )
    {
        logging.error( L"Unable to create demangler UDP socket for port %u", entry.port );
        return false;
    }

    int reuse = 1;
#ifdef _WIN32
    setsockopt( entry.udp_socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>( &reuse ), sizeof( reuse ) );
#else
    setsockopt( entry.udp_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ) );
#endif

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons( entry.port );
    if( bind_ip_.empty() || bind_ip_ == "0.0.0.0" ) address.sin_addr.s_addr = htonl( INADDR_ANY );
    else if( inet_pton( AF_INET, bind_ip_.c_str(), &address.sin_addr ) != 1 )
    {
        logging.error( L"Invalid demangler bind address: %hs", bind_ip_.c_str() );
        return false;
    }

    if( bind( entry.udp_socket, reinterpret_cast<sockaddr *>( &address ), sizeof( address ) ) == SOCKET_ERROR )
    {
        logging.error( L"Unable to bind demangler UDP %hs:%u. Error: %d", bind_ip_.c_str(), entry.port, WSAGetLastError() );
        return false;
    }

    return set_nonblocking( entry.udp_socket );
}

void CDemanglerServer::io_thread()
{
    auto last_cleanup = std::chrono::steady_clock::now();

    while( run_ )
    {
        fd_set read_set;
        FD_ZERO( &read_set );
        SOCKET max_fd = 0;
        for( const auto &entry : listeners_ )
        {
            if( entry.tcp_listener != INVALID_SOCKET )
            {
                FD_SET( entry.tcp_listener, &read_set );
                max_fd = std::max( max_fd, entry.tcp_listener );
            }
            if( entry.udp_socket != INVALID_SOCKET )
            {
                FD_SET( entry.udp_socket, &read_set );
                max_fd = std::max( max_fd, entry.udp_socket );
            }
        }

        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 10000;

#ifdef _WIN32
        const auto ready = select( 0, &read_set, nullptr, nullptr, &timeout );
#else
        const auto ready = select( max_fd + 1, &read_set, nullptr, nullptr, &timeout );
#endif
        if( ready == SOCKET_ERROR )
        {
            if( false == run_ ) break;
            logging.error( L"Demangler select failed. Error: %d", WSAGetLastError() );
            std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
            continue;
        }

        if( ready > 0 )
        {
            for( auto &entry : listeners_ )
            {
                if( entry.udp_socket != INVALID_SOCKET && FD_ISSET( entry.udp_socket, &read_set ) ) handle_udp_packets( entry );
                if( entry.tcp_listener != INVALID_SOCKET && FD_ISSET( entry.tcp_listener, &read_set ) ) accept_tcp_clients( entry );
            }
        }

        const auto now = std::chrono::steady_clock::now();
        if( now - last_cleanup >= std::chrono::seconds( 5 ) )
        {
            cleanup_sessions();
            last_cleanup = now;
        }
    }
}

void CDemanglerServer::accept_tcp_clients( listener &entry )
{
    for( ;; )
    {
        sockaddr_in remote{};
#ifdef _WIN32
        int remote_size = sizeof( remote );
#else
        socklen_t remote_size = sizeof( remote );
#endif
        const auto fd = accept( entry.tcp_listener, reinterpret_cast<sockaddr *>( &remote ), &remote_size );
        if( fd == INVALID_SOCKET )
        {
            if( WSAGetLastError() != WSAEWOULDBLOCK ) logging.error( L"Demangler accept failed on port %u. Error: %d", entry.port, WSAGetLastError() );
            break;
        }

        handle_tcp_client( fd, remote, entry.port );
        ::shutdown( fd, SD_BOTH );
        closesocket( fd );
    }
}

void CDemanglerServer::handle_tcp_client( SOCKET fd, const sockaddr_in &remote, uint16_t listener_port )
{
    std::string request;
    if( false == read_http_request( fd, request ) ) return;

    const auto header_end = demangler_http_header_end( request );
    if( header_end == std::string::npos ) return;

    const auto first_line_end = request.find_first_of( "\r\n" );
    if( first_line_end == std::string::npos ) return;

    const auto first_line = request.substr( 0, first_line_end );
    std::istringstream first_line_stream( first_line );
    std::string method;
    std::string target;
    std::string version;
    first_line_stream >> method >> target >> version;

    const auto headers = request.substr( first_line_end + ( request[first_line_end] == '\r' ? 2 : 1 ), header_end - first_line_end );
    const auto body = request.substr( header_end );
    const auto remote_ip = demangler_address_text( remote );

    logging.debug( L"Demangler HTTP %hs %hs <- %hs:%u listener=%u", method.c_str(), target.c_str(), remote_ip.c_str(), ntohs( remote.sin_port ), listener_port );

    if( method == "GET" && target.rfind( "/getPeerAddress", 0 ) == 0 )
    {
        handle_get_peer_address( fd, remote, target, headers, listener_port );
        return;
    }

    if( method == "POST" && target == "/connectionStatus" )
    {
        handle_connection_status( fd, remote, body, headers, listener_port );
        return;
    }

    send_http_response( fd, build_failure_response() );
}

bool CDemanglerServer::read_http_request( SOCKET fd, std::string &request )
{
    std::array<char, 4096> buffer{};
    size_t expected_size = std::string::npos;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds( 2 );

    while( std::chrono::steady_clock::now() < deadline && request.size() < 16384 )
    {
        fd_set read_set;
        FD_ZERO( &read_set );
        FD_SET( fd, &read_set );
        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;

#ifdef _WIN32
        const auto ready = select( 0, &read_set, nullptr, nullptr, &timeout );
#else
        const auto ready = select( fd + 1, &read_set, nullptr, nullptr, &timeout );
#endif
        if( ready == SOCKET_ERROR ) return false;
        if( ready == 0 ) continue;

#ifdef _WIN32
        const auto received = recv( fd, buffer.data(), static_cast<int>( buffer.size() ), 0 );
#else
        const auto received = recv( fd, buffer.data(), buffer.size(), 0 );
#endif
        if( received <= 0 ) return false;
        request.append( buffer.data(), static_cast<size_t>( received ) );

        const auto header_end = demangler_http_header_end( request );
        if( header_end == std::string::npos ) continue;

        if( expected_size == std::string::npos )
        {
            const auto headers = request.substr( 0, header_end );
            const auto content_length_text = demangler_get_header( headers, "Content-Length" );
            size_t content_length = 0;
            if( false == content_length_text.empty() )
            {
                try
                {
                    content_length = static_cast<size_t>( std::stoul( content_length_text ) );
                }
                catch( ... )
                {
                    return false;
                }
            }
            expected_size = header_end + content_length;
        }

        if( request.size() >= expected_size )
        {
            request.resize( expected_size );
            return true;
        }
    }

    return false;
}

bool CDemanglerServer::send_http_response( SOCKET fd, const std::string &body )
{
    std::ostringstream response;
    response << "HTTP/1.1 200 OK\r\n";
    response << "Content-Type: text/plain\r\n";
    response << "Content-Length: " << body.size() << "\r\n";
    response << "Connection: close\r\n";
    response << "\r\n";
    response << body;

    const auto data = response.str();
    size_t offset = 0;
    while( offset < data.size() )
    {
#ifdef _WIN32
        const auto sent = send( fd, data.data() + offset, static_cast<int>( data.size() - offset ), 0 );
#else
        const auto sent = send( fd, data.data() + offset, data.size() - offset, 0 );
#endif
        if( sent <= 0 ) return false;
        offset += static_cast<size_t>( sent );
    }
    return true;
}

void CDemanglerServer::handle_get_peer_address( SOCKET fd, const sockaddr_in &remote, const std::string &request_target, const std::string &headers, uint16_t listener_port )
{
    const auto question = request_target.find( '?' );
    const auto query = question == std::string::npos ? std::string{} : request_target.substr( question + 1 );
    const auto private_ip = demangler_get_parameter( query, "myIP" );
    const auto private_port = demangler_parse_port( demangler_get_parameter( query, "myPort" ) );
    const auto session_id = demangler_get_session_id( headers );

    if( private_ip.empty() || private_port == 0 || session_id.empty() )
    {
        logging.warning( L"Demangler rejected malformed getPeerAddress request" );
        send_http_response( fd, build_failure_response() );
        return;
    }

    const auto key = make_participant_key( remote, private_ip, private_port );
    const auto session_key = make_session_key( session_id, listener_port );
    const auto now = std::chrono::steady_clock::now();
    auto &session_entry = sessions_[session_key];
    session_entry.id = session_id;
    session_entry.listener_port = listener_port;
    session_entry.last_seen = now;

    auto &entry = get_or_create_participant( session_entry, session_key, key, private_ip, private_port, listener_port );
    entry.last_seen = now;

    if( entry.mapped )
    {
        if( auto *peer = find_mapped_peer( session_entry, key ) )
        {
            logging.information( L"Demangler success session=%hs listener=%u peer=%hs:%u", session_id.c_str(), listener_port, peer->public_ip.c_str(), peer->public_port );
            send_http_response( fd, build_success_response( *peer ) );
            return;
        }
    }

    logging.debug( L"Demangler probe session=%hs listener=%u tag=%hs source=%hs:%u", session_id.c_str(), listener_port, entry.tag.c_str(), private_ip.c_str(), private_port );
    send_http_response( fd, build_probe_response( entry ) );
}

void CDemanglerServer::handle_connection_status( SOCKET fd, const sockaddr_in &remote, const std::string &body, const std::string &headers, uint16_t listener_port )
{
    const auto session_id = demangler_get_session_id( headers );
    const auto status = demangler_get_parameter( body, "status" );
    const auto game_feature_id = demangler_get_parameter( body, "gameFeatureID" );
    const auto latency = demangler_get_parameter( body, "latency" );
    const auto remote_ip = demangler_address_text( remote );

    logging.information(
        L"Demangler report session=%hs listener=%u client=%hs status=%hs game=%hs latency=%hs",
        session_id.c_str(),
        listener_port,
        remote_ip.c_str(),
        status.c_str(),
        game_feature_id.c_str(),
        latency.empty() ? "n/a" : latency.c_str()
    );

    send_http_response( fd, {} );
}

std::string CDemanglerServer::build_probe_response( const participant &entry ) const
{
    std::ostringstream body;
    body << "status=probe\n";
    body << "targetIP-1=" << public_ip_ << "\n";
    body << "targetPort-1=" << entry.listener_port << "\n";
    body << "tag-1=" << entry.tag << "\n";
    body << "sendCount-1=" << probe_count_ << "\n";
    body << "sourcePort-1=" << entry.private_port << "\n";
    return body.str();
}

std::string CDemanglerServer::build_success_response( const participant &peer ) const
{
    std::ostringstream body;
    body << "status=success\n";
    body << "peerIP=" << peer.public_ip << "\n";
    body << "peerPort=" << peer.public_port << "\n";
    return body.str();
}

std::string CDemanglerServer::build_failure_response() const
{
    return "status=failure\n";
}

CDemanglerServer::participant &CDemanglerServer::get_or_create_participant( session &session_entry, const std::string &session_key, const std::string &key, const std::string &private_ip, uint16_t private_port, uint16_t listener_port )
{
    if( auto *entry = find_participant( session_entry, key ) ) return *entry;

    participant entry;
    entry.key = key;
    entry.private_ip = private_ip;
    entry.private_port = private_port;
    entry.listener_port = listener_port;
    entry.tag = make_tag();
    entry.last_seen = std::chrono::steady_clock::now();
    session_entry.participants.push_back( std::move( entry ) );

    auto &created = session_entry.participants.back();
    tags_[created.tag] = { session_key, created.key, listener_port };
    logging.information( L"Demangler participant session=%hs listener=%u local=%hs:%u tag=%hs", session_entry.id.c_str(), listener_port, private_ip.c_str(), private_port, created.tag.c_str() );
    return created;
}

CDemanglerServer::participant *CDemanglerServer::find_participant( session &session_entry, const std::string &key )
{
    const auto iter = std::find_if( session_entry.participants.begin(), session_entry.participants.end(), [&]( const participant &entry ) { return entry.key == key; } );
    if( iter == session_entry.participants.end() ) return nullptr;
    return &*iter;
}

CDemanglerServer::participant *CDemanglerServer::find_mapped_peer( session &session_entry, const std::string &key )
{
    const auto iter = std::find_if(
        session_entry.participants.begin(),
        session_entry.participants.end(),
        [&]( const participant &entry ) { return entry.key != key && entry.mapped; }
    );
    if( iter == session_entry.participants.end() ) return nullptr;
    return &*iter;
}

std::string CDemanglerServer::make_participant_key( const sockaddr_in &remote, const std::string &private_ip, uint16_t private_port ) const
{
    std::ostringstream key;
    key << demangler_address_text( remote ) << '|' << private_ip << '|' << private_port;
    return key.str();
}

std::string CDemanglerServer::make_session_key( const std::string &session_id, uint16_t listener_port ) const
{
    std::ostringstream key;
    key << listener_port << '|' << session_id;
    return key.str();
}

std::string CDemanglerServer::make_tag()
{
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now().time_since_epoch() ).count();
    std::ostringstream tag;
    tag << std::hex << now << '-' << next_tag_++;
    return tag.str();
}

void CDemanglerServer::handle_udp_packets( listener &listener_entry )
{
    std::array<char, 2048> buffer{};

    for( ;; )
    {
        sockaddr_in remote{};
#ifdef _WIN32
        int remote_size = sizeof( remote );
        const auto received = recvfrom( listener_entry.udp_socket, buffer.data(), static_cast<int>( buffer.size() ), 0, reinterpret_cast<sockaddr *>( &remote ), &remote_size );
#else
        socklen_t remote_size = sizeof( remote );
        const auto received = recvfrom( listener_entry.udp_socket, buffer.data(), buffer.size(), 0, reinterpret_cast<sockaddr *>( &remote ), &remote_size );
#endif
        if( received == SOCKET_ERROR )
        {
            if( WSAGetLastError() != WSAEWOULDBLOCK ) logging.error( L"Demangler UDP recvfrom failed. Error: %d", WSAGetLastError() );
            break;
        }
        if( received <= 0 ) break;

        std::string payload( buffer.data(), static_cast<size_t>( received ) );
        const auto nul = payload.find( '\0' );
        if( nul != std::string::npos ) payload.resize( nul );
        const auto tag = demangler_get_parameter( [&]() {
            std::string normalized = payload;
            std::replace( normalized.begin(), normalized.end(), '\r', '&' );
            std::replace( normalized.begin(), normalized.end(), '\n', '&' );
            return normalized;
        }(), "tag" );
        if( tag.empty() ) continue;

        const auto binding_iter = tags_.find( tag );
        if( binding_iter == tags_.end() )
        {
            logging.warning( L"Demangler UDP probe with unknown tag %hs", tag.c_str() );
            continue;
        }

        if( binding_iter->second.listener_port != listener_entry.port )
        {
            logging.warning( L"Demangler UDP probe tag %hs arrived on port %u but was issued for port %u", tag.c_str(), listener_entry.port, binding_iter->second.listener_port );
            continue;
        }

        const auto session_iter = sessions_.find( binding_iter->second.session_key );
        if( session_iter == sessions_.end() ) continue;
        auto *entry = find_participant( session_iter->second, binding_iter->second.participant_key );
        if( nullptr == entry ) continue;

        const auto public_ip = demangler_address_text( remote );
        const auto public_port = ntohs( remote.sin_port );
        const auto mapping_changed = false == entry->mapped || entry->public_ip != public_ip || entry->public_port != public_port;
        entry->public_ip = public_ip;
        entry->public_port = public_port;
        entry->mapped = true;
        entry->last_seen = std::chrono::steady_clock::now();
        session_iter->second.last_seen = entry->last_seen;

        if( mapping_changed )
        {
            logging.information(
                L"Demangler mapped session=%hs listener=%u %hs:%u -> %hs:%u",
                session_iter->second.id.c_str(),
                listener_entry.port,
                entry->private_ip.c_str(),
                entry->private_port,
                entry->public_ip.c_str(),
                entry->public_port
            );
        }
    }
}

void CDemanglerServer::cleanup_sessions()
{
    const auto now = std::chrono::steady_clock::now();
    const auto timeout = std::chrono::seconds( session_timeout_seconds_ );

    for( auto iter = sessions_.begin(); iter != sessions_.end(); )
    {
        if( now - iter->second.last_seen < timeout )
        {
            ++iter;
            continue;
        }

        logging.debug( L"Demangler expired session=%hs", iter->second.id.c_str() );
        remove_session_tags( iter->second );
        iter = sessions_.erase( iter );
    }
}

void CDemanglerServer::remove_session_tags( const session &session_entry )
{
    for( const auto &entry : session_entry.participants ) tags_.erase( entry.tag );
}
