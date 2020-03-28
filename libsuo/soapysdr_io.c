/* SoapySDR I/O:
 * Main loop with SoapySDR interfacing
 */

#include "suo.h"
#include "suo_macros.h"
#include "soapysdr_io.h"

#include <string.h>
#include <stdio.h>
#include <signal.h>
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



volatile int running = 1;

void sighandler(int sig)
{
	(void)sig;
	running = 0;
}


typedef unsigned char sample1_t[2];

// TODO make BUFLEN configurable
#define BUFLEN 2048

int execute(void *arg)
{
    struct soapysdr_io *self = arg;
	SoapySDRDevice *sdr = NULL;
	SoapySDRStream *rxstream = NULL, *txstream = NULL;

	struct soapysdr_io_conf *const radioconf = &self->conf;

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

	SoapySDRKwargs args = {};
#if 0
	/* Pass command line parameters prefixed with soapy- */
	int i;
	for(i=1; i<argc-1; i++) {
		if(strncmp(argv[i], "soapy-", 6) == 0) {
			SoapySDRKwargs_set(&args, argv[i]+6, argv[i+1]);
		}
	}
#else
    // TODO: configuration for these
    SoapySDRKwargs_set(&args, "driver", "lime");
#endif
	sdr = SoapySDRDevice_make(&args);
	SoapySDRKwargs_clear(&args);
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

	if(radioconf->tx_on) {
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

	if(radioconf->tx_on) {
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
	if(radioconf->tx_on) {
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
	if(radioconf->tx_on)
		SOAPYCHECK(SoapySDRDevice_activateStream, sdr,
			txstream, 0, 0, 0);


	/*----------------------------
	 --------- Main loop ---------
	 -----------------------------*/

	bool tx_burst_going = 0;
	while(running) {
		sample_t rxbuf[BUFLEN];
		sample_t txbuf[BUFLEN];
		void *rxbuffs[] = { rxbuf };
		const void *txbuffs[] = { txbuf };
		int flags = 0;
		long long rx_timestamp = 0;
		int ret = SoapySDRDevice_readStream(sdr, rxstream,
			rxbuffs, BUFLEN, &flags, &rx_timestamp, 200000);
		// TODO: implement metadata and add timestamps there
		if(ret > 0) {
			self->receiver->execute(self->receiver_arg, rxbuf, ret, rx_timestamp);
		} else if(ret <= 0) {
			soapy_fail("SoapySDRDevice_readStream", ret);
		}

		if(radioconf->tx_on) {
			/* Handling of TX timestamps and burst start/end
			 * might change. Not sure if this is the best way.
			 * Maybe the modem should tell when a burst ends
			 * in an additional field in the metadata struct? */
			int ntx;
			timestamp_t tx_timestamp = rx_timestamp + radioconf->rx_tx_latency;
			ntx = self->transmitter->execute(self->transmitter_arg, txbuf, BUFLEN, &tx_timestamp);
			if(ntx > 0) {
				flags = SOAPY_SDR_HAS_TIME;
				/* If there were less than the maximum number of samples,
				 * assume a burst has ended */
				if(ntx < BUFLEN) {
					flags |= SOAPY_SDR_END_BURST;
					tx_burst_going = 0;
				} else {
					tx_burst_going = 1;
				}

				ret = SoapySDRDevice_writeStream(sdr, txstream,
					txbuffs, ntx, &flags,
					tx_timestamp, 100000);
				if(ret <= 0)
					soapy_fail("SoapySDRDevice_writeStream", ret);
			} else {
				/* Nothing to transmit.
				 * If end of burst flag wasn't sent in last round,
				 * send it now together with one dummy sample.
				 * One sample is sent because trying to send
				 * zero samples gave a timeout error. */
				if(tx_burst_going) {
					txbuf[0] = 0;
					flags = SOAPY_SDR_HAS_TIME | SOAPY_SDR_END_BURST;
					ret = SoapySDRDevice_writeStream(sdr, txstream,
						txbuffs, 1, &flags,
						rx_timestamp + radioconf->rx_tx_latency, 100000);
					if(ret <= 0)
						soapy_fail("SoapySDRDevice_writeStream (end of burst)", ret);
				}
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


void *init(const void *conf)
{
    struct soapysdr_io *self;
    self = malloc(sizeof(*self));
    if (self == NULL)
        return self;
    self->conf = *(struct soapysdr_io_conf*)conf;
    return self;
}


int destroy(void *arg)
{
    // TODO
    (void)arg;
    return 0;
}


int set_callbacks(void *arg, const struct receiver_code *receiver, void *receiver_arg, const struct transmitter_code *transmitter, void *transmitter_arg)
{
    struct soapysdr_io *self = arg;
    self->receiver = receiver;
    self->receiver_arg = receiver_arg;
    self->transmitter = transmitter;
    self->transmitter_arg = transmitter_arg;
    return 0;
}


const struct soapysdr_io_conf soapysdr_io_defaults = {
    .samplerate = 1e6,
    .rx_centerfreq = 433.8e6,
    .tx_centerfreq = 433.8e6,
    .rx_gain = 60,
    .tx_gain = 80,
    .rx_channel = 0,
    .tx_channel = 0,
    .tx_on = 0,
    .rx_tx_latency = 50000000,
    // Default antennas for testing with LimeSDR
    .rx_antenna = "LNAL",
    .tx_antenna = "TX/RX"
};

CONFIG_BEGIN(soapysdr_io)
CONFIG_F(samplerate)
CONFIG_F(rx_centerfreq)
CONFIG_F(tx_centerfreq)
CONFIG_F(rx_gain)
CONFIG_F(tx_gain)
CONFIG_I(rx_channel)
CONFIG_I(tx_channel)
CONFIG_I(tx_on)
CONFIG_F(rx_tx_latency)
CONFIG_C(rx_antenna)
CONFIG_C(tx_antenna)
CONFIG_END()


const struct signal_io_code soapysdr_io_code = { init, destroy, init_conf, set_conf, set_callbacks, execute };
