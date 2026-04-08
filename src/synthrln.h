/*
 * SYNTHRLN - Multi-Platform BBS-to-TCP Bridge
 * synthrln.h - Shared constants, enums, and type definitions
 */

#ifndef SYNTHRLN_H
#define SYNTHRLN_H

#include <stdint.h>

/* -------------------------------------------------------------------------
 * Version
 * ------------------------------------------------------------------------- */
#define SYNTHRLN_VERSION_MAJOR 1
#define SYNTHRLN_VERSION_MINOR 0
#define SYNTHRLN_VERSION_PATCH 0
#define SYNTHRLN_VERSION_STR   "1.0.0"

/* -------------------------------------------------------------------------
 * Protocol identifiers
 * ------------------------------------------------------------------------- */
#define PROTOCOL_RLOGIN  0
#define PROTOCOL_TELNET  1

/* -------------------------------------------------------------------------
 * Dropfile type identifiers
 * ------------------------------------------------------------------------- */
#define DROPFILE_DOOR32   0
#define DROPFILE_DORINFO1 1

/* -------------------------------------------------------------------------
 * Boolean helpers
 * ------------------------------------------------------------------------- */
#ifndef TRUE
#  define TRUE  1
#endif
#ifndef FALSE
#  define FALSE 0
#endif

/* -------------------------------------------------------------------------
 * Application Configuration Context
 * Populated by config_load(); CLI args override file values.
 * ------------------------------------------------------------------------- */
typedef struct {
    char cfg_file[256];         /* Path to config file                    */
    char dropfile_path[256];    /* Absolute/relative dropfile path        */
    int  dropfile_type;         /* DROPFILE_DOOR32 or DROPFILE_DORINFO1   */
    int  protocol;              /* PROTOCOL_RLOGIN or PROTOCOL_TELNET     */
    char server_ip[256];        /* Target server IP or hostname           */
    int  server_port;           /* Target TCP port                        */
    char client_user_map[256];  /* Dropfile field -> rlogin client-user   */
    char server_user_map[256];  /* Dropfile field -> rlogin server-user   */
    char term_type[256];        /* Terminal type string for rlogin        */
    int  respect_time;          /* Boolean: honour dropfile time limit    */
    int  local_mode;            /* Boolean: --local diagnostic mode       */
} app_config_t;

/* -------------------------------------------------------------------------
 * Unified Session Context
 * Populated by dropfile_parse(); drives rlogin handshake and timeouts.
 * ------------------------------------------------------------------------- */
typedef struct {
    char username[36];       /* User's handle/alias                       */
    char real_name[36];      /* User's real name                          */
    char doorname[36];       /* Target door/system name                   */
    int  time_left;          /* Remaining session time in minutes         */
    int  ansi_enabled;       /* 1 = ANSI graphics, 0 = ASCII only         */
    int  comm_port;          /* DOS/FOSSIL COM port (DORINFO1.DEF)        */
    long socket_handle;      /* Windows socket handle (DOOR32.SYS)       */
    int  bps_rate;           /* Connection BPS rate                       */
} session_context_t;

#endif /* SYNTHRLN_H */
