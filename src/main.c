/*
 * SYNTHRLN - Multi-Platform BBS-to-TCP Bridge
 * main.c - Entry point, handshake generation, and dual-shuttle loop
 *
 * Targets: MS-DOS (OpenWatcom), Windows (MinGW-w64/MSVC), Linux (GCC/Clang)
 */

/* POSIX feature test macros - must precede all system headers */
#if defined(__linux__) || (defined(__unix__) && !defined(__MSDOS__))
#  define _POSIX_C_SOURCE 200112L
#  define _DEFAULT_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#if defined(__linux__) || (defined(__unix__) && !defined(__MSDOS__))
#  include <sys/select.h>
#  include <sys/time.h>
#endif

#include "synthrln.h"
#include "config.h"
#include "dropfile.h"
#include "bbs_io.h"
#include "net_io.h"

/* -------------------------------------------------------------------------
 * Platform yield (CPU-friendly polling)
 * ------------------------------------------------------------------------- */
static void platform_yield(void)
{
#if defined(_WIN32)
    Sleep(1);
#elif defined(__linux__) || defined(__unix__)
    {
        struct timeval tv;
        tv.tv_sec  = 0;
        tv.tv_usec = 1000; /* 1 ms */
        select(0, NULL, NULL, NULL, &tv);
    }
#elif defined(__MSDOS__) || defined(__WATCOMC__)
    /* On DOS, yield via a short delay or HLT if idle */
    delay(1);
#else
    /* Generic fallback - busy loop with no yield */
    (void)0;
#endif
}

/* -------------------------------------------------------------------------
 * Telnet IAC filter state
 * RFC 854: 0xFF = IAC, followed by command byte and option byte.
 * We respond with DONT/WONT to refuse all negotiations.
 * ------------------------------------------------------------------------- */
#define IAC  0xFF
#define WILL 0xFB
#define WONT 0xFC
#define DO   0xFD
#define DONT 0xFE

typedef enum {
    TELNET_NORMAL = 0,
    TELNET_SAW_IAC,
    TELNET_SAW_CMD
} telnet_state_t;

static telnet_state_t g_telnet_state = TELNET_NORMAL;
static uint8_t        g_telnet_cmd   = 0;

/*
 * Filter incoming server bytes when in TELNET mode.
 * Consumes IAC sequences and sends DONT/WONT responses via net_write.
 * Returns the number of bytes remaining in buf after filtering.
 */
static int telnet_filter_server(uint8_t *buf, int len)
{
    uint8_t out[1024];
    int     out_len = 0;
    int     i;

    for (i = 0; i < len; i++) {
        uint8_t b = buf[i];

        switch (g_telnet_state) {
        case TELNET_NORMAL:
            if (b == IAC) {
                g_telnet_state = TELNET_SAW_IAC;
            } else {
                out[out_len++] = b;
            }
            break;

        case TELNET_SAW_IAC:
            if (b == IAC) {
                /* Escaped 0xFF literal - pass through */
                out[out_len++] = IAC;
                g_telnet_state = TELNET_NORMAL;
            } else if (b == WILL || b == DO) {
                g_telnet_cmd   = b;
                g_telnet_state = TELNET_SAW_CMD;
            } else if (b == WONT || b == DONT) {
                g_telnet_cmd   = b;
                g_telnet_state = TELNET_SAW_CMD;
            } else {
                /* Single-byte command (GA, NOP, etc.) - consume */
                g_telnet_state = TELNET_NORMAL;
            }
            break;

        case TELNET_SAW_CMD: {
            /* Send refusal: WILL -> WONT, DO -> DONT */
            uint8_t resp[3];
            resp[0] = IAC;
            resp[1] = (g_telnet_cmd == WILL) ? WONT :
                      (g_telnet_cmd == DO)   ? DONT :
                      (g_telnet_cmd == WONT) ? WONT : DONT;
            resp[2] = b;
            net_write(resp, 3);
            g_telnet_state = TELNET_NORMAL;
            break;
        }
        }
    }

    if (out_len > 0) {
        memcpy(buf, out, out_len);
    }
    return out_len;
}

