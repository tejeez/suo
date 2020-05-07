#ifndef LIBSUO_ALSA_IO_H
#define LIBSUO_ALSA_IO_H
#include "suo.h"

struct alsa_io_conf {
	// Configuration flags
	unsigned char
	rx_on:1,     // Enable reception
	tx_on:1;     // Enable transmission
	// Audio sample rate for both RX and TX
	uint32_t samplerate;
	// Number of samples in each RX block processed
	unsigned buffer;
	/* How much ahead TX signal should be generated (samples).
	 * Should usually be a few times the RX buffer length. */
	unsigned tx_latency;
	// RX sound card name
	const char *rx_name;
	// TX sound card name
	const char *tx_name;
};

extern const struct alsa_io_conf alsa_io_defaults;
extern const struct signal_io_code alsa_io_code;

#endif
