#include <liquid/liquid.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include "burstfsk.h"

const float pi2f = 6.2831853f;

#define DMI_N_MAX 5
typedef struct {
	unsigned dm_sps;
	float dm_fs, inresamp_ratio;

	// preamble detector parameters
	unsigned pd_fft_len, pd_win_len, pd_win_period;
	unsigned pd_power_bin1, pd_power_bin2;
	unsigned pd_peak_bin1, pd_peak_bin2;
	unsigned pd_sideband_bins;
	float pd_snr_thres;

	// preamble detector state
	unsigned pd_win_c;
	sample_t *pd_fft_in, *pd_fft_out;
	float pd_prev_peakc, pd_prev_peak_bin, pd_prev_snr;
	sample_t pd_prev_sb;

	// parameters and constant data used by demodulators
	const sample_t *corr_taps;
	unsigned corr_len, corr_num;

	// demodulator instances
	unsigned dmi_n, packet_num;
	void *dmi_array[DMI_N_MAX];

	// liquid-dsp objects (prefixed with l_)
	nco_crcf l_ddc_nco;
	msresamp_crcf l_inresamp;
	windowcf l_pd_win;
	fftplan l_pd_fft;
} burstfsk_state_t;

static inline unsigned next_power_of_2(unsigned v) {
	unsigned r;
	for(r=1; r<v; r*=2);
	return r;
}

static inline int clip_int(int minv, int maxv, int v) {
	if(v < minv) return minv;
	if(v > maxv) return maxv;
	return v;
}

void fskdemod_init(void *state1);
void *fskdemod_init_instance(void *state1, int id);
void fskdemod_start(void *state, float freqoffset);
void fskdemod_execute(void *state, sample_t *signal, unsigned nsamples);
int fskdemod_is_free(void *state);
static inline unsigned freq_to_pd_bin(burstfsk_state_t *st, int clip_margin, float f) {
	unsigned fftlen = st->pd_fft_len;
	return clip_int(clip_margin, fftlen-1-clip_margin,
	round((0.5f + f / st->dm_fs) * fftlen));
}

void *burstfsk_init(burstfsk_config_t *conf) {
	burstfsk_state_t *st;
	st = malloc(sizeof(burstfsk_state_t));
	memset(st, 0, sizeof(burstfsk_state_t));

	st->dm_sps         = 4;
	st->dm_fs          = conf->symbol_rate * st->dm_sps;
	st->inresamp_ratio = st->dm_fs / conf->input_sample_rate;
	st->l_inresamp     = msresamp_crcf_create(st->inresamp_ratio, 60);

	st->l_ddc_nco = nco_crcf_create(LIQUID_NCO);
	nco_crcf_set_frequency(st->l_ddc_nco, -pi2f*conf->center_freq/conf->input_sample_rate);

	st->pd_win_len     = st->dm_sps * conf->pd_window_symbols;
	st->pd_fft_len     = next_power_of_2(2 * st->pd_win_len);
	st->pd_win_period  = st->pd_win_len/4;

	st->pd_sideband_bins = st->pd_fft_len / st->dm_sps / 2;

	st->pd_power_bin1  = freq_to_pd_bin(st, 0,
	                     -0.5f*conf->pd_power_bandwidth);
	st->pd_power_bin2  = freq_to_pd_bin(st, 0,
	                      0.5f*conf->pd_power_bandwidth);
	st->pd_peak_bin1   = freq_to_pd_bin(st, st->pd_sideband_bins,
	                     -conf->pd_max_freq_offset);
	st->pd_peak_bin2   = freq_to_pd_bin(st, st->pd_sideband_bins,
	                      conf->pd_max_freq_offset);

	st->pd_snr_thres   = 5e-2; // TODO?

	//printf("%f  %u %u  %u %u  %u %u\n", (double)st->dm_fs, st->pd_win_len, st->pd_fft_len, st->pd_power_bin1, st->pd_power_bin2, st->pd_peak_bin1, st->pd_peak_bin2);

	st->l_pd_win = windowcf_create(st->pd_win_len);

	st->pd_fft_in  = malloc(st->pd_fft_len * sizeof(sample_t));
	st->pd_fft_out = malloc(st->pd_fft_len * sizeof(sample_t));
	st->l_pd_fft = fft_create_plan(
		st->pd_fft_len, st->pd_fft_in, st->pd_fft_out,
		LIQUID_FFT_FORWARD, 0);

	fskdemod_init(st);

	st->dmi_n = DMI_N_MAX;
	unsigned i;
	for(i=0; i<st->dmi_n; i++) {
		st->dmi_array[i] = fskdemod_init_instance(st, i);
	}
	return st;
}