/* -------------------------------------------------------------------------
 * Rlogin handshake (RFC 1282)
 * Format: \0<client-user>\0<server-user>\0<term-type/speed>\0
 * ------------------------------------------------------------------------- */
static int send_rlogin_handshake(const app_config_t *cfg,
                                 const session_context_t *sess)
{
    uint8_t handshake[512];
    int     pos = 0;
    size_t  n;

    /* Leading NUL */
    handshake[pos++] = '\0';

    /* client-user: map to configured field */
    if (strcmp(cfg->client_user_map, "USERNAME") == 0) {
        n = strlen(sess->username);
        memcpy(handshake + pos, sess->username, n);
        pos += (int)n;
    } else if (strcmp(cfg->client_user_map, "REALNAME") == 0) {
        n = strlen(sess->real_name);
        memcpy(handshake + pos, sess->real_name, n);
        pos += (int)n;
    } else {
        /* Use literal value */
        n = strlen(cfg->client_user_map);
        memcpy(handshake + pos, cfg->client_user_map, n);
        pos += (int)n;
    }
    handshake[pos++] = '\0';

    /* server-user: map to configured field */
    if (strcmp(cfg->server_user_map, "DOORNAME") == 0) {
        n = strlen(sess->doorname);
        memcpy(handshake + pos, sess->doorname, n);
        pos += (int)n;
    } else if (strcmp(cfg->server_user_map, "USERNAME") == 0) {
        n = strlen(sess->username);
        memcpy(handshake + pos, sess->username, n);
        pos += (int)n;
    } else {
        n = strlen(cfg->server_user_map);
        memcpy(handshake + pos, cfg->server_user_map, n);
        pos += (int)n;
    }
    handshake[pos++] = '\0';

    /* terminal-type/speed */
    n = strlen(cfg->term_type);
    memcpy(handshake + pos, cfg->term_type, n);
    pos += (int)n;
    handshake[pos++] = '\0';

    return net_write(handshake, pos);
}

/* -------------------------------------------------------------------------
 * check_timeout: Returns 1 if the session time limit has been exceeded.
 * start_time is a Unix timestamp captured at session start.
 * time_left is in minutes (from dropfile).
 * ------------------------------------------------------------------------- */
static int check_timeout(time_t start_time, int time_left_minutes)
{
    time_t now     = time(NULL);
    double elapsed = difftime(now, start_time);
    double limit   = (double)(time_left_minutes) * 60.0;
    return (elapsed >= limit) ? 1 : 0;
}

/* -------------------------------------------------------------------------
 * print_usage
 * ------------------------------------------------------------------------- */
static void print_usage(const char *prog)
{
    fprintf(stderr,
        "SYNTHRLN - Multi-Platform BBS-to-TCP Bridge\n"
        "Usage: %s [options]\n\n"
        "Options (/ or -- prefix accepted):\n"
        "  cfgfile <path>    Path to alternate config file\n"
        "  server  <addr>    Override server IP/hostname\n"
        "  port    <num>     Override server port\n"
        "  proto   <proto>   Override protocol (rlogin|telnet)\n"
        "  clntuser <name>   Override rlogin client username\n"
        "  srvruser <name>   Override rlogin server username\n"
        "  termtype <type>   Override rlogin terminal type\n"
        "  local             Local diagnostic mode (no FOSSIL/sockets for BBS I/O)\n\n"
        "Example: %s --local --server 10.0.0.5 --port 23 --proto telnet\n",
        prog, prog);
}

