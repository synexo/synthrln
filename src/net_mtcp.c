/*
 * SYNTHRLN - Multi-Platform BBS-to-TCP Bridge
 * net_mtcp.c - Outbound TCP for MS-DOS using the mTCP library
 *
 * mTCP is an open-source TCP/IP stack for DOS that works via a packet driver.
 * It provides BSD-socket-like APIs: TcpSocket, connect(), send(), recv(), etc.
 *
 * mTCP library headers expected at: mtcp/include/
 * mTCP library binary expected at:  mtcp/lib/mtcplib.lib
 *
 * Environment variable MTCPCFG must point to the mTCP config file
 * (e.g., set MTCPCFG=C:\MTCP\MTCP.CFG) before running SYNTHRLN.
 *
 * Key mTCP types/functions used:
 *   TcpSocket          - TCP socket object
 *   Utils::initStack() - Initialise the network stack
 *   Utils::endStack()  - Tear down the network stack
 *   TcpSocket::connect()
 *   TcpSocket::send()
 *   TcpSocket::recv()
 *   TcpSocket::isConnected()
 *   TcpSocket::close()
 *   TcpSocket::bytesWaiting()
 */

#if defined(__MSDOS__) || defined(__WATCOMC__)

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/*
 * mTCP is a C++ library; these headers may require a C++ wrapper.
 * If building with OpenWatcom in C++ mode, include directly.
 * If building in C mode, a thin C wrapper (mtcp_wrap.cpp) is needed.
 * We declare the minimal extern "C" interface here.
 */
#ifdef __cplusplus
#  include "mtcp/include/tcpinc.h"
#  include "mtcp/include/utils.h"
#else
/* C-callable wrapper stubs (implemented in net_mtcp_wrap.cpp) */
extern int  mtcp_init(void);
extern void mtcp_shutdown(void);
extern int  mtcp_connect(const char *host, int port);
extern int  mtcp_has_data(void);
extern int  mtcp_recv(unsigned char *buf, int max_len);
extern int  mtcp_send(const unsigned char *buf, int len);
extern int  mtcp_is_connected(void);
extern void mtcp_close(void);
extern void mtcp_process(void);  /* Drive the packet driver / background I/O */
#endif

#include "net_io.h"

static int g_mtcp_active = 0;

/* -------------------------------------------------------------------------
 * net_connect
 * ------------------------------------------------------------------------- */
int net_connect(const char *host, int port)
{
    if (mtcp_init() != 0) {
        fprintf(stderr, "SYNTHRLN: mTCP stack init failed.\n");
        fprintf(stderr, "          Is MTCPCFG set and packet driver loaded?\n");
        return -1;
    }

    if (mtcp_connect(host, port) != 0) {
        fprintf(stderr, "SYNTHRLN: mTCP connect to %s:%d failed.\n", host, port);
        mtcp_shutdown();
        return -1;
    }

    g_mtcp_active = 1;
    fprintf(stderr, "SYNTHRLN: mTCP connected to %s:%d\n", host, port);
    return 0;
}

/* -------------------------------------------------------------------------
 * net_has_data
 * ------------------------------------------------------------------------- */
int net_has_data(void)
{
    if (!g_mtcp_active) return 0;
    mtcp_process(); /* Service the packet driver */
    return mtcp_has_data();
}

/* -------------------------------------------------------------------------
 * net_read
 * ------------------------------------------------------------------------- */
int net_read(uint8_t *buf, int max_len)
{
    if (!g_mtcp_active) return -1;
    mtcp_process();
    return mtcp_recv(buf, max_len);
}

/* -------------------------------------------------------------------------
 * net_write
 * ------------------------------------------------------------------------- */
int net_write(const uint8_t *buf, int len)
{
    int sent;
    if (!g_mtcp_active) return -1;
    mtcp_process();
    sent = mtcp_send(buf, len);
    mtcp_process();
    return sent;
}

/* -------------------------------------------------------------------------
 * net_is_connected
 * ------------------------------------------------------------------------- */
int net_is_connected(void)
{
    if (!g_mtcp_active) return 0;
    mtcp_process();
    return mtcp_is_connected();
}

/* -------------------------------------------------------------------------
 * net_close
 * ------------------------------------------------------------------------- */
void net_close(void)
{
    if (!g_mtcp_active) return;
    mtcp_close();
    mtcp_shutdown();
    g_mtcp_active = 0;
}

#endif /* __MSDOS__ || __WATCOMC__ */
