#include "libsuo/suo.h"
#include <stdio.h>

struct test_output {
	struct decoder_code decoder;
	void *decoder_arg;
};

void *test_output_init(const void *conf)
{
	(void)conf;
	struct test_output *self = malloc(sizeof(struct test_output));

	struct basic_decoder_conf decconf =  {
		.lsb_first = 0
	};
	self->decoder = basic_decoder_code;
	self->decoder_arg = self->decoder.init(&decconf);
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


int test_input_get_frame(void *arg, bit_t *bits, size_t maxbits, struct transmitter_metadata *metadata)
{
	(void)arg;
	const uint8_t packet[] = {
		0x55,0x55,0x55,0x55,0x55,0x55,
		0x1A,0xCF,0xFC,0x1D,
		't','e','s','t','i',0,0,0,0 };
	if(metadata->timestamp % 4000000000LL < 100000000LL) {
		size_t len = sizeof(packet)*8;
		if(len > maxbits) len = maxbits;
		size_t i;
		for(i=0; i<len; i++)
			bits[i] = 1 & (packet[i/8] >> (7&(7-i)));
		return len;
	}
	return -1;
}


const struct tx_input_code test_input_code = { test_framer_init, test_input_get_frame };
