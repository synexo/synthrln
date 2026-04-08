/*
 * SYNTHRLN - Multi-Platform BBS-to-TCP Bridge
 * net_io.h - Abstract outbound network (game server) I/O interface
 *
 * Implementations:
 *   net_posix.c - Windows (Winsock) and Linux (BSD sockets)
 *   net_mtcp.c  - DOS (mTCP library via packet driver)
 */

#ifndef NET_IO_H
#define NET_IO_H

#include <stdint.h>

/*
 * net_connect()
 * Establish a TCP connection to the given host and port.
 * Returns 0 on success, non-zero on failure.
 */
int net_connect(const char *host, int port);

/*
 * net_has_data()
 * Non-blocking check. Returns 1 if data is available from the server.
 */
int net_has_data(void);

/*
 * net_read()
 * Non-blocking read from the server.
 * Returns bytes read, 0 for no data, <0 on error/disconnect.
 */
int net_read(uint8_t *buf, int max_len);

/*
 * net_write()
 * Send bytes to the server.
 * Returns bytes written, <0 on error.
 */
int net_write(const uint8_t *buf, int len);

/*
 * net_is_connected()
 * Returns 1 if the TCP connection to the server is alive.
 */
int net_is_connected(void);

/*
 * net_close()
 * Gracefully close the TCP connection and free resources.
 */
void net_close(void);

#endif /* NET_IO_H */