static void burstfsk_1_execute(void *state, sample_t *samp, unsigned nsamp);

#define DMSAMP_MAX 256
void burstfsk_execute(void *state, sample_t *insamp, unsigned n_insamp) {
	/* Input is samples from receiver.
	 * This function frequency shifts and resamples the signal
	 * and gives it to burstfsk_1_execute. */
	burstfsk_state_t *st = (burstfsk_state_t*)state;
	sample_t dmsamp[DMSAMP_MAX];
	unsigned max_insamp = (DMSAMP_MAX-1) / st->inresamp_ratio;
	while(n_insamp > 0) {
		unsigned ndms = 0, i;
		unsigned n_insamp2 = n_insamp < max_insamp ? n_insamp : max_insamp;
		sample_t freqshifted[n_insamp2];
		for(i=0; i<n_insamp2; i++) {
			sample_t oscout=0;
			nco_crcf_step(st->l_ddc_nco);
			nco_crcf_cexpf(st->l_ddc_nco, &oscout);
			freqshifted[i] = insamp[i] * oscout;
		}
		msresamp_crcf_execute(st->l_inresamp, freqshifted, n_insamp2, dmsamp, &ndms);
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
		unsigned i;
		for(i=0; i<st->dmi_n; i++)
			fskdemod_execute(st->dmi_array[i], samp+samp_i, 1);
		windowcf_push(st->l_pd_win, samp[samp_i]);
		if(++st->pd_win_c >= st->pd_win_period) {
			st->pd_win_c = 0;
			windowcf_read(st->l_pd_win, &win);
			burstfsk_2_execute(state, win);
		}
	}
}


static inline float mag2(sample_t v) {
	return crealf(v)*crealf(v) + cimagf(v)*cimagf(v);
}

static inline void fftshift(sample_t *v, unsigned n) {
	unsigned i, n2;
	n2 = n/2;
	for(i=0; i<n2; i++) {
		sample_t temp = v[i + n2];
		v[i + n2] = v[i];
		v[i] = temp;
	}
}

static inline float sum_floats(
float *v, unsigned firstindex, unsigned lastindex) {
	unsigned i;
	float s=0;
	for(i=firstindex; i<=lastindex; i++)
		s += v[i];
	return s;
}

static inline unsigned float_peakpos(
float *v, unsigned firstindex, unsigned lastindex) {
	unsigned i, pp=firstindex;
	float pv=0;
	for(i=firstindex; i<=lastindex; i++) {
		if(v[i] > pv) {
			pv = v[i];
			pp = i;
		}
	}
	return pp;
}

static inline float angle_to_positive(float v) {
	if(v < 0) return v + pi2f;
	else return v;
}

