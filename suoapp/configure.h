#ifndef CONFIGURATION_H
#define CONFIGURATION_H

#include "libsuo/suo.h"

struct radio_conf {
	float samplerate, rx_centerfreq, tx_centerfreq; /* Hz */
	float rx_gain, tx_gain; /* dB */
	size_t rx_channel, tx_channel;
	bool tx_on;
	long long rx_tx_latency; /* ns */
	const char *rx_antenna, *tx_antenna;
};


struct suo {
	const struct receiver_code *receiver;
	void *receiver_arg;

	const struct transmitter_code *transmitter;
	void *transmitter_arg;

	const struct decoder_code *decoder;
	void *decoder_arg;

	const struct encoder_code *encoder;
	void *encoder_arg;

	const struct rx_output_code *rx_output;
	void *rx_output_arg;

	const struct tx_input_code *tx_input;
	void *tx_input_arg;

	struct radio_conf radio_conf;
};

int configure(struct suo *, int argc, char *argv[]);
int deinitialize(struct suo *);

#endif
