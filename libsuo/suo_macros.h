#ifndef LIBSUO_CONFIGURATION_H
#define LIBSUO_CONFIGURATION_H

/* Common code used to generate configuration parsers in libsuo
 * ------------------------------------------------------------
 * CONFIG_BEGIN generates an init_conf function and start of a
 * set_conf function.
 * Macros for configuration parameters of different types:
 * CONFIG_F for floating point values (double)
 * CONFIG_I for integer values (int)
 * CONFIG_C for strings (const char *)
 *
 * If a module has no configuration parameters, use CONFIG_NONE()
 */

#include <string.h>
#include <stdlib.h>

#define CONFIG_BEGIN(NAME) \
static void *init_conf(void) \
{ \
	struct NAME ## _conf *conf; \
	conf = malloc(sizeof(*conf)); \
	if (conf != NULL) \
		*conf = NAME ## _defaults; \
	return conf; \
} \
static int set_conf(void *conf, char *parameter, char *value) \
{ \
	struct NAME ## _conf *c = conf;

#define CONFIG_F(param) if (strcmp(parameter, #param) == 0) { c->param = atof(value); return 0; }

#define CONFIG_I(param) if (strcmp(parameter, #param) == 0) { c->param = atoll(value); return 0; }

#define CONFIG_C(param) if (strcmp(parameter, #param) == 0) { int l = strlen(value+1); char *str = malloc(l); strncpy(str, value, l); c->param = str; return 0; }

#define CONFIG_END() \
	return -1; /* Unknown configuration parameter */ \
}


#define CONFIG_NONE() \
static void *init_conf(void) { return NULL; } \
static int set_conf(void *conf, char *parameter, char *value) \
{ \
	(void)conf; (void)parameter; (void)value; \
	return -1; /* No configuration parameters */ \
}


#endif
