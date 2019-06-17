#include "libsuo/suo.h"
#include "libsuo/simple_receiver.h"
#include "libsuo/basic_decoder.h"
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

	struct basic_decoder_conf decconf =  {
		.lsb_first = 1
	};
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




typedef uint8_t cu8_t[2];
typedef int16_t cs16_t[2];
enum inputformat { FORMAT_CU8, FORMAT_CS16 };

#define BUFLEN 4096
int main() {
	enum inputformat inputformat = FORMAT_CS16;
	struct simple_receiver_conf conf = {
		.samplerate = 1000000, .symbolrate = 9600,
		.centerfreq = 193000,
		.syncword = 0x55F68D, .synclen = 24,
		.framelen = 16*8
	};
	struct receiver_code rx      = simple_receiver_code;

	struct frame_output_code out = test_output_code;

	void *out_arg = out.init(NULL);
	void *rx_arg  = rx.init(&conf);

	rx.set_callbacks(rx_arg, &out, out_arg);

	sample_t buf2[BUFLEN];
	for(;;) {
		size_t n, i;
		if(inputformat == FORMAT_CU8) {
			cu8_t buf1[BUFLEN];
			n = fread(buf1, sizeof(cu8_t), BUFLEN, stdin);
			if(n == 0) break;
			for(i=0; i<n; i++)
				buf2[i] = (float)buf1[i][0] - 127.4f
					+((float)buf1[i][1] - 127.4f)*I;
		} else {
			cs16_t buf1[BUFLEN];
			n = fread(buf1, sizeof(cs16_t), BUFLEN, stdin);
			if(n == 0) break;
			for(i=0; i<n; i++)
				buf2[i] = (float)buf1[i][0]
					+((float)buf1[i][1])*I;
		}
		rx.execute(rx_arg, buf2, n);
	}

	return 0;
}
