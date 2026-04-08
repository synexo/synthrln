/*
 * SYNTHRLN - Multi-Platform BBS-to-TCP Bridge
 * bbs_fossil.c - DOS BBS I/O via FOSSIL driver (INT 14h)
 *
 * FOSSIL (Fido/Opus/SEAdog Standard Interface Layer) provides a standard
 * serial I/O interface via INT 14h, used by DOS BBS software to abstract
 * the COM port hardware.
 *
 * INT 14h function codes used here:
 *   AH=00h  - Initialise serial port
 *   AH=01h  - Send character (blocking)
 *   AH=02h  - Receive character (blocking, but we poll first)
 *   AH=03h  - Get status
 *   AH=04h  - Extended initialise (FOSSIL 5.0+)
 *
 * The COM port number (0-based: 0=COM1, 1=COM2) comes from DORINFO1.DEF.
 *
 * Only compiled for DOS targets.
 */

#if defined(__MSDOS__) || defined(__WATCOMC__)

#include <stdio.h>
#include <string.h>
#include <dos.h>     /* For int86() / MK_FP() */
#include <i86.h>     /* OpenWatcom: union REGS / struct SREGS */

#include "synthrln.h"
#include "bbs_io.h"

/* FOSSIL initialisation magic (AH=04h) */
#define FOSSIL_INIT_MAGIC  0x1954

/* Baud rate codes for AH=00h (bits 7-5 of AL) */
/* We'll pass 0x83 = 38400 8N1 or just use whatever the BBS configured */
#define FOSSIL_BAUD_38400  0x83
#define FOSSIL_BAUD_19200  0x63
#define FOSSIL_BAUD_9600   0x43

/* Status register bits (from AH=03h) */
#define FOSSIL_TX_EMPTY    (1 << 13) /* Transmit holding register empty */
#define FOSSIL_RX_READY    (1 <<  8) /* Data ready in receive buffer    */

static int  g_fossil_port   = 0;  /* 0 = COM1, 1 = COM2, etc.   */
static int  g_fossil_active = 0;

/* -------------------------------------------------------------------------
 * Translate BPS rate to FOSSIL baud init byte (bits 7-5 of AL)
 * Default to 9600 if unrecognised.
 * ------------------------------------------------------------------------- */
static unsigned char bps_to_fossil_byte(int bps)
{
    switch (bps) {
    case 115200: return 0xE3;
    case  57600: return 0xC3;
    case  38400: return 0x83; /* Note: Non-standard on some FOSSIL impls */
    case  19200: return 0x63;
    case   9600: return 0x43;
    case   4800: return 0x23;
    case   2400: return 0x03;
    default:     return 0x43; /* 9600 8N1 */
    }
}

/* -------------------------------------------------------------------------
 * Get FOSSIL port status via INT 14h AH=03h
 * Returns the DX-format status word.
 * ------------------------------------------------------------------------- */
static unsigned int fossil_status(int port)
{
    union REGS r;
    r.h.ah = 0x03;
    r.x.dx = (unsigned short)port;
    int86(0x14, &r, &r);
    return r.x.ax;
}

/* -------------------------------------------------------------------------
 * bbs_init
 * ------------------------------------------------------------------------- */
