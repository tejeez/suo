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
	void *z_rx_pub;
	void *z_decw, *z_decr;

	/* Callbacks */
	struct decoder_code decoder;
	void *decoder_arg;
};

static void *zmq_decoder_main(void*);


static void *zmq_output_init(const void *confv)
{
	const struct zmq_rx_output_conf *conf = confv;
	struct zmq_output *self = malloc(sizeof(struct zmq_output));
	if(self == NULL) return NULL;
	memset(self, 0, sizeof(*self));

	self->decoder = *conf->decoder;
	self->decoder_arg = conf->decoder_arg;

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


static int zmq_output_destroy(void *arg)
{
	struct zmq_output *self = arg;
	pthread_kill(self->decoder_thread, SIGTERM);
	pthread_join(self->decoder_thread, NULL);
	return 0;
}


#define BITS_MAXLEN 0x200
#define DECODED_MAXLEN 0x200
static void *zmq_decoder_main(void *arg)
{
	int ret;
	struct zmq_output *self = arg;

	bit_t bits[BITS_MAXLEN];
	uint8_t decoded[DECODED_MAXLEN];

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
			print_fail_zmq("zmq_recv", ret);
			goto fail;
		}
	}

fail:
	return NULL;
}


static int zmq_output_frame(void *arg, const bit_t *bits, size_t nbits)
{
	struct zmq_output *self = arg;

	/* Non-blocking send to avoid blocking the receiver in case
	 * decoder runs out of CPU time and ZMQ buffer fills up.
	 * Frames are just discarded with a warning message in the case. */
	ZMQCHECK(zmq_send(self->z_decw, bits, sizeof(bit_t)*nbits, ZMQ_DONTWAIT));
	return 0;
fail:
	return -1;
}






struct zmq_tx_input {
	void *z_tx_sub;
	struct encoder_code encoder;
	void *encoder_arg;
};


static int zmq_tx_input_destroy(void *);
static void *zmq_encoder_main(void*);


static void *zmq_tx_input_init(const void *confv)
{
	const struct zmq_tx_input_conf *conf = confv;
	struct zmq_tx_input *self = malloc(sizeof(struct zmq_tx_input));
	if(self == NULL) return NULL;
	memset(self, 0, sizeof(*self));
	
	self->encoder = *conf->encoder;
	self->encoder_arg = conf->encoder_arg;

	/* If this is called from another thread than zmq_output_init,
	 * a race condition is possible where two contexts are created.
	 * Just initialize everything in one thread to avoid problems. */
	if(zmq == NULL)
		zmq = zmq_ctx_new();

	self->z_tx_sub = zmq_socket(zmq, ZMQ_SUB);
	ZMQCHECK(zmq_bind(self->z_tx_sub, conf->zmq_addr));
	ZMQCHECK(zmq_setsockopt(self->z_tx_sub, ZMQ_SUBSCRIBE, "", 0));

	/* TODO start thread */
	(void)zmq_encoder_main;

	return self;
fail:
	zmq_tx_input_destroy(self);
	return NULL;
}


static int zmq_tx_input_destroy(void *arg)
{
	struct zmq_tx_input *self = arg;
	if(self == NULL) return 0;
	/* TODO kill thread etc */
	return 0;
}


static int zmq_tx_input_get_frame(void *arg, bit_t *bits, size_t nbits, struct transmitter_metadata *metadata)
{
	struct zmq_tx_input *self = arg;
	/* TODO receive from encoder thread etc */
	(void)self; (void)bits; (void)nbits; (void)metadata;
	return -1;
}


static void *zmq_encoder_main(void *arg)
{
	struct zmq_tx_input *self = arg;
	/* TODO loop */
	(void)self;
	return 0;
}



const struct rx_output_code zmq_rx_output_code = { zmq_output_init, zmq_output_destroy, zmq_output_frame };

const struct tx_input_code zmq_tx_input_code = { zmq_tx_input_init, zmq_tx_input_destroy, zmq_tx_input_get_frame };
