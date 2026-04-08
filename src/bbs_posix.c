/*
 * SYNTHRLN - Multi-Platform BBS-to-TCP Bridge
 * bbs_posix.c - BBS I/O for Windows and Linux, including --local mode
 *
 * Runtime dispatch on cfg->local_mode:
 *   local_mode == 0: Windows=inherited Winsock socket, Linux=stdio pipes
 *   local_mode == 1: Windows=conio/console API,        Linux=termios raw mode
 *
 * This single file provides all bbs_io.h implementations for Win/Linux.
 * The DOS FOSSIL implementation lives in bbs_fossil.c.
 */

#if defined(_WIN32) || defined(__linux__) || (defined(__unix__) && !defined(__MSDOS__))

#if !defined(_WIN32)
#  define _POSIX_C_SOURCE 200112L
#  define _DEFAULT_SOURCE
#endif

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <winsock2.h>
#  include <windows.h>
#  pragma comment(lib, "ws2_32.lib")
typedef SOCKET bbs_socket_t;
#  define INVALID_BBS_SOCKET INVALID_SOCKET
#else
#  include <unistd.h>
#  include <termios.h>
#  include <sys/select.h>
#  include <sys/time.h>
#  include <errno.h>
   typedef int bbs_socket_t;
#  define INVALID_BBS_SOCKET (-1)
#endif

#include "synthrln.h"
#include "bbs_io.h"

/* Runtime state */
static int g_bbs_active  = 0;
static int g_local_mode  = 0;

#ifdef _WIN32
static bbs_socket_t g_bbs_sock   = INVALID_BBS_SOCKET;
static int          g_use_socket = 0;
#else
/* Linux termios saved state for --local mode restore */
static struct termios g_old_termios;
static int            g_termios_saved = 0;
#endif

/* =========================================================================
 * bbs_init
 * ========================================================================= */
int bbs_init(const app_config_t *cfg, const session_context_t *sess)
{
    g_local_mode = cfg->local_mode;

#ifdef _WIN32
    {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            fprintf(stderr, "SYNTHRLN: WSAStartup failed.\n");
            return -1;
        }
    }

    if (cfg->local_mode) {
        /* Local mode: use Windows console APIs */
        g_use_socket = 0;
        g_bbs_active = 1;
        return 0;
    }

    /* BBS mode: inherit Winsock socket from DOOR32.SYS */
    if (sess->socket_handle <= 0) {
        fprintf(stderr, "SYNTHRLN: Invalid socket handle from dropfile: %ld\n",
                sess->socket_handle);
        return -1;
    }
    g_bbs_sock   = (SOCKET)sess->socket_handle;
    g_use_socket = 1;
    g_bbs_active = 1;
    return 0;

#else /* Linux */
    (void)sess;

    if (cfg->local_mode) {
        /* Put terminal in raw mode for direct keyboard I/O */
        struct termios raw;
        if (tcgetattr(STDIN_FILENO, &g_old_termios) == 0) {
            g_termios_saved = 1;
        }
        raw = g_old_termios;
        raw.c_iflag &= ~(unsigned)(IGNBRK | BRKINT | PARMRK | ISTRIP |
                                   INLCR  | IGNCR  | ICRNL  | IXON);
        raw.c_oflag &= ~(unsigned)OPOST;
        raw.c_lflag &= ~(unsigned)(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
        raw.c_cflag &= ~(unsigned)(CSIZE | PARENB);
        raw.c_cflag |= CS8;
        raw.c_cc[VMIN]  = 0;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
            fprintf(stderr, "SYNTHRLN: Failed to set raw terminal mode.\n");
            return -1;
        }
    }
    /* BBS mode on Linux: stdin/stdout already connected to user by the BBS */
    g_bbs_active = 1;
    return 0;
#endif
}

/* =========================================================================
 * bbs_has_data
 * ========================================================================= */
