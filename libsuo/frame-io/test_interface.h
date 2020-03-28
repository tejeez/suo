#ifndef LIBSUO_TEST_INTERFACE_H
#define LIBSUO_TEST_INTERFACE_H

#include "suo.h"

struct test_rx_output_conf {
	// no configuration
};

struct test_tx_input_conf {
	// no configuration
};

extern const struct rx_output_code test_rx_output_code;
extern const struct tx_input_code test_tx_input_code;

#endif
