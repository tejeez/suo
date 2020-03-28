#ifndef LIBSUO_BASIC_DECODER_H
#define LIBSUO_BASIC_DECODER_H
#include "suo.h"

struct basic_decoder_conf {
	bool lsb_first, rs;
};

extern const struct basic_decoder_conf basic_decoder_defaults;

extern const struct decoder_code basic_decoder_code;

#endif
