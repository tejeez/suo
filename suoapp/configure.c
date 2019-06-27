#include "configure.h"
#include "libsuo/simple_receiver.h"
#include "libsuo/simple_transmitter.h"
#include "libsuo/basic_decoder.h"
#include "libsuo/basic_encoder.h"
#include "zmq_interface.h"
#include <string.h>

/* The code is going to be ugly anyway so let's use a macro
 * to avoid having to write type name 3 times everywhere.
 * Still have to write it twice though :(
 * Well, this will probably disappear when configuration file
 * parsing is done. */
#define ALLOC(dst, type) *(type*)(dst = malloc(sizeof(type)))

int configure(struct configuration *conf, int argc, char *argv[])
{
	(void)argc; (void)argv;
	memset(conf, 0, sizeof(*conf));

	conf->radio = (struct radio_conf) {
		.samplerate = 250000,
		.rx_centerfreq = 437e6,
		.tx_centerfreq = 437e6,
		.rx_gain = 60,
		.tx_gain = 60,
		.rx_channel = 0,
		.tx_channel = 0,
		.tx_on = 1,
		.rx_tx_latency = 50000000,
		.driver = "uhd",
		.rx_antenna = "TX/RX",
		.tx_antenna = "TX/RX"
	};

	float receivefreq = 437.038e6, transmitfreq = 437.038e6;
	uint32_t syncword = 0x1ACFFC1D;

	conf->receiver = &simple_receiver_code;
	ALLOC(conf->receiver_conf, struct simple_receiver_conf)
	= (struct simple_receiver_conf) {
		.samplerate = conf->radio.samplerate, .symbolrate = 9600,
		.centerfreq = receivefreq - conf->radio.rx_centerfreq,
		.syncword = syncword, .synclen = 32,
		.framelen = 30*8
	};

	conf->transmitter = &simple_transmitter_code;
	ALLOC(conf->transmitter_conf, struct simple_transmitter_conf)
	= (struct simple_transmitter_conf) {
		.samplerate = conf->radio.samplerate, .symbolrate = 9600,
		.centerfreq = transmitfreq - conf->radio.tx_centerfreq,
		.modindex = 0.5
	};

	conf->decoder = &basic_decoder_code;
	ALLOC(conf->decoder_conf, struct basic_decoder_conf)
	= (struct basic_decoder_conf) {
		.lsb_first = 0
	};

	conf->encoder = &basic_encoder_code;
	ALLOC(conf->encoder_conf, struct basic_encoder_conf)
	= (struct basic_encoder_conf) {
		.lsb_first = 0,
		.syncword = 0xAAAAAAAA00000000ULL | syncword,
		.synclen = 64
	};

	conf->rx_output = &zmq_rx_output_code;
	ALLOC(conf->rx_output_conf, struct zmq_rx_output_conf)
	= (struct zmq_rx_output_conf) {
		.zmq_addr = "tcp://*:43700"
	};

	conf->tx_input = &zmq_tx_input_code;
	ALLOC(conf->tx_input_conf, struct zmq_tx_input_conf)
	= (struct zmq_tx_input_conf) {
		.zmq_addr = "tcp://*:43701"
	};

	return 0;
}
