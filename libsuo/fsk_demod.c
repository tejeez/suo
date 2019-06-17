#include "fsk_demod.h"
#include <string.h>
#include <assert.h>
//#include <stdio.h>
#include <liquid/liquid.h>

/*static const float pi2f = 6.2831853f;*/

#define MAX_CORRELATORS 8
struct fskdemod_state {
	/* configuration */
	unsigned id, sps;
	unsigned corr_len, corr_num;
	const sample_t *corr_taps;

	/* state */
	unsigned running, symphase, nbitsdone;
	//float freqoffset;

	/* liquid-dsp objects */
	dotprod_cccf correlators[MAX_CORRELATORS];
	nco_crcf l_nco;
	windowcf l_win;

	/* callbacks */
	void *out_arg;
	void (*out_reset)(void *arg);
	void (*out_bit)(void *arg, int bit);
	struct deframer_code deframer;
	void *deframer_arg;
};


// for now it's a fixed correlator bank generated in python by
// ','.join(map(lambda x: '%.4ff%+.4ff*I' % (x.real, x.imag), oh2eat.gfsk_bank(4, 0.7, 1.0, 0, 3, 2)))
const sample_t fixed_correlators[] = {
-0.6704f-0.1062f*I,-0.9497f-0.1430f*I,-0.9921f+0.1251f*I,-0.7853f+0.6191f*I,-0.3461f+0.9382f*I,0.1951f+0.9808f*I,0.6788f+0.7343f*I,0.9625f+0.2714f*I,0.9625f-0.2714f*I,0.6788f-0.7343f*I,0.1951f-0.9808f*I,-0.3461f-0.9382f*I,-0.7853f-0.6191f*I,-0.9921f-0.1251f*I,-0.9497f+0.1430f*I,-0.6704f+0.1062f*I,-0.6704f-0.1062f*I,-0.9497f-0.1430f*I,-0.9921f+0.1251f*I,-0.7853f+0.6191f*I,-0.3461f+0.9382f*I,0.1951f+0.9808f*I,0.6788f+0.7343f*I,0.9625f+0.2714f*I,0.9625f-0.2714f*I,0.6899f-0.7239f*I,0.6899f-0.7239f*I,0.9625f-0.2714f*I,0.9625f+0.2714f*I,0.6844f+0.7291f*I,0.4425f+0.8523f*I,0.3082f+0.6048f*I,0.3082f+0.6048f*I,0.4425f+0.8523f*I,0.6844f+0.7291f*I,0.9625f+0.2714f*I,0.9625f-0.2714f*I,0.6899f-0.7239f*I,0.6899f-0.7239f*I,0.9625f-0.2714f*I,0.9625f+0.2714f*I,0.6899f+0.7239f*I,0.6899f+0.7239f*I,0.9625f+0.2714f*I,0.9625f-0.2714f*I,0.6844f-0.7291f*I,0.4425f-0.8523f*I,0.3082f-0.6048f*I,0.3082f+0.6048f*I,0.4425f+0.8523f*I,0.6844f+0.7291f*I,0.9625f+0.2714f*I,0.9625f-0.2714f*I,0.6899f-0.7239f*I,0.6899f-0.7239f*I,0.9625f-0.2714f*I,0.9625f+0.2714f*I,0.6788f+0.7343f*I,0.1951f+0.9808f*I,-0.3461f+0.9382f*I,-0.7853f+0.6191f*I,-0.9921f+0.1251f*I,-0.9497f-0.1430f*I,-0.6704f-0.1062f*I,0.3082f-0.6048f*I,0.4425f-0.8523f*I,0.6844f-0.7291f*I,0.9625f-0.2714f*I,0.9625f+0.2714f*I,0.6899f+0.7239f*I,0.6899f+0.7239f*I,0.9625f+0.2714f*I,0.9625f-0.2714f*I,0.6788f-0.7343f*I,0.1951f-0.9808f*I,-0.3461f-0.9382f*I,-0.7853f-0.6191f*I,-0.9921f-0.1251f*I,-0.9497f+0.1430f*I,-0.6704f+0.1062f*I,0.3082f-0.6048f*I,0.4425f-0.8523f*I,0.6844f-0.7291f*I,0.9625f-0.2714f*I,0.9625f+0.2714f*I,0.6899f+0.7239f*I,0.6899f+0.7239f*I,0.9625f+0.2714f*I,0.9625f-0.2714f*I,0.6899f-0.7239f*I,0.6899f-0.7239f*I,0.9625f-0.2714f*I,0.9625f+0.2714f*I,0.6844f+0.7291f*I,0.4425f+0.8523f*I,0.3082f+0.6048f*I,-0.6704f+0.1062f*I,-0.9497f+0.1430f*I,-0.9921f-0.1251f*I,-0.7853f-0.6191f*I,-0.3461f-0.9382f*I,0.1951f-0.9808f*I,0.6788f-0.7343f*I,0.9625f-0.2714f*I,0.9625f+0.2714f*I,0.6899f+0.7239f*I,0.6899f+0.7239f*I,0.9625f+0.2714f*I,0.9625f-0.2714f*I,0.6844f-0.7291f*I,0.4425f-0.8523f*I,0.3082f-0.6048f*I,-0.6704f+0.1062f*I,-0.9497f+0.1430f*I,-0.9921f-0.1251f*I,-0.7853f-0.6191f*I,-0.3461f-0.9382f*I,0.1951f-0.9808f*I,0.6788f-0.7343f*I,0.9625f-0.2714f*I,0.9625f+0.2714f*I,0.6788f+0.7343f*I,0.1951f+0.9808f*I,-0.3461f+0.9382f*I,-0.7853f+0.6191f*I,-0.9921f+0.1251f*I,-0.9497f-0.1430f*I,-0.6704f-0.1062f*I
};


