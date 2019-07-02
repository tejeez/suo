#include "simple_receiver.h"
#include <string.h>
#include <assert.h>
#include <stdio.h> // for debug prints only
#include <liquid/liquid.h>


#define FRAMELEN_MAX 0x900
#define OVERSAMPLING 4
//#define USE_LIQUID_SYMSYNC
#define USE_COMB_SYMSYNC

static const float pi2f = 6.283185307179586f;

struct simple_receiver {
	/* Configuration */
	struct simple_receiver_conf c;
	//float resamprate;
	unsigned resampint;
	uint64_t syncmask;
	float nco_1Hz, afc_speed;

	/* State */
	//uint32_t total_samples;
	uint64_t latest_bits;
	unsigned framepos, totalbits;
	bool receiving_frame;

	float freq_min, freq_max, freq_center, freq_adj; /* Used by AFC */
	float fm_level0, fm_level1; /* Used by AFC */
	float est_power; /* estimates for metadata */

	float ss_comb[OVERSAMPLING];

	float demod_prev;
	float ss_prev, ss_middle; /* samples for timing synchronizer */
	bool ss_prevd;
	uint32_t timing_phase;
	unsigned ss_p, ss_ps;

	/* liquid-dsp objects */
	nco_crcf l_nco;
	resamp_crcf l_resamp;
	//freqdem l_fdem;
	firfilt_cccf l_fir0, l_fir1;
	firfilt_rrrf l_eqfir;
	symsync_rrrf l_symsync;

	/* Callbacks */
	struct rx_output_code output;
	void *output_arg;

	/* Buffers */
	struct rx_metadata metadata;
	bit_t framebuf[FRAMELEN_MAX];
};


/* Fixed matched filters for 4x oversampling, h=0.6 and BT=0.5 */
#define FIXED_MF_LEN 12
static const float complex fixed_mf0[FIXED_MF_LEN] = {
-0.0422f-0.0581f*I,0.2284f+0.3138f*I,0.4524f+0.6064f*I,0.6247f+0.7153f*I,0.8126f+0.5769f*I,0.9750f+0.2220f*I,0.9750f-0.2220f*I,0.8126f-0.5769f*I,0.6247f-0.7153f*I,0.4524f-0.6064f*I,0.2284f-0.3138f*I,-0.0422f+0.0581f*I
};
static const float complex fixed_mf1[FIXED_MF_LEN] = {
-0.0422f+0.0581f*I,0.2284f-0.3138f*I,0.4524f-0.6064f*I,0.6247f-0.7153f*I,0.8126f-0.5769f*I,0.9750f-0.2220f*I,0.9750f+0.2220f*I,0.8126f+0.5769f*I,0.6247f+0.7153f*I,0.4524f+0.6064f*I,0.2284f+0.3138f*I,-0.0422f-0.0581f*I
};


static inline float mag2f(float complex v)
{
	return crealf(v)*crealf(v) + cimagf(v)*cimagf(v);
}

static inline float clampf(float v, float limit)
{
	if(v != v) return 0;
	if(v >=  limit) return  limit;
	if(v <= -limit) return -limit;
	return v;
}


