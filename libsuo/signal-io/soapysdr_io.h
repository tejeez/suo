#ifndef LIBSUO_SOAPYSDR_IO_H
#define LIBSUO_SOAPYSDR_IO_H
#include "suo.h"

#include <SoapySDR/Device.h>

#define SOAPYIO_RX_ON 1
#define SOAPYIO_TX_ON 2

struct soapysdr_io_conf {
	// Flags, such as SOAPYIO_TX_ON
	unsigned flags;
	// Number of samples in one RX buffer
	unsigned buffer;
	/* How much ahead TX signal should be generated (samples).
	 * Should usually be a few times the RX buffer length. */
	unsigned tx_latency;
	// Radio sample rate for both RX and TX
	double samplerate;
	// Radio center frequency for RX (Hz)
	double rx_centerfreq;
	// Radio center frequency for TX (Hz)
	double tx_centerfreq;
	// Radio RX gain (dB)
	float rx_gain;
	// Radio TX gain (dB)
	float tx_gain;
	// Radio RX channel number
	size_t rx_channel;
	// Radio TX channel number
	size_t tx_channel;
	// Radio RX antenna name
	const char *rx_antenna;
	// Radio TX antenna name
	const char *tx_antenna;
	// SoapySDR device args, such as the driver to use
	SoapySDRKwargs args;
};

extern const struct soapysdr_io_conf soapysdr_io_defaults;

extern const struct signal_io_code soapysdr_io_code;

#endif
