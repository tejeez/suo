#include "frame-io/test_interface.h"
#include "frame-io/zmq_interface.h"
#include "signal-io/file_io.h"
#include "signal-io/soapysdr_io.h"

/* Tables to list all the modules implemented */

const struct rx_output_code *suo_rx_outputs[] = {
	&test_rx_output_code,
	&zmq_rx_output_code,
	NULL
};

const struct tx_input_code *suo_tx_inputs[] = {
	&test_tx_input_code,
	&zmq_tx_input_code,
	NULL
};

const struct signal_io_code *suo_signal_ios[] = {
	&file_io_code,
	&soapysdr_io_code,
	NULL
};
