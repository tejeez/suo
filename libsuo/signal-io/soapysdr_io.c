/* SoapySDR I/O:
 * Main loop with SoapySDR interfacing
 */

#include "suo.h"
#include "suo_macros.h"
#include "soapysdr_io.h"

#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <assert.h>
#include <SoapySDR/Version.h>
#include <SoapySDR/Device.h>
#include <SoapySDR/Formats.h>
#include <SoapySDR/Errors.h>
#include <SoapySDR/Logger.h>

#ifdef _WIN32
#include <windows.h>
#endif

struct soapysdr_io {
	const struct receiver_code *receiver;
	void *receiver_arg;
	const struct transmitter_code *transmitter;
	void *transmitter_arg;
	struct soapysdr_io_conf conf;
};


static void soapy_fail(const char *function, int ret)
{
	fprintf(stderr, "%s failed (%d): %s\n", function, ret, SoapySDRDevice_lastError());
}
#define SOAPYCHECK(function,...) do { int ret = function(__VA_ARGS__); if(ret != 0) { soapy_fail(#function, ret); goto exit_soapy; } } while(0)



static volatile int running = 1;

#ifdef _WIN32
static BOOL WINAPI winhandler(DWORD ctrl)
{
	switch (ctrl) {
	case CTRL_C_EVENT:
	case CTRL_CLOSE_EVENT:
		running = 0;
		return TRUE;
	default:
		return FALSE;
	}
}
#else
static void sighandler(int sig)
{
	(void)sig;
	running = 0;
}
#endif


