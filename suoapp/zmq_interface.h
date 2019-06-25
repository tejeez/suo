#ifndef ZMQ_INTERFACE_H
#define ZMQ_INTERFACE_H

#include "libsuo/suo.h"

struct zmq_rx_output_conf {
	const char *zmq_addr;
	const struct decoder_code *decoder;
	void *decoder_arg;
};

struct zmq_tx_input_conf {
	const char *zmq_addr;
	const struct encoder_code *encoder;
	void *encoder_arg;
};

extern const struct rx_output_code zmq_rx_output_code;
extern const struct tx_input_code zmq_tx_input_code;

#endif
