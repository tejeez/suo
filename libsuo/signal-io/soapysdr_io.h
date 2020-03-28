#ifndef LIBSUO_SOAPYSDR_IO_H
#define LIBSUO_SOAPYSDR_IO_H
#include "suo.h"

struct soapysdr_io_conf {
	float samplerate, rx_centerfreq, tx_centerfreq; /* Hz */
	float rx_gain, tx_gain; /* dB */
	size_t rx_channel, tx_channel;
	bool tx_on;
	long long rx_tx_latency; /* ns */
	const char *rx_antenna, *tx_antenna;
};

extern const struct soapysdr_io_conf soapysdr_io_defaults;

extern const struct signal_io_code soapysdr_io_code;

#endif
