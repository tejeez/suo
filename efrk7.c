#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <liquid/liquid.h>

typedef struct {
	fec fecdecoder;
} efrk7_t;

efrk7_t *efrk7_init() {
	efrk7_t *s = malloc(sizeof(efrk7_t));
	s->fecdecoder = fec_create(LIQUID_FEC_CONV_V27, NULL);
	return s;
}

uint8_t swap_byte_bit_order(uint8_t v) {
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
void efrk7_decode(efrk7_t *s, int *pktb, int len) {
	int i;
	if(len != PKT_BITS) return;
	const int msg_rec_bytes = (PKT_BITS+7)/8;
	uint8_t msg_rec[msg_rec_bytes], msg_dec[MSG_DEC_BYTES];
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
	for(i = 0; i < MSG_DEC_BYTES; i++) {
		printf("%02x ", msg_dec[i]);
	}
	printf("\n");
}
