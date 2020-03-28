#ifndef LIBSUO_SIMPLE_RECEIVER_H
#define LIBSUO_SIMPLE_RECEIVER_H
#include "suo.h"

struct simple_receiver_conf {
	float samplerate, symbolrate, centerfreq;
	uint64_t syncword;
	unsigned synclen, framelen;
};

extern const struct simple_receiver_conf simple_receiver_defaults;

extern const struct receiver_code simple_receiver_code;

#endif
