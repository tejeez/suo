#include "suo.h"
#include "ddc.h"
#include <liquid/liquid.h>
#include <assert.h>

//#define MSRESAMP

struct suo_ddc {
	nco_crcf nco;
	msresamp_crcf resamp;
	resamp_crcf resamp1;
	unsigned flags;
	float resamprate;
	timestamp_t delay_ns;
};

static const float pi2f = 6.283185307179586f;

/* Initialize a digital down or up converter for a given
 * input sample rate, output sample rate and center frequency.
 *
 * For downconversion, mix the signal down before resampling.
 * For upconversion, mix the signal up after resampling.
 */
struct suo_ddc *suo_ddc_init(float fs_in, float fs_out, float cf, unsigned flags)
{
	struct suo_ddc *self;
	self = calloc(1, sizeof(*self));
	if (self == NULL)
		return NULL;
	self->flags = flags;

	float rate = fs_out / fs_in;
	self->resamprate = rate;
#ifdef MSRESAMP
	self->resamp = msresamp_crcf_create(rate, 60.0f);
	self->delay_ns = (timestamp_t)(1.0e9f / fs_in *
		msresamp_crcf_get_delay(self->resamp));
#else
	/* The arbitrary resampler was tested with some sine wave sweeps.
	 * Using these parameters, its response starts to roll off
	 * at about 1/4 of the lower sample rate.
	 * Worst case alias rejection below this point is around 60 dB.
	 * This should work well if at least 2 times oversampling is used
	 * on both the input and output sides.
	 *
	 * It actually seems to run a bit faster than the multi-stage
	 * resampler, maybe because we have better control over
	 * the filter length and other parameters. */

	float bw = (rate < 1) ? (0.333f * rate) : 0.333f;
	self->resamp1 = resamp_crcf_create(rate, roundf(3.0f/bw), bw, 60.0f, 16);
	self->delay_ns = 0; // TODO
#endif

	self->nco = nco_crcf_create(LIQUID_NCO);
	nco_crcf_set_frequency(self->nco,
		pi2f * cf /
		((flags & DDC_UP) ? fs_out : fs_in));

	return self;
}


/* Calculate the required size of output buffer
 * for a given number of input samples */
size_t suo_ddc_out_size(struct suo_ddc *ddc, size_t inlen)
{
	return (int)ceilf(ddc->resamprate * inlen);
}


#define DDC_BLOCKSIZE 64

/* Do digital down-conversion for given input samples.
 * Return the number of output samples.
 * Update the timestamp to correspond to start of the output buffer. */
size_t suo_ddc_execute(struct suo_ddc *self, const sample_t *in, size_t inlen, sample_t *out, timestamp_t *timestamp)
{
	size_t i, outlen = 0;
	size_t max_outlen = suo_ddc_out_size(self, inlen); // for assertion
#ifdef MSRESAMP
	/* This code could probably be optimized a lot!
	 * The functions provided by liquid-dsp don't really appear
	 * that efficient, since the NCO works one sample at a time.
	 * Now it's a bit faster since msresamp is given samples
	 * in bigger blocks. */
	for (i = 0; i < inlen;) {
		sample_t mixed[DDC_BLOCKSIZE];
		size_t j, len2 = DDC_BLOCKSIZE;
		if (i + len2 > inlen)
			len2 = inlen - i;

		for (j = 0; j < len2; j++) {
			nco_crcf_step(self->nco);
			nco_crcf_mix_down(self->nco, *in++, &mixed[j]);
		}

		unsigned outn = 0;
		msresamp_crcf_execute(self->resamp, mixed, len2, out + outlen, &outn);
		outlen += outn;
		assert(outlen <= max_outlen);
		i += len2;
	}
#else
	for (i = 0; i < inlen; i++) {
		sample_t mixed;
		nco_crcf_step(self->nco);
		nco_crcf_mix_down(self->nco, *in++, &mixed);

		unsigned outn = 0;
		resamp_crcf_execute(self->resamp1, mixed, out + outlen, &outn);
		outlen += outn;
		assert(outlen <= max_outlen);
	}
#endif
	assert(i == inlen);

	/* The timestamp is not exactly accurate now, since it does not
	 * take into account the timing phase of the resampler. */
	*timestamp -= self->delay_ns;

	return outlen;
}


/* Calculate the required number of input samples
 * for a given number of output samples.
 * Also correct the timestamp. */
size_t suo_duc_in_size(struct suo_ddc *ddc, size_t outlen, timestamp_t *timestamp)
{
	/* The timestamp is not exactly accurate now, since it does not
	 * take into account the timing phase of the resampler. */
	*timestamp += ddc->delay_ns;

	size_t s = (float)outlen / ddc->resamprate;
	/* msresamp sometimes produces a bit too much, so let's try
	 * giving one samples less input. */
	if (s >= 2)
		return s - 1;
	else
		return 0;
}


/* Do digital up-conversion for given input samples.
 * Return the number of output samples.
 *
 * Note: at the moment, the number of output samples may be less
 * than the number requested, so beware if you try to use with
 * an I/O module that expects an exact number of samples.
 * With the SoapySDR I/O module, it should work though, since it
 * will just ask for a bit more samples the next time. */
tx_return_t suo_duc_execute(struct suo_ddc *self, const sample_t *in, size_t inlen, sample_t *out)
{
	unsigned outn = 0;
#ifdef MSRESAMP
	msresamp_crcf_execute(self->resamp, (sample_t*)in, inlen, out, &outn);
#else
	size_t j;
	for (j = 0; j < inlen; j++) {
		unsigned outn1;
		resamp_crcf_execute(self->resamp1, *in++, &out[outn], &outn1);
		outn += outn1;
	}
#endif

	// Threshold power for burst begin and end
	const float th = 1e-6f;

	unsigned i, b = 0, e = 0;
	for (i = 0; i < outn; i++) {
		sample_t s = out[i];
		float p = crealf(s)*crealf(s) + cimagf(s)*cimagf(s);
		if (p >= th) {
			if (e == 0)
				b = i;
			e = i + 1;
		}
		nco_crcf_step(self->nco);
		nco_crcf_mix_up(self->nco, s, &out[i]);
	}

	return (tx_return_t){
		.len = outn,
		.begin = b,
		.end = e
	};
}
