#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
/*#include <stdio.h>
#include <liquid/liquid.h>*/

typedef uint64_t bw_t; // bit window

#define SYNCWORD_EFR32 0b010101011111011010001101
#define SYNCMASK_EFR32 0b111111111111111111111111


#define SYNC_WIN_START 40
#define SYNC_WIN_END 300
#define SYNC_THRESHOLD 40
#define PKT_BITS 304
#define BITS_STORED (SYNC_WIN_END+PKT_BITS+100)
typedef struct {
	bw_t syncword, syncmask, last_bits;
	int bit_num;
	int syncp, running, least_errs, least_errs_p;
	int syncerrs[SYNC_WIN_END];
	int pbits[BITS_STORED];

	/* callbacks */
	void *pkt_arg;
	void (*pkt_received)(void *arg, int *bits, int len);
} deframer_t;


void deframer_reset(deframer_t *s) {
	s->bit_num = 0;
	s->last_bits = 0;
	s->least_errs = 100;
	s->least_errs_p = 0;
	s->syncp = -1;
	s->running = 1;
}

/* TODO */
void *efrk7_init();
void efrk7_decode();

deframer_t *deframer_init() {
	deframer_t *s = malloc(sizeof(deframer_t));

	/* TODO find neat way to change packet handler */
	s->pkt_arg = efrk7_init();
	s->pkt_received = efrk7_decode;
	/* TODO: find neat way to pass also these other parameters from somewhere */
	s->syncword = SYNCWORD_EFR32;
	s->syncmask = SYNCMASK_EFR32;

	deframer_reset(s);
	s->running = 0;

	return s;
}


void deframer_bit(deframer_t *s, int b) {
	int errs;
	if(!s->running) return;
	//putchar('a'+b);
	if(s->bit_num < BITS_STORED) {
		s->pbits[s->bit_num] = b;
	} else {
		s->running = 0;
		return;
	}
	if(s->bit_num < SYNC_WIN_END) {
		s->last_bits = (s->last_bits<<1) | (b&1);
		errs = __builtin_popcountll((s->last_bits ^ s->syncword) & s->syncmask);
		s->syncerrs[s->bit_num] = errs;
		if(errs < s->least_errs) {
			s->least_errs = errs;
			s->least_errs_p = s->bit_num;
		}
	}
	if(s->least_errs <= SYNC_THRESHOLD && s->bit_num == s->least_errs_p+8)
		s->syncp = s->least_errs_p+1;
	if(s->syncp >= 0 && s->bit_num >= s->syncp + PKT_BITS) {
		s->pkt_received(s->pkt_arg, s->pbits + s->syncp/* -16*/, PKT_BITS);
		s->running = 0;
	}
	s->bit_num++;
}

