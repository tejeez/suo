#include "basic_encoder.h"
#include "suo_macros.h"
#include <string.h>
#include <assert.h>
#include <liquid/liquid.h>

#define FEC_BUFSIZE 0x100

struct basic_encoder {
	struct basic_encoder_conf conf;
	fec_scheme l_scheme;
	fec l_fec;
	uint8_t buf[FEC_BUFSIZE];
};


const struct basic_encoder_conf basic_encoder_defaults = {
	.syncword = 0x36994625,
	.preamblelen = 64,
	.synclen = 32,
	.lsb_first = 0,
	.rs = 0,
	.bypass = 0
};


static int destroy(void *arg)
{
	struct basic_encoder *self = arg;
	fec_destroy(self->l_fec);
	free(arg);
	return 0;
}




static void *init(const void *confv)
{
	struct basic_encoder *self = malloc(sizeof(struct basic_encoder));
	memset(self, 0, sizeof(*self));
	self->conf = *(struct basic_encoder_conf*)confv;

	if(self->conf.rs)
		self->l_scheme = LIQUID_FEC_RS_M8;
	else
		self->l_scheme = LIQUID_FEC_NONE;

	self->l_fec = fec_create(self->l_scheme, NULL);

	if(self->l_fec == NULL) {
		destroy(self);
		return NULL;
	}

	return self;
}


static size_t bytes_to_bits(bit_t *bits, size_t nbits, const uint8_t *bytes, bool lsb_first)
{
	size_t i;
	for (i = 0; i < nbits; i++) {
		const size_t bytenum = i >> 3, bitnum = i & 7;
		if (lsb_first)
			bits[i] = (bytes[bytenum] & (1 << bitnum)) ? 1 : 0;
		else
			bits[i] = (bytes[bytenum] & (0x80 >> bitnum)) ? 1 : 0;
	}
	return nbits;
}


static size_t word_to_bits(bit_t *bits, size_t nbits, uint64_t word)
{
	size_t i;
	for (i = nbits; i-- > 0; ) {
		bits[i] = word & 1;
		word >>= 1;
	}
	return nbits;
}


static int encode(void *arg, const struct frame *in, struct frame *out, size_t maxlen)
{
	struct basic_encoder *self = arg;
	out->m = in->m; // Copy metadata
	if (self->conf.bypass) {
		size_t len = in->m.len;
		if (len > maxlen) len = maxlen;
		memcpy(out->data, in->data, len);
		out->m.len = len;
		return len;
	}
	size_t nbytes = in->m.len;

	size_t nenc = fec_get_enc_msg_length(self->l_scheme, nbytes);
	if (nenc > FEC_BUFSIZE)
		return -1; // too small intermediate buffer, can't encode

	size_t payload_nbits = nenc * 8;
	size_t total_nbits = self->conf.preamblelen + self->conf.synclen + payload_nbits;

	if (total_nbits > maxlen)
		return -1; // too small output buffer, can't encode

	uint8_t *bitp = out->data;
	size_t i;
	for (i = 0; i < self->conf.preamblelen; i++)
		*bitp++ = i & 1;

	bitp += word_to_bits(bitp, self->conf.synclen, self->conf.syncword);

	fec_encode(self->l_fec, nbytes, (unsigned char*)in->data, self->buf);
	bitp += bytes_to_bits(bitp, payload_nbits, self->buf, self->conf.lsb_first);

	assert(bitp == out->data + total_nbits);
	out->m.len = total_nbits;
	return total_nbits;
}


CONFIG_BEGIN(basic_encoder)
CONFIG_I(syncword)
CONFIG_I(synclen)
CONFIG_I(preamblelen)
CONFIG_I(lsb_first)
CONFIG_I(bypass)
CONFIG_I(rs)
CONFIG_END()

const struct encoder_code basic_encoder_code = { "basic_encoder", init, destroy, init_conf, set_conf, encode };
