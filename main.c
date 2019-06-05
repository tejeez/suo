#include "common.h"
#include "preamble_acq.h"
#include "fsk_demod.h"
#include "syncword_deframer.h"
#include "efrk7_decoder.h"
#include <stdio.h>

void *printf_output_init(const void *conf)
{
	(void)conf;
	return NULL;
}

int printf_output_packet(void *arg, uint8_t *bytes, size_t nbytes)
{
	(void)arg;
	size_t i;
	for(i = 0; i < nbytes; i++)
		printf("%02x ", bytes[i]);
	printf("\n");
	return 0;
}

const struct output_code printf_output_code = { printf_output_init, printf_output_packet };

typedef unsigned char sample1_t[2];

#define BUFLEN 4096
int main() {
	/*extern const struct acq_code      preamble_acq_code;
	extern const struct demod_code    fskdemod_acq_code;
	extern const struct deframer_code syncword_deframer_code;
	extern const struct decoder_code  efrk7_decoder_code;*/
	struct receiver_conf conf = {
		.acq      = preamble_acq_code,
		.acq_conf = &(const struct preamble_acq_conf){
		},
		.demod      = fsk_demod_code,
		.demod_conf = &(const struct fsk_demod_conf){
			.id = 4,
			.sps = 4
		},
		.deframer      = syncword_deframer_code,
		.deframer_conf = &(const struct syncword_deframer_conf){
		},
		.decoder      = efrk7_decoder_code,
		.decoder_conf = &(const struct efrk7_decoder_conf){
		},
		.output      = printf_output_code,
		.output_conf = NULL
	};
	void *rx = receiver_init(&conf);

	sample1_t buf1[BUFLEN];
	sample_t buf2[BUFLEN];
	for(;;) {
		size_t n, i;
		n = fread(buf1, sizeof(sample1_t), BUFLEN, stdin);
		if(n == 0) break;
		for(i=0; i<n; i++)
			buf2[i] = (float)buf1[i][0] - 127.4f
			        +((float)buf1[i][1] - 127.4f)*I;
		receiver_execute(rx, buf2, n);
	}

	return 0;
}
