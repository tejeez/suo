#ifndef CONFIGURATION_H
#define CONFIGURATION_H

#include "libsuo/suo.h"

struct radio_conf {
	float samplerate, rx_centerfreq, tx_centerfreq; /* Hz */
	float rx_gain, tx_gain; /* dB */
	size_t rx_channel, tx_channel;
	bool tx_on;
	long long rx_tx_latency; /* ns */
	const char *driver, *rx_antenna, *tx_antenna;
};

struct configuration {
	const struct receiver_code *receiver;
	void *receiver_conf;

	const struct transmitter_code *transmitter;
	void *transmitter_conf;

	const struct decoder_code *decoder;
	void *decoder_conf;

	const struct encoder_code *encoder;
	void *encoder_conf;

	const struct rx_output_code *rx_output;
	void *rx_output_conf;

	const struct tx_input_code *tx_input;
	void *tx_input_conf;
	
	struct radio_conf radio;
};

int configure(struct configuration *, int argc, char *argv[]);

#endif
