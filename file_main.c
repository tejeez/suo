#include "suo.h"
#include "simple_receiver.h"
#include "basic_decoder.h"
#include <stdio.h>

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




typedef unsigned char sample1_t[2];

#define BUFLEN 4096
int main() {
	struct simple_receiver_conf conf = {
		.samplerate = 300000, .symbolrate = 9600,
		.centerfreq = 17500,
		.syncword = 0x1ACFFC1D, .synclen = 32,
		.framelen = (3+30)*8
	};
	struct receiver_code rx      = simple_receiver_code;

	struct frame_output_code out = test_output_code;

	void *out_arg = out.init(NULL);
	void *rx_arg  = rx.init(&conf);

	rx.set_callbacks(rx_arg, &out, out_arg);

	sample1_t buf1[BUFLEN];
	sample_t buf2[BUFLEN];
	for(;;) {
		size_t n, i;
		n = fread(buf1, sizeof(sample1_t), BUFLEN, stdin);
		if(n == 0) break;
		for(i=0; i<n; i++)
			buf2[i] = (float)buf1[i][0] - 127.4f
			        +((float)buf1[i][1] - 127.4f)*I;
		rx.execute(rx_arg, buf2, n);
	}

	return 0;
}