int bbs_init(const app_config_t *cfg, const session_context_t *sess)
{
    union REGS r;
    unsigned int status;

    if (cfg->local_mode) {
        /* Local mode on DOS: just use conio, handled by bbs_local.c */
        g_fossil_active = 0;
        return 0;
    }

    /* COM port from DORINFO1: comm_port is 1-based (COM1=1), INT 14h is 0-based */
    g_fossil_port = (sess->comm_port > 0) ? (sess->comm_port - 1) : 0;

    /* Attempt FOSSIL extended init (AH=04h) */
    r.h.ah = 0x04;
    r.x.dx = (unsigned short)g_fossil_port;
    r.x.bx = FOSSIL_INIT_MAGIC;
    int86(0x14, &r, &r);

    if (r.x.ax != FOSSIL_INIT_MAGIC) {
        /* Extended init not supported; fall back to standard init (AH=00h) */
        unsigned char baud_byte = bps_to_fossil_byte(sess->bps_rate);
        r.h.ah = 0x00;
        r.h.al = baud_byte;
        r.x.dx = (unsigned short)g_fossil_port;
        int86(0x14, &r, &r);
    }

    /* Verify status */
    status = fossil_status(g_fossil_port);
    (void)status; /* Status check is informational; proceed regardless */

    g_fossil_active = 1;
    fprintf(stderr, "SYNTHRLN: FOSSIL init on COM%d OK.\n", g_fossil_port + 1);
    return 0;
}

/* -------------------------------------------------------------------------
 * bbs_has_data
 * Check bit 8 of status word (Receive Data Ready)
 * ------------------------------------------------------------------------- */
int bbs_has_data(void)
{
    if (!g_fossil_active) return 0;
    return (fossil_status(g_fossil_port) & FOSSIL_RX_READY) ? 1 : 0;
}

/* -------------------------------------------------------------------------
 * bbs_read
 * Non-blocking: check status before reading.
 * FOSSIL AH=02h blocks; we guard with has_data() first.
 * ------------------------------------------------------------------------- */
int bbs_read(uint8_t *buf, int max_len)
{
    int count = 0;
    union REGS r;

    if (!g_fossil_active) return -1;

    while (count < max_len && bbs_has_data()) {
        r.h.ah = 0x02;
        r.x.dx = (unsigned short)g_fossil_port;
        int86(0x14, &r, &r);
        if (r.h.ah & 0x80) {
            /* Error condition or carrier detect lost */
            g_fossil_active = 0;
            return (count > 0) ? count : -1;
        }
        buf[count++] = r.h.al;
    }

    return count;
}

/* -------------------------------------------------------------------------
 * bbs_write
 * FOSSIL AH=01h - Send character (blocks until TX holding register empty)
 * ------------------------------------------------------------------------- */
int bbs_write(const uint8_t *buf, int len)
{
    int i;
    union REGS r;

    if (!g_fossil_active) return -1;

    for (i = 0; i < len; i++) {
        r.h.ah = 0x01;
        r.h.al = buf[i];
        r.x.dx = (unsigned short)g_fossil_port;
        int86(0x14, &r, &r);
        if (r.h.ah & 0x80) {
            /* Carrier detect lost mid-write */
            g_fossil_active = 0;
            return i;
        }
    }
    return len;
}

/* -------------------------------------------------------------------------
 * bbs_flush
 * Wait for transmit buffer to drain (poll TX empty status)
 * ------------------------------------------------------------------------- */
void bbs_flush(void)
{
    int timeout = 5000;
    if (!g_fossil_active) return;
    while (timeout-- > 0) {
        if (fossil_status(g_fossil_port) & FOSSIL_TX_EMPTY) break;
        delay(1);
    }
}

/* -------------------------------------------------------------------------
 * bbs_is_connected
 * Check carrier detect via FOSSIL status
 * ------------------------------------------------------------------------- */
int bbs_is_connected(void)
{
    if (!g_fossil_active) return 0;
    /* Bit 7 of low byte = carrier detect on most FOSSIL implementations */
    return ((fossil_status(g_fossil_port) & 0x80) != 0) ? 1 : g_fossil_active;
}

/* -------------------------------------------------------------------------
 * bbs_close
 * Deinit FOSSIL (AH=05h)
 * ------------------------------------------------------------------------- */
void bbs_close(void)
{
    union REGS r;
    if (!g_fossil_active) return;

    r.h.ah = 0x05;
    r.x.dx = (unsigned short)g_fossil_port;
    int86(0x14, &r, &r);

    g_fossil_active = 0;
}

#endif /* __MSDOS__ || __WATCOMC__ */
