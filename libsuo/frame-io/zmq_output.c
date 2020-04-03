#include "zmq_interface.h"
#include "suo_macros.h"
#include <string.h>
#include <assert.h>
#include <zmq.h>
#include <pthread.h>
#include <signal.h>

#define PRINT_DIAGNOSTICS
#define DECODER_THREAD // TODO: configuration flag for this

// TODO: make these configurable
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

#ifdef DECODER_THREAD // TODO: make it a configuration flag
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
#endif

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
#ifdef DECODER_THREAD // TODO: make it a configuration flag
	pthread_create(&self->decoder_thread, NULL, zmq_decoder_main, self);
#endif
	return 0;
}


static int zmq_output_destroy(void *arg)
{
	struct zmq_output *self = arg;
	if(self == NULL) return 0;
	if(self->running) {
		self->running = 0;
#ifdef DECODER_THREAD // TODO: make it a configuration flag
		pthread_kill(self->decoder_thread, SIGTERM);
		pthread_join(self->decoder_thread, NULL);
#endif
	}
	return 0;
}


static void *zmq_decoder_main(void *arg)
{
	struct zmq_output *self = arg;

	char decoded_buf[sizeof(struct frame) + DECODED_MAXLEN];
	struct frame *decoded = (struct frame *)decoded_buf;

	struct metadata metadata_;
	struct metadata *metadata = &metadata_;

	// TODO: pass metadata to this thread
	memset(metadata, 0, sizeof(*metadata));

	/* Read frames from the receiver-to-decoder queue
	 * transmit buffer queue. */
	while(self->running) {
		int nread;
		zmq_msg_t input_msg;
		zmq_msg_init(&input_msg);
		nread = zmq_msg_recv(&input_msg, self->z_decr, 0);
		if(nread >= 0) {
			int ndecoded = self->decoder.decode(self->decoder_arg, zmq_msg_data(&input_msg), decoded, DECODED_MAXLEN);
			if(ndecoded >= 0) {
				ZMQCHECK(zmq_send(self->z_rx_pub, decoded, sizeof(struct frame) + ndecoded, 0));
			} else {
				/* Decode failed. TODO: send or save diagnostics somewhere */
			}

#ifdef PRINT_DIAGNOSTICS
			printf("Decode: %d\n", ndecoded);
			printf("Timestamp: %lld ns   Mode: %u  CFO: %E Hz  RSSI: %6.2f dB\n\n",
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


static int zmq_output_frame(void *arg, const struct frame *frame)
{
	struct zmq_output *self = arg;

	/* Non-blocking send to avoid blocking the receiver in case
	 * decoder runs out of CPU time and ZMQ buffer fills up.
	 * Frames are just discarded with a warning message in the case. */
#ifdef DECODER_THREAD // TODO: make it a configuration flag
	ZMQCHECK(zmq_send(self->z_decw, frame, sizeof(struct frame) + frame->m.len, ZMQ_DONTWAIT));
#else
	ZMQCHECK(zmq_send(self->z_rx_pub, frame, sizeof(struct frame) + frame->m.len, ZMQ_DONTWAIT));
#endif
	return 0;
fail:
	return -1;
}


const struct zmq_rx_output_conf zmq_rx_output_defaults = {
	.address = "tcp://*:43300",
	.flags = ZMQIO_BIND | ZMQIO_METADATA | ZMQIO_THREAD
};

CONFIG_BEGIN(zmq_rx_output)
CONFIG_C(address)
CONFIG_I(flags)
CONFIG_END()

const struct rx_output_code zmq_rx_output_code = { "zmq_output", zmq_output_init, zmq_output_destroy, init_conf, set_conf, zmq_output_set_callbacks, zmq_output_frame };
