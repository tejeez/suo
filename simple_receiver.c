#include <complex.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <liquid/liquid.h>
#include <assert.h>
#include <stdio.h> // for debug prints only
#include "common.h"
#include "simple_receiver.h"

#define FRAMELEN_MAX 0x800
const float pi2f = 6.283185307179586;

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

	/* liquid-dsp objects */
	nco_crcf l_nco;
	resamp_crcf l_resamp;
	freqdem l_fdem;
	symsync_rrrf l_symsync;

	/* Callbacks */
	struct frame_output_code output;
	void *output_arg;

	/* Buffers */
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

	self->l_symsync = symsync_rrrf_create_rnyquist(LIQUID_FIRFILT_GMSKRX, OVERSAMPLING, 3, 0.5f, 32);
	symsync_rrrf_set_lf_bw(self->l_symsync, 0.01f);

	return self;
}


static void simple_deframer_execute(struct simple_receiver *self, unsigned bit)
{
	unsigned framepos = self->framepos;
	const unsigned framelen = self->c.framelen;

	if(framepos < framelen) {
		self->framebuf[framepos] = bit;
		framepos++;
		if(framepos == framelen) {
			self->output.frame(self->output_arg, self->framebuf, framelen);
		}
	}

	/* Look for syncword */
	uint64_t latest_bits = self->latest_bits;
	latest_bits <<= 1;
	latest_bits |= bit;
	self->latest_bits = latest_bits;
	unsigned syncerrs = __builtin_popcountll((latest_bits & self->syncmask) ^ self->c.syncword);

	if(syncerrs <= 2) {
		/* Syncword found, start saving bits when next bit arrives */
		framepos = 0;
	}

	self->framepos = framepos;

#if 0
	if((self->totalbits & 63) == 0) putchar('\n');
	putchar(bit?'x':' ');
#endif

	self->totalbits++;
}


static int simple_receiver_execute(void *arg, sample_t *samples, size_t nsamp)
{
	struct simple_receiver *self = arg;

	/* Copy some configuration as local variables to make code
	 * more clear and possibly also faster */
	//const struct simple_receiver_conf c = self->c;

	/* Allocate small buffers from stack */
	sample_t samples2[self->resampint];

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

			freqdem_demodulate(self->l_fdem, samples2[si2], &fm_demodulated);
			symsync_rrrf_execute(self->l_symsync, &fm_demodulated, 1, &synchronized, &nsynchronized);
			//synchronized = fm_demodulated; nsynchronized = 1; // test: bypass synchronizer
			assert(nsynchronized <= 1);

			if(nsynchronized == 1) {
				/* Process one output symbol from synchronizer */
				unsigned decision = (synchronized > 0) ? 1 : 0;
				simple_deframer_execute(self, decision);
			}
		}
		
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



static int simple_receiver_set_callbacks(void *arg, const struct frame_output_code *output, void *output_arg)
{
	struct simple_receiver *self = arg;
	self->output = *output;
	self->output_arg = output_arg;
	return 0;
}


const struct receiver_code simple_receiver_code = { simple_receiver_init, simple_receiver_set_callbacks, simple_receiver_execute };