void fskdemod_conf_default(struct fsk_demod_conf *c) {
	c->sps = 0;//TODO
	c->id = 0;
	c->corr_taps = fixed_correlators;
	c->corr_len = 4*4;
	c->corr_num = 8;
}


void fskdemod_generate_correlators() {
#if 0 // TODO
	cpfskmod mod;
	int sps = st->dm_sps;
	mod = cpfskmod_create(1, 0.7f, sps, 1, 1.0f, LIQUID_CPFSK_GMSK);
	float modulated[sps];
	cpfskmod_modulate(mod, 0, modulated);
	for(symi=0; symi<5; symi++) {
		cpfskmod_modulate(mod, sym, modulated);
	}
	cpfskmod_destroy(mod);
#endif
}




static inline float mag2(sample_t v) {
	return crealf(v)*crealf(v) + cimagf(v)*cimagf(v);
}


/* TODO */
void *deframer_init();
void deframer_reset();
void deframer_bit();
void *fskdemod_init(const void *conf) {
	const struct fsk_demod_conf *c = conf;
	struct fskdemod_state *st2;
	st2 = malloc(sizeof(struct fskdemod_state));
	memset(st2, 0, sizeof(struct fskdemod_state));

	st2->id = c->id;
	st2->sps = c->sps;
	st2->corr_len = c->corr_len;
	st2->corr_num = c->corr_num;
	st2->corr_taps = c->corr_taps;

	st2->l_nco = nco_crcf_create(LIQUID_NCO);
	st2->l_win = windowcf_create(st2->corr_len);
	unsigned i;
	for(i=0; i<st2->corr_num; i++)
		st2->correlators[i] = dotprod_cccf_create(
		 (sample_t*)st2->corr_taps + st2->corr_len*i, st2->corr_len);

	/* TODO: find out neat way to change deframer and have the choice as parameter */
	st2->out_arg = deframer_init();
	st2->out_reset = deframer_reset;
	st2->out_bit = deframer_bit;

	return st2;
}


