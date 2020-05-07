/* ALSA I/O
 *
 * This is slightly redundant since SoapySDR also has an audio module,
 * but directly using the ALSA API lets us use snd_pcm_link to
 * start the recording and playback streams synchronously.
 * Recovery from a stream underrun can also be handled properly by
 * restarting both streams to re-establish synchronization.
 */

#if ENABLE_ALSA
#include "alsa_io.h"
#include "suo_macros.h"
#include "conversion.h"
#include <alsa/asoundlib.h>
#include <alsa/control.h>
#include <assert.h>
#include <stdio.h>

typedef int16_t cs16_t[2];

struct alsa_io {
	const struct receiver_code *receiver;
	void *receiver_arg;
	const struct transmitter_code *transmitter;
	void *transmitter_arg;

	snd_pcm_t *rx_pcm, *tx_pcm;

	struct alsa_io_conf conf;
};

#define ALSACHECK(a) do {\
	int retcheck = a; \
	if (retcheck < 0) { \
	fprintf(stderr, "\nALSA error in %s: %s\n", #a, snd_strerror(retcheck)); \
	goto err; } } while(0)

snd_pcm_t *open_alsa(const struct alsa_io_conf *conf, snd_pcm_stream_t dir)
{
	snd_pcm_t *pcm = NULL;
	snd_pcm_hw_params_t *hwp;
	ALSACHECK(snd_pcm_open(&pcm,
		(dir == SND_PCM_STREAM_PLAYBACK) ? conf->tx_name : conf->rx_name,
		dir, 0));
	ALSACHECK(snd_pcm_hw_params_malloc(&hwp));
	ALSACHECK(snd_pcm_hw_params_any(pcm, hwp));
	ALSACHECK(snd_pcm_hw_params_set_access(pcm, hwp, SND_PCM_ACCESS_RW_INTERLEAVED));
	ALSACHECK(snd_pcm_hw_params_set_format(pcm, hwp, SND_PCM_FORMAT_S16_LE));
	unsigned fs = conf->samplerate;
	ALSACHECK(snd_pcm_hw_params_set_rate(pcm, hwp, fs, 0));
	ALSACHECK(snd_pcm_hw_params_set_channels(pcm, hwp, 2));
	unsigned frags = conf->buffer;
	ALSACHECK(snd_pcm_hw_params_set_periods_near(pcm, hwp, &frags, 0));
	// Make the total buffer size a couple of times tx_latency
	snd_pcm_uframes_t bufsize = conf->tx_latency * 2;
	ALSACHECK(snd_pcm_hw_params_set_buffer_size_near(pcm, hwp, &bufsize));

	ALSACHECK(snd_pcm_hw_params(pcm, hwp));

	fprintf(stderr, "fs=%d, frags=%u, bufsize=%u\n", fs, frags, (unsigned)bufsize);
	return pcm;
err:
	return NULL;
}

/* Convert a number of samples to a timestamp while avoiding
 * accumulation of rounding errors.
 * Sample rate needs to be an integer (in Hz). */
static timestamp_t samples_to_ns(uint64_t n, uint32_t fs) {
	return n / fs * 1000000000ULL +
	(uint64_t)((float)(n % fs) * (1000000000.0f / fs));
}


static void *init(const void *confv)
{
	struct alsa_io *self = calloc(1, sizeof(*self));
	if (self == NULL)
		return self;
	self->conf = *(const struct alsa_io_conf *)confv;

	if (self->conf.rx_on) {
		fprintf(stderr, "ALSA: Opening capture\n");
		self->rx_pcm = open_alsa(&self->conf, SND_PCM_STREAM_CAPTURE);
		if(self->rx_pcm == NULL)
			goto err;
	}
	if (self->conf.tx_on) {
		fprintf(stderr, "ALSA: Opening playback\n");
		self->tx_pcm = open_alsa(&self->conf, SND_PCM_STREAM_PLAYBACK);
		if(self->tx_pcm == NULL)
			goto err;
	}

	if (self->conf.rx_on && self->conf.tx_on)
		ALSACHECK(snd_pcm_link(self->rx_pcm, self->tx_pcm));

	return self;
err:
	// TODO: close device
	return NULL;
}


