/*
 * SYNTHRLN - Multi-Platform BBS-to-TCP Bridge
 * config.h - Configuration loader interface
 */

#ifndef CONFIG_H
#define CONFIG_H

#include "synthrln.h"

/* Default config filename searched in the executable's directory */
#define DEFAULT_CFG_FILENAME "synthrln.cfg"

/*
 * config_load()
 *
 * 1. Determines config file path (default or from /cfgfile CLI arg).
 * 2. Parses Key=Value pairs from the config file into *cfg.
 * 3. Applies any additional CLI overrides on top.
 *
 * Returns 0 on success, non-zero on fatal error.
 * A missing config file is non-fatal if enough CLI overrides are present.
 */
int config_load(app_config_t *cfg, int argc, char *argv[]);

/*
 * config_dump()
 *
 * Prints the active configuration to stderr for diagnostic purposes.
 */
void config_dump(const app_config_t *cfg);

#endif /* CONFIG_H */
