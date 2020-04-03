#ifndef LIBSUO_ZMQ_INTERFACE_H
#define LIBSUO_ZMQ_INTERFACE_H

#include "suo.h"

// Flag: bind to a socket. Otherwise, connect.
#define ZMQIO_BIND 1
// Flag: include frame metadata in ZeroMQ messages (TODO)
#define ZMQIO_METADATA 2
// Flag: run decoder or encoder in a separate thread (TODO)
#define ZMQIO_THREAD 4

struct zmq_rx_output_conf {
	const char *address;
	uint32_t flags;
};

struct zmq_tx_input_conf {
	const char *address;
	uint32_t flags;
};

extern const struct zmq_rx_output_conf zmq_rx_output_defaults;
extern const struct zmq_tx_input_conf zmq_tx_input_defaults;

extern const struct rx_output_code zmq_rx_output_code;
extern const struct tx_input_code zmq_tx_input_code;

#endif
