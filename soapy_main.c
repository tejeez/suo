#include "suo.h"
#include "simple_receiver.h"
#include "basic_decoder.h"
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
	const float sdr_samplerate = 300000, sdr_centerfreq = 437e6,
	receivefreq = 437.0175e6;
	size_t sdr_channel = 0;
	const char *sdr_driver = "rtlsdr";

	struct simple_receiver_conf conf = {
		.samplerate = sdr_samplerate, .symbolrate = 9600,
		.centerfreq = receivefreq - sdr_centerfreq,
		.syncword = 0x1ACFFC1D, .synclen = 32,
		.framelen = (3+255)*8
	};
	struct receiver_code receiver = simple_receiver_code;

	struct frame_output_code out = test_output_code;

	void *out_arg = out.init(NULL);
	void *receiver_arg = receiver.init(&conf);

	receiver.set_callbacks(receiver_arg, &out, out_arg);


	struct sigaction sigact;
	sigact.sa_handler = sighandler;
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigaction(SIGINT, &sigact, NULL);
	sigaction(SIGTERM, &sigact, NULL);
	sigaction(SIGQUIT, &sigact, NULL);
	sigaction(SIGPIPE, &sigact, NULL);


	SoapySDRDevice *sdr = NULL;
	SoapySDRStream *rxstream = NULL;

	SoapySDRKwargs args = {};
	SoapySDRKwargs_set(&args, "driver", sdr_driver);
	sdr = SoapySDRDevice_make(&args);
	SoapySDRKwargs_clear(&args);
	if(sdr == NULL) {
		print_fail("SoapySDRDevice_make", 0);
		goto exit_soapy;
	}

	SOAPYCHECK(SoapySDRDevice_setFrequency, sdr, 0, 0, sdr_centerfreq, NULL);
	SOAPYCHECK(SoapySDRDevice_setSampleRate, sdr, SOAPY_SDR_RX, 0, sdr_samplerate);
	//SOAPYCHECK(SoapySDRDevice_setAntenna, sdr, SOAPY_SDR_RX, 0, "");

#if SOAPY_SDR_API_VERSION < 0x00080000
	SOAPYCHECK(SoapySDRDevice_setupStream, sdr, &rxstream, SOAPY_SDR_RX, SOAPY_SDR_CF32, &sdr_channel, 1, NULL);
#else
	rxstream = SoapySDRDevice_setupStream(sdr, SOAPY_SDR_RX, SOAPY_SDR_CF32, &sdr_channel, 1, NULL);
	if(rxstream == NULL) {
		print_fail("SoapySDRDevice_setupStream", 0);
		goto exit_soapy;
	}
#endif

	fprintf(stderr, "Starting to receive\n");
	SOAPYCHECK(SoapySDRDevice_activateStream, sdr, rxstream, 0, 0, 0);

	while(running) {
		sample_t rxbuf[BUFLEN];
		void *rxbuffs[] = { rxbuf };
		int flags = 0;
		long long int timestamp = 0;
		int ret = SoapySDRDevice_readStream(sdr, rxstream, rxbuffs, BUFLEN, &flags, &timestamp, 1000000);
		// TODO: implement metadata and add timestamps there
		if(ret > 0) {
			receiver.execute(receiver_arg, rxbuf, ret);
		} else if(ret <= 0) {
			print_fail("SoapySDRDevice_readStream", ret);
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
