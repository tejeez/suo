#ifndef LIBSUO_PSK_TRANSMITTER_H
#define LIBSUO_PSK_TRANSMITTER_H
#include "suo.h"

struct psk_transmitter_conf {
	float samplerate;
	float symbolrate;
	float centerfreq;
};

extern const struct psk_transmitter_conf psk_transmitter_defaults;

extern const struct transmitter_code psk_transmitter_code;

#endif
