#ifndef LIBSUO_BASIC_DECODER_H
#define LIBSUO_BASIC_DECODER_H
#include "suo.h"

struct basic_decoder_conf {
	// 0 = most significant bit first, 1 = least significant bit first
	bool lsb_first;
	// bypass: just copy data to output
	bool bypass;
	// use Reed-Solomon (255,223) coding for decoded bytes
	bool rs;
};

extern const struct basic_decoder_conf basic_decoder_defaults;

extern const struct decoder_code basic_decoder_code;

#endif