static int execute(void *arg)
{
	struct alsa_io *self = arg;
	struct alsa_io_conf *conf = &self->conf;
	snd_pcm_t *rx_pcm = self->rx_pcm, *tx_pcm = self->tx_pcm;
	char streaming = 0;

	snd_pcm_uframes_t blksize = self->conf.buffer;
	snd_pcm_uframes_t tx_latency = self->conf.tx_latency;

	// Reserve buffer somewhat bigger than blksize
	size_t buf_max = blksize * 2;
	cs16_t *silence = calloc(tx_latency, sizeof(cs16_t));
	cs16_t *buf_int = malloc(sizeof(cs16_t) * buf_max);
	sample_t *buf_flt = malloc(sizeof(sample_t) * buf_max);

	// RX and TX samples since starting the stream
	uint64_t rx_samps = 0, tx_samps = 0;
	/* Total samples before the stream was started.
	 * This is to keep timestamp progressing if stream is restarted. */
	uint64_t base_samps = 0;

	for (;;) {
		snd_pcm_sframes_t ret;
		if (!streaming) {
			base_samps += rx_samps;
			rx_samps = 0;
			tx_samps = 0;
			//fprintf(stderr, "ALSA: (Re)starting streams\n");
			// http://www.saunalahti.fi/~s7l/blog/2005/08/21/Full%20Duplex%20ALSA
			ALSACHECK(snd_pcm_drop(rx_pcm));
			ALSACHECK(snd_pcm_drop(tx_pcm));
			ALSACHECK(snd_pcm_prepare(tx_pcm));
			ret = snd_pcm_writei(tx_pcm, silence, tx_latency);
			fprintf(stderr, "ALSA first write: %ld/%ld\n", ret, tx_latency);
			if (ret > 0) {
				tx_samps = ret;
				streaming = 1;
			}
			// TODO: handle rx-only (or non-linked) case
		}

		if (conf->rx_on) {
			ret = snd_pcm_readi(rx_pcm, buf_int, blksize);
			if (ret < 0) {
				fprintf(stderr, "ALSA read:  %s\n", snd_strerror(ret));
				streaming = 0;
				continue;
			}
			size_t n = ret;
			assert(n <= buf_max);
			cs16_to_cf(buf_int, buf_flt, n);
			self->receiver->execute(self->receiver_arg,
				buf_flt, n,
				samples_to_ns(base_samps + rx_samps, conf->samplerate));
			rx_samps += n;
		} else {
			// RX disabled. Increment rx_samps anyway to make TX work
			rx_samps += blksize;
		}

		if (conf->tx_on) {
			tx_return_t ntx = { 0, 0, 0 };
			uint64_t tx_until = rx_samps + tx_latency;
			// How many samples needed now
			uint64_t tx_n = tx_until - tx_samps;
			//printf("tx_n = %lu ", (long unsigned)tx_n);
			if (tx_n > buf_max)
				tx_n = buf_max;

			ntx = self->transmitter->execute(self->transmitter_arg,
				buf_flt, tx_n,
				samples_to_ns(base_samps + tx_samps, conf->samplerate));

			size_t n = ntx.len;
			assert(n <= buf_max);
			cf_to_cs16(buf_flt, buf_int, n);
			ret = snd_pcm_writei(tx_pcm, buf_int, n);
			if (ret < 0) {
				fprintf(stderr, "ALSA write: %s\n", snd_strerror(ret));
				streaming = 0;
				continue;
			} else if (ret != ntx.len) {
				fprintf(stderr, "ALSA: short write (%ld/%d)\n", ret, ntx.len);
			}
			tx_samps += ret;
		}
	}
err:
	free(silence);
	free(buf_int);
	free(buf_flt);
	return -1;
}



static int destroy(void *arg)
{
	// TODO
	(void)arg;
	return 0;
}

static int set_callbacks(void *arg, const struct receiver_code *receiver, void *receiver_arg, const struct transmitter_code *transmitter, void *transmitter_arg)
{
	struct alsa_io *self = arg;
	self->receiver = receiver;
	self->receiver_arg = receiver_arg;
	self->transmitter = transmitter;
	self->transmitter_arg = transmitter_arg;
	return 0;
}

const struct alsa_io_conf alsa_io_defaults = {
	.rx_on = 1,
	.tx_on = 1,
	.samplerate = 48000,
	.buffer = 96,
	.tx_latency = 96*4,
	.rx_name = "hw:0,0",
	.tx_name = "hw:0,0"
};

CONFIG_BEGIN(alsa_io)
CONFIG_I(rx_on)
CONFIG_I(tx_on)
CONFIG_I(samplerate)
CONFIG_I(buffer)
CONFIG_I(tx_latency)
CONFIG_C(rx_name)
CONFIG_C(tx_name)
CONFIG_END()

const struct signal_io_code alsa_io_code = { "alsa_io", init, destroy, init_conf, set_conf, set_callbacks, execute };
#endif
