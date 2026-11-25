#pragma once

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef FD_SETSIZE
#define FD_SETSIZE 1024
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment( lib, "Ws2_32.lib" )
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

using SOCKET = int;
constexpr SOCKET INVALID_SOCKET = -1;
constexpr int SOCKET_ERROR = -1;
constexpr int SD_BOTH = SHUT_RDWR;
constexpr int WSAEWOULDBLOCK = EWOULDBLOCK;
constexpr int WSAEINPROGRESS = EINPROGRESS;
constexpr int WSAECONNRESET = ECONNRESET;

inline int closesocket( SOCKET socket )
{
    return close( socket );
}

inline int WSAGetLastError()
{
    return errno;
}
#endif
