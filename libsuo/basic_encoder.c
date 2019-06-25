#include "basic_encoder.h"
#include <string.h>
#include <assert.h>
#include <liquid/liquid.h>

struct basic_encoder {
	struct basic_encoder_conf conf;
};


const struct basic_encoder_conf basic_encoder_defaults = {
	.lsb_first = 0,
	.syncword = 0xAAAAAAAA36994625,
	.synclen = 64,
};


static void *init(const void *confv)
{
	struct basic_encoder *self = malloc(sizeof(struct basic_encoder));
	self->conf = *(struct basic_encoder_conf*)confv;
	return self;
}


static int destroy(void *arg)
{
	free(arg);
	return 0;
}


static int encode(void *arg, bit_t *bits, size_t max_nbits, const uint8_t *data, size_t nbytes)
{
	struct basic_encoder *self = arg;
	const bool lsb_first = self->conf.lsb_first;
	const unsigned headerbits = self->conf.synclen;

	size_t nbits = nbytes * 8 + headerbits;

	if (nbits > max_nbits)
		nbits = max_nbits;

	size_t i;
	uint64_t header = self->conf.syncword;
	for (i = headerbits; i-- > 0; ) {
		bits[i] = header & 1;
		header <<= 1;
	}

	for (i = headerbits; i < nbits; i++) {
		const size_t bytenum = (i-headerbits) >> 3, bitnum = (i-headerbits) & 7;
		if (lsb_first)
			bits[i] = (data[bytenum] & (1 << bitnum)) ? 1 : 0;
		else
			bits[i] = (data[bytenum] & (0x80 >> bitnum)) ? 1 : 0;
	}

	return nbits;
}


const struct encoder_code basic_encoder_code = { init, destroy, encode };

