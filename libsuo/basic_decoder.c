#include "basic_decoder.h"
#include <string.h>
#include <assert.h>
#include <liquid/liquid.h>

struct basic_decoder {
	struct basic_decoder_conf conf;
	uint8_t *buf, *buf2;
	fec l_fec;
};


const struct basic_decoder_conf basic_decoder_defaults = {
	.lsb_first = 0,
	.rs = 0
};


static void *init(const void *confv)
{
	struct basic_decoder *self = malloc(sizeof(struct basic_decoder));
	if(self == NULL) return NULL;
	memset(self, 0, sizeof(*self));
	self->conf = *(struct basic_decoder_conf*)confv;

	if(self->conf.rs) {
		self->l_fec = fec_create(LIQUID_FEC_RS_M8, NULL);
		self->buf = malloc(255);
		self->buf2 = malloc(255);
	}

	return self;
}


static int destroy(void *arg)
{
	if(arg == NULL) return 0;
	free(arg);
	return 0;
}


static int bits_to_bytes(const bit_t *bits, size_t nbits, uint8_t *bytes, size_t max_bytes, bool lsb_first)
{
	size_t nbytes = nbits / 8;
	if(nbytes > max_bytes)
		nbytes = max_bytes;

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
		bytes[i] = byte;
	}
	return (int)nbytes;
}


static int decode(void *arg, const bit_t *bits, size_t nbits, uint8_t *decoded, size_t max_decoded_len, struct rx_metadata *metadata)
{
	struct basic_decoder *self = arg;
	if(self->l_fec != NULL) {
		/* Reed-Solomon decode */
		int n, ndec;
		n = bits_to_bytes(bits, nbits, self->buf, 255, self->conf.lsb_first);

		ndec = n - 32; // decoded message length
		if(ndec < 0)
			return -1; // not enough data
		if((size_t)ndec > max_decoded_len)
			return -1; // too small output buffer, can't decode

		/* Hmm, liquid-dsp doesn't return whether decode succeeded or not :(
		 * TODO: switch to libcorrect */
		fec_decode(self->l_fec, ndec, self->buf, decoded);

		/* Re-encode it in order to calculate BER.
		 * If the number of differing octets exceeds 16, assume decoding
		 * has failed, since 32 parity bytes can't correct more than that.
		 * Not sure if this is a very reliable way to detect a failure. */
		fec_encode(self->l_fec, ndec, decoded, self->buf2);

		int i, bit_errors = 0, octet_errors = 0;
		for(i=0; i<n; i++) {
			uint8_t v1 = self->buf[i], v2 = self->buf2[i];
			bit_errors += __builtin_popcount(v1 ^ v2);
			if(v1 != v2) octet_errors++;
		}
		metadata->ber = (float)bit_errors / (float)(n*8);
		metadata->oer = (float)octet_errors / (float)n;

		if(octet_errors > 16)
			return -1;
		return ndec;
	} else {
		return bits_to_bytes(bits, nbits, decoded, max_decoded_len, self->conf.lsb_first);
	}
}


const struct decoder_code basic_decoder_code = { init, destroy, decode };

