#include "zmq_interface.h"
#include "suo_macros.h"
#include <string.h>
#include <assert.h>
#include <zmq.h>
#include <pthread.h>
#include <signal.h>

#define PRINT_DIAGNOSTICS

// TODO: make these configurable
#define BITS_MAXLEN 0x900
#define DECODED_MAXLEN 0x200

/* One global ZeroMQ context, initialized only once */
void *zmq = NULL;

static void print_fail_zmq(const char *function, int ret)
{
	fprintf(stderr, "%s failed (%d): %s\n", function, ret, zmq_strerror(errno));
}
#define ZMQCHECK(function) { int ret = (function); if(ret < 0) { print_fail_zmq(#function, ret); goto fail; } }


struct zmq_output {
	/* State */
	volatile bool running;

	/* Threads */
	pthread_t decoder_thread;

	/* ZeroMQ sockets */
	void *z_rx_pub; /* Publish decoded frames */
	void *z_decw, *z_decr; /* Receiver-to-decoder queue */

	/* Callbacks */
	struct decoder_code decoder;
	void *decoder_arg;
};

static void *zmq_decoder_main(void*);


static void *zmq_output_init(const void *confv)
{
	const struct zmq_rx_output_conf *conf = confv;
	struct zmq_output *self = malloc(sizeof(*self));
	if(self == NULL) return NULL;
	memset(self, 0, sizeof(*self));

	if(zmq == NULL)
		zmq = zmq_ctx_new();

	self->z_rx_pub = zmq_socket(zmq, ZMQ_PUB);
	ZMQCHECK(zmq_bind(self->z_rx_pub, conf->address));

	/* Decoder runs in a separate thread.
	 * ZeroMQ inproc pair transfers the frames to be decoded.
	 * Initialize writing and reading ends of the pair
	 * and then start a thread. */

	/* Create unique name in case multiple instances are initialized */
	char pair_name[20];
	static char pair_number=0;
	snprintf(pair_name, 20, "inproc://dec_%d", ++pair_number);

	self->z_decr = zmq_socket(zmq, ZMQ_PAIR);
	ZMQCHECK(zmq_bind(self->z_decr, pair_name));
	self->z_decw = zmq_socket(zmq, ZMQ_PAIR);
	ZMQCHECK(zmq_connect(self->z_decw, pair_name));

#if 0
	self->running = 1;
	pthread_create(&self->decoder_thread, NULL, zmq_decoder_main, self);
#endif

	return self;

fail: // TODO cleanup
	return NULL;
}


static int zmq_output_set_callbacks(void *arg, const struct decoder_code *decoder, void *decoder_arg)
{
	struct zmq_output *self = arg;
	self->decoder = *decoder;
	self->decoder_arg = decoder_arg;

	/* Create thread only after callbacks have been set
	 * so it doesn't accidentally try to call them before */
	self->running = 1;
	pthread_create(&self->decoder_thread, NULL, zmq_decoder_main, self);
	return 0;
}


static int zmq_output_destroy(void *arg)
{
	struct zmq_output *self = arg;
	if(self == NULL) return 0;
	if(self->running) {
		self->running = 0;
		pthread_kill(self->decoder_thread, SIGTERM);
		pthread_join(self->decoder_thread, NULL);
	}
	return 0;
}


static void *zmq_decoder_main(void *arg)
{
	struct zmq_output *self = arg;

	bit_t bits[BITS_MAXLEN];
	uint8_t decoded[DECODED_MAXLEN];
	struct rx_metadata metadata_;
	struct rx_metadata *metadata = &metadata_;

	// TODO: pass metadata to this thread
	memset(metadata, 0, sizeof(*metadata));

	/* Read frames from the receiver-to-decoder queue
	 * transmit buffer queue. */
	while(self->running) {
		int nread;

		nread = zmq_recv(self->z_decr, bits, sizeof(bit_t)*BITS_MAXLEN, 0);
		if(nread >= 0) {
			int nbits = nread / sizeof(bit_t);

			int ndecoded = self->decoder.decode(self->decoder_arg,
				bits, nbits, decoded, DECODED_MAXLEN, metadata);
			assert(ndecoded <= DECODED_MAXLEN);

			if(ndecoded >= 0) {
				ZMQCHECK(zmq_send(self->z_rx_pub, decoded, ndecoded, 0));
			} else {
				/* Decode failed. TODO: send or save diagnostics somewhere */
			}

#ifdef PRINT_DIAGNOSTICS
			printf("Decode: %d\n", ndecoded);
			printf("Timestamp: %lld ns   CFO: %E Hz  CFOD: %E Hz  RSSI: %6.2f dB  SNR: %6.2f dB  BER: %E  OER: %E  Mode: %u\n\n",
				(long long)metadata->timestamp,
				(double)metadata->cfo, (double)metadata->cfod,
				(double)metadata->rssi, (double)metadata->snr,
				(double)metadata->ber, (double)metadata->oer,
				metadata->mode);
#endif
		} else {
			print_fail_zmq("zmq_recv", nread);
			goto fail;
		}
	}

fail:
	return NULL;
}


static int zmq_output_frame(void *arg, const bit_t *bits, size_t nbits, struct rx_metadata *metadata)
{
	struct zmq_output *self = arg;

	// TODO: pass metadata to decoder thread
	(void)metadata;

	/* Non-blocking send to avoid blocking the receiver in case
	 * decoder runs out of CPU time and ZMQ buffer fills up.
	 * Frames are just discarded with a warning message in the case. */
	ZMQCHECK(zmq_send(self->z_decw, bits, sizeof(bit_t)*nbits, ZMQ_DONTWAIT));
	return 0;
fail:
	return -1;
}


const struct zmq_rx_output_conf zmq_rx_output_defaults = {
	.address = "tcp://*:43300"
};

CONFIG_BEGIN(zmq_rx_output)
CONFIG_C(address)
CONFIG_END()

const struct rx_output_code zmq_rx_output_code = { "zmq_output", zmq_output_init, zmq_output_destroy, init_conf, set_conf, zmq_output_set_callbacks, zmq_output_frame };