static int execute(void *arg)
{
	struct soapysdr_io *self = arg;
	SoapySDRDevice *sdr = NULL;
	SoapySDRStream *rxstream = NULL, *txstream = NULL;

	const struct soapysdr_io_conf *const conf = &self->conf;

	const double sample_ns = 1.0e9 / conf->samplerate;
	const long long tx_latency_time = sample_ns * conf->tx_latency;
	const size_t rx_buflen = conf->buffer;
	// Reserve a bit more space in TX buffer to allow for timing variations
	const size_t tx_buflen = rx_buflen * 3 / 2;
	// Timeout a few times the buffer length
	const long timeout_us = sample_ns * 0.001 * 10.0 * rx_buflen;
	// Used for lost sample detection
	const long long timediff_max = sample_ns * 0.5;

	/*--------------------------------
	 ---- Hardware initialization ----
	 ---------------------------------*/

#ifdef _WIN32
	SetConsoleCtrlHandler(winhandler, TRUE);
#else
	{
	struct sigaction sigact;
	sigact.sa_handler = sighandler;
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigaction(SIGINT, &sigact, NULL);
	sigaction(SIGTERM, &sigact, NULL);
	sigaction(SIGQUIT, &sigact, NULL);
	sigaction(SIGPIPE, &sigact, NULL);
	}
#endif

	sdr = SoapySDRDevice_make(&conf->args);
	if(sdr == NULL) {
		soapy_fail("SoapySDRDevice_make", 0);
		goto exit_soapy;
	}

	if (conf->rx_on) {
		fprintf(stderr, "Configuring RX\n");
		/* On some devices (e.g. xtrx), sample rate needs to be set before
		* center frequency or the driver crashes */
		SOAPYCHECK(SoapySDRDevice_setSampleRate,
			sdr, SOAPY_SDR_RX, conf->rx_channel,
			conf->samplerate);

		SOAPYCHECK(SoapySDRDevice_setFrequency,
			sdr, SOAPY_SDR_RX, conf->rx_channel,
			conf->rx_centerfreq, NULL);

		if(conf->rx_antenna != NULL)
			SOAPYCHECK(SoapySDRDevice_setAntenna,
				sdr, SOAPY_SDR_RX, conf->rx_channel,
				conf->rx_antenna);

		SOAPYCHECK(SoapySDRDevice_setGain,
			sdr, SOAPY_SDR_RX, conf->rx_channel,
			conf->rx_gain);
	}

	if (conf->tx_on) {
		fprintf(stderr, "Configuring TX\n");
		SOAPYCHECK(SoapySDRDevice_setFrequency,
			sdr, SOAPY_SDR_TX, conf->tx_channel,
			conf->tx_centerfreq, NULL);

		if(conf->tx_antenna != NULL)
			SOAPYCHECK(SoapySDRDevice_setAntenna,
				sdr, SOAPY_SDR_TX, conf->tx_channel,
				conf->tx_antenna);

		SOAPYCHECK(SoapySDRDevice_setGain,
			sdr, SOAPY_SDR_TX, conf->tx_channel,
			conf->tx_gain);

		SOAPYCHECK(SoapySDRDevice_setSampleRate,
			sdr, SOAPY_SDR_TX, conf->tx_channel,
			conf->samplerate);
	}

#if SOAPY_SDR_API_VERSION < 0x00080000
	if (conf->rx_on) {
		SOAPYCHECK(SoapySDRDevice_setupStream,
			sdr, &rxstream, SOAPY_SDR_RX,
			SOAPY_SDR_CF32, &conf->rx_channel, 1, &conf->rx_args);
	}

	if (conf->tx_on) {
		SOAPYCHECK(SoapySDRDevice_setupStream,
			sdr, &txstream, SOAPY_SDR_TX,
			SOAPY_SDR_CF32, &conf->tx_channel, 1, &conf->tx_args);
	}
#else
	if (conf->rx_on) {
		rxstream = SoapySDRDevice_setupStream(sdr,
			SOAPY_SDR_RX, SOAPY_SDR_CF32, &conf->rx_channel, 1, &conf->rx_args);
		if(rxstream == NULL) {
			soapy_fail("SoapySDRDevice_setupStream", 0);
			goto exit_soapy;
		}
	}

	if (conf->tx_on) {
		txstream = SoapySDRDevice_setupStream(sdr,
			SOAPY_SDR_TX, SOAPY_SDR_CF32, &conf->tx_channel, 1, &conf->tx_args);
		if(txstream == NULL) {
			soapy_fail("SoapySDRDevice_setupStream", 0);
			goto exit_soapy;
		}
	}
#endif



	/*----------------------------
	 --------- Main loop ---------
	 -----------------------------*/

	bool streaming = 0;
	bool tx_burst_going = 0;
	long long current_time = 0;
	long long tx_last_end_time = 0;

	sample_t *initial_tx_zeros = calloc(conf->tx_latency, sizeof(sample_t));

	while(running) {
		if (!streaming) {
			streaming = 1;
			fprintf(stderr, "Starting streams\n");
			if (conf->rx_on)
				SOAPYCHECK(SoapySDRDevice_activateStream, sdr,
					rxstream, 0, 0, 0);
			if (conf->tx_on)
				SOAPYCHECK(SoapySDRDevice_activateStream, sdr,
					txstream, 0, 0, 0);

			if ((!conf->use_time) && conf->rx_on && conf->tx_on) {
				const void *txbuffs[] = { initial_tx_zeros };
				int ret;
				int flags = 0;
				ret = SoapySDRDevice_writeStream(sdr, txstream,
					txbuffs, conf->tx_latency, &flags,
					0, tx_latency_time / 100);
				if (ret <= 0)
					goto fix_xrun;
			}

			if (conf->use_time)
				current_time = SoapySDRDevice_getHardwareTime(sdr, "");
			/* tx_last_end_time is when the previous produced TX buffer
			* ended, i.e. where the next buffer should begin */
			tx_last_end_time = current_time + tx_latency_time;
		}

		if (conf->rx_on) {
			sample_t rxbuf[rx_buflen];
			void *rxbuffs[] = { rxbuf };
			long long rx_timestamp = 0;
			int flags = 0, ret;
			ret = SoapySDRDevice_readStream(sdr, rxstream,
				rxbuffs, rx_buflen, &flags, &rx_timestamp, timeout_us);
			if (ret > 0) {
				/* Estimate current time from the end of the received buffer.
				* If there's no timestamp, make one up by incrementing time.
				*
				* If there were no lost samples, the received buffer should
				* begin from the previous "current" time. Calculate the
				* difference to detect lost samples.
				* TODO: if configured, feed zero padding samples to receiver
				* module to correct timing after lost samples. */
				if (conf->use_time && (flags & SOAPY_SDR_HAS_TIME)) {
					long long prev_time = current_time;
					current_time = rx_timestamp + sample_ns * ret;

					long long timediff = rx_timestamp - prev_time;
					// this can produce a lot of print, not the best way to do it
					if (timediff < -timediff_max)
						fprintf(stderr, "%20lld: Time went backwards %lld ns!\n", rx_timestamp, -timediff);
					else if (timediff > timediff_max)
						fprintf(stderr, "%20lld: Lost samples for %lld ns!\n", rx_timestamp, timediff);
				} else {
					rx_timestamp = current_time; // from previous iteration
					current_time += sample_ns * ret + 0.5;
				}
				self->receiver->execute(self->receiver_arg, rxbuf, ret, rx_timestamp);
			} else if(ret <= 0) {
				soapy_fail("SoapySDRDevice_readStream", ret);
			}
			if ((!conf->use_time) && (ret == SOAPY_SDR_OVERFLOW || ret == SOAPY_SDR_UNDERFLOW))
				goto fix_xrun;
		} else {
			/* TX-only case */
			if (conf->use_time) {
				/* There should be a blocking call somewhere, so maybe
				 * there should be some kind of a sleep here.
				 * When use_time == 0, however, the TX buffer is written
				 * until it's full and writeStream blocks, so the buffer
				 * "back-pressure" works for timing in that case.
				 * For now, in TX-only use, it's recommended to set
				 * use_time=0 and tx_cont=1. */
				current_time = SoapySDRDevice_getHardwareTime(sdr, "");
			} else {
				current_time += sample_ns * conf->buffer + 0.5;
			}
		}

		if (conf->tx_on) {
			sample_t txbuf[tx_buflen];
			int flags = 0, ret;
			tx_return_t ntx = { 0, 0, 0 };
			timestamp_t tx_from_time, tx_until_time;
			tx_from_time = tx_last_end_time;
			tx_until_time = current_time + tx_latency_time;
			int nsamp = round((double)(tx_until_time - tx_from_time) / sample_ns);
			//fprintf(stderr, "TX nsamp: %d\n", nsamp);

			if (nsamp > 0) {
				if ((unsigned)nsamp > tx_buflen)
					nsamp = tx_buflen;
				ntx = self->transmitter->execute(self->transmitter_arg, txbuf, nsamp, tx_from_time);
				assert(ntx.len >= 0 && ntx.len <= nsamp);
				assert(ntx.end >= 0 && ntx.end <= /*ntx.len*/nsamp);
				assert(ntx.begin >= 0 && ntx.begin <= /*ntx.len*/nsamp);
				if (conf->tx_cont) {
					// Disregard begin and end in case continuous transmit stream is configured
					ntx.begin = 0;
					ntx.end = ntx.len;
				}
				tx_last_end_time = tx_from_time + (timestamp_t)(sample_ns * ntx.len);
			}

			if (conf->use_time)
				flags = SOAPY_SDR_HAS_TIME;

			if (tx_burst_going && ntx.begin > 0) {
				/* If end of burst flag wasn't sent in last round,
				 * send it now together with one dummy sample.
				 * One sample is sent because trying to send
				 * zero samples gave a timeout error. */
				txbuf[0] = 0;
				const void *txbuffs[] = { txbuf };
				flags |= SOAPY_SDR_END_BURST;
				ret = SoapySDRDevice_writeStream(sdr, txstream,
					txbuffs, 1, &flags,
					tx_from_time, timeout_us);
				if(ret <= 0)
					soapy_fail("SoapySDRDevice_writeStream (end of burst)", ret);
				tx_burst_going = 0;
			}

			if (ntx.end > ntx.begin) {
				// If ntx.end does not point to end of the buffer, a burst has ended
				if (ntx.end < ntx.len) {
					flags |= SOAPY_SDR_END_BURST;
					tx_burst_going = 0;
				} else {
					tx_burst_going = 1;
				}

				const void *txbuffs[] = { txbuf + ntx.begin };

				ret = SoapySDRDevice_writeStream(sdr, txstream,
					txbuffs, ntx.end - ntx.begin, &flags,
					tx_from_time + (timestamp_t)(sample_ns * ntx.begin),
					timeout_us);
				if(ret <= 0)
					soapy_fail("SoapySDRDevice_writeStream", ret);
				if ((!conf->use_time) && (ret == SOAPY_SDR_OVERFLOW || ret == SOAPY_SDR_UNDERFLOW))
					goto fix_xrun;
			} else {
				tx_burst_going = 0;
			}
		}
		continue;
		fix_xrun:
		fprintf(stderr, "Restarting streams to recover from over- or underrun\n");
		streaming = 0;
		if (conf->rx_on)
			SoapySDRDevice_deactivateStream(sdr, rxstream, 0, 0);
		if (conf->tx_on)
			SoapySDRDevice_deactivateStream(sdr, txstream, 0, 0);
	}

	fprintf(stderr, "Stopped receiving\n");

exit_soapy:
	//deinitialize(suo); //TODO moved somewhere else

	if(rxstream != NULL) {
		fprintf(stderr, "Deactivating stream\n");
		SoapySDRDevice_deactivateStream(sdr, rxstream, 0, 0);
		SoapySDRDevice_closeStream(sdr, rxstream);
	}
	if(sdr != NULL) {
		fprintf(stderr, "Closing device\n");
		SoapySDRDevice_unmake(sdr);
	}

	fprintf(stderr, "Done\n");
	return 0;
}


