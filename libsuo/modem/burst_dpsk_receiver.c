/* Burst PI/4 DQPSK receiver, primarily made for TETRA */

#include "burst_dpsk_receiver.h"
#include "suo_macros.h"
#include "ddc.h"
#include <assert.h>
#include <stdio.h> //debug prints
#include <liquid/liquid.h>


#define OVERSAMP 4

struct burst_dpsk_receiver {
	/* Configuration */
	struct burst_dpsk_receiver_conf c;

	/* Callbacks */
	struct rx_output_code output;
	void *output_arg;

	/* liquid-dsp and suo things */
	struct suo_ddc *ddc;
	firfilt_crcf l_mf; // Matched filter
	wdelaycf l_delay; // Delay for differential demodulation

	/* Other receiver state */
	float avg_mag2;
};


static inline float mag2f(float complex v)
{
	return crealf(v)*crealf(v) + cimagf(v)*cimagf(v);
}


static int execute(void *arg, const sample_t *samples, size_t nsamp, timestamp_t timestamp)
{
	struct burst_dpsk_receiver *self = arg;
	sample_t in[suo_ddc_out_size(self->ddc, nsamp)];
	size_t i, in_n;
	in_n = suo_ddc_execute(self->ddc, samples, nsamp, in, &timestamp);
	float avg_mag2 = self->avg_mag2;
	for (i = 0; i < in_n; i++) {
		sample_t s = in[i], s1 = 0, dp;

		// Matched filtering
		firfilt_crcf_push(self->l_mf, s);
		firfilt_crcf_execute(self->l_mf, &s);

		// Differential phase demodulation and AGC
		wdelaycf_push(self->l_delay, s);
		wdelaycf_read(self->l_delay, &s1);
		avg_mag2 += (mag2f(s) + mag2f(s1) - avg_mag2) * 0.1f;
		dp = s * conjf(s1) * (1.0f / avg_mag2);
		//dp = s * conjf(s1) * (1.0f / (mag2f(s) + mag2f(s1)));
		if (dp != dp)
			dp = 0;
		print_samples(0, &dp, 1);
	}
	self->avg_mag2 = avg_mag2;
	return 0;
}


static void *init(const void *conf_v)
{
	/* Initialize state and copy configuration */
	struct burst_dpsk_receiver *self;
	self = calloc(1, sizeof(*self));
	self->c = *(const struct burst_dpsk_receiver_conf *)conf_v;

	self->ddc = suo_ddc_init(self->c.samplerate, self->c.symbolrate * OVERSAMP, self->c.centerfreq, 0);

	// design the matched filter
#define MFDELAY (OVERSAMP*4)
#define MFTAPS (MFDELAY*OVERSAMP*2+1)
	float taps[MFTAPS];
	liquid_firdes_rrcos(OVERSAMP, MFDELAY, 0.35, 0, taps);
	self->l_mf = firfilt_crcf_create(taps, MFTAPS);

	self->l_delay = wdelaycf_create(OVERSAMP);
	return self;
}


static int set_callbacks(void *arg, const struct rx_output_code *output, void *output_arg)
{
	struct burst_dpsk_receiver *self = arg;
	self->output = *output;
	self->output_arg = output_arg;
	return 0;
}


static int destroy(void *arg)
{
	(void)arg;
	return 0;
}


const struct burst_dpsk_receiver_conf burst_dpsk_receiver_defaults = {
	.samplerate = 1e6,
	.symbolrate = 18000,
	.centerfreq = 100000,
	.syncword = 0x36994625,
	.synclen = 32,
	.framelen = 800
};


CONFIG_BEGIN(burst_dpsk_receiver)
CONFIG_F(samplerate)
CONFIG_F(symbolrate)
CONFIG_F(centerfreq)
CONFIG_I(syncword)
CONFIG_END()


const struct receiver_code burst_dpsk_receiver_code = { "burst_dpsk_receiver", init, destroy, init_conf, set_conf, set_callbacks, execute };
