#include "simple_receiver.h"
#include <string.h>
#include <assert.h>
#include <stdio.h> // for debug prints only
#include <liquid/liquid.h>


#define FRAMELEN_MAX 0x900
static const float pi2f = 6.283185307179586f;

struct simple_receiver {
	/* Configuration */
	struct simple_receiver_conf c;
	//float resamprate;
	unsigned resampint;
	uint64_t syncmask;

	/* State */
	//uint32_t total_samples;
	uint64_t latest_bits;
	unsigned framepos, totalbits;
	bool receiving_frame;

	float fm_dc, fm_level0, fm_level1; /* Used by AFC */
	float est_power; /* estimates for metadata */

	/* liquid-dsp objects */
	nco_crcf l_nco;
	resamp_crcf l_resamp;
	freqdem l_fdem;
	symsync_rrrf l_symsync;

	/* Callbacks */
	struct rx_output_code output;
	void *output_arg;

	/* Buffers */
	struct rx_metadata metadata;
	bit_t framebuf[FRAMELEN_MAX];
};

#define OVERSAMPLING 4

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
	self->l_resamp = resamp_crcf_create(resamprate, 15, 0.5f / OVERSAMPLING, 60.0f, 32);
	/* Calculate maximum number of output samples after feeding one sample
	 * to the resampler. This is needed to allocate a big enough array. */
	self->resampint = ceilf(resamprate);

	/* Other liquid-dsp objects */
	self->l_fdem = freqdem_create(1.0f);

	self->l_nco = nco_crcf_create(LIQUID_NCO);
	nco_crcf_set_frequency(self->l_nco, pi2f * c.centerfreq / c.samplerate);

	self->l_symsync = symsync_rrrf_create_rnyquist(LIQUID_FIRFILT_GMSKRX, OVERSAMPLING, 7, 0.5f, 32);
	//self->l_symsync = symsync_rrrf_create_rnyquist(LIQUID_FIRFILT_RRC, OVERSAMPLING, 7, 1.0f, 32);
	symsync_rrrf_set_lf_bw(self->l_symsync, 0.0001f);

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
			symsync_rrrf_unlock(self->l_symsync);
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
			self->metadata.cfo = self->fm_dc; /* TODO: convert to Hz */
			self->metadata.rssi = 10.0f * log10f(self->est_power);

#if 0
			printf("_%d_", syncerrs);
#endif
			symsync_rrrf_lock(self->l_symsync);
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


static inline float mag2f(float complex v)
{
	return crealf(v)*crealf(v) + cimagf(v)*cimagf(v);
}


static int simple_receiver_execute(void *arg, const sample_t *samples, size_t nsamp, timestamp_t timestamp)
{
	struct simple_receiver *self = arg;

	/* Copy some often used variables to local variables */
	float fm_dc = self->fm_dc, fm_level0 = self->fm_level0, fm_level1 = self->fm_level1, est_power = self->est_power;

	/* Allocate small buffers from stack */
	sample_t samples2[self->resampint];

	self->metadata.timestamp = timestamp;
	/* TODO: increment timestamp in loop */

	size_t si;
	for(si = 0; si < nsamp; si++) {
		unsigned nsamp2 = 0, si2;
		sample_t s = samples[si];

		/* Downconvert and resample one input sample at a time */
		nco_crcf_step(self->l_nco);
		nco_crcf_mix_down(self->l_nco, s, &s);
		resamp_crcf_execute(self->l_resamp, s, samples2, &nsamp2);
		assert(nsamp2 <= self->resampint);

		/* Process output from the resampler one sample at a time */
		for(si2 = 0; si2 < nsamp2; si2++) {
			float fm_demodulated = 0, synchronized = 0;
			unsigned nsynchronized = 0;
			sample_t s2 = samples2[si2];

			est_power += (mag2f(s2) - est_power) * 0.01f;

			freqdem_demodulate(self->l_fdem, s2, &fm_demodulated);

			/* Simple alternative to AFC: track DC offset in FM demodulator
			 * output. When looking for a preamble, just run it as
			 * a high pass filter. When receiving a frame, switch
			 * to decision directed mode to avoid baseline wander.
			 * Using a coefficient of 0.01, it should settle within 7 %
			 * during 64 symbols (and 4x oversampling). */
			if(!self->receiving_frame) {
				fm_dc += (fm_demodulated - fm_dc) * 0.01f;
			}
			fm_demodulated -= fm_dc;


			symsync_rrrf_execute(self->l_symsync, &fm_demodulated, 1, &synchronized, &nsynchronized);

			//synchronized = fm_demodulated; nsynchronized = 1; // test: bypass synchronizer
			assert(nsynchronized <= 1);

			if(nsynchronized == 1) {
				//fm_level0 += (synchronized - fm_level0) * .04f;
				/* Process one output symbol from synchronizer */
				bool decision;

				float threshold = 0;
				if(self->receiving_frame)
					threshold = 0.5f * (fm_level0 + fm_level1);

				if(synchronized >= threshold) {
					decision = 1;
					fm_level1 += (synchronized - fm_level1) * 0.05f;
				} else {
					decision = 0;
					fm_level0 += (synchronized - fm_level0) * 0.05f;
				}

				self->fm_dc = fm_dc;
				self->fm_level0 = fm_level0;
				self->fm_level1 = fm_level1;
				self->est_power = est_power;

				simple_deframer_execute(self, decision);
			}
		}

		self->fm_dc = fm_dc;
		//self->fm_level0 = fm_level0;
		//self->fm_level1 = fm_level1;
		self->est_power = est_power;
		
#if 0
		/* Simple test: periodically output some random frames */
		if(((++self->total_samples) & 0x3FFFFF) == 0) {
			bit_t testframe[c.framelen];
			size_t i;
			for(i=0; i<c.framelen; i++)
				testframe[i] = 1 & rand();
			self->output.frame(self->output_arg, testframe, c.framelen);
		}
#endif
	}
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