static void burstfsk_2_execute(void *state, sample_t *win) {
	/* This function gets successive windows of resampled signal
	 * and does preamble detection. */
	burstfsk_state_t *st = (burstfsk_state_t*)state;
	sample_t *ffti = st->pd_fft_in,  *ffto = st->pd_fft_out;
	unsigned winn = st->pd_win_len,  fftn = st->pd_fft_len;
	unsigned i;
	float fftm[fftn];
	for(i=0; i<winn; i++) ffti[i] = win[i];
	for(; i<fftn; i++) ffti[i] = 0;
	fft_execute(st->l_pd_fft);
	fftshift(ffto, fftn);

	for(i=0; i<fftn; i++) fftm[i] = mag2(ffto[i]);

	float power, peakl, peakc, peakr, snr; unsigned peakp;
	power = sum_floats(fftm, st->pd_power_bin1, st->pd_power_bin2);
	peakp = float_peakpos(fftm, st->pd_peak_bin1, st->pd_peak_bin2);
	peakl = fftm[peakp-1];
	peakc = fftm[peakp];
	peakr = fftm[peakp+1];
	snr = peakc / power;

	sample_t sideband_phase =
	 (ffto[peakp + st->pd_sideband_bins]-
	  ffto[peakp - st->pd_sideband_bins])*
	  conjf(ffto[peakp]);

#if 0
	printf("%E  %c %5u %E  %E %E %E  %E %E\n", (double)power, snr > st->pd_snr_thres ? '!' : ' ', peakp, (double)snr, (double)peakl, (double)peakc, (double)peakr,

		/*(double)fftm[peakp - st->pd_sideband_bins],
		(double)fftm[peakp + st->pd_sideband_bins]*/
		(double)cabsf(sideband_phase),(double)cargf(sideband_phase)
	);
#endif

	if(st->pd_prev_snr > st->pd_snr_thres && peakc < st->pd_prev_peakc) {
		// peak was highest in previous window: start demodulating
		float freqoffset, timingsamples;
		const float timing_samples_offset = 0; // TODO find magic constant
		freqoffset = st->pd_prev_peak_bin * (pi2f / fftn);
		timingsamples = angle_to_positive(cargf(st->pd_prev_sb))
		              * (2.0f * st->dm_sps / pi2f)
				    + timing_samples_offset;
		printf("detect: %5f %5f\n", (double)freqoffset, (double)timingsamples);
		void *dmi_p = NULL;
#if 0
		unsigned i;
		for(i=0; i<st->dmi_n; i++) {
			void *dmi_p_l = st->dmi_array[i];
			if(fskdemod_is_free(dmi_p_l)) {
				dmi_p = dmi_p_l;
				break;
			}
		}
#else
		dmi_p = st->dmi_array[(++st->packet_num) % st->dmi_n];
#endif
		if(dmi_p != NULL) {
			int skipsamples = (int)timingsamples;
			fskdemod_start(dmi_p, freqoffset);
			/* Feed it an almost complete window of samples
			 * but skip samples from start to align symbol timing. */
			assert(skipsamples >= 0 && skipsamples < (int)winn);
			fskdemod_execute(dmi_p, win + skipsamples, winn - skipsamples);
		}
	}
	if(peakc > st->pd_prev_peakc) {
		st->pd_prev_peak_bin =
		(float)((int)peakp - (int)fftn/2) +
		(peakr - peakl) / (peakl + peakc + peakr);
	st->pd_prev_sb = sideband_phase;
	st->pd_prev_snr = snr;
	} else {
		st->pd_prev_snr = 0;
	}
	st->pd_prev_peakc = peakc;
}


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
} demodinstance_state_t;


/* TODO */
void *deframer_init();
void deframer_reset();
void deframer_bit();

void *fskdemod_init_instance(void *state1, int id) {
	burstfsk_state_t *st1 = state1;
	demodinstance_state_t *st2;
	st2 = malloc(sizeof(demodinstance_state_t));
	memset(st2, 0, sizeof(demodinstance_state_t));
	st2->id = id;

	st2->sps = st1->dm_sps;
	st2->corr_len = st1->corr_len;
	st2->corr_num = st1->corr_num;

	st2->l_nco = nco_crcf_create(LIQUID_NCO);
	st2->l_win = windowcf_create(st2->corr_len);
	unsigned i;
	for(i=0; i<st2->corr_num; i++)
		st2->correlators[i] = dotprod_cccf_create(
		 (sample_t*)st1->corr_taps + st2->corr_len*i, st2->corr_len);

	st2->bit_fd = 3 + id;

	/* TODO: find out neat way to change deframer and have the choice as parameter */
	st2->out_arg = deframer_init();
	st2->out_reset = deframer_reset;
	st2->out_bit = deframer_bit;

	return st2;
}


void fskdemod_start(void *state, float freqoffset) {
	demodinstance_state_t *st = state;
	st->running = 1;
	st->symphase = 0;
	//st->freqoffset = freqoffset;
	nco_crcf_set_phase(st->l_nco, 0);
	nco_crcf_set_frequency(st->l_nco, -freqoffset);
	st->out_reset(st->out_arg);
}


void fskdemod_execute(void *state, sample_t *signal, unsigned nsamples) {
	demodinstance_state_t *st = state;
	unsigned samp_i;
	if(!st->running) return;
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
}


int fskdemod_is_free(void *state) {
	demodinstance_state_t *st = state;
	return !st->running;
}
