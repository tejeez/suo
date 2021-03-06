#include "file_io.h"
#include "suo_macros.h"
#include "conversion.h"
#include <stdio.h>
#include <assert.h>


enum inputformat { FORMAT_CU8, FORMAT_CS16, FORMAT_CF32 };

struct file_io {
	const struct receiver_code *receiver;
	void *receiver_arg;
	const struct transmitter_code *transmitter;
	void *transmitter_arg;
	FILE *in, *out;
	struct file_io_conf conf;
};


static void *init(const void *conf)
{
	struct file_io *self;
	self = calloc(1, sizeof(*self));
	if (self == NULL)
		return self;
	self->conf = *(struct file_io_conf*)conf;

	if (self->conf.input != NULL)
		self->in = fopen(self->conf.input, "rb");
	else
		self->in = stdin;
	if (self->in == NULL)
		perror("Failed to open signal input");

	if (self->conf.output != NULL)
		self->out = fopen(self->conf.output, "wb");
	else
		self->out = stdout;

	return self;
}


static int destroy(void *arg)
{
	struct file_io *self = arg;
	if (self->in)
		fclose(self->in);
	if (self->out)
		fclose(self->out);
	return 0;
}


static int set_callbacks(void *arg, const struct receiver_code *receiver, void *receiver_arg, const struct transmitter_code *transmitter, void *transmitter_arg)
{
	struct file_io *self = arg;
	self->receiver_arg = receiver_arg;
	self->receiver = receiver;
	self->transmitter_arg = transmitter_arg;
	self->transmitter = transmitter;
	return 0;
}

// TODO configuration for BUFLEN
#define BUFLEN 4096

static int execute(void *arg)
{
	struct file_io *self = arg;
	enum inputformat inputformat = self->conf.format;
	timestamp_t timestamp = 0, tx_latency_time = 0;
	sample_t buf2[BUFLEN];

	if (self->in == NULL)
		return -1;
	for(;;) {
		size_t n = BUFLEN;
		if (self->receiver != NULL) {
			if(inputformat == FORMAT_CU8) {
				cu8_t buf1[BUFLEN];
				n = fread(buf1, sizeof(cu8_t), BUFLEN, self->in);
				if(n == 0) break;
				cu8_to_cf(buf1, buf2, n);
			} else if(inputformat == FORMAT_CS16) {
				cs16_t buf1[BUFLEN];
				n = fread(buf1, sizeof(cs16_t), BUFLEN, self->in);
				if(n == 0) break;
				cs16_to_cf(buf1, buf2, n);
			} else {
				n = fread(buf2, sizeof(sample_t), BUFLEN, self->in);
				if(n == 0) break;
			}
			self->receiver->execute(self->receiver_arg, buf2, n, timestamp);
		}

		if (self->transmitter != NULL) {
			assert(n <= BUFLEN);
			tx_return_t tr;
			tr = self->transmitter->execute(self->transmitter_arg, buf2, n, timestamp + tx_latency_time);
			fwrite(buf2, sizeof(sample_t), tr.len, self->out);
		}

		timestamp += 1e9 * n / self->conf.samplerate;
	}

	return 0;
}


const struct file_io_conf file_io_defaults = {
	.samplerate = 1e6,
	.input = NULL,
	.output = NULL,
	.format = 1
};

CONFIG_BEGIN(file_io)
CONFIG_F(samplerate)
CONFIG_C(input)
CONFIG_C(output)
CONFIG_I(format)
CONFIG_END()


const struct signal_io_code file_io_code = { "file_io", init, destroy, init_conf, set_conf, set_callbacks, execute };
