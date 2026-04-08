/*
 * SYNTHRLN - Multi-Platform BBS-to-TCP Bridge
 * dropfile.c - Dropfile parsers for DOOR32.SYS and DORINFO1.DEF
 *
 * DOOR32.SYS format (line-oriented):
 *   Line  1: Comm type (0=Local, 1=Serial, 2=Telnet/Socket)
 *   Line  2: Comm or socket handle
 *   Line  3: BPS rate
 *   Line  4: BBS software name
 *   Line  5: User record number
 *   Line  6: User's real name
 *   Line  7: User's handle/alias
 *   Line  8: Security level
 *   Line  9: Time left (minutes)
 *   Line 10: ANSI (1=yes, 0=no)
 *   Line 11: Screen columns
 *   Line 12: Page length
 *   (Lines 13+ are optional extensions)
 *
 * DORINFO1.DEF format (line-oriented):
 *   Line 1:  BBS Name
 *   Line 2:  Sysop first name
 *   Line 3:  Sysop last name
 *   Line 4:  COM port (COM1, COM2, ...) or "0" for local
 *   Line 5:  BPS rate (numeric, with leading modifier)
 *   Line 6:  Network type (0 = generic)
 *   Line 7:  User first name
 *   Line 8:  User last name
 *   Line 9:  User location
 *   Line 10: ANSI (1=yes, 0=no)
 *   Line 11: Security level
 *   Line 12: Time left (minutes)
 *   Line 13: Door registration string
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "synthrln.h"
#include "dropfile.h"

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static char *read_line(FILE *fp, char *buf, int sz)
{
    char *p;
    if (!fgets(buf, sz, fp)) return NULL;
    /* Strip trailing CR/LF */
    p = buf + strlen(buf) - 1;
    while (p >= buf && (*p == '\r' || *p == '\n')) { *p = '\0'; p--; }
    return buf;
}

static char *trim(char *s)
{
    char *end;
    while (isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';
    return s;
}

/* -------------------------------------------------------------------------
 * DOOR32.SYS parser
 * ------------------------------------------------------------------------- */
static int parse_door32(const char *path, session_context_t *sess)
{
    FILE *fp;
    char  line[256];
    int   lineno = 0;

    fp = fopen(path, "r");
    if (!fp) return -1;

    while (read_line(fp, line, sizeof(line)) != NULL) {
        char *p = trim(line);
        lineno++;

        switch (lineno) {
        case 1: /* Comm type - informational, not used directly */
            break;
        case 2: /* Comm/socket handle */
            sess->socket_handle = atol(p);
            break;
        case 3: /* BPS rate */
            sess->bps_rate = atoi(p);
            break;
        case 4: /* BBS software name - skip */
            break;
        case 5: /* User record number - skip */
            break;
        case 6: /* Real name */
            strncpy(sess->real_name, p, sizeof(sess->real_name) - 1);
            break;
        case 7: /* Handle/alias */
            strncpy(sess->username, p, sizeof(sess->username) - 1);
            /* Use alias as doorname default; can be overridden by cfg mapping */
            strncpy(sess->doorname, p, sizeof(sess->doorname) - 1);
            break;
        case 8: /* Security level - skip */
            break;
        case 9: /* Time left */
            sess->time_left = atoi(p);
            break;
        case 10: /* ANSI */
            sess->ansi_enabled = (atoi(p) == 1) ? 1 : 0;
            break;
        /* Lines 11+ optional; ignore extra lines gracefully */
        default:
            break;
        }
    }

    fclose(fp);

    if (lineno < 9) {
        fprintf(stderr, "SYNTHRLN: DOOR32.SYS appears truncated (%d lines read).\n", lineno);
        return -1;
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * DORINFO1.DEF parser
 * ------------------------------------------------------------------------- */
static int parse_dorinfo1(const char *path, session_context_t *sess)
{
    FILE *fp;
    char  line[256];
    int   lineno = 0;
    char  first_name[64] = {0};
    char  last_name[64]  = {0};

    fp = fopen(path, "r");
    if (!fp) return -1;

    while (read_line(fp, line, sizeof(line)) != NULL) {
        char *p = trim(line);
        lineno++;

        switch (lineno) {
        case 1: /* BBS name - use as doorname */
            strncpy(sess->doorname, p, sizeof(sess->doorname) - 1);
            break;
        case 2: /* Sysop first name - skip */
            break;
        case 3: /* Sysop last name - skip */
            break;
        case 4: /* COM port: "COM1", "COM2", or "0" for local */
            if (p[0] == '0') {
                sess->comm_port = 0;
            } else {
                /* COM1 -> 1, COM2 -> 2, etc. */
                const char *num = p;
                while (*num && !isdigit((unsigned char)*num)) num++;
                sess->comm_port = (*num) ? atoi(num) : 0;
            }
            break;
        case 5: /* BPS rate - may have leading "-1 " marker; parse last token */
            {
                const char *tok = p;
                const char *last_space = NULL;
                const char *t;
                for (t = p; *t; t++) {
                    if (*t == ' ') last_space = t;
                }
                if (last_space) tok = last_space + 1;
                sess->bps_rate = atoi(tok);
            }
            break;
        case 6: /* Network type - skip */
            break;
        case 7: /* User first name */
            strncpy(first_name, p, sizeof(first_name) - 1);
            break;
        case 8: /* User last name */
            strncpy(last_name, p, sizeof(last_name) - 1);
            /* Compose full name and use as both real_name and username */
            {
                char full[72];
                snprintf(full, sizeof(full), "%s %s", first_name, last_name);
                snprintf(sess->real_name, sizeof(sess->real_name), "%.*s", (int)(sizeof(sess->real_name) - 1), full);
                snprintf(sess->username,  sizeof(sess->username),  "%.*s", (int)(sizeof(sess->username)  - 1), full);
            }
            break;
        case 9: /* User location - skip */
            break;
        case 10: /* ANSI */
            sess->ansi_enabled = (atoi(p) == 1) ? 1 : 0;
            break;
        case 11: /* Security level - skip */
            break;
        case 12: /* Time left */
            sess->time_left = atoi(p);
            break;
        default:
            break;
        }
    }

    fclose(fp);

    if (lineno < 12) {
        fprintf(stderr, "SYNTHRLN: DORINFO1.DEF appears truncated (%d lines read).\n", lineno);
        /* Non-fatal; we may still have enough data */
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

int dropfile_exists(const app_config_t *cfg)
{
    FILE *fp = fopen(cfg->dropfile_path, "r");
    if (!fp) return 0;
    fclose(fp);
    return 1;
}

int dropfile_parse(const app_config_t *cfg, session_context_t *sess)
{
    memset(sess, 0, sizeof(*sess));

    if (cfg->dropfile_type == DROPFILE_DOOR32) {
        return parse_door32(cfg->dropfile_path, sess);
    } else if (cfg->dropfile_type == DROPFILE_DORINFO1) {
        return parse_dorinfo1(cfg->dropfile_path, sess);
    }

    fprintf(stderr, "SYNTHRLN: Unknown dropfile type %d.\n", cfg->dropfile_type);
    return -1;
}
