#include "configure.h"
#include "libsuo/simple_receiver.h"
#include "libsuo/simple_transmitter.h"
#include "libsuo/basic_decoder.h"
#include "libsuo/basic_encoder.h"
#include "zmq_interface.h"
#include "test_interface.h"
#include <string.h>
#include <stdio.h>

#define PARAMETERF(name) if(strcmp(argv[i], #name) == 0) { p_##name = atof(argv[i+1]); i++; continue; }

#define PARAMETERI(name) if(strcmp(argv[i], #name) == 0) { p_##name = atoi(argv[i+1]); i++; continue; }

#define PARAMETERC(name) if(strcmp(argv[i], #name) == 0) { p_##name = argv[i+1]; i++; continue; }

int configure(struct suo *suo, int argc, char *argv[])
{
	float p_samplerate = 1e6;
	float p_rxcenter = 433.8e6, p_txcenter = 433.8e6;
	float p_rxfreq = 433.92e6, p_txfreq = 433.92e6;
	float p_symbolrate = 9600;
	uint32_t p_syncword = 0x1ACFFC1D;
	unsigned p_bytes = 30;
	bool p_rs = 0, p_tx = 0, p_zmq = 0;
	const char *p_rxant = NULL, *p_txant = NULL;

	int i;
	for(i=1; i<argc-1; i++) {
		PARAMETERF(samplerate)
		PARAMETERF(rxcenter)
		PARAMETERF(txcenter)
		PARAMETERF(rxfreq)
		PARAMETERF(txfreq);
		PARAMETERF(symbolrate)
		PARAMETERI(syncword)  // atoi doesn't understand hex though :(
		PARAMETERI(bytes)
		PARAMETERI(rs)
		PARAMETERI(tx)
		PARAMETERI(zmq)
		PARAMETERC(rxant)
		PARAMETERC(txant)
		/* Skip SoapySDR parameters (parsed in main) */
		if(strncmp(argv[i], "soapy-", 6) == 0) {
			i++;
			continue;
		}
		fprintf(stderr, "Unknown parameter %s\n", argv[i]);
	}

	memset(suo, 0, sizeof(*suo));

	suo->radio_conf = (struct radio_conf) {
		.samplerate = p_samplerate,
		.rx_centerfreq = p_rxcenter,
		.tx_centerfreq = p_txcenter,
		.rx_gain = 60,
		.tx_gain = 80,
		.rx_channel = 0,
		.tx_channel = 0,
		.tx_on = p_tx,
		.rx_tx_latency = 50000000,
		.rx_antenna = p_rxant,
		.tx_antenna = p_txant
	};

	int receiver_type = 0, transmitter_type = 0,
		decoder_type = 0, encoder_type = 0,
		output_type = p_zmq ? 1 : 0, input_type = p_zmq ? 1 : 0;

	if(receiver_type == 0) {
		suo->receiver = &simple_receiver_code;
		struct simple_receiver_conf c = {
			.samplerate = p_samplerate, .symbolrate = p_symbolrate,
			.centerfreq = p_rxfreq - p_rxcenter,
			.syncword = p_syncword, .synclen = 32,
			.framelen = p_bytes*8
		};
		if (p_rs)
			c.framelen += 32*8;
		suo->receiver_arg = suo->receiver->init(&c);
	}

	if(p_tx && transmitter_type == 0) {
		suo->transmitter = &simple_transmitter_code;
		struct simple_transmitter_conf c = {
			.samplerate = p_samplerate, .symbolrate = p_symbolrate,
			.centerfreq = p_txfreq - p_txcenter,
			.modindex = 0.5
		};
		suo->transmitter_arg = suo->transmitter->init(&c);
	}

	if(decoder_type == 0) {
		suo->decoder = &basic_decoder_code;
		struct basic_decoder_conf c = {
			.lsb_first = 0,
			.rs = p_rs
		};
		suo->decoder_arg = suo->decoder->init(&c);
	}

	if(p_tx && encoder_type == 0) {
		suo->encoder = &basic_encoder_code;
		struct basic_encoder_conf c = basic_encoder_defaults;
		c.syncword = p_syncword;
		c.synclen = 32;
		c.rs = p_rs;
		suo->encoder_arg = suo->encoder->init(&c);
	}

	if(output_type == 0) {
		suo->rx_output = &test_rx_output_code;
		suo->rx_output_arg = suo->rx_output->init(NULL);

	} else if(output_type == 1) {
		suo->rx_output = &zmq_rx_output_code;
		struct zmq_rx_output_conf c = {
			.zmq_addr = "tcp://*:43300"
		};
		suo->rx_output_arg = suo->rx_output->init(&c);
	}

	if(p_tx && input_type == 0) {
		suo->tx_input = &test_tx_input_code;
		suo->tx_input_arg = suo->tx_input->init(NULL);

	} else if(p_tx && input_type == 1) {
		suo->tx_input = &zmq_tx_input_code;
		struct zmq_tx_input_conf c = {
			.zmq_addr = "tcp://*:43301"
		};
		suo->tx_input_arg = suo->tx_input->init(&c);
	}


	suo->rx_output  ->set_callbacks(suo->rx_output_arg, suo->decoder, suo->decoder_arg);
	suo->receiver   ->set_callbacks(suo->receiver_arg, suo->rx_output, suo->rx_output_arg);

	if(p_tx) {
		suo->tx_input   ->set_callbacks(suo->tx_input_arg, suo->encoder, suo->encoder_arg);
		suo->transmitter->set_callbacks(suo->transmitter_arg, suo->tx_input, suo->tx_input_arg);
	}

	return 0;
}


int deinitialize(struct suo *suo)
{
	if(suo->rx_output_arg)
		suo->rx_output->destroy(suo->rx_output_arg);

	if(suo->tx_input_arg)
		suo->tx_input->destroy(suo->tx_input_arg);

	return 0;
}
