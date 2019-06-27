#include "libsuo/suo.h"
#include "zmq_interface.h"
#include <string.h>
#include <assert.h>
#include <zmq.h>
#include <pthread.h>
#include <signal.h>

/* One global ZeroMQ context, initialized only once */
void *zmq = NULL;

void print_fail_zmq(const char *function, int ret)
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
	ZMQCHECK(zmq_bind(self->z_rx_pub, conf->zmq_addr));

	/* Decoder runs in a separate thread.
	 * ZeroMQ inproc pair transfers the frames to be decoded.
	 * Initialize writing and reading ends of the pair
	 * and then start a thread. */

	/* Create unique name in case multiple instances are initialized */
	char pair_name[20];
	static int pair_number=0;
	snprintf(pair_name, 20, "inproc://dec_%d", ++pair_number);

	self->z_decr = zmq_socket(zmq, ZMQ_PAIR);
	ZMQCHECK(zmq_bind(self->z_decr, pair_name));
	self->z_decw = zmq_socket(zmq, ZMQ_PAIR);
	ZMQCHECK(zmq_connect(self->z_decw, pair_name));

	self->running = 1;
	pthread_create(&self->decoder_thread, NULL, zmq_decoder_main, self);

	return self;

fail: // TODO cleanup
	return NULL;
}


static int zmq_output_set_callbacks(void *arg, const struct decoder_code *decoder, void *decoder_arg)
{
	struct zmq_output *self = arg;
	self->decoder = *decoder;
	self->decoder_arg = decoder_arg;
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


#define BITS_MAXLEN 0x200
#define DECODED_MAXLEN 0x200
static void *zmq_decoder_main(void *arg)
{
	struct zmq_output *self = arg;

	bit_t bits[BITS_MAXLEN];
	uint8_t decoded[DECODED_MAXLEN];

	/* Read frames from the receiver-to-decoder queue
	 * transmit buffer queue. */
	while(self->running) {
		int nread;

		nread = zmq_recv(self->z_decr, bits, sizeof(bit_t)*BITS_MAXLEN, 0);
		if(nread >= 0) {
			int nbits = nread / sizeof(bit_t);

			int ndecoded = self->decoder.decode(self->decoder_arg,
				bits, nbits, decoded, DECODED_MAXLEN);
			assert(ndecoded <= DECODED_MAXLEN);

			if(ndecoded >= 0) {
				ZMQCHECK(zmq_send(self->z_rx_pub, decoded, ndecoded, 0));
			} else {
				/* Decode failed. TODO: send or save diagnostics somewhere */
			}
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

	printf("%lld %f\n", (long long)metadata->timestamp, (double)metadata->cfo);

	/* Non-blocking send to avoid blocking the receiver in case
	 * decoder runs out of CPU time and ZMQ buffer fills up.
	 * Frames are just discarded with a warning message in the case. */
	ZMQCHECK(zmq_send(self->z_decw, bits, sizeof(bit_t)*nbits, ZMQ_DONTWAIT));
	return 0;
fail:
	return -1;
}






struct zmq_input {
	/* State */
	volatile bool running;

	/* Threads */
	pthread_t encoder_thread;

	/* ZeroMQ sockets */
	void *z_tx_sub; /* Subscribe frames to be encoded */
	void *z_txbuf_w, *z_txbuf_r; /* Encoded-to-transmitter queue */

	/* Callbacks */
	struct encoder_code encoder;
	void *encoder_arg;
};


static int zmq_input_destroy(void *);
static void *zmq_encoder_main(void*);


static void *zmq_input_init(const void *confv)
{
	const struct zmq_tx_input_conf *conf = confv;
	struct zmq_input *self = malloc(sizeof(*self));
	if(self == NULL) return NULL;
	memset(self, 0, sizeof(*self));
	
	/* If this is called from another thread than zmq_output_init,
	 * a race condition is possible where two contexts are created.
	 * Just initialize everything in one thread to avoid problems. */
	if(zmq == NULL)
		zmq = zmq_ctx_new();

	self->z_tx_sub = zmq_socket(zmq, ZMQ_SUB);
	ZMQCHECK(zmq_bind(self->z_tx_sub, conf->zmq_addr));
	ZMQCHECK(zmq_setsockopt(self->z_tx_sub, ZMQ_SUBSCRIBE, "", 0));

	/* Create unique name in case multiple instances are initialized */
	char pair_name[20];
	static int pair_number=0;
	snprintf(pair_name, 20, "inproc://txbuf_%d", ++pair_number);
	
	self->z_txbuf_r = zmq_socket(zmq, ZMQ_PAIR);
	ZMQCHECK(zmq_bind(self->z_txbuf_r, pair_name));
	self->z_txbuf_w = zmq_socket(zmq, ZMQ_PAIR);
	ZMQCHECK(zmq_connect(self->z_txbuf_w, pair_name));

	self->running = 1;
	pthread_create(&self->encoder_thread, NULL, zmq_encoder_main, self);

	return self;
fail:
	zmq_input_destroy(self);
	return NULL;
}


int zmq_input_set_callbacks(void *arg, const struct encoder_code *encoder, void *encoder_arg)
{
	struct zmq_input *self = arg;
	self->encoder = *encoder;
	self->encoder_arg = encoder_arg;
	return 0;
}


static void *zmq_encoder_main(void *arg)
{
	struct zmq_input *self = arg;

	bit_t bits[BITS_MAXLEN];
	uint8_t decoded[DECODED_MAXLEN];

	/* Read frames from the SUB socket, encode them and put them
	 * in the transmit buffer queue. */
	while(self->running) {
		int nread, nbits;

		nread = zmq_recv(self->z_tx_sub, decoded, DECODED_MAXLEN, 0);
		if(nread >= 0) {
			nbits = self->encoder.encode(self->encoder_arg,
				bits, BITS_MAXLEN, decoded, nread);
			assert(nbits <= BITS_MAXLEN);

			if(nbits >= 0) {
				ZMQCHECK(zmq_send(self->z_txbuf_w, bits, sizeof(bit_t)*nbits, 0));
			} else {
				/* Encode failed, should not happen */
			}
		} else {
			print_fail_zmq("zmq_recv", nread);
			goto fail;
		}
	}

fail:
	return NULL;
}


static int zmq_input_get_frame(void *arg, bit_t *bits, size_t max_nbits, timestamp_t timestamp, struct tx_metadata *metadata)
{
	int nread, nbits;
	struct zmq_input *self = arg;

	(void)timestamp; // Not used since protocol stack doesn't run here
	(void)metadata; // TODO

	nread = zmq_recv(self->z_txbuf_r, bits, sizeof(bit_t)*max_nbits, ZMQ_DONTWAIT);
	if(nread >= 0) {
		nbits = nread / sizeof(bit_t);
		return nbits;
	} else {
		/* No frame in queue */
		return -1;
	}
}


static int zmq_input_destroy(void *arg)
{
	struct zmq_input *self = arg;
	if(self == NULL) return 0;
	if(self->running) {
		self->running = 0;
		pthread_kill(self->encoder_thread, SIGTERM);
		pthread_join(self->encoder_thread, NULL);
	}
	return 0;
}


const struct rx_output_code zmq_rx_output_code = { zmq_output_init, zmq_output_destroy, zmq_output_set_callbacks, zmq_output_frame };

const struct tx_input_code zmq_tx_input_code = { zmq_input_init, zmq_input_destroy, zmq_input_set_callbacks, zmq_input_get_frame };
