#include "efrk7_decoder.h"
#include <string.h>
#include <assert.h>
#include <liquid/liquid.h>

struct efrk7 {
	fec fecdecoder;
};


const struct efrk7_decoder_conf efrk7_decoder_defaults = {
	/* Configuration TODO */
};


static void *efrk7_init(const void *confv) {
	//const struct efrk7_decoder_conf *conf = confv;
	(void)confv;
	struct efrk7 *s = malloc(sizeof(struct efrk7));
	s->fecdecoder = fec_create(LIQUID_FEC_CONV_V27, NULL);
	return s;
}


static uint8_t swap_byte_bit_order(uint8_t v) {
	return
	((0x80&v) >> 7) |
	((0x40&v) >> 5) |
	((0x20&v) >> 3) |
	((0x10&v) >> 1) |
	((0x08&v) << 1) |
	((0x04&v) << 3) |
	((0x02&v) << 5) |
	((0x01&v) << 7);
}


#define PKT_BITS 304
#define MSG_DEC_BYTES 18
static int efrk7_decode(void *arg, bit_t *pktb, size_t len, uint8_t *decoded, size_t max_decoded_len)
{
	struct efrk7 *s = arg;
	if(len != PKT_BITS || max_decoded_len < MSG_DEC_BYTES) return -1;
	int i;
	const int msg_rec_bytes = (PKT_BITS+7)/8;
	uint8_t msg_rec[msg_rec_bytes]/*, msg_dec[MSG_DEC_BYTES]*/;
	uint8_t *msg_dec = decoded;

	memset(msg_rec, 0, msg_rec_bytes);
	// deinterleave
	for(i = 0; i < PKT_BITS; i++) {
		int oi;
		oi = (i&(~15)) | (15 - ((i&15)/4) - ((i&3)*4));
		oi ^= 1;
		assert(oi/8 < msg_rec_bytes);
		if(pktb[i]) msg_rec[oi/8] |= 0x80 >> (oi&7);
	}
	fec_decode(s->fecdecoder, MSG_DEC_BYTES, msg_rec, msg_dec);
	for(i = 0; i < MSG_DEC_BYTES; i++)
		msg_dec[i] = swap_byte_bit_order(msg_dec[i]);
	//unsigned a = crc_generate_key(LIQUID_CRC_16, msg_dec, 16);
	//printf("crc=%02x ", a);

	return MSG_DEC_BYTES;
}


const struct decoder_code efrk7_decoder_code = { efrk7_init, efrk7_decode };

