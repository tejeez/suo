/* Burst PI/4 DQPSK receiver, primarily made for TETRA */

#include "burst_dpsk_receiver.h"
#include "suo_macros.h"
#include "ddc.h"
#include <assert.h>
#include <stdio.h> //debug prints
#include <liquid/liquid.h>


#define OVERSAMP 4

#define FRAMELEN_MAX 600

struct burst_dpsk_receiver {
	/* Configuration */
	struct burst_dpsk_receiver_conf c;
	uint64_t syncmask, syncmask3;
	float sample_ns;
	unsigned win_len;

	/* Callbacks */
	const struct rx_output_code *output;
	void *output_arg;

	/* liquid-dsp and suo things */
	struct suo_ddc *ddc;
	firfilt_crcf l_mf; // Matched filter
	windowcf l_win;

	/* Other receiver state */
	float avg_mag2;
	uint8_t osph; //oversampling phase
	uint64_t lastbits[OVERSAMP];
	float clockest[OVERSAMP];

	/* Buffers */
	struct frame frame;
	/* Allocate space for flexible array member */
	bit_t frame_buffer[FRAMELEN_MAX];
};


static inline float mag2f(float complex v)
{
	return crealf(v)*crealf(v) + cimagf(v)*cimagf(v);
}

static inline float maxf(const float *v, unsigned n)
{
	unsigned i;
	float m = 0;
	for (i = 0; i < n; i++)
		if (v[i] > m)
			m = v[i];
	return m;
}


static softbit_t float_to_softbit(float v)
{
	long o = 128.0f + 128.0f * v;
	if (o <= 0)
		return 0;
	if (o >= 0xFF)
		return 0xFF;
	return o;
}


static void output_frame(struct burst_dpsk_receiver *self, timestamp_t ts, unsigned type)
{
	sample_t *win;
	windowcf_read(self->l_win, &win);
	unsigned i, len;

	// AGC: calculate gain to normalize power to 1
	float gain = 0;
	len = self->win_len;
	for (i = 0; i < len; i++) {
		gain += mag2f(win[i]);
	}
	gain = 1.0f * len / gain;

	len = self->c.framelen;
	for (i = 0; i < len; i++) {
		sample_t dp;
		// Differential phase
		dp = win[(i+1) * OVERSAMP] * conjf(win[i * OVERSAMP]) * gain;
		self->frame.data[2*i]   = float_to_softbit(-cimagf(dp));
		self->frame.data[2*i+1] = float_to_softbit(-crealf(dp));
	}
	self->frame.m.len = 2*i;
	//TODO: check array size somewhere
	assert(self->frame.m.len <= FRAMELEN_MAX);
	self->frame.m.mode = type;
	self->frame.m.time = ts;
	self->output->frame(self->output_arg, &self->frame);
}


/* Check for different syncwords.
 * TODO: add a window for symbols and output a frame if a syncword matches.
 * Now it just prints some debug information. */
static inline void check_sync(struct burst_dpsk_receiver *self, uint64_t lb, timestamp_t ts) {
	unsigned syncerrs;
	syncerrs = __builtin_popcountll((lb & self->syncmask) ^ self->c.syncword1);
	if (syncerrs <= 0) {
		fprintf(stderr, "%20lu ns: Found syncword 1 with %u errors\n", ts, syncerrs);
		output_frame(self, ts, 1);
	}
	syncerrs = __builtin_popcountll((lb & self->syncmask) ^ self->c.syncword2);
	if (syncerrs <= 0) {
		fprintf(stderr, "%20lu ns: Found syncword 2 with %u errors\n", ts, syncerrs);
		output_frame(self, ts, 2);
	}
	syncerrs = __builtin_popcountll((lb & self->syncmask3) ^ self->c.syncword3);
	if (syncerrs <= 3) {
		fprintf(stderr, "%20lu ns: Found syncword 3 with %u errors\n", ts, syncerrs);
		output_frame(self, ts, 3);
	}
}


