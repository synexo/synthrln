/*
 * SYNTHRLN - Multi-Platform BBS-to-TCP Bridge
 * net_mtcp_wrap.cpp - C-callable wrapper around the mTCP C++ library
 *
 * This file must be compiled with a C++ compiler (OpenWatcom wpp386).
 * It provides extern "C" entry points called by net_mtcp.c.
 *
 * mTCP requires:
 *   - MTCPCFG environment variable pointing to mTCP config file
 *   - Packet driver loaded and running
 */

#if defined(__MSDOS__) || defined(__WATCOMC__)

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

/* mTCP headers - adjust path if your layout differs */
#include "tcpinc.h"
#include "utils.h"
#include "dns.h"
#include "tcp.h"

extern "C" {

/* -------------------------------------------------------------------------
 * Module state
 * ------------------------------------------------------------------------- */
static TcpSocket *g_sock  = NULL;
static int        g_inited = 0;

/* -------------------------------------------------------------------------
 * mtcp_init
 * Initialise the mTCP network stack. Reads MTCPCFG env var internally.
 * Returns 0 on success, -1 on failure.
 * ------------------------------------------------------------------------- */
int mtcp_init(void)
{
    if (g_inited) return 0;

    /* Utils::initStack(recv_bufs, xmit_bufs) */
    if (Utils::initStack(5, 5) != 0) {
        return -1;
    }

    g_inited = 1;
    return 0;
}

/* -------------------------------------------------------------------------
 * mtcp_shutdown
 * Tear down the mTCP stack and release packet driver.
 * ------------------------------------------------------------------------- */
void mtcp_shutdown(void)
{
    if (!g_inited) return;
    Utils::endStack();
    g_inited = 0;
}

/* -------------------------------------------------------------------------
 * mtcp_connect
 * Resolve hostname and connect. Blocks until connected or timeout.
 * Returns 0 on success, -1 on failure.
 * ------------------------------------------------------------------------- */
int mtcp_connect(const char *host, int port)
{
    IpAddr_t addr;

    if (!g_inited) return -1;

    /* Resolve hostname to IP (synchronous, with packet driver polling) */
    if (Dns::resolve(host, addr, 1) != 0) {
        /* Try treating host as a dotted-decimal IP */
        if (IpUtils::parseIp(host, addr) != 0) {
            return -1;
        }
    }

    g_sock = new TcpSocket();
    if (!g_sock) return -1;

    /* connect(addr, port, timeout_ms) */
    int rc = g_sock->connect(addr, (uint16_t)port, 10000);
    if (rc != 0) {
        delete g_sock;
        g_sock = NULL;
        return -1;
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * mtcp_has_data
 * Returns 1 if bytes are waiting in the receive buffer.
 * ------------------------------------------------------------------------- */
int mtcp_has_data(void)
{
    if (!g_sock) return 0;
    return (g_sock->bytesWaiting() > 0) ? 1 : 0;
}

/* -------------------------------------------------------------------------
 * mtcp_recv
 * Non-blocking receive. Returns bytes read, 0 if none, -1 on error.
 * ------------------------------------------------------------------------- */
int mtcp_recv(unsigned char *buf, int max_len)
{
    int n;
    if (!g_sock) return -1;
    n = g_sock->recv((uint8_t *)buf, (uint16_t)max_len);
    return (n < 0) ? 0 : n;
}

/* -------------------------------------------------------------------------
 * mtcp_send
 * Blocking send. Returns bytes sent, -1 on error.
 * ------------------------------------------------------------------------- */
int mtcp_send(const unsigned char *buf, int len)
{
    if (!g_sock) return -1;
    return g_sock->send((uint8_t *)buf, (uint16_t)len);
}

/* -------------------------------------------------------------------------
 * mtcp_is_connected
 * Returns 1 if the TCP connection is still established.
 * ------------------------------------------------------------------------- */
int mtcp_is_connected(void)
{
    if (!g_sock) return 0;
    return g_sock->isConnected() ? 1 : 0;
}

/* -------------------------------------------------------------------------
 * mtcp_close
 * Close the TCP connection and free the socket object.
 * ------------------------------------------------------------------------- */
void mtcp_close(void)
{
    if (!g_sock) return;
    g_sock->close();
    delete g_sock;
    g_sock = NULL;
}

/* -------------------------------------------------------------------------
 * mtcp_process
 * Drive mTCP background I/O - must be called regularly in the main loop.
 * This pumps the packet driver and processes incoming/outgoing packets.
 * ------------------------------------------------------------------------- */
void mtcp_process(void)
{
    if (!g_inited) return;
    Utils::idle();
}

} /* extern "C" */

#endif /* __MSDOS__ || __WATCOMC__ */
