#ifndef LIBSUO_BASIC_ENCODER_H
#define LIBSUO_BASIC_ENCODER_H
#include "suo.h"

struct basic_encoder_conf {
	uint64_t syncword;
	unsigned synclen, preamblelen;
	bool lsb_first, bypass, rs;
};

extern const struct basic_encoder_conf basic_encoder_defaults;

extern const struct encoder_code basic_encoder_code;

#endif
