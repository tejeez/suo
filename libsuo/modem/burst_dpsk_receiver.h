#ifndef LIBSUO_BURST_DPSK_RECEIVER_H
#define LIBSUO_BURST_DPSK_RECEIVER_H
#include "suo.h"

struct burst_dpsk_receiver_conf {
	float samplerate, symbolrate, centerfreq;
	uint64_t syncword1, syncword2, syncword3;
	unsigned synclen, synclen3;
	unsigned syncpos;
	unsigned framelen;
};

extern const struct burst_dpsk_receiver_conf burst_dpsk_receiver_defaults;

extern const struct receiver_code burst_dpsk_receiver_code;

#endif
