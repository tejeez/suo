#include <liquid/liquid.h>
#include <stdlib.h>
#include <string.h>
#include "burstfsk.h"

typedef struct {
	unsigned dm_sps, acq_win_c;
	unsigned acq_fft_len, acq_win_len, acq_win_every_n_samples;
	float dm_fs, inresamp_ratio;
	sample_t *acq_fft_in, *acq_fft_out;
	// liquid-dsp objects prefixed with l_
	msresamp_crcf l_inresamp;
	windowcf l_acq_win;
	fftplan l_acq_fft;
} burstfsk_state_t;


void *burstfsk_init(burstfsk_config_t *conf) {
	burstfsk_state_t *st;
	st = malloc(sizeof(burstfsk_state_t));
	memset(st, 0, sizeof(burstfsk_state_t));

	st->dm_sps = 4;
	st->dm_fs = conf->symbol_rate * st->dm_sps;
	st->inresamp_ratio = st->dm_fs / conf->input_sample_rate;
	st->l_inresamp = msresamp_crcf_create(st->inresamp_ratio, 60);

	st->acq_fft_len = 512;
	st->acq_win_len = 256;
	st->acq_win_every_n_samples = 64;
	st->l_acq_win = windowcf_create(st->acq_win_len);

	st->acq_fft_in = (sample_t*)
		malloc(st->acq_fft_len * sizeof(sample_t));
	st->acq_fft_out = (sample_t*)
		malloc(st->acq_fft_len * sizeof(sample_t));
	st->l_acq_fft = fft_create_plan(
		st->acq_fft_len, st->acq_fft_in, st->acq_fft_out,
		LIQUID_FFT_FORWARD, 0);

	return st;
}


static void burstfsk_1_execute(void *state, sample_t *samp, unsigned nsamp);

#define DMSAMP_MAX 256
void burstfsk_execute(void *state, sample_t *insamp, unsigned n_insamp) {
	/* Input is samples from receiver.
	 * This function resamples the signal
	 * and gives it to burstfsk_1_execute.
	 * TODO: Add frequency conversion before resampling. */
	burstfsk_state_t *st = (burstfsk_state_t*)state;
	sample_t dmsamp[DMSAMP_MAX];
	unsigned max_insamp = (DMSAMP_MAX-1) / st->inresamp_ratio;
	while(n_insamp > 0) {
		unsigned ndms = 0;
		unsigned n_insamp2 = n_insamp < max_insamp ? n_insamp : max_insamp;
		msresamp_crcf_execute(st->l_inresamp, insamp, n_insamp2, dmsamp, &ndms);
		burstfsk_1_execute(state, dmsamp, ndms);
		insamp += n_insamp2;
		n_insamp -= n_insamp2;
	}
}

static void burstfsk_2_execute(void *state, sample_t *win);

static void burstfsk_1_execute(void *state, sample_t *samp, unsigned nsamp) {
	burstfsk_state_t *st = (burstfsk_state_t*)state;
	unsigned samp_i;
	for(samp_i=0; samp_i<nsamp; samp_i++) {
		sample_t *win;
		windowcf_push(st->l_acq_win, samp[samp_i]);
		if(++st->acq_win_c >= st->acq_win_every_n_samples) {
			st->acq_win_c = 0;
			windowcf_read(st->l_acq_win, &win);
			burstfsk_2_execute(state, win);
		}
	}
}


static void burstfsk_2_execute(void *state, sample_t *win) {
	/* This function gets successive windows of resampled signal
	 * and does preamble detection.
	 * TODO: preamble detection... */
	burstfsk_state_t *st = (burstfsk_state_t*)state;
	sample_t *ffti = st->acq_fft_in, *ffto = st->acq_fft_out;
	unsigned i, n, fftn;
	n = st->acq_win_len;
	fftn = st->acq_fft_len;
	for(i=0; i<n; i++) ffti[i] = win[i];
	for(; i<fftn; i++) ffti[i] = 0;
	fft_execute(st->l_acq_fft);
}