int bbs_has_data(void)
{
    if (!g_bbs_active) return 0;

#ifdef _WIN32
    if (g_use_socket && !g_local_mode) {
        fd_set fds;
        struct timeval tv;
        tv.tv_sec = 0; tv.tv_usec = 0;
        FD_ZERO(&fds); FD_SET(g_bbs_sock, &fds);
        return (select(0, &fds, NULL, NULL, &tv) > 0) ? 1 : 0;
    } else {
        /* Console input */
        HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
        DWORD events = 0;
        INPUT_RECORD ir;
        DWORD peeked = 0;
        if (GetNumberOfConsoleInputEvents(h, &events) && events > 0) {
            PeekConsoleInputA(h, &ir, 1, &peeked);
            if (peeked > 0 && ir.EventType == KEY_EVENT &&
                ir.Event.KeyEvent.bKeyDown)
                return 1;
        }
        return 0;
    }
#else
    {
        struct timeval tv;
        fd_set fds;
        tv.tv_sec = 0; tv.tv_usec = 0;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        return (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) ? 1 : 0;
    }
#endif
}

/* =========================================================================
 * bbs_read
 * ========================================================================= */
int bbs_read(uint8_t *buf, int max_len)
{
    if (!g_bbs_active) return -1;

#ifdef _WIN32
    if (g_use_socket && !g_local_mode) {
        int n = recv(g_bbs_sock, (char *)buf, max_len, 0);
        if (n == 0 || n == SOCKET_ERROR) { g_bbs_active = 0; return -1; }
        return n;
    } else {
        DWORD n = 0;
        HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
        if (!ReadConsoleA(h, buf, (DWORD)max_len, &n, NULL)) {
            if (!ReadFile(h, buf, (DWORD)max_len, &n, NULL))
                return -1;
        }
        return (int)n;
    }
#else
    {
        ssize_t n = read(STDIN_FILENO, buf, (size_t)max_len);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
            g_bbs_active = 0;
            return -1;
        }
        if (n == 0) { g_bbs_active = 0; return -1; }
        return (int)n;
    }
#endif
}

/* =========================================================================
 * bbs_write
 * ========================================================================= */
int bbs_write(const uint8_t *buf, int len)
{
    if (!g_bbs_active) return -1;

#ifdef _WIN32
    if (g_use_socket && !g_local_mode) {
        int sent = 0;
        while (sent < len) {
            int n = send(g_bbs_sock, (const char *)(buf + sent), len - sent, 0);
            if (n == SOCKET_ERROR) { g_bbs_active = 0; return -1; }
            sent += n;
        }
        return sent;
    } else {
        DWORD written = 0;
        WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE),
                      buf, (DWORD)len, &written, NULL);
        return (int)written;
    }
#else
    {
        ssize_t total = 0;
        while (total < (ssize_t)len) {
            ssize_t n = write(STDOUT_FILENO, buf + total,
                              (size_t)((ssize_t)len - total));
            if (n < 0) {
                if (errno == EAGAIN) continue;
                g_bbs_active = 0;
                return -1;
            }
            total += n;
        }
        return (int)total;
    }
#endif
}

/* =========================================================================
 * bbs_flush
 * ========================================================================= */
void bbs_flush(void)
{
#ifdef _WIN32
    FlushFileBuffers(GetStdHandle(STD_OUTPUT_HANDLE));
#else
    (void)fflush(stdout);
    fsync(STDOUT_FILENO);
#endif
}

/* =========================================================================
 * bbs_is_connected
 * ========================================================================= */
int bbs_is_connected(void)
{
    return g_bbs_active;
}

/* =========================================================================
 * bbs_close
 * ========================================================================= */
void bbs_close(void)
{
    if (!g_bbs_active) return;
    g_bbs_active = 0;

#ifdef _WIN32
    if (g_use_socket && g_bbs_sock != INVALID_SOCKET) {
        closesocket(g_bbs_sock);
        g_bbs_sock = INVALID_SOCKET;
    }
    WSACleanup();
#else
    if (g_local_mode && g_termios_saved) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_old_termios);
        g_termios_saved = 0;
    }
#endif
}

#endif /* _WIN32 || __linux__ */
