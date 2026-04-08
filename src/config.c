/*
 * SYNTHRLN - Multi-Platform BBS-to-TCP Bridge
 * config.c - Configuration file parser and CLI override handler
 *
 * Config file format: Key = Value  (case-insensitive keys, # comments)
 * CLI args: /key value  or  --key value  (8-char max key names)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "synthrln.h"
#include "config.h"

/* -------------------------------------------------------------------------
 * Internal helpers
 * ------------------------------------------------------------------------- */

/* Trim leading/trailing whitespace in-place; returns pointer to result */
static char *str_trim(char *s)
{
    char *end;
    while (isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';
    return s;
}

/* Case-insensitive strcmp */
static int str_icmp(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) break;
        a++; b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

/* Apply a single Key=Value pair to the config struct */
static void apply_kv(app_config_t *cfg, const char *key, const char *val)
{
    if (str_icmp(key, "DropfileType") == 0) {
        if (str_icmp(val, "DOOR32") == 0)
            cfg->dropfile_type = DROPFILE_DOOR32;
        else if (str_icmp(val, "DORINFO1") == 0)
            cfg->dropfile_type = DROPFILE_DORINFO1;
    }
    else if (str_icmp(key, "DropfilePath") == 0) {
        snprintf(cfg->dropfile_path, sizeof(cfg->dropfile_path), "%s", val);
    }
    else if (str_icmp(key, "Protocol") == 0) {
        if (str_icmp(val, "RLOGIN") == 0)
            cfg->protocol = PROTOCOL_RLOGIN;
        else if (str_icmp(val, "TELNET") == 0)
            cfg->protocol = PROTOCOL_TELNET;
    }
    else if (str_icmp(key, "Server") == 0) {
        snprintf(cfg->server_ip, sizeof(cfg->server_ip), "%s", val);
    }
    else if (str_icmp(key, "Port") == 0) {
        cfg->server_port = atoi(val);
    }
    else if (str_icmp(key, "ClientUser") == 0) {
        snprintf(cfg->client_user_map, sizeof(cfg->client_user_map), "%s", val);
    }
    else if (str_icmp(key, "ServerUser") == 0) {
        snprintf(cfg->server_user_map, sizeof(cfg->server_user_map), "%s", val);
    }
    else if (str_icmp(key, "TermType") == 0) {
        snprintf(cfg->term_type, sizeof(cfg->term_type), "%s", val);
    }
    else if (str_icmp(key, "RespectTime") == 0) {
        cfg->respect_time = (str_icmp(val, "TRUE") == 0 || strcmp(val, "1") == 0) ? 1 : 0;
    }
}

/* -------------------------------------------------------------------------
 * Config file parser
 * ------------------------------------------------------------------------- */
static int parse_cfg_file(app_config_t *cfg, const char *path)
{
    FILE *fp;
    char  line[512];

    fp = fopen(path, "r");
    if (!fp) return -1;

    while (fgets(line, sizeof(line), fp)) {
        char *p  = str_trim(line);
        char *eq;
        char  key[128], val[256];

        /* Skip comments and blank lines */
        if (*p == '#' || *p == ';' || *p == '\0') continue;

        eq = strchr(p, '=');
        if (!eq) continue;

        *eq = '\0';
        strncpy(key, str_trim(p),      sizeof(key) - 1);
        strncpy(val, str_trim(eq + 1), sizeof(val) - 1);

        apply_kv(cfg, key, val);
    }

    fclose(fp);
    return 0;
}

/* -------------------------------------------------------------------------
 * CLI argument normaliser
 * Strips leading / or -- from an argument token and lowercases it.
 * Returns pointer into the original string (past any prefix characters).
 * ------------------------------------------------------------------------- */
static const char *normalise_arg(const char *arg)
{
    /* Strip -- */
    if (arg[0] == '-' && arg[1] == '-') return arg + 2;
    /* Strip single - */
    if (arg[0] == '-') return arg + 1;
    /* Strip / */
    if (arg[0] == '/') return arg + 1;
    return arg;
}

/* -------------------------------------------------------------------------
 * CLI override pass
 * ------------------------------------------------------------------------- */
static void apply_cli(app_config_t *cfg, int argc, char *argv[])
{
    int i;
    for (i = 1; i < argc; i++) {
        const char *key = normalise_arg(argv[i]);

        if (str_icmp(key, "local") == 0) {
            cfg->local_mode = 1;
        }
        else if (i + 1 < argc) {
            const char *val = argv[i + 1];

            if (str_icmp(key, "server") == 0) {
                snprintf(cfg->server_ip, sizeof(cfg->server_ip), "%s", val);
                i++;
            }
            else if (str_icmp(key, "port") == 0) {
                cfg->server_port = atoi(val);
                i++;
            }
            else if (str_icmp(key, "proto") == 0) {
                if (str_icmp(val, "rlogin") == 0)
                    cfg->protocol = PROTOCOL_RLOGIN;
                else if (str_icmp(val, "telnet") == 0)
                    cfg->protocol = PROTOCOL_TELNET;
                i++;
            }
            else if (str_icmp(key, "clntuser") == 0) {
                snprintf(cfg->client_user_map, sizeof(cfg->client_user_map), "%s", val);
                i++;
            }
            else if (str_icmp(key, "srvruser") == 0) {
                snprintf(cfg->server_user_map, sizeof(cfg->server_user_map), "%s", val);
                i++;
            }
            else if (str_icmp(key, "termtype") == 0) {
                snprintf(cfg->term_type, sizeof(cfg->term_type), "%s", val);
                i++;
            }
            else if (str_icmp(key, "cfgfile") == 0) {
                /* Already handled in config_load() before this pass */
                i++;
            }
        }
    }
}

/* -------------------------------------------------------------------------
 * Locate the config file path
 * 1. If /cfgfile was passed on CLI, use that.
 * 2. Otherwise, use DEFAULT_CFG_FILENAME in the current directory.
 * ------------------------------------------------------------------------- */
static void find_cfg_path(char *out, int out_sz, int argc, char *argv[])
{
    int i;
    for (i = 1; i < argc - 1; i++) {
        const char *key = normalise_arg(argv[i]);
        if (str_icmp(key, "cfgfile") == 0) {
            strncpy(out, argv[i + 1], out_sz - 1);
            return;
        }
    }
    strncpy(out, DEFAULT_CFG_FILENAME, out_sz - 1);
}

/* -------------------------------------------------------------------------
 * Set defaults before any file or CLI parsing
 * ------------------------------------------------------------------------- */
static void set_defaults(app_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));

    strncpy(cfg->cfg_file,         DEFAULT_CFG_FILENAME, sizeof(cfg->cfg_file) - 1);
    cfg->dropfile_type = DROPFILE_DOOR32;
    cfg->protocol      = PROTOCOL_RLOGIN;
    cfg->server_port   = 513;  /* Default rlogin port */
    strncpy(cfg->client_user_map,  "USERNAME", sizeof(cfg->client_user_map) - 1);
    strncpy(cfg->server_user_map,  "DOORNAME", sizeof(cfg->server_user_map) - 1);
    strncpy(cfg->term_type,        "ANSI",     sizeof(cfg->term_type) - 1);
    cfg->respect_time  = 1;
    cfg->local_mode    = 0;

    /* Platform-specific dropfile default */
