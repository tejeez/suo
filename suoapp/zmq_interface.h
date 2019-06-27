#ifndef ZMQ_INTERFACE_H
#define ZMQ_INTERFACE_H

#include "libsuo/suo.h"

struct zmq_rx_output_conf {
	const char *zmq_addr;
};

struct zmq_tx_input_conf {
	const char *zmq_addr;
};

extern const struct rx_output_code zmq_rx_output_code;
extern const struct tx_input_code zmq_tx_input_code;

#endif
