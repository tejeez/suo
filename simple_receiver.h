#ifndef SIMPLE_RECEIVER_H
#define SIMPLE_RECEIVER_H
#include "common.h"

struct simple_receiver_conf {
	float samplerate, symbolrate, centerfreq;
	uint64_t syncword;
	unsigned synclen, framelen;
};

extern const struct receiver_code simple_receiver_code;

#endif
