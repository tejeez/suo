#include "libsuo/suo.h"
#include "libsuo/simple_receiver.h"
#include "libsuo/simple_transmitter.h"
#include "libsuo/basic_decoder.h"
#include "libsuo/basic_encoder.h"
//#include "test_interface.h"
#include "zmq_interface.h"

#include <stdio.h>
#include <signal.h>
#include <SoapySDR/Device.h>
#include <SoapySDR/Formats.h>



/* Main loop with SoapySDR interfacing */



static void soapy_fail(const char *function, int ret)
{
	fprintf(stderr, "%s failed (%d): %s\n", function, ret, SoapySDRDevice_lastError());
}
#define SOAPYCHECK(function,...) { int ret = function(__VA_ARGS__); if(ret != 0) { soapy_fail(#function, ret); goto exit_soapy; } }



volatile int running = 1;

void sighandler(int sig)
{
	(void)sig;
	running = 0;
}


typedef unsigned char sample1_t[2];

struct suoapp_conf *conf = NULL;

#define BUFLEN 2048
int main()
{
	SoapySDRDevice *sdr = NULL;
	SoapySDRStream *rxstream = NULL, *txstream = NULL;

	/*----------------------
	 ---- Configuration ----
	 -----------------------*/

	// TODO: configuration file or command line arguments
	const float
#if 1
		sdr_samplerate = 250000,
		sdr_centerfreq = 437e6, sdr_tx_centerfreq = 437e6,
		receivefreq = 437.035e6 /*437.0175e6*/,
		transmitfreq = 437.035e6,
#else
		sdr_samplerate = 500000,
		sdr_centerfreq = 2395.1e6, sdr_tx_centerfreq = 2395e6,
		receivefreq = 2394.993e6,
		transmitfreq = 437.035e6,
#endif
		sdr_gain = 60, sdr_tx_gain = /*28*/ 50;
	size_t sdr_channel = 0, sdr_tx_channel = 0;
#if 0
	const char *sdr_driver = "xtrx", *sdr_antenna = "LNAW", *sdr_tx_antenna = "BAND1";
#else
	const char *sdr_driver = "uhd", *sdr_antenna = "TX/RX", *sdr_tx_antenna = "TX/RX";
#endif
	bool transmit_on = 1;

	const long long rx_tx_latency_ns = 50000000;

	const struct receiver_code receiver = simple_receiver_code;
	const struct simple_receiver_conf rxconf = {
		.samplerate = sdr_samplerate, .symbolrate = 9600,
		.centerfreq = receivefreq - sdr_centerfreq,
#if 0
		.syncword = 0x55F68D, .synclen = 24,
		.framelen = 16*8
#else
		.syncword = 0x1ACFFC1D, .synclen = 32,
		.framelen = 30*8
#endif
	};

	const struct transmitter_code transmitter = simple_transmitter_code;
	const struct simple_transmitter_conf txconf = {
		.samplerate = sdr_samplerate, .symbolrate = 9600,
		.centerfreq = transmitfreq - sdr_tx_centerfreq,
		.modindex = 0.5
	};


	const struct decoder_code *decoder = &basic_decoder_code;
	const struct basic_decoder_conf decoder_conf = {
		.lsb_first = 0
	};

	const struct encoder_code *encoder = &basic_encoder_code;
	const struct basic_encoder_conf encoder_conf = {
		.lsb_first = 0,
		.syncword = 0xAAAAAAAA00000000ULL | rxconf.syncword,
		.synclen = 64
	};
	

	const struct rx_output_code *rx_output = &zmq_rx_output_code;
	struct zmq_rx_output_conf rx_output_conf = {
		.zmq_addr = "tcp://*:43700"
	};

	const struct tx_input_code *tx_input = &zmq_tx_input_code;
	struct zmq_tx_input_conf tx_input_conf = {
		.zmq_addr = "tcp://*:43701"
	};

	/*-----------------------
	 ---- Initialization ----
	 ------------------------*/
	void     *encoder_arg =     encoder->init(&encoder_conf);
	void     *decoder_arg =     encoder->init(&decoder_conf);

	rx_output_conf.decoder = decoder;
	rx_output_conf.decoder_arg = decoder_arg;
	tx_input_conf.encoder = encoder;
	tx_input_conf.encoder_arg = encoder_arg;

	void    *receiver_arg =     receiver.init(&rxconf);
	void *transmitter_arg =  transmitter.init(&txconf);

	void   *rx_output_arg =   rx_output->init(&rx_output_conf);
	void    *tx_input_arg =    tx_input->init(&tx_input_conf);

	receiver.set_callbacks(receiver_arg, rx_output, rx_output_arg);
	transmitter.set_callbacks(transmitter_arg, tx_input, tx_input_arg);


	/*----------------------------
	 ---- More initialization ----
	 -----------------------------*/

	struct sigaction sigact;
	sigact.sa_handler = sighandler;
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigaction(SIGINT, &sigact, NULL);
	sigaction(SIGTERM, &sigact, NULL);
	sigaction(SIGQUIT, &sigact, NULL);
	sigaction(SIGPIPE, &sigact, NULL);


	SoapySDRKwargs args = {};
	SoapySDRKwargs_set(&args, "driver", sdr_driver);
	sdr = SoapySDRDevice_make(&args);
	SoapySDRKwargs_clear(&args);
	if(sdr == NULL) {
		soapy_fail("SoapySDRDevice_make", 0);
		goto exit_soapy;
	}

	fprintf(stderr, "Configuring RX\n");
	/* On some devices (e.g. xtrx), sample rate needs to be set before
	 * center frequency or the driver crashes */
	SOAPYCHECK(SoapySDRDevice_setSampleRate,
		sdr, SOAPY_SDR_RX, sdr_channel,
		sdr_samplerate);

	SOAPYCHECK(SoapySDRDevice_setFrequency,
		sdr, SOAPY_SDR_RX, sdr_channel,
		sdr_centerfreq, NULL);

	if(sdr_antenna != NULL)
		SOAPYCHECK(SoapySDRDevice_setAntenna,
			sdr, SOAPY_SDR_RX, sdr_channel,
			sdr_antenna);

	SOAPYCHECK(SoapySDRDevice_setGain,
		sdr, SOAPY_SDR_RX, sdr_channel,
		sdr_gain);

	if(transmit_on) {
		fprintf(stderr, "Configuring TX\n");
		SOAPYCHECK(SoapySDRDevice_setFrequency,
			sdr, SOAPY_SDR_TX, sdr_tx_channel,
			sdr_tx_centerfreq, NULL);

		if(sdr_tx_antenna != NULL)
			SOAPYCHECK(SoapySDRDevice_setAntenna,
				sdr, SOAPY_SDR_TX, sdr_tx_channel,
				sdr_tx_antenna);

		SOAPYCHECK(SoapySDRDevice_setGain,
			sdr, SOAPY_SDR_TX, sdr_tx_channel,
			sdr_tx_gain);

		SOAPYCHECK(SoapySDRDevice_setSampleRate,
			sdr, SOAPY_SDR_TX, sdr_tx_channel,
			sdr_samplerate);
	}

#if SOAPY_SDR_API_VERSION < 0x00080000
	SOAPYCHECK(SoapySDRDevice_setupStream,
		sdr, &rxstream, SOAPY_SDR_RX,
		SOAPY_SDR_CF32, &sdr_channel, 1, NULL);

	if(transmit_on) {
		SOAPYCHECK(SoapySDRDevice_setupStream,
			sdr, &txstream, SOAPY_SDR_TX,
			SOAPY_SDR_CF32, &sdr_tx_channel, 1, NULL);
	}
#else
	rxstream = SoapySDRDevice_setupStream(sdr,
		SOAPY_SDR_RX, SOAPY_SDR_CF32, &sdr_channel, 1, NULL);
	if(rxstream == NULL) {
		soapy_fail("SoapySDRDevice_setupStream", 0);
		goto exit_soapy;
	}
	if(transmit_on) {
		txstream = SoapySDRDevice_setupStream(sdr,
			SOAPY_SDR_TX, SOAPY_SDR_CF32, &sdr_tx_channel, 1, NULL);
		if(txstream == NULL) {
			soapy_fail("SoapySDRDevice_setupStream", 0);
			goto exit_soapy;
		}
	}
#endif

	fprintf(stderr, "Starting to receive\n");
	SOAPYCHECK(SoapySDRDevice_activateStream, sdr,
		rxstream, 0, 0, 0);
	if(transmit_on)
		SOAPYCHECK(SoapySDRDevice_activateStream, sdr,
			txstream, 0, 0, 0);

	/*----------------------------
	 --------- Main loop ---------
	 -----------------------------*/
	bool tx_burst_going = 0;
	while(running) {
		sample_t rxbuf[BUFLEN];
		sample_t txbuf[BUFLEN];
		void *rxbuffs[] = { rxbuf };
		const void *txbuffs[] = { txbuf };
		int flags = 0;
		long long rx_timestamp = 0;
		int ret = SoapySDRDevice_readStream(sdr, rxstream,
			rxbuffs, BUFLEN, &flags, &rx_timestamp, 200000);
		// TODO: implement metadata and add timestamps there
		if(ret > 0) {
			receiver.execute(receiver_arg, rxbuf, ret);
		} else if(ret <= 0) {
			soapy_fail("SoapySDRDevice_readStream", ret);
		}

		if(transmit_on) {
			/* Handling of TX timestamps and burst start/end
			 * might change. Not sure if this is the best way.
			 * Maybe the modem should tell when a burst ends
			 * in an additional field in the metadata struct? */
			int ntx;
			struct transmitter_metadata tx_metadata = {
				.timestamp = rx_timestamp + rx_tx_latency_ns
			};
			ntx = transmitter.execute(transmitter_arg, txbuf, BUFLEN, &tx_metadata);
			if(ntx > 0) {
				flags = SOAPY_SDR_HAS_TIME;
				/* If there were less than the maximum number of samples,
				 * assume a burst has ended */
				if(ntx < BUFLEN) {
					flags |= SOAPY_SDR_END_BURST;
					tx_burst_going = 0;
				} else {
					tx_burst_going = 1;
				}

				ret = SoapySDRDevice_writeStream(sdr, txstream,
					txbuffs, ntx, &flags,
					tx_metadata.timestamp, 100000);
				if(ret <= 0)
					soapy_fail("SoapySDRDevice_writeStream", ret);
			} else {
				/* Nothing to transmit.
				 * If end of burst flag wasn't sent in last round,
				 * send it now together with one dummy sample.
				 * One sample is sent because trying to send
				 * zero samples gave a timeout error. */
				if(tx_burst_going) {
					txbuf[0] = 0;
					flags = SOAPY_SDR_HAS_TIME | SOAPY_SDR_END_BURST;
					ret = SoapySDRDevice_writeStream(sdr, txstream,
						txbuffs, 1, &flags,
						rx_timestamp + rx_tx_latency_ns, 100000);
					if(ret <= 0)
						soapy_fail("SoapySDRDevice_writeStream (end of burst)", ret);
				}
				tx_burst_going = 0;
			}
		}
	}

	fprintf(stderr, "Stopped receiving\n");

exit_soapy:
	if(rx_output_arg)
		rx_output->destroy(rx_output_arg);

	if(tx_input_arg)
		tx_input->destroy(tx_input_arg);

	if(rxstream != NULL) {
		fprintf(stderr, "Deactivating stream\n");
		SoapySDRDevice_deactivateStream(sdr, rxstream, 0, 0);
		SoapySDRDevice_closeStream(sdr, rxstream);
	}
	if(sdr != NULL) {
		fprintf(stderr, "Closing device\n");
		SoapySDRDevice_unmake(sdr);
	}

	fprintf(stderr, "Done\n");
	return 0;
}
