#include "zmq_interface.h"
#include "suo_macros.h"
#include <string.h>
#include <assert.h>
#include <zmq.h>
#include <pthread.h>
#include <signal.h>


// TODO: make these configurable
#define PRINT_DIAGNOSTICS
#define ENCODED_MAXLEN 0x900

/* One global ZeroMQ context, initialized only once */
extern void *zmq;

static void print_fail_zmq(const char *function, int ret)
{
	fprintf(stderr, "%s failed (%d): %s\n", function, ret, zmq_strerror(errno));
}
#define ZMQCHECK(function) do { int ret = (function); if(ret < 0) { print_fail_zmq(#function, ret); goto fail; } } while(0)


struct zmq_input {
	/* Configuration */
	uint32_t flags;

	/* Encoder thread */
	volatile bool encoder_running;
	pthread_t encoder_thread;

	/* ZeroMQ sockets */
	void *z_tx_sub; /* Subscribe frames to be encoded */
	void *z_tick_pub; /* Publish ticks */
	void *z_txbuf_w, *z_txbuf_r; /* Encoded-to-transmitter queue */

	/* Callbacks */
	const struct encoder_code *encoder;
	void *encoder_arg;
};


static int destroy(void *);
static void *zmq_encoder_main(void*);


static void *init(const void *confv)
{
	const struct zmq_tx_input_conf *conf = confv;
	struct zmq_input *self = calloc(1, sizeof(*self));
	if(self == NULL) return NULL;
	self->flags = conf->flags;
	self->z_txbuf_r = NULL;

	/* If this is called from another thread than zmq_output_init,
	 * a race condition is possible where two contexts are created.
	 * Just initialize everything in one thread to avoid problems. */
	if(zmq == NULL)
		zmq = zmq_ctx_new();

	self->z_tx_sub = zmq_socket(zmq, ZMQ_SUB);
	if (self->flags & ZMQIO_BIND)
		ZMQCHECK(zmq_bind(self->z_tx_sub, conf->address));
	else
		ZMQCHECK(zmq_connect(self->z_tx_sub, conf->address));
	ZMQCHECK(zmq_setsockopt(self->z_tx_sub, ZMQ_SUBSCRIBE, "", 0));

	self->z_tick_pub = zmq_socket(zmq, ZMQ_PUB);
	if (self->flags & ZMQIO_BIND_TICK)
		ZMQCHECK(zmq_bind(self->z_tick_pub, conf->address_tick));
	else
		ZMQCHECK(zmq_connect(self->z_tick_pub, conf->address_tick));

	return self;
fail:
	destroy(self);
	return NULL;
}


static int set_callbacks(void *arg, const struct encoder_code *encoder, void *encoder_arg)
{
	struct zmq_input *self = arg;
	self->encoder_arg = encoder_arg;
	self->encoder = encoder;

	/* Create the encoder thread only if an encoder is set */
	if (encoder != NULL) {
		/* Create a socket for inter-thread communication.
		 * Create unique name in case multiple instances are initialized */
		char pair_name[20];
		static char pair_number=0;
		snprintf(pair_name, 20, "inproc://txbuf_%d", ++pair_number);

		self->z_txbuf_r = zmq_socket(zmq, ZMQ_PAIR);
		ZMQCHECK(zmq_bind(self->z_txbuf_r, pair_name));
		self->z_txbuf_w = zmq_socket(zmq, ZMQ_PAIR);
		ZMQCHECK(zmq_connect(self->z_txbuf_w, pair_name));

		self->encoder_running = 1;
		pthread_create(&self->encoder_thread, NULL, zmq_encoder_main, self);
	}
	return 0;
fail:
	return -1;
}


static void *zmq_encoder_main(void *arg)
{
	struct zmq_input *self = arg;

	char encoded_buf[sizeof(struct frame) + ENCODED_MAXLEN];
	struct frame *encoded = (struct frame *)encoded_buf;

	/* Read frames from the SUB socket, encode them and put them
	 * in the transmit buffer queue. */
	while (self->encoder_running) {
		int nread, nbits;
		zmq_msg_t input_msg;
		zmq_msg_init(&input_msg);
		nread = zmq_msg_recv(&input_msg, self->z_tx_sub, 0);
		if(nread >= 0) {
			nbits = self->encoder->encode(self->encoder_arg,
				zmq_msg_data(&input_msg), encoded, ENCODED_MAXLEN);
			assert(nbits <= ENCODED_MAXLEN);

			if(nbits >= 0) {
				ZMQCHECK(zmq_send(self->z_txbuf_w, encoded, sizeof(struct frame) + nbits, 0));
			} else {
				/* Encode failed, should not happen */
			}
		} else {
			print_fail_zmq("zmq_recv", nread);
			goto fail;
		}
		zmq_msg_close(&input_msg);
	}

fail:
	return NULL;
}


static int get_frame(void *arg, struct frame *frame, size_t maxlen, timestamp_t timenow)
{
	int nread;
	struct zmq_input *self = arg;

	(void)timenow; // Not used since protocol stack doesn't run here

	/* If encoder is not set, the encoder thread is not created
	 * and the inter-thread socket isn't created either.
	 * Read straight from the subscriber socket in that case. */
	void *s = self->z_txbuf_r;
	if (s == NULL)
		s = self->z_tx_sub;
	nread = zmq_recv(s, frame, sizeof(*frame) + maxlen, ZMQ_DONTWAIT);

	if (nread <= 0) {
		/* No frame in queue */
		return -1;
	} else if((size_t)nread == sizeof(*frame) + frame->m.len) {
		return frame->m.len;
	} else {
		fprintf(stderr, "Warning: too long frame?\n");
		return -1;
	}
}


static int destroy(void *arg)
{
	struct zmq_input *self = arg;
	if(self == NULL) return 0;
	if(self->encoder_running) {
		self->encoder_running = 0;
		pthread_kill(self->encoder_thread, SIGTERM);
		pthread_join(self->encoder_thread, NULL);
	}
	return 0;
}


static int tick(void *arg, timestamp_t timenow)
{
	struct zmq_input *self = arg;
	void *s = self->z_tick_pub;
	if (s == NULL)
		goto fail;
	struct timing msg = {
		.id = 3,
		.flags = 0,
		.time = timenow
	};
	ZMQCHECK(zmq_send(s, &msg, sizeof(msg), ZMQ_DONTWAIT));
	return 0;
fail:
	return -1;
}


const struct zmq_tx_input_conf zmq_tx_input_defaults = {
	.address = "tcp://*:43301",
#if 1
	// transmit ticks in a separate socket
	.address_tick = "tcp://*:43303",
	.flags = ZMQIO_BIND | ZMQIO_METADATA | ZMQIO_THREAD | ZMQIO_BIND_TICK
#else
	// transmit ticks in the RX socket
	.address_tick = "tcp://localhost:43300",
	.flags = ZMQIO_BIND | ZMQIO_METADATA | ZMQIO_THREAD
#endif
};

CONFIG_BEGIN(zmq_tx_input)
CONFIG_C(address)
CONFIG_C(address_tick)
CONFIG_I(flags)
CONFIG_END()

const struct tx_input_code zmq_tx_input_code = { "zmq_input", init, destroy, init_conf, set_conf, set_callbacks, get_frame, tick };