#if defined(__MSDOS__) || defined(__WATCOMC__)
    cfg->dropfile_type = DROPFILE_DORINFO1;
    strncpy(cfg->dropfile_path, "DORINFO1.DEF", sizeof(cfg->dropfile_path) - 1);
    cfg->server_port   = 513;
#elif defined(_WIN32)
    cfg->dropfile_type = DROPFILE_DOOR32;
    strncpy(cfg->dropfile_path, "DOOR32.SYS",  sizeof(cfg->dropfile_path) - 1);
#else
    /* Linux / POSIX */
    cfg->dropfile_type = DROPFILE_DOOR32;
    strncpy(cfg->dropfile_path, "DOOR32.SYS",  sizeof(cfg->dropfile_path) - 1);
#endif
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */
int config_load(app_config_t *cfg, int argc, char *argv[])
{
    char cfg_path[256] = {0};
    int  file_ok;

    set_defaults(cfg);

    /* Determine which config file to read */
    find_cfg_path(cfg_path, sizeof(cfg_path), argc, argv);
    snprintf(cfg->cfg_file, sizeof(cfg->cfg_file), "%s", cfg_path);

    /* Parse config file (non-fatal if missing) */
    file_ok = (parse_cfg_file(cfg, cfg_path) == 0);

    /* Apply CLI overrides last */
    apply_cli(cfg, argc, argv);

    /* Validation: we need a server address to do anything */
    if (!cfg->local_mode && cfg->server_ip[0] == '\0') {
        if (!file_ok) {
            fprintf(stderr,
                "SYNTHRLN: No config file found (%s) and no --server specified.\n"
                "          Use --local for diagnostic mode, or create synthrln.cfg\n",
                cfg_path);
            return -1;
        }
        /* Config file existed but had no Server= line */
        fprintf(stderr, "SYNTHRLN: Warning - no Server= defined in config.\n");
    }

    return 0;
}

void config_dump(const app_config_t *cfg)
{
    fprintf(stderr, "--- SYNTHRLN Configuration ---\n");
    fprintf(stderr, "  Config file:    %s\n", cfg->cfg_file);
    fprintf(stderr, "  Dropfile type:  %s\n",
            cfg->dropfile_type == DROPFILE_DOOR32 ? "DOOR32.SYS" : "DORINFO1.DEF");
    fprintf(stderr, "  Dropfile path:  %s\n", cfg->dropfile_path);
    fprintf(stderr, "  Protocol:       %s\n",
            cfg->protocol == PROTOCOL_RLOGIN ? "RLOGIN" : "TELNET");
    fprintf(stderr, "  Server:         %s:%d\n", cfg->server_ip, cfg->server_port);
    fprintf(stderr, "  ClientUser map: %s\n", cfg->client_user_map);
    fprintf(stderr, "  ServerUser map: %s\n", cfg->server_user_map);
    fprintf(stderr, "  TermType:       %s\n", cfg->term_type);
    fprintf(stderr, "  Respect time:   %s\n", cfg->respect_time ? "YES" : "NO");
    fprintf(stderr, "  Local mode:     %s\n", cfg->local_mode   ? "YES" : "NO");
    fprintf(stderr, "------------------------------\n");
}
