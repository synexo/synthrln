/*
 * SYNTHRLN - Multi-Platform BBS-to-TCP Bridge
 * bbs_io.h - Abstract BBS-facing I/O interface
 *
 * Implementations:
 *   bbs_fossil.c  - DOS FOSSIL driver (INT 14h)
 *   bbs_posix.c   - Windows (Winsock handle) / Linux (stdin/stdout pipes)
 *   bbs_local.c   - Local diagnostic mode (conio / termios)
 *
 * The correct implementation is selected at link time by the build system.
 * main.c calls only these abstract functions.
 */

#ifndef BBS_IO_H
#define BBS_IO_H

#include <stdint.h>
#include "synthrln.h"

/*
 * bbs_init()
 * Initialise the BBS-facing I/O transport.
 * Returns 0 on success, non-zero on failure.
 */
int bbs_init(const app_config_t *cfg, const session_context_t *sess);

/*
 * bbs_has_data()
 * Non-blocking check. Returns 1 if data is available to read, 0 if not.
 */
int bbs_has_data(void);

/*
 * bbs_read()
 * Non-blocking read. Returns number of bytes read, 0 for no data, <0 on error/disconnect.
 */
int bbs_read(uint8_t *buf, int max_len);

/*
 * bbs_write()
 * Write bytes to the BBS user. Returns bytes written, <0 on error.
 */
int bbs_write(const uint8_t *buf, int len);

/*
 * bbs_flush()
 * Flush any pending output buffers to the BBS user.
 */
void bbs_flush(void);

/*
 * bbs_is_connected()
 * Returns 1 if the BBS-side connection is still alive, 0 otherwise.
 */
int bbs_is_connected(void);

/*
 * bbs_close()
 * Shut down the BBS-facing transport and free resources.
 */
void bbs_close(void);

#endif /* BBS_IO_H */
