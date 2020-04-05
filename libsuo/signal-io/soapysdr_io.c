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
#define SOAPYCHECK(function,...) { int ret = function(__VA_ARGS__); if(ret != 0) { soapy_fail(#function, ret); goto exit_soapy; } }



static volatile int running = 1;

static void sighandler(int sig)
{
	(void)sig;
	running = 0;
}


typedef unsigned char sample1_t[2];

static int execute(void *arg)
{
	struct soapysdr_io *self = arg;
	SoapySDRDevice *sdr = NULL;
	SoapySDRStream *rxstream = NULL, *txstream = NULL;

	struct soapysdr_io_conf *const radioconf = &self->conf;

	const double sample_ns = 1.0e9 / radioconf->samplerate;
	const long long tx_latency_time = sample_ns * radioconf->tx_latency;
	const size_t rx_buflen = radioconf->buffer;
	// Reserve a bit more space in TX buffer to allow for timing variations
	const size_t tx_buflen = rx_buflen * 3 / 2;
	// Timeout a few times the buffer length
	const long timeout_us = sample_ns * 0.001 * 10.0 * rx_buflen;
	// Used for lost sample detection
	const long long timediff_max = sample_ns * 0.5;

	/*--------------------------------
	 ---- Hardware initialization ----
	 ---------------------------------*/

	struct sigaction sigact;
	sigact.sa_handler = sighandler;
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigaction(SIGINT, &sigact, NULL);
	sigaction(SIGTERM, &sigact, NULL);
	sigaction(SIGQUIT, &sigact, NULL);
	sigaction(SIGPIPE, &sigact, NULL);

	sdr = SoapySDRDevice_make(&self->conf.args);
	SoapySDRKwargs_clear(&self->conf.args);
	if(sdr == NULL) {
		soapy_fail("SoapySDRDevice_make", 0);
		goto exit_soapy;
	}

	fprintf(stderr, "Configuring RX\n");
	/* On some devices (e.g. xtrx), sample rate needs to be set before
	 * center frequency or the driver crashes */
	SOAPYCHECK(SoapySDRDevice_setSampleRate,
		sdr, SOAPY_SDR_RX, radioconf->rx_channel,
		radioconf->samplerate);

	SOAPYCHECK(SoapySDRDevice_setFrequency,
		sdr, SOAPY_SDR_RX, radioconf->rx_channel,
		radioconf->rx_centerfreq, NULL);

	if(radioconf->rx_antenna != NULL)
		SOAPYCHECK(SoapySDRDevice_setAntenna,
			sdr, SOAPY_SDR_RX, radioconf->rx_channel,
			radioconf->rx_antenna);

	SOAPYCHECK(SoapySDRDevice_setGain,
		sdr, SOAPY_SDR_RX, radioconf->rx_channel,
		radioconf->rx_gain);

	if (radioconf->flags & SOAPYIO_TX_ON) {
		fprintf(stderr, "Configuring TX\n");
		SOAPYCHECK(SoapySDRDevice_setFrequency,
			sdr, SOAPY_SDR_TX, radioconf->tx_channel,
			radioconf->tx_centerfreq, NULL);

		if(radioconf->tx_antenna != NULL)
			SOAPYCHECK(SoapySDRDevice_setAntenna,
				sdr, SOAPY_SDR_TX, radioconf->tx_channel,
				radioconf->tx_antenna);

		SOAPYCHECK(SoapySDRDevice_setGain,
			sdr, SOAPY_SDR_TX, radioconf->tx_channel,
			radioconf->tx_gain);

		SOAPYCHECK(SoapySDRDevice_setSampleRate,
			sdr, SOAPY_SDR_TX, radioconf->tx_channel,
			radioconf->samplerate);
	}

#if SOAPY_SDR_API_VERSION < 0x00080000
	SOAPYCHECK(SoapySDRDevice_setupStream,
		sdr, &rxstream, SOAPY_SDR_RX,
		SOAPY_SDR_CF32, &radioconf->rx_channel, 1, NULL);

	if (radioconf->flags & SOAPYIO_TX_ON) {
		SOAPYCHECK(SoapySDRDevice_setupStream,
			sdr, &txstream, SOAPY_SDR_TX,
			SOAPY_SDR_CF32, &radioconf->tx_channel, 1, NULL);
	}
#else
	rxstream = SoapySDRDevice_setupStream(sdr,
		SOAPY_SDR_RX, SOAPY_SDR_CF32, &radioconf->rx_channel, 1, NULL);
	if(rxstream == NULL) {
		soapy_fail("SoapySDRDevice_setupStream", 0);
		goto exit_soapy;
	}
	if (radioconf->flags & SOAPYIO_TX_ON) {
		txstream = SoapySDRDevice_setupStream(sdr,
			SOAPY_SDR_TX, SOAPY_SDR_CF32, &radioconf->tx_channel, 1, NULL);
		if(txstream == NULL) {
			soapy_fail("SoapySDRDevice_setupStream", 0);
			goto exit_soapy;
		}
	}
#endif

	fprintf(stderr, "Starting to receive\n");
	SOAPYCHECK(SoapySDRDevice_activateStream, sdr,
		rxstream, 0, 0, 0);
	if (radioconf->flags & SOAPYIO_TX_ON)
		SOAPYCHECK(SoapySDRDevice_activateStream, sdr,
			txstream, 0, 0, 0);


	/*----------------------------
	 --------- Main loop ---------
	 -----------------------------*/

	bool tx_burst_going = 0;

	long long current_time = SoapySDRDevice_getHardwareTime(sdr, "");
	/* tx_last_end_time is when the previous produced TX buffer
	 * ended, i.e. where the next buffer should begin */
	long long tx_last_end_time = current_time + tx_latency_time;

	while(running) {
		sample_t rxbuf[rx_buflen];
		sample_t txbuf[tx_buflen];
		void *rxbuffs[] = { rxbuf };
		int flags = 0;
		long long rx_timestamp = 0;
		int ret = SoapySDRDevice_readStream(sdr, rxstream,
			rxbuffs, rx_buflen, &flags, &rx_timestamp, timeout_us);

		if(ret > 0) {
			/* Estimate current time from the end of the received buffer.
			 * If there's no timestamp, make one up by incrementing time.
			 *
			 * If there were no lost samples, the received buffer should
			 * begin from the previous "current" time. Calculate the
			 * difference to detect lost samples.
			 * TODO: if configured, feed zero padding samples to receiver
			 * module to correct timing after lost samples. */
			if (flags & SOAPY_SDR_HAS_TIME) {
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
				current_time += sample_ns * ret;
			}
			self->receiver->execute(self->receiver_arg, rxbuf, ret, rx_timestamp);
		} else if(ret <= 0) {
			soapy_fail("SoapySDRDevice_readStream", ret);
			long long t = SoapySDRDevice_getHardwareTime(sdr, "");
			if (t != 0)
				current_time = t;
		}

		if (radioconf->flags & SOAPYIO_TX_ON) {
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
				tx_last_end_time = tx_from_time + (timestamp_t)(sample_ns * ntx.len);
			}

			if (tx_burst_going && ntx.begin > 0) {
				/* If end of burst flag wasn't sent in last round,
				 * send it now together with one dummy sample.
				 * One sample is sent because trying to send
				 * zero samples gave a timeout error. */
				if(tx_burst_going) {
					txbuf[0] = 0;
					const void *txbuffs[] = { txbuf };
					flags = SOAPY_SDR_HAS_TIME | SOAPY_SDR_END_BURST;
					ret = SoapySDRDevice_writeStream(sdr, txstream,
						txbuffs, 1, &flags,
						tx_from_time, timeout_us);
					if(ret <= 0)
						soapy_fail("SoapySDRDevice_writeStream (end of burst)", ret);
					tx_burst_going = 0;
				}
			}

			if (ntx.end > ntx.begin) {
				flags = SOAPY_SDR_HAS_TIME;
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
			} else {
				tx_burst_going = 0;
			}
		}
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
	.flags = SOAPYIO_RX_ON | SOAPYIO_TX_ON,
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
CONFIG_I(flags)
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
CONFIG_END()


const struct signal_io_code soapysdr_io_code = { "soapysdr_io", init, destroy, init_conf, set_conf, set_callbacks, execute };
