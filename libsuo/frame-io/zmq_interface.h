#ifndef LIBSUO_ZMQ_INTERFACE_H
#define LIBSUO_ZMQ_INTERFACE_H

#include "suo.h"

struct zmq_rx_output_conf {
	const char *address;
};

struct zmq_tx_input_conf {
	const char *address;
};

extern const struct zmq_rx_output_conf zmq_rx_output_defaults;
extern const struct zmq_tx_input_conf zmq_tx_input_defaults;

extern const struct rx_output_code zmq_rx_output_code;
extern const struct tx_input_code zmq_tx_input_code;

#endif