static void *init(const void *conf)
{
	struct soapysdr_io *self;
	self = calloc(1, sizeof(*self));
	if (self == NULL)
		return self;
	self->conf = *(struct soapysdr_io_conf*)conf;
	if (strcmp(SoapySDR_getABIVersion(), SOAPY_SDR_ABI_VERSION) != 0)
		fprintf(stderr, "Warning: Wrong SoapySDR ABI version\n");
	return self;
}


static int destroy(void *arg)
{
	// TODO
	(void)arg;
	return 0;
}


static int set_callbacks(void *arg, const struct receiver_code *receiver, void *receiver_arg, const struct transmitter_code *transmitter, void *transmitter_arg)
{
	struct soapysdr_io *self = arg;
	self->receiver = receiver;
	self->receiver_arg = receiver_arg;
	self->transmitter = transmitter;
	self->transmitter_arg = transmitter_arg;
	return 0;
}


const struct soapysdr_io_conf soapysdr_io_defaults = {
	.buffer = 2048,
	.rx_on = 1,
	.tx_on = 1,
	.tx_cont = 0,
	.use_time = 1,
	.tx_latency = 8192,
	.samplerate = 1e6,
	.rx_centerfreq = 433.8e6,
	.tx_centerfreq = 433.8e6,
	.rx_gain = 60,
	.tx_gain = 80,
	.rx_channel = 0,
	.tx_channel = 0,
	.rx_antenna = NULL,
	.tx_antenna = NULL
};