/* -------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */
int main(int argc, char *argv[])
{
    app_config_t     cfg;
    session_context_t sess;
    time_t           session_start;
    int              session_active = 1;
    uint8_t          buffer[1024];
    int              bytes;
    int              i;

    /* ------------------------------------------------------------------
     * Phase 1: Configuration & Parsing
     * ------------------------------------------------------------------ */

    /* Show help if requested */
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        while (*a == '-' || *a == '/') a++;
        if (strcmp(a, "help") == 0 || strcmp(a, "h") == 0 || strcmp(a, "?") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }

    /* Load default config then apply CLI overrides */
    if (config_load(&cfg, argc, argv) != 0) {
        fprintf(stderr, "SYNTHRLN: Fatal - could not load configuration.\n");
        return 1;
    }

    /* Parse dropfile (skip if local mode and file missing) */
    memset(&sess, 0, sizeof(sess));
    if (!cfg.local_mode || dropfile_exists(&cfg)) {
        if (dropfile_parse(&cfg, &sess) != 0) {
            if (!cfg.local_mode) {
                fprintf(stderr, "SYNTHRLN: Fatal - could not parse dropfile: %s\n",
                        cfg.dropfile_path);
                return 1;
            }
            /* In local mode, fill dummy session data */
            strncpy(sess.username,  "SYSOP",    sizeof(sess.username)  - 1);
            strncpy(sess.real_name, "Sysop",    sizeof(sess.real_name) - 1);
            strncpy(sess.doorname,  "LOCALTEST",sizeof(sess.doorname)  - 1);
            sess.time_left    = 60;
            sess.ansi_enabled = 1;
        }
    } else {
        /* Local mode with no dropfile - fill dummy */
        strncpy(sess.username,  "SYSOP",    sizeof(sess.username)  - 1);
        strncpy(sess.real_name, "Sysop",    sizeof(sess.real_name) - 1);
        strncpy(sess.doorname,  "LOCALTEST",sizeof(sess.doorname)  - 1);
        sess.time_left    = 60;
        sess.ansi_enabled = 1;
    }

    /* ------------------------------------------------------------------
     * Phase 2: Transport Initialization
     * ------------------------------------------------------------------ */

    if (bbs_init(&cfg, &sess) != 0) {
        fprintf(stderr, "SYNTHRLN: Fatal - could not initialize BBS I/O transport.\n");
        return 1;
    }

    if (net_connect(cfg.server_ip, cfg.server_port) != 0) {
        fprintf(stderr, "SYNTHRLN: Fatal - could not connect to %s:%d\n",
                cfg.server_ip, cfg.server_port);
        bbs_close();
        return 1;
    }

    /* ------------------------------------------------------------------
     * Phase 3: Protocol Negotiation
     * ------------------------------------------------------------------ */

    if (cfg.protocol == PROTOCOL_RLOGIN) {
        if (send_rlogin_handshake(&cfg, &sess) < 0) {
            fprintf(stderr, "SYNTHRLN: Warning - rlogin handshake send failed.\n");
        }
    }
    /* TELNET: no handshake generated; IAC filter handles negotiation reactively */

    /* ------------------------------------------------------------------
     * Phase 4: Dual-Ended Shuttle Loop
     * ------------------------------------------------------------------ */

    session_start = time(NULL);

    while (session_active) {

        /* --- BBS/Local User -> Remote Server --- */
        if (bbs_has_data()) {
            bytes = bbs_read(buffer, sizeof(buffer));
            if (bytes > 0) {
                net_write(buffer, bytes);
            } else if (bytes < 0) {
                /* BBS side disconnected */
                session_active = 0;
                break;
            }
        }

        /* --- Remote Server -> BBS/Local User --- */
        if (net_has_data()) {
            bytes = net_read(buffer, sizeof(buffer));
            if (bytes > 0) {
                /* Apply telnet IAC filter if in telnet mode */
                if (cfg.protocol == PROTOCOL_TELNET) {
                    bytes = telnet_filter_server(buffer, bytes);
                }
                if (bytes > 0) {
                    bbs_write(buffer, bytes);
                }
            } else if (bytes < 0) {
                /* Server disconnected */
                session_active = 0;
                break;
            }
        }

        /* --- Status & Timeout Checks --- */
        if (cfg.respect_time && sess.time_left > 0) {
            if (check_timeout(session_start, sess.time_left)) {
                /* Optionally notify user */
                const char *msg = "\r\n[SYNTHRLN: Time limit reached. Disconnecting.]\r\n";
                bbs_write((const uint8_t *)msg, (int)strlen(msg));
                session_active = 0;
                break;
            }
        }

        if (!net_is_connected() || !bbs_is_connected()) {
            session_active = 0;
            break;
        }

        /* --- CPU Yield --- */
        platform_yield();
    }

    /* ------------------------------------------------------------------
     * Phase 5: Graceful Shutdown
     * ------------------------------------------------------------------ */

    net_close();
    bbs_flush();
    bbs_close();

    return 0;
}
