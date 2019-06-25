#include "basic_decoder.h"
#include <string.h>
#include <assert.h>
#include <liquid/liquid.h>

struct basic_decoder {
	struct basic_decoder_conf conf;
};


const struct basic_decoder_conf basic_decoder_defaults = {
	.lsb_first = 0
};


static void *init(const void *confv)
{
	struct basic_decoder *self = malloc(sizeof(struct basic_decoder));
	self->conf = *(struct basic_decoder_conf*)confv;
	return self;
}


static int destroy(void *arg)
{
	free(arg);
	return 0;
}


static int decode(void *arg, const bit_t *bits, size_t nbits, uint8_t *decoded, size_t max_decoded_len)
{
	struct basic_decoder *self = arg;
	const bool lsb_first = self->conf.lsb_first;
	size_t nbytes = nbits / 8;
	if(nbytes > max_decoded_len)
		nbytes = max_decoded_len;

	size_t i;
	for(i = 0; i < nbytes; i++) {
		uint8_t byte = 0;
		size_t j;
		if(!lsb_first) {
			for(j = 0; j < 8; j++) {
				byte <<= 1;
				byte |= 1 & *bits;
				bits++;
			}
		} else {
			for(j = 0; j < 8; j++) {
				byte >>= 1;
				byte |= (1 & *bits) << 7;
				bits++;
			}
		}
		decoded[i] = byte;
	}

	return nbytes;
}


const struct decoder_code basic_decoder_code = { init, destroy, decode };

