#include "suo.h"
#include "ddc.h"
#include <liquid/liquid.h>
#include <assert.h>

struct suo_ddc {
	nco_crcf nco;
	msresamp_crcf resamp;
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

	self->resamprate = fs_out / fs_in;
	self->resamp = msresamp_crcf_create(self->resamprate, 60.0f);
	self->flags = flags;

	self->nco = nco_crcf_create(LIQUID_NCO);
	nco_crcf_set_frequency(self->nco,
		pi2f * cf /
		((flags & DDC_UP) ? fs_out : fs_in));

	self->delay_ns = (timestamp_t)(1.0e9f / fs_in *
		msresamp_crcf_get_delay(self->resamp));
	return self;
}


/* Calculate the required size of output buffer
 * for a given number of input samples */
size_t suo_ddc_out_size(struct suo_ddc *ddc, size_t inlen)
{
	return (int)ceilf(ddc->resamprate * inlen);
}


/* Do digital down-conversion for given input samples.
 * Return the number of output samples.
 * Update the timestamp to correspond to start of the output buffer. */
size_t suo_ddc_execute(struct suo_ddc *self, const sample_t *in, size_t inlen, sample_t *out, timestamp_t *timestamp)
{
	size_t i, outlen = 0;
	size_t max_outlen = suo_ddc_out_size(self, inlen); // for assertion
	/* This code could probably be optimized a lot!
	 * The functions provided by liquid-dsp don't really appear
	 * that efficient, since the NCO works one sample at a time. */
	for (i = 0; i < inlen; i++) {
		sample_t mixed = 0;
		unsigned outn = 0;
		nco_crcf_step(self->nco);
		nco_crcf_mix_down(self->nco, in[i], &mixed);
		msresamp_crcf_execute(self->resamp, &mixed, 1, out + outlen, &outn);
		outlen += outn;
		assert(outlen <= max_outlen);
	}

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

	return (int)floorf((float)outlen / ddc->resamprate);
}


/* Do digital up-conversion for given input samples.
 * Return the number of output samples.
 *
 * Note: at the moment, the number of output samples may be less
 * than the number requested, so beware if you try to use with
 * an I/O module that expects an exact number of samples.
 * With the SoapySDR I/O module, it should work though, since it
 * will just ask for a bit more samples the next time. */
size_t suo_duc_execute(struct suo_ddc *self, const sample_t *in, size_t inlen, sample_t *out)
{
	unsigned outn = 0;
	msresamp_crcf_execute(self->resamp, in, inlen, out, &outn);

	size_t i, outlen = outn;
	for (i = 0; i < outlen; i++) {
		nco_crcf_step(self->nco);
		nco_crcf_mix_up(self->nco, out[i], &out[i]);
	}

	return outlen;
}
