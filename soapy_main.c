#include "suo.h"
#include "simple_receiver.h"
#include "basic_decoder.h"
#include "simple_transmitter.h"
#include <stdio.h>
/* __USE_POSIX is needed for "struct sigaction" when
 * compiled with  -std=c99. This is probably not the right way.
 * Only tested on Linux and gcc, might not work on other platforms.
 * Maybe this one file should be compiled with different compiler
 * flags instead? */
#define __USE_POSIX
#include <signal.h>
#include <SoapySDR/Device.h>
#include <SoapySDR/Formats.h>


/* Receiver testing things */
struct test_output {
	struct decoder_code decoder;
	void *decoder_arg;
};

void *test_output_init(const void *conf)
{
	(void)conf;
	struct test_output *self = malloc(sizeof(struct test_output));

	self->decoder = basic_decoder_code;
	self->decoder_arg = self->decoder.init(&basic_decoder_defaults);
	return self;
}


int test_output_frame(void *arg, bit_t *bits, size_t nbits)
{
	struct test_output *self = arg;
	uint8_t decoded[0x200];
	size_t i;
	int ret;

	for(i = 0; i < nbits; i++)
		printf("%d", bits[i]);
	printf("\n");

	ret = self->decoder.decode(self->decoder_arg, bits, nbits, decoded, 0x200);
	if(ret >= 0) {
		for(i = 0; i < (size_t)ret; i++)
			printf("%02x ", decoded[i]);
		printf("\n");
		/* Print those which are valid ASCII characters */
		for(i = 0; i < (size_t)ret; i++) {
			char c = (char)decoded[i];
			if(c >= 32 && c <= 126)
				putchar(c);
		}
		printf("\n");
	} else {
		printf("Decode failed (%d)\n", ret);
	}
	return 0;
}


const struct frame_output_code test_output_code = { test_output_init, test_output_frame };



/* Transmitter testing things */
void *test_framer_init(const void *conf)
{
	(void)conf;
	return NULL;
}


int test_framer_get_frame(void *arg, bit_t *bits, size_t maxbits, struct transmitter_metadata *metadata)
{
	(void)arg;
	if(metadata->timestamp % 1000000000LL < 100000000LL) {
		size_t len = 1000;
		if(len > maxbits) len = maxbits;
		size_t i;
		for(i=0; i<len; i++)
			bits[i] = 1 & rand();
		return len;
	}
	return -1;
}


const struct framer_code test_framer_code = { test_framer_init, test_framer_get_frame };



/* Main loop with SoapySDR interfacing */



void print_fail(const char *function, int ret)
{
	fprintf(stderr, "%s failed (%d): %s\n", function, ret, SoapySDRDevice_lastError());
}
#define SOAPYCHECK(function,...) { int ret = function(__VA_ARGS__); if(ret != 0) { print_fail(#function, ret); goto exit_soapy; } }


volatile int running = 1;

void sighandler(int sig)
{
	(void)sig;
	running = 0;
}


typedef unsigned char sample1_t[2];

#define BUFLEN 4096
int main()
{
	// TODO: configuration file or command line arguments
	const float
		sdr_samplerate = 250000,
		sdr_centerfreq = 437e6, sdr_tx_centerfreq = 437e6,
		receivefreq = 437.035e6 /*437.0175e6*/,
		transmitfreq = 437.06e6,
		sdr_gain = 40, sdr_tx_gain = 60;
	size_t sdr_channel = 0, sdr_tx_channel = 0;
	const char *sdr_driver = "uhd", *sdr_antenna = "TX/RX", *sdr_tx_antenna = "TX/RX";
	bool transmit_on = 1;

	const long long rx_tx_latency_ns = 50000000;

	struct simple_receiver_conf rxconf = {
		.samplerate = sdr_samplerate, .symbolrate = 9600,
		.centerfreq = receivefreq - sdr_centerfreq,
		.syncword = 0x1ACFFC1D, .synclen = 32,
		.framelen = (3+30)*8
	};
	struct receiver_code receiver = simple_receiver_code;

	struct simple_transmitter_conf txconf = {
		.samplerate = sdr_samplerate, .symbolrate = 9600,
		.centerfreq = transmitfreq - sdr_tx_centerfreq,
		.modindex = 0.5
	};
	struct transmitter_code transmitter = simple_transmitter_code;

	struct frame_output_code out = test_output_code;

	void *out_arg = out.init(NULL);
	void *receiver_arg = receiver.init(&rxconf);
	void *transmitter_arg = transmitter.init(&txconf);

	receiver.set_callbacks(receiver_arg, &out, out_arg);
	transmitter.set_callbacks(transmitter_arg, &test_framer_code, NULL);


	struct sigaction sigact;
	sigact.sa_handler = sighandler;
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigaction(SIGINT, &sigact, NULL);
	sigaction(SIGTERM, &sigact, NULL);
	sigaction(SIGQUIT, &sigact, NULL);
	sigaction(SIGPIPE, &sigact, NULL);


	SoapySDRDevice *sdr = NULL;
	SoapySDRStream *rxstream = NULL, *txstream = NULL;

	SoapySDRKwargs args = {};
	SoapySDRKwargs_set(&args, "driver", sdr_driver);
	sdr = SoapySDRDevice_make(&args);
	SoapySDRKwargs_clear(&args);
	if(sdr == NULL) {
		print_fail("SoapySDRDevice_make", 0);
		goto exit_soapy;
	}

	fprintf(stderr, "Configuring RX\n");
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

	SOAPYCHECK(SoapySDRDevice_setSampleRate,
		sdr, SOAPY_SDR_RX, sdr_channel,
		sdr_samplerate);

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
		print_fail("SoapySDRDevice_setupStream", 0);
		goto exit_soapy;
	}
	if(transmit_on) {
		txstream = SoapySDRDevice_setupStream(sdr,
			SOAPY_SDR_TX, SOAPY_SDR_CF32, &sdr_tx_channel, 1, NULL);
		if(txstream == NULL) {
			print_fail("SoapySDRDevice_setupStream", 0);
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
			print_fail("SoapySDRDevice_readStream", ret);
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
					txbuffs, BUFLEN, &flags,
					tx_metadata.timestamp, 100000);
				if(ret <= 0)
					print_fail("SoapySDRDevice_writeStream", ret);
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
						print_fail("SoapySDRDevice_writeStream (end of burst)", ret);
				}
				tx_burst_going = 0;
			}
		}
	}

	fprintf(stderr, "Stopped receiving\n");

exit_soapy:
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