int fskdemod_reset(void *state, float freqoffset) {
	struct fskdemod_state *st = state;
	st->running = 1;
	st->symphase = 0;
	//st->freqoffset = freqoffset;
	nco_crcf_set_phase(st->l_nco, 0);
	nco_crcf_set_frequency(st->l_nco, -freqoffset);
	st->out_reset(st->out_arg);
	return 0;
}


int fskdemod_execute(void *state, sample_t *signal, size_t nsamples) {
	struct fskdemod_state *st = state;
	size_t samp_i;
	if(!st->running) return -1;
	unsigned corr_num = st->corr_num;
	for(samp_i=0; samp_i<nsamples; samp_i++) {
		sample_t oscout=0, o;
		nco_crcf_step(st->l_nco);
		nco_crcf_cexpf(st->l_nco, &oscout);
		windowcf_push(st->l_win, o = signal[samp_i] * oscout);
		//write(3+st->id, &o, sizeof(sample_t)); // debug
		if(++st->symphase >= st->sps) {
			st->symphase = 0;
			sample_t *win;
			windowcf_read(st->l_win, &win);
			unsigned i, max_i = 0;
			float max_m = 0;
			for(i=0; i<corr_num; i++) {
				sample_t r=0;
				dotprod_cccf_execute(st->correlators[i], win, &r);
				float m = mag2(r);
				if(m > max_m) { max_m = m; max_i = i; }
			}
			//printf("%d %f %u\n", st->id, (double)max_m, max_i&2);
			st->out_bit(st->out_arg, (max_i&2) ? 0 : 1);
			if((++st->nbitsdone) >= 800) st->running = 0;
		}

	}
	return 0;
}


int fskdemod_is_free(void *state) {
	struct fskdemod_state *st = state;
	return !st->running;
}


const struct demod_code fsk_demod_code = { fskdemod_init, fskdemod_execute, fskdemod_reset };

#if 0
/// asdf