static int execute(void *arg, const sample_t *samples, size_t nsamp, timestamp_t timestamp)
{
	struct burst_dpsk_receiver *self = arg;
	self->output->tick(self->output_arg, timestamp);

	sample_t in[suo_ddc_out_size(self->ddc, nsamp)];
	size_t i, in_n;
	in_n = suo_ddc_execute(self->ddc, samples, nsamp, in, &timestamp);
	float avg_mag2 = self->avg_mag2;
	unsigned osph = self->osph; // oversampling phase
	const int syncpos = 133 * OVERSAMP;
	for (i = 0; i < in_n; i++) {
		sample_t s = in[i], s1 = 0, dp;

		// Matched filtering
		firfilt_crcf_push(self->l_mf, s);
		firfilt_crcf_execute(self->l_mf, &s);

		windowcf_push(self->l_win, s);
		sample_t *win;
		windowcf_read(self->l_win, &win);

		/* Pick the sample at the end of the syncword if
		 * the window contains a full burst */
		s = win[syncpos];
		// One symbol before that for differential demodulation
		s1 = win[syncpos - OVERSAMP];

		// AGC
		avg_mag2 += (mag2f(s) + mag2f(s1) - avg_mag2) * 0.1f;
		float gain = 1.0f / sqrtf(avg_mag2);
		if (gain != gain)
			gain = 0;
		s *= gain;
		s1 *= gain;

		// Differential phase demodulation
		dp = s * conjf(s1);

		// Store latest bits separately for each symbol timing phase
		uint64_t lb =
		self->lastbits[osph] = (self->lastbits[osph] << 2) |
			(cimagf(dp) < 0 ? 2 : 0) |
			(crealf(dp) < 0 ? 1 : 0);

		/* Let's try using the mean magnitude of symbols to estimate
		 * symbol timing. It should peak on the best timing phase. */
		float ce = self->clockest[osph];
		ce += (sqrtf(mag2f(s)) - ce) * 0.05f;
		if (ce != ce)
			ce = 0;
		self->clockest[osph] = ce;

		/* If ce peaks, this could be the optimum timing phase.
		 * Check for different syncwords */
		if (ce == maxf(self->clockest, OVERSAMP))
			check_sync(self, lb, timestamp + (timestamp_t)(self->sample_ns * i));

		osph = (osph + 1) % OVERSAMP;
	}
	self->avg_mag2 = avg_mag2;
	self->osph = osph;
	return 0;
}


static void *init(const void *conf_v)
{
	/* Initialize state and copy configuration */
	struct burst_dpsk_receiver *self;
	self = calloc(1, sizeof(*self));
	self->c = *(const struct burst_dpsk_receiver_conf *)conf_v;

	self->syncmask  = (1ULL << self->c.synclen ) - 1;
	self->syncmask3 = (1ULL << self->c.synclen3) - 1;

	const float fs_out = self->c.symbolrate * OVERSAMP;
	self->sample_ns = 1.0e9f / fs_out;
	self->ddc = suo_ddc_init(self->c.samplerate, fs_out, self->c.centerfreq, 0);

	// design the matched filter
#define MFDELAY (3)
#define MFTAPS (MFDELAY*OVERSAMP*2+1)
	float taps[MFTAPS];
	liquid_firdes_rrcos(OVERSAMP, MFDELAY, 0.35, 0, taps);
	self->l_mf = firfilt_crcf_create(taps, MFTAPS);

	self->win_len = (self->c.framelen + 2) * OVERSAMP;
	self->l_win = windowcf_create(self->win_len);
	return self;
}


static int set_callbacks(void *arg, const struct rx_output_code *output, void *output_arg)
{
	struct burst_dpsk_receiver *self = arg;
	self->output = output;
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
	.syncword1 = 0b1101000011101001110100,
	.syncword2 = 0b0111101001000011011110,
	.syncword3 = 0b11000001100111001110100111000001100111,
	.synclen = 22,
	.synclen3 = 38,
	.framelen = 256
};


CONFIG_BEGIN(burst_dpsk_receiver)
CONFIG_F(samplerate)
CONFIG_F(symbolrate)
CONFIG_F(centerfreq)
CONFIG_I(syncword1)
CONFIG_I(syncword2)
CONFIG_I(syncword3)
CONFIG_I(synclen)
CONFIG_I(synclen3)
CONFIG_I(framelen)
CONFIG_END()


const struct receiver_code burst_dpsk_receiver_code = { "burst_dpsk_receiver", init, destroy, init_conf, set_conf, set_callbacks, execute };
