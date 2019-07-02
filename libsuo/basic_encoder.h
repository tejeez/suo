#ifndef BASIC_ENCODER_H
#define BASIC_ENCODER_H
#include "suo.h"

struct basic_encoder_conf {
	uint64_t syncword;
	unsigned synclen, preamblelen;
	bool lsb_first, rs;
};

extern const struct basic_encoder_conf basic_encoder_defaults;

extern const struct encoder_code basic_encoder_code;

#endif