static void *simple_receiver_init(const void *conf_v)
{
	struct simple_receiver_conf c;

	/* Initialize state and copy configuration */
	struct simple_receiver *self = malloc(sizeof(struct simple_receiver));
	memset(self, 0, sizeof(struct simple_receiver));
	c = self->c = *(const struct simple_receiver_conf *)conf_v;

	self->syncmask = (1ULL << c.synclen) - 1;
	self->framepos = c.framelen;

	/* Configure a resampler for a fixed oversampling ratio */
	float resamprate = c.symbolrate * OVERSAMPLING / c.samplerate;
	self->l_resamp = resamp_crcf_create(resamprate, 25, 0.4f / OVERSAMPLING, 60.0f, 32);
	/* Calculate maximum number of output samples after feeding one sample
	 * to the resampler. This is needed to allocate a big enough array. */
	self->resampint = ceilf(resamprate);

	/* NCO */
	/* Limit AFC range to half of symbol rate to keep it
	 * from wandering too far */
	self->nco_1Hz = pi2f / c.samplerate;
	self->freq_min = self->nco_1Hz * (c.centerfreq - 0.1f*c.symbolrate);
	self->freq_max = self->nco_1Hz * (c.centerfreq + 0.1f*c.symbolrate);
	self->freq_center = self->nco_1Hz * c.centerfreq;
	self->l_nco = nco_crcf_create(LIQUID_NCO);
	/* afc_speed is maximum adjustment of frequency per input sample.
	 * Convert Hz/sec into it. */
	float afc_hzsec = 0.01f * c.symbolrate * c.symbolrate;
	self->afc_speed = self->nco_1Hz * afc_hzsec / c.samplerate;

	nco_crcf_set_frequency(self->l_nco, self->freq_center);

	/* Other liquid-dsp objects */
	/* Matched filters for 0 and 1 */
	self->l_fir0 = firfilt_cccf_create((float complex*)fixed_mf0, FIXED_MF_LEN);
	self->l_fir1 = firfilt_cccf_create((float complex*)fixed_mf1, FIXED_MF_LEN);
	/*self->l_eqfir = firfilt_rrrf_create((float*)
		(const float[3]){ -.5f, 2.f, -.5f }, 3);*/
	self->l_eqfir = firfilt_rrrf_create((float*)
		(const float[5]){ -.5f, 0, 2.f, 0, -.5f }, 5);

	//self->l_fdem = freqdem_create(1.0f);

#ifdef USE_LIQUID_SYMSYNC
	self->l_symsync = symsync_rrrf_create_rnyquist(LIQUID_FIRFILT_GMSKRX, OVERSAMPLING, 7, 0.5f, 32);
	//self->l_symsync = symsync_rrrf_create_rnyquist(LIQUID_FIRFILT_RRC, OVERSAMPLING, 7, 1.0f, 32);
	symsync_rrrf_set_lf_bw(self->l_symsync, 0.0001f);
#endif

	return self;
}


static int simple_receiver_destroy(void *arg)
{
	/* TODO (low priority since memory gets freed in the end anyway) */
	(void)arg;
	return 0;
}


static void simple_deframer_execute(struct simple_receiver *self, unsigned bit)
{
	unsigned framepos = self->framepos;
	const unsigned framelen = self->c.framelen;
	bool receiving_frame = self->receiving_frame;

	if(framepos < framelen) {
		self->framebuf[framepos] = bit;
		framepos++;
		if(framepos == framelen) {
			self->output.frame(self->output_arg, self->framebuf, framelen, &self->metadata);
			receiving_frame = 0;
			//printf("End:   %7.4f %7.4f %7.4f\n", (double)self->fm_dc,(double)self->fm_level0, (double)self->fm_level1);
			//symsync_rrrf_unlock(self->l_symsync);
		}
	} else {
		receiving_frame = 0;
	}

	/* Look for syncword */
	uint64_t latest_bits = self->latest_bits;
	latest_bits <<= 1;
	latest_bits |= bit;
	self->latest_bits = latest_bits;
	/* Don't look for new syncword inside a frame */
	if(!receiving_frame) {
		unsigned syncerrs = __builtin_popcountll((latest_bits & self->syncmask) ^ self->c.syncword);

		if(syncerrs <= 3) {
			/* Syncword found, start saving bits when next bit arrives */
			framepos = 0;
			receiving_frame = 1;
			//printf("Start: %7.4f %7.4f %7.4f\n", (double)self->fm_dc, (double)self->fm_level0, (double)self->fm_level1);

			/* Fill in some metadata at start of the frame */

			self->metadata.cfo = (nco_crcf_get_frequency(self->l_nco)
				- self->freq_center ) / self->nco_1Hz;
			self->metadata.rssi = 10.0f * log10f(self->est_power);

#if 0
			printf("_%d_", syncerrs);
#endif
			//symsync_rrrf_lock(self->l_symsync);
		}
	}

	self->receiving_frame = receiving_frame;
	self->framepos = framepos;

#if 0
	if((self->totalbits & 63) == 0) putchar('\n');
	putchar(bit?'x':'.');
#endif

	self->totalbits++;
}


