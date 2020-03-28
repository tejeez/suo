#include "preamble_acq.h"
#include <string.h>
#include <assert.h>
//#include <stdio.h>
#include <liquid/liquid.h>

static const float pi2f = 6.2831853f;

#define MAX_DEMODULATORS 5
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

	// liquid-dsp objects (prefixed with l_)
	nco_crcf l_ddc_nco;
	msresamp_crcf l_inresamp;
	windowcf l_pd_win;
	fftplan l_pd_fft;

	// Callbacks to demodulator instances
	unsigned demod_n;
	struct demod_code demod;
	void *demod_arg[MAX_DEMODULATORS];
	unsigned packet_num;//TODO cleanup
} preamble_acq_state_t;

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

static inline unsigned freq_to_pd_bin(preamble_acq_state_t *st, int clip_margin, float f) {
	unsigned fftlen = st->pd_fft_len;
	return clip_int(clip_margin, fftlen-1-clip_margin,
	round((0.5f + f / st->dm_fs) * fftlen));
}

void *preamble_acq_init(const struct preamble_acq_conf *conf) {
	preamble_acq_state_t *st;
	st = malloc(sizeof(preamble_acq_state_t));
	memset(st, 0, sizeof(preamble_acq_state_t));

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

	st->dmi_n = DMI_N_MAX;

#if 0
	/* TODO more configurable etc */
	unsigned i;
	fskdemod_conf_t fskdemod_conf;
	fskdemod_conf_default(&fskdemod_conf);
	for(i=0; i<st->dmi_n; i++) {
		fskdemod_conf.id = i;
		fskdemod_conf.sps = st->dm_sps;
		st->dmi_array[i] = fskdemod_init(&fskdemod_conf);
	}
#endif
	return st;
}


static void preamble_acq_1_execute(void *state, sample_t *samp, unsigned nsamp);

#define DMSAMP_MAX 256
void preamble_acq_execute(void *state, sample_t *insamp, unsigned n_insamp) {
	/* Input is samples from receiver.
	 * This function frequency shifts and resamples the signal
	 * and gives it to preamble_acq_1_execute. */
	preamble_acq_state_t *st = (preamble_acq_state_t*)state;
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
		preamble_acq_1_execute(state, dmsamp, ndms);
		insamp += n_insamp2;
		n_insamp -= n_insamp2;
	}
}

static void preamble_acq_2_execute(void *state, sample_t *win);

static void preamble_acq_1_execute(void *state, sample_t *samp, unsigned nsamp) {
	preamble_acq_state_t *st = (preamble_acq_state_t*)state;
	unsigned samp_i;
	/* Split signal into windows, feed the windowed samples to preamble detection FFT
	 * and then feed the first samples from each window to demodulator instances */
	for(samp_i=0; samp_i<nsamp; samp_i++) {
		sample_t *win;
		windowcf_push(st->l_pd_win, samp[samp_i]);
		if(++st->pd_win_c >= st->pd_win_period) {
			st->pd_win_c = 0;
			windowcf_read(st->l_pd_win, &win);
			preamble_acq_2_execute(state, win);
			
			unsigned i;
			for(i=0; i<st->demod_n; i++)
				st->demod.execute(st->demod_arg[i], win, st->pd_win_period);
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

static void preamble_acq_2_execute(void *state, sample_t *win) {
	/* This function gets successive windows of resampled signal
	 * and does preamble detection. */
	preamble_acq_state_t *st = (preamble_acq_state_t*)state;
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

const struct acq_code preamble_acq_code = { preamble_acq_init, preamble_acq_execute };

