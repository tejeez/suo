#include "test_interface.h"
#include "suo_macros.h"
#include <stdio.h>
#include <string.h>

struct test_output {
	const struct decoder_code *decoder;
	void *decoder_arg;
};

void *test_output_init(const void *conf)
{
	(void)conf;
	struct test_output *self = malloc(sizeof(struct test_output));
	memset(self, 0, sizeof(*self));
	return self;
}


static int test_output_frame(void *arg, const struct frame *frame)
{
	struct test_output *self = arg;
	char decoded_buf[sizeof(struct frame) + 0x200];
	struct frame *decoded = (struct frame *)decoded_buf;
	size_t i, nbits = frame->m.len;
	const struct metadata *metadata = &frame->m;
	int ret;

	for(i = 0; i < nbits; i++)
		printf("%3d ", frame->data[i]);
	printf("\n\n");

	if (self->decoder) {
		ret = self->decoder->decode(self->decoder_arg, frame, decoded, 0x200);
	} else {
		decoded = (struct frame *)frame;
		ret = decoded->m.len;
	}
	if(ret >= 0) {
		for(i = 0; i < (size_t)ret; i++)
			printf("%02x ", decoded->data[i]);
		printf("\n");
		/* Print those which are valid ASCII characters */
		for(i = 0; i < (size_t)ret; i++) {
			char c = (char)decoded->data[i];
			if(c >= 32 && c <= 126)
				putchar(c);
		}
		printf("\n");
	} else {
		printf("Decode failed (%d)\n", ret);
	}
	printf("Timestamp: %lld ns   Mode: %3u  RSSI: %6.2f dB  CFO: %E Hz  BER: %E  SER: %E\n\n",
		(long long)metadata->time, metadata->mode,
		(double)metadata->power, (double)metadata->cfo,
		(double)metadata->ber, (double)metadata->ser);
	return 0;
}


int test_output_destroy(void *arg)
{
	free(arg);
	return 0;
}


int test_output_set_callbacks(void *arg, const struct decoder_code *decoder, void *decoder_arg)
{
	struct test_output *self = arg;
	self->decoder = decoder;
	self->decoder_arg = decoder_arg;
	return 0;
}


int tick(void *arg, timestamp_t timenow)
{
	(void)arg; (void)timenow;
	return 0;
}


CONFIG_NONE()

const struct rx_output_code test_rx_output_code = { "test_output", test_output_init, test_output_destroy, init_conf, set_conf, test_output_set_callbacks, test_output_frame, tick };



/* Transmitter testing things */

struct test_input {
	const struct encoder_code *encoder;
	void *encoder_arg;
};


void *test_input_init(const void *conf)
{
	(void)conf;
	struct test_input *self = malloc(sizeof(struct test_input));
	memset(self, 0, sizeof(*self));
	return self;
}


int test_input_destroy(void *arg)
{
	free(arg);
	return 0;
}


int test_input_set_callbacks(void *arg, const struct encoder_code *encoder, void *encoder_arg)
{
	struct test_input *self = arg;
	self->encoder = encoder;
	self->encoder_arg = encoder_arg;
	return 0;
}


int test_input_get_frame(void *arg, struct frame *frame, size_t maxlen, timestamp_t time_dl)
{
	struct test_input *self = arg;
	(void)self;

	if(time_dl % 400000000LL < 100000000LL) {
		frame->m.flags = METADATA_FLAGS | METADATA_TIME | METADATA_NO_LATE;
		// Test this boundary case where a frame wants to be just on the deadline
		frame->m.time = time_dl;

#define TESTLEN 30
		if (maxlen < TESTLEN)
			return -1;
		memcpy(frame->data, (const uint8_t[TESTLEN]){
			0,0, 0,0,
			1,1, 0,1, 0,0, 0,0, 1,1, 1,0, 1,0, 0,1, 1,1, 0,1, 0,0,
			0,0, 0,0
		}, TESTLEN);
		frame->m.len = TESTLEN;
		return TESTLEN;
	}
	return -1;
}


const struct tx_input_code test_tx_input_code = { "test_input", test_input_init, test_input_destroy, init_conf, set_conf, test_input_set_callbacks, test_input_get_frame, tick };