static int simple_receiver_execute(void *arg, const sample_t *samples, size_t nsamp, timestamp_t timestamp)
{
	struct simple_receiver *self = arg;

	/* Copy some often used variables to local variables */
	float fm_level0 = self->fm_level0, fm_level1 = self->fm_level1, est_power = self->est_power;
	uint32_t timing_phase = self->timing_phase;

	/* Allocate small buffers from stack */
	sample_t samples2[self->resampint];

	self->metadata.timestamp = timestamp;
	/* TODO: increment timestamp in loop */

	//printf("%E ", (double)nco_crcf_get_frequency(self->l_nco) - (double)self->freq_center);

	size_t si;
	for(si = 0; si < nsamp; si++) {
		unsigned nsamp2 = 0, si2;
		sample_t s = samples[si];

		/* Downconvert and resample one input sample at a time */
		nco_crcf_adjust_frequency(self->l_nco, self->freq_adj);
		nco_crcf_step(self->l_nco);
		nco_crcf_mix_down(self->l_nco, s, &s);
		resamp_crcf_execute(self->l_resamp, s, samples2, &nsamp2);
		assert(nsamp2 <= self->resampint);

		/* Process output from the resampler one sample at a time */
		for(si2 = 0; si2 < nsamp2; si2++) {
			float demod1, demod = 0, synchronized = 0;
			unsigned nsynchronized = 0;

			sample_t s2 = samples2[si2];
			sample_t mf0out = 0, mf1out = 0;
			float power0, power1;

			/*   Demodulation
			 * ----------------
			 * Compare the output amplitude of two filters.
			 * The output seems to have quite strong ISI, so just
			 * feed it into some ad-hoc FIR "equalizer"... */
			firfilt_cccf_push(self->l_fir0, s2);
			firfilt_cccf_push(self->l_fir1, s2);

			firfilt_cccf_execute(self->l_fir0, &mf0out);
			firfilt_cccf_execute(self->l_fir1, &mf1out);
			power0 = mag2f(mf0out);
			power1 = mag2f(mf1out);

			demod1 = (power1 - power0) / (power1 + power0);
			est_power += (power1 + power0 - est_power) * 0.01f;

			firfilt_rrrf_push(self->l_eqfir, demod1);
			firfilt_rrrf_execute(self->l_eqfir, &demod);


			/*   AFC
			 ----------*/
			if(!self->receiving_frame) {
				float adjustment = demod * self->afc_speed;

				float freq_now = nco_crcf_get_frequency(self->l_nco);
				if(freq_now < self->freq_min && adjustment < 0)
					adjustment = 0;
				if(freq_now > self->freq_max && adjustment >= 0)
					adjustment = 0;
				self->freq_adj = adjustment;
			} else {
				/* Lock AFC during frame */
				self->freq_adj = 0;
			}


			/* Debugging outputs */
			int write(int, const void*, size_t);
			write(3, &demod, sizeof(float));
			float asdf = nco_crcf_get_frequency(self->l_nco);
			write(3, &asdf, sizeof(float));
			asdf = 0;
			

#ifndef USE_LIQUID_SYMSYNC
#ifndef USE_COMB_SYMSYNC
			uint32_t prev_timing_phase = timing_phase;
			timing_phase += 0x100000000ULL / OVERSAMPLING;

			if(timing_phase < prev_timing_phase) {
				synchronized = demod;
				nsynchronized = 1;
			}
			if(timing_phase      >= 0x80000000UL &&
			   prev_timing_phase <  0x80000000UL) {
				self->ss_middle = demod;
			}
#else
			/* Feed-forward timing synchronizer
			 * --------------------------------
			 * Feed a rectified demodulated signal into a comb filter.
			 * When the output of the comb filter peaks, take a symbol.
			 * When a frame is detected, keep timing free running
			 * for rest of the frame. */
			unsigned ss_p = self->ss_p;
			const float comb_prev = self->ss_comb[ss_p];
			const float comb_prev2
			= self->ss_comb[(ss_p+OVERSAMPLING-1) % OVERSAMPLING];
			ss_p = (ss_p+1) % OVERSAMPLING;
			float comb = self->ss_comb[ss_p];

			comb += (clampf(fabsf(demod), 1.0f) - comb) * 0.03f;
			write(3, &comb, sizeof(float));

			self->ss_comb[ss_p] = comb;
			self->ss_p = ss_p;


			if(!self->receiving_frame) {
				if(comb_prev > comb && comb_prev > comb_prev2) {
					nsynchronized = 1;
					synchronized = self->demod_prev;
					self->ss_ps = (ss_p+OVERSAMPLING-1) % OVERSAMPLING;
				}
			} else {
				if(ss_p == self->ss_ps) {
					nsynchronized = 1;
					synchronized = demod;
				}
			}
			self->demod_prev = demod;
#endif
#else
			symsync_rrrf_execute(self->l_symsync, &demod, 1, &synchronized, &nsynchronized);

			//synchronized = demod; nsynchronized = 1; // test: bypass synchronizer
			assert(nsynchronized <= 1);
#endif

			if(nsynchronized == 1) {
				asdf = synchronized;
				//fm_level0 += (synchronized - fm_level0) * .04f;
				/* Process one output symbol from synchronizer */
				bool decision;

				float threshold = 0;
				if(0 && self->receiving_frame)
					threshold = 0.5f * (fm_level0 + fm_level1);

				if(synchronized >= threshold) {
					decision = 1;
					fm_level1 += (synchronized - fm_level1) * 0.05f;
				} else {
					decision = 0;
					fm_level0 += (synchronized - fm_level0) * 0.05f;
				}

				self->fm_level0 = fm_level0;
				self->fm_level1 = fm_level1;
				self->est_power = est_power;

				simple_deframer_execute(self, decision);

#ifndef USE_LIQUID_SYMSYNC
#ifndef USE_COMB_SYMSYNC
				/* Timing error estimator
				 * ----------------------
				 * If decision is different from the previous
				 * decision, assume a transition happened.
				 * Take a sample in between these two symbols,
				 * which should be roughly equal to the average of
				 * the surrounding symbols when timing is correct.
				 * Use the difference from the average as a
				 * timing error estimate. */
				if(decision != self->ss_prevd) {
					// more convenient variable names
					float s0 = self->ss_prev, s1 = self->ss_middle,
						  s2 = synchronized, diff, diff_max = 0.5f;

					diff = clampf((s1 - 0.5f*(s0+s2)) / (s2-s0), diff_max);

					//putchar('5'+8*diff);
					timing_phase += diff * 1000000.0f;
				}

				self->ss_prev = synchronized;
				self->ss_prevd = decision;
			}
#else
			}
#endif
#else
			}
#endif
			
			write(3, &asdf, sizeof(float));
		}	
	}

	//self->fm_level0 = fm_level0;
	//self->fm_level1 = fm_level1;
	self->est_power = est_power;
	self->timing_phase = timing_phase;
	return 0;
}



static int simple_receiver_set_callbacks(void *arg, const struct rx_output_code *output, void *output_arg)
{
	struct simple_receiver *self = arg;
	self->output = *output;
	self->output_arg = output_arg;
	return 0;
}


const struct receiver_code simple_receiver_code = { simple_receiver_init, simple_receiver_destroy, simple_receiver_set_callbacks, simple_receiver_execute };
