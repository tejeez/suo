#ifndef LIBSUO_SIMPLE_TRANSMITTER_H
#define LIBSUO_SIMPLE_TRANSMITTER_H
#include "suo.h"

struct simple_transmitter_conf {
	float samplerate, symbolrate, centerfreq;
	float modindex;
};

extern const struct simple_transmitter_conf simple_transmitter_defaults;

extern const struct transmitter_code simple_transmitter_code;

#endif
