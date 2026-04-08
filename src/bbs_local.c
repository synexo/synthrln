/*
 * SYNTHRLN - Multi-Platform BBS-to-TCP Bridge
 * bbs_local.c - Local diagnostic mode I/O
 *
 * Used when --local flag is passed. Bypasses FOSSIL/sockets/pipes entirely
 * and connects the sysop's local keyboard and screen directly to the bridge.
 *
 * Windows/DOS: Uses <conio.h>  kbhit() / getch() / putch()
 * Linux:       Uses termios to put stdin in raw non-canonical mode,
 *              with select() for non-blocking input.
 */

/* POSIX feature test macros - must come before all system headers */
#if !defined(_WIN32) && !defined(__MSDOS__) && !defined(__WATCOMC__)
#  define _POSIX_C_SOURCE 200112L
#  define _DEFAULT_SOURCE
#endif

#include <stdio.h>
#include <string.h>

#include "synthrln.h"
#include "bbs_io.h"

/* Only compiled when local_mode is in use; since we have a single binary,
 * we compile this on all platforms and let bbs_init gate the logic. */

#if defined(_WIN32) || defined(__MSDOS__) || defined(__WATCOMC__)

/* =========================================================================
 * Windows / DOS conio implementation
 * ========================================================================= */
#include <conio.h>
#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

static int g_local_active = 0;

int bbs_init(const app_config_t *cfg, const session_context_t *sess)
{
    (void)sess;
    if (!cfg->local_mode) {
        /* This file only handles local mode; if not local, init fails.
         * On Windows/DOS, bbs_posix.c handles non-local mode — the build
         * system must not link both for the same target. */
        fprintf(stderr, "SYNTHRLN: bbs_local.c compiled but not in local mode.\n");
        return -1;
    }
    g_local_active = 1;
    return 0;
}

int bbs_has_data(void)
{
    if (!g_local_active) return 0;
    return kbhit() ? 1 : 0;
}

int bbs_read(uint8_t *buf, int max_len)
{
    int count = 0;
    if (!g_local_active) return -1;
    while (count < max_len && kbhit()) {
        int c = getch();
        if (c == 0 || c == 0xE0) {
            /* Extended key: consume second byte and ignore */
            getch();
            continue;
        }
        buf[count++] = (uint8_t)c;
    }
    return count;
}

int bbs_write(const uint8_t *buf, int len)
{
    int i;
    if (!g_local_active) return -1;
    for (i = 0; i < len; i++) {
#ifdef _WIN32
        DWORD written;
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), &buf[i], 1, &written, NULL);
#else
        putch((int)buf[i]);
#endif
    }
    return len;
}

void bbs_flush(void)
{
#ifdef _WIN32
    FlushFileBuffers(GetStdHandle(STD_OUTPUT_HANDLE));
#endif
}

int bbs_is_connected(void)
{
    return g_local_active;
}

void bbs_close(void)
{
    g_local_active = 0;
}

#else /* Linux / POSIX */

/* =========================================================================
 * Linux termios raw-mode implementation
 * ========================================================================= */
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#include <sys/time.h>
#include <errno.h>

static int            g_local_active = 0;
static struct termios g_old_termios;
static int            g_termios_saved = 0;

int bbs_init(const app_config_t *cfg, const session_context_t *sess)
{
    struct termios raw;
    (void)sess;

    if (!cfg->local_mode) {
        fprintf(stderr, "SYNTHRLN: bbs_local.c compiled but not in local mode.\n");
        return -1;
    }

    /* Save current terminal settings */
    if (tcgetattr(STDIN_FILENO, &g_old_termios) == 0) {
        g_termios_saved = 1;
    }

    /* Configure raw mode */
    raw = g_old_termios;
    raw.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR |
                     ICRNL | IXON);
    raw.c_oflag &= ~OPOST;
    raw.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    raw.c_cflag &= ~(CSIZE | PARENB);
    raw.c_cflag |= CS8;
    raw.c_cc[VMIN]  = 0;  /* Non-blocking */
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
        fprintf(stderr, "SYNTHRLN: Failed to set raw terminal mode.\n");
        return -1;
    }

    g_local_active = 1;
    return 0;
}

int bbs_has_data(void)
{
    struct timeval tv = {0, 0};
    fd_set fds;
    if (!g_local_active) return 0;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    return (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) ? 1 : 0;
}

int bbs_read(uint8_t *buf, int max_len)
{
    ssize_t n;
    if (!g_local_active) return -1;
    n = read(STDIN_FILENO, buf, (size_t)max_len);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        g_local_active = 0;
        return -1;
    }
    return (int)n;
}

int bbs_write(const uint8_t *buf, int len)
{
    ssize_t total = 0;
    if (!g_local_active) return -1;
    while (total < len) {
        ssize_t n = write(STDOUT_FILENO, buf + total, (size_t)(len - total));
        if (n < 0) {
            if (errno == EAGAIN) continue;
            g_local_active = 0;
            return -1;
        }
        total += n;
    }
    return (int)total;
}

void bbs_flush(void)
{
    /* stdout is unbuffered in raw mode; flush just in case */
    fsync(STDOUT_FILENO);
}

int bbs_is_connected(void)
{
    return g_local_active;
}

void bbs_close(void)
{
    if (!g_local_active) return;
    g_local_active = 0;

    /* Restore original terminal settings */
    if (g_termios_saved) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_old_termios);
        g_termios_saved = 0;
    }
}

#endif /* Linux */
