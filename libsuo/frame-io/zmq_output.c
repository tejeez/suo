#include "zmq_interface.h"
#include "suo_macros.h"
#include <string.h>
#include <assert.h>
#include <zmq.h>
#include <pthread.h>
#include <signal.h>

// TODO: make these configurable
#define PRINT_DIAGNOSTICS
#define BITS_MAXLEN 0x900
#define DECODED_MAXLEN 0x200

/* One global ZeroMQ context, initialized only once */
void *zmq = NULL;

static void print_fail_zmq(const char *function, int ret)
{
	fprintf(stderr, "%s failed (%d): %s\n", function, ret, zmq_strerror(errno));
}
#define ZMQCHECK(function) do { int ret = (function); if(ret < 0) { print_fail_zmq(#function, ret); goto fail; } } while(0)


struct zmq_output {
	/* Configuration */
	uint32_t flags;

	/* Decoder thread */
	volatile bool decoder_running;
	pthread_t decoder_thread;

	/* ZeroMQ sockets */
	void *z_rx_pub; /* Publish decoded frames */
	void *z_tick_pub; /* Publish ticks */
	void *z_decw, *z_decr; /* Receiver-to-decoder queue */

	/* Callbacks */
	const struct decoder_code *decoder;
	void *decoder_arg;
};

static void *zmq_decoder_main(void*);


static void *init(const void *confv)
{
	const struct zmq_rx_output_conf *conf = confv;
	struct zmq_output *self = calloc(1, sizeof(*self));
	if(self == NULL) return NULL;
	self->flags = conf->flags;

	if(zmq == NULL)
		zmq = zmq_ctx_new();

	self->z_rx_pub = zmq_socket(zmq, ZMQ_PUB);
	if (self->flags & ZMQIO_BIND)
		ZMQCHECK(zmq_bind(self->z_rx_pub, conf->address));
	else
		ZMQCHECK(zmq_connect(self->z_rx_pub, conf->address));

	self->z_tick_pub = zmq_socket(zmq, ZMQ_PUB);
	if (self->flags & ZMQIO_BIND_TICK)
		ZMQCHECK(zmq_bind(self->z_tick_pub, conf->address_tick));
	else
		ZMQCHECK(zmq_connect(self->z_tick_pub, conf->address_tick));

	return self;

fail: // TODO cleanup
	return NULL;
}


static int set_callbacks(void *arg, const struct decoder_code *decoder, void *decoder_arg)
{
	struct zmq_output *self = arg;
	self->decoder = decoder;
	self->decoder_arg = decoder_arg;

	/* Create the decoder thread only if a decoder is set */
	if (decoder != NULL) {
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

		self->decoder_running = 1;
		pthread_create(&self->decoder_thread, NULL, zmq_decoder_main, self);
	}
	return 0;
fail:
	return -1;
}


static int destroy(void *arg)
{
	struct zmq_output *self = arg;
	if(self == NULL) return 0;
	if(self->decoder_running) {
		self->decoder_running = 0;
		pthread_kill(self->decoder_thread, SIGTERM);
		pthread_join(self->decoder_thread, NULL);
	}
	return 0;
}


static void *zmq_decoder_main(void *arg)
{
	struct zmq_output *self = arg;

	char decoded_buf[sizeof(struct frame) + DECODED_MAXLEN];
	struct frame *decoded = (struct frame *)decoded_buf;

	/* Read frames from the receiver-to-decoder queue
	 * transmit buffer queue. */
	while(self->decoder_running) {
		int nread;
		zmq_msg_t input_msg;
		zmq_msg_init(&input_msg);
		nread = zmq_msg_recv(&input_msg, self->z_decr, 0);
		if(nread >= 0) {
			int ndecoded = self->decoder->decode(self->decoder_arg, zmq_msg_data(&input_msg), decoded, DECODED_MAXLEN);
			if(ndecoded >= 0) {
				ZMQCHECK(zmq_send(self->z_rx_pub, decoded, sizeof(struct frame) + ndecoded, 0));
			} else {
				/* Decode failed. TODO: send or save diagnostics somewhere */
			}

#ifdef PRINT_DIAGNOSTICS
		const struct metadata *metadata = &decoded->m;
			printf("%d  Timestamp: %lld ns   Mode: %u  CFO: %E Hz  RSSI: %6.2f dB\n\n",
				ndecoded,
				(long long)metadata->time,
				metadata->mode,
				(double)metadata->cfo, (double)metadata->power);
#endif
		} else {
			print_fail_zmq("zmq_recv", nread);
			goto fail;
		}
		zmq_msg_close(&input_msg);
	}

fail:
	return NULL;
}


static int frame(void *arg, const struct frame *frame)
{
	struct zmq_output *self = arg;

	/* Read straight from the subscriber socket
	 * if decoder thread and its socket is not created */
	void *s = self->z_decw;
	if (s == NULL)
		s = self->z_rx_pub;

	/* Non-blocking send to avoid blocking the receiver in case
	 * decoder runs out of CPU time and ZMQ buffer fills up.
	 * Frames are just discarded with a warning message in the case. */
	ZMQCHECK(zmq_send(s, frame, sizeof(struct frame) + frame->m.len, ZMQ_DONTWAIT));
	return 0;
fail:
	return -1;
}


static int tick(void *arg, timestamp_t timenow)
{
	struct zmq_output *self = arg;
	void *s = self->z_tick_pub;
	if (s == NULL)
		goto fail;
	struct timing msg = {
		.id = 2,
		.flags = 0,
		.time = timenow
	};
	ZMQCHECK(zmq_send(s, &msg, sizeof(msg), ZMQ_DONTWAIT));
	return 0;
fail:
	return -1;
}


const struct zmq_rx_output_conf zmq_rx_output_defaults = {
	.address = "tcp://*:43300",
	.address_tick = "tcp://*:43302",
	.flags = ZMQIO_BIND | ZMQIO_METADATA | ZMQIO_THREAD | ZMQIO_BIND_TICK
};

CONFIG_BEGIN(zmq_rx_output)
CONFIG_C(address)
CONFIG_C(address_tick)
CONFIG_I(flags)
CONFIG_END()

const struct rx_output_code zmq_rx_output_code = { "zmq_output", init, destroy, init_conf, set_conf, set_callbacks, frame, tick };