// for now it's a fixed correlator bank generated in python by
// ','.join(map(lambda x: '%.4ff%+.4ff*I' % (x.real, x.imag), oh2eat.gfsk_bank(4, 0.7, 1.0, 0, 3, 2)))
const sample_t fixed_correlators[] = {
-0.6704f-0.1062f*I,-0.9497f-0.1430f*I,-0.9921f+0.1251f*I,-0.7853f+0.6191f*I,-0.3461f+0.9382f*I,0.1951f+0.9808f*I,0.6788f+0.7343f*I,0.9625f+0.2714f*I,0.9625f-0.2714f*I,0.6788f-0.7343f*I,0.1951f-0.9808f*I,-0.3461f-0.9382f*I,-0.7853f-0.6191f*I,-0.9921f-0.1251f*I,-0.9497f+0.1430f*I,-0.6704f+0.1062f*I,-0.6704f-0.1062f*I,-0.9497f-0.1430f*I,-0.9921f+0.1251f*I,-0.7853f+0.6191f*I,-0.3461f+0.9382f*I,0.1951f+0.9808f*I,0.6788f+0.7343f*I,0.9625f+0.2714f*I,0.9625f-0.2714f*I,0.6899f-0.7239f*I,0.6899f-0.7239f*I,0.9625f-0.2714f*I,0.9625f+0.2714f*I,0.6844f+0.7291f*I,0.4425f+0.8523f*I,0.3082f+0.6048f*I,0.3082f+0.6048f*I,0.4425f+0.8523f*I,0.6844f+0.7291f*I,0.9625f+0.2714f*I,0.9625f-0.2714f*I,0.6899f-0.7239f*I,0.6899f-0.7239f*I,0.9625f-0.2714f*I,0.9625f+0.2714f*I,0.6899f+0.7239f*I,0.6899f+0.7239f*I,0.9625f+0.2714f*I,0.9625f-0.2714f*I,0.6844f-0.7291f*I,0.4425f-0.8523f*I,0.3082f-0.6048f*I,0.3082f+0.6048f*I,0.4425f+0.8523f*I,0.6844f+0.7291f*I,0.9625f+0.2714f*I,0.9625f-0.2714f*I,0.6899f-0.7239f*I,0.6899f-0.7239f*I,0.9625f-0.2714f*I,0.9625f+0.2714f*I,0.6788f+0.7343f*I,0.1951f+0.9808f*I,-0.3461f+0.9382f*I,-0.7853f+0.6191f*I,-0.9921f+0.1251f*I,-0.9497f-0.1430f*I,-0.6704f-0.1062f*I,0.3082f-0.6048f*I,0.4425f-0.8523f*I,0.6844f-0.7291f*I,0.9625f-0.2714f*I,0.9625f+0.2714f*I,0.6899f+0.7239f*I,0.6899f+0.7239f*I,0.9625f+0.2714f*I,0.9625f-0.2714f*I,0.6788f-0.7343f*I,0.1951f-0.9808f*I,-0.3461f-0.9382f*I,-0.7853f-0.6191f*I,-0.9921f-0.1251f*I,-0.9497f+0.1430f*I,-0.6704f+0.1062f*I,0.3082f-0.6048f*I,0.4425f-0.8523f*I,0.6844f-0.7291f*I,0.9625f-0.2714f*I,0.9625f+0.2714f*I,0.6899f+0.7239f*I,0.6899f+0.7239f*I,0.9625f+0.2714f*I,0.9625f-0.2714f*I,0.6899f-0.7239f*I,0.6899f-0.7239f*I,0.9625f-0.2714f*I,0.9625f+0.2714f*I,0.6844f+0.7291f*I,0.4425f+0.8523f*I,0.3082f+0.6048f*I,-0.6704f+0.1062f*I,-0.9497f+0.1430f*I,-0.9921f-0.1251f*I,-0.7853f-0.6191f*I,-0.3461f-0.9382f*I,0.1951f-0.9808f*I,0.6788f-0.7343f*I,0.9625f-0.2714f*I,0.9625f+0.2714f*I,0.6899f+0.7239f*I,0.6899f+0.7239f*I,0.9625f+0.2714f*I,0.9625f-0.2714f*I,0.6844f-0.7291f*I,0.4425f-0.8523f*I,0.3082f-0.6048f*I,-0.6704f+0.1062f*I,-0.9497f+0.1430f*I,-0.9921f-0.1251f*I,-0.7853f-0.6191f*I,-0.3461f-0.9382f*I,0.1951f-0.9808f*I,0.6788f-0.7343f*I,0.9625f-0.2714f*I,0.9625f+0.2714f*I,0.6788f+0.7343f*I,0.1951f+0.9808f*I,-0.3461f+0.9382f*I,-0.7853f+0.6191f*I,-0.9921f+0.1251f*I,-0.9497f-0.1430f*I,-0.6704f-0.1062f*I
};

void fskdemod_init(void *state) {
	burstfsk_state_t *st = (burstfsk_state_t*)state;
	st->corr_taps = fixed_correlators;
	st->corr_len = 4*4;
	st->corr_num = 8;
#if 0 // TODO
	cpfskmod mod;
	int sps = st->dm_sps;
	mod = cpfskmod_create(1, 0.7f, sps, 1, 1.0f, LIQUID_CPFSK_GMSK);
	float modulated[sps];
	cpfskmod_modulate(mod, 0, modulated);
	for(symi=0; symi<5; symi++) {
		cpfskmod_modulate(mod, sym, modulated);
	}
	cpfskmod_destroy(mod);
#endif
}


#define MAX_CORRELATORS 8
typedef struct {
	unsigned id, running, symphase, nbitsdone;
	unsigned sps, corr_len, corr_num;
	dotprod_cccf correlators[MAX_CORRELATORS];
	int bit_fd;
	//float freqoffset;
	nco_crcf l_nco;
	windowcf l_win;

	// callbacks
	void *out_arg;
	void (*out_reset)(void *arg);
	void (*out_bit)(void *arg, int bit);
} struct fskdemod_state;


/* TODO */
void *deframer_init();
void deframer_reset();
void deframer_bit();

#endif
