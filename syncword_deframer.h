#ifndef SYNCWORD_DEFRAMER_H
#define SYNCWORD_DEFRAMER_H
#include "suo.h"

struct syncword_deframer_conf {
	/* Everything is hardcoded for now. No configuration. */
};

extern const struct deframer_code syncword_deframer_code;

#endif
