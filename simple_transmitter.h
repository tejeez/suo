#ifndef SIMPLE_TRANSMITTER_H
#define SIMPLE_TRANSMITTER_H
#include "suo.h"

struct simple_transmitter_conf {
	float samplerate, symbolrate, centerfreq;
	float modindex;
};

extern const struct transmitter_code simple_transmitter_code;

#endif
