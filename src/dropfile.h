/*
 * SYNTHRLN - Multi-Platform BBS-to-TCP Bridge
 * dropfile.h - Dropfile parser interface
 *
 * Supports:
 *   DOOR32.SYS  - Used by Windows and Linux BBS software
 *   DORINFO1.DEF - Used by DOS BBS software
 */

#ifndef DROPFILE_H
#define DROPFILE_H

#include "synthrln.h"

/*
 * dropfile_exists()
 *
 * Returns 1 if the dropfile specified in cfg->dropfile_path exists
 * and can be opened, 0 otherwise.
 */
int dropfile_exists(const app_config_t *cfg);

/*
 * dropfile_parse()
 *
 * Parses the configured dropfile and populates the session context.
 * Returns 0 on success, non-zero on failure.
 */
int dropfile_parse(const app_config_t *cfg, session_context_t *sess);

#endif /* DROPFILE_H */