CONFIG_BEGIN(soapysdr_io)
CONFIG_I(rx_on)
CONFIG_I(tx_on)
CONFIG_I(tx_cont)
CONFIG_I(use_time)
CONFIG_I(buffer)
CONFIG_I(tx_latency)
CONFIG_F(samplerate)
CONFIG_F(rx_centerfreq)
CONFIG_F(tx_centerfreq)
CONFIG_F(rx_gain)
CONFIG_F(tx_gain)
CONFIG_I(rx_channel)
CONFIG_I(tx_channel)
CONFIG_C(rx_antenna)
CONFIG_C(tx_antenna)
	if (strncmp(parameter, "soapy-", 6) == 0) {
		SoapySDRKwargs_set(&c->args, parameter+6, value);
		return 0;
	}
	if (strncmp(parameter, "device:", 7) == 0) {
		SoapySDRKwargs_set(&c->args, parameter+7, value);
		return 0;
	}
	if (strncmp(parameter, "rx_stream:", 10) == 0) {
		SoapySDRKwargs_set(&c->rx_args, parameter+10, value);
		return 0;
	}
	if (strncmp(parameter, "tx_stream:", 10) == 0) {
		SoapySDRKwargs_set(&c->tx_args, parameter+10, value);
		return 0;
	}
CONFIG_END()


const struct signal_io_code soapysdr_io_code = { "soapysdr_io", init, destroy, init_conf, set_conf, set_callbacks, execute };
