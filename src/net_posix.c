/*
 * SYNTHRLN - Multi-Platform BBS-to-TCP Bridge
 * net_posix.c - Outbound TCP for Windows (Winsock) and Linux (BSD sockets)
 *
 * Handles both IPv4 hostname resolution and direct IP address connections.
 * Non-blocking I/O is implemented via select() with zero timeout.
 */

#if defined(_WIN32) || defined(__linux__) || (defined(__unix__) && !defined(__MSDOS__))

/* POSIX feature test macros - must come before all system headers */
#if !defined(_WIN32)
#  define _POSIX_C_SOURCE 200112L
#  define _DEFAULT_SOURCE
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
typedef SOCKET net_sock_t;
#  define NET_INVALID_SOCKET INVALID_SOCKET
#  define NET_SOCKET_ERROR   SOCKET_ERROR
#  define net_errno          WSAGetLastError()
#  define EWOULDBLOCK_LOCAL  WSAEWOULDBLOCK
#  define socklen_t          int
#else
#  include <unistd.h>
#  include <sys/types.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <netdb.h>
#  include <arpa/inet.h>
#  include <fcntl.h>
#  include <errno.h>
#  include <sys/select.h>
#  include <sys/time.h>
typedef int net_sock_t;
#  define NET_INVALID_SOCKET (-1)
#  define NET_SOCKET_ERROR   (-1)
#  define net_errno          errno
#  define EWOULDBLOCK_LOCAL  EWOULDBLOCK
#endif

#include "net_io.h"

static net_sock_t g_net_sock   = NET_INVALID_SOCKET;
static int        g_net_active = 0;

/* -------------------------------------------------------------------------
 * Internal: set non-blocking mode
 * ------------------------------------------------------------------------- */
static int set_nonblocking(net_sock_t sock)
{
#ifdef _WIN32
    u_long mode = 1;
    return (ioctlsocket(sock, FIONBIO, &mode) == 0) ? 0 : -1;
#else
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0) return -1;
    return (fcntl(sock, F_SETFL, flags | O_NONBLOCK) == 0) ? 0 : -1;
#endif
}

/* -------------------------------------------------------------------------
 * net_connect
 * ------------------------------------------------------------------------- */
int net_connect(const char *host, int port)
{
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct addrinfo *rp;
    char             port_str[16];
    int              rc;
    int              connected = 0;

#ifdef _WIN32
    {
        WSADATA wsa;
        /* Winsock may already be initialised by bbs_posix.c; calling again is safe */
        WSAStartup(MAKEWORD(2, 2), &wsa);
    }
#endif

    snprintf(port_str, sizeof(port_str), "%d", port);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;       /* IPv4 only */
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    rc = getaddrinfo(host, port_str, &hints, &res);
    if (rc != 0) {
#ifdef _WIN32
        fprintf(stderr, "SYNTHRLN: getaddrinfo failed for %s: %d\n", host, WSAGetLastError());
#else
        fprintf(stderr, "SYNTHRLN: getaddrinfo failed for %s: %s\n", host, gai_strerror(rc));
#endif
        return -1;
    }

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        g_net_sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (g_net_sock == NET_INVALID_SOCKET) continue;

        if (connect(g_net_sock, rp->ai_addr, (socklen_t)rp->ai_addrlen) == 0) {
            connected = 1;
            break;
        }

        /* Close and try next */
#ifdef _WIN32
        closesocket(g_net_sock);
#else
        close(g_net_sock);
#endif
        g_net_sock = NET_INVALID_SOCKET;
    }

    freeaddrinfo(res);

    if (!connected) {
        fprintf(stderr, "SYNTHRLN: Could not connect to %s:%d\n", host, port);
        return -1;
    }

    /* Switch to non-blocking after connection */
    if (set_nonblocking(g_net_sock) != 0) {
        fprintf(stderr, "SYNTHRLN: Warning - could not set non-blocking socket mode.\n");
    }

    g_net_active = 1;
    fprintf(stderr, "SYNTHRLN: Connected to %s:%d\n", host, port);
    return 0;
}

/* -------------------------------------------------------------------------
 * net_has_data
 * ------------------------------------------------------------------------- */
int net_has_data(void)
{
    struct timeval tv = {0, 0};
    fd_set fds;
    if (!g_net_active || g_net_sock == NET_INVALID_SOCKET) return 0;
    FD_ZERO(&fds);
    FD_SET(g_net_sock, &fds);
#ifdef _WIN32
    return (select(0, &fds, NULL, NULL, &tv) > 0) ? 1 : 0;
#else
    return (select((int)g_net_sock + 1, &fds, NULL, NULL, &tv) > 0) ? 1 : 0;
#endif
}

/* -------------------------------------------------------------------------
 * net_read
 * ------------------------------------------------------------------------- */
int net_read(uint8_t *buf, int max_len)
{
    int n;
    if (!g_net_active || g_net_sock == NET_INVALID_SOCKET) return -1;

#ifdef _WIN32
    n = recv(g_net_sock, (char *)buf, max_len, 0);
    if (n == 0 || n == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) return 0;
        g_net_active = 0;
        return -1;
    }
#else
    n = (int)recv(g_net_sock, buf, (size_t)max_len, 0);
    if (n == 0) {
        g_net_active = 0;
        return -1;
    }
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        g_net_active = 0;
        return -1;
    }
#endif
    return n;
}

/* -------------------------------------------------------------------------
 * net_write
 * ------------------------------------------------------------------------- */
int net_write(const uint8_t *buf, int len)
{
    int sent = 0;
    if (!g_net_active || g_net_sock == NET_INVALID_SOCKET) return -1;

    while (sent < len) {
        int n;
#ifdef _WIN32
        n = send(g_net_sock, (const char *)(buf + sent), len - sent, 0);
        if (n == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) continue;
            g_net_active = 0;
            return -1;
        }
#else
        n = (int)send(g_net_sock, buf + sent, (size_t)(len - sent), 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            g_net_active = 0;
            return -1;
        }
#endif
        sent += n;
    }
    return sent;
}

/* -------------------------------------------------------------------------
 * net_is_connected
 * ------------------------------------------------------------------------- */
int net_is_connected(void)
{
    return g_net_active;
}

/* -------------------------------------------------------------------------
 * net_close
 * ------------------------------------------------------------------------- */
void net_close(void)
{
    if (g_net_sock != NET_INVALID_SOCKET) {
#ifdef _WIN32
        shutdown(g_net_sock, SD_BOTH);
        closesocket(g_net_sock);
        WSACleanup();
#else
        shutdown(g_net_sock, SHUT_RDWR);
        close(g_net_sock);
#endif
        g_net_sock = NET_INVALID_SOCKET;
    }
    g_net_active = 0;
}

#endif /* _WIN32 || __linux__ */
