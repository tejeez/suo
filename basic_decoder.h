#ifndef BASIC_DECODER_H
#define BASIC_DECODER_H
#include "suo.h"

struct basic_decoder_conf {
	bool lsb_first;
};

extern const struct basic_decoder_conf basic_decoder_defaults;

extern const struct decoder_code basic_decoder_code;

#endif
