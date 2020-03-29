#include "file_io.h"
#include "suo_macros.h"
#include <stdio.h>



typedef uint8_t cu8_t[2];
typedef int16_t cs16_t[2];
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
	self->receiver = receiver;
	self->receiver_arg = receiver_arg;
	self->transmitter = transmitter;
	self->transmitter_arg = transmitter_arg;
	return 0;
}

// TODO configuration for BUFLEN
#define BUFLEN 4096

static int execute(void *arg)
{
	struct file_io *self = arg;
	enum inputformat inputformat = FORMAT_CF32;
	timestamp_t timestamp = 0;
	sample_t buf2[BUFLEN];

	for(;;) {
		size_t n, i;
		if(inputformat == FORMAT_CU8) {
			cu8_t buf1[BUFLEN];
			n = fread(buf1, sizeof(cu8_t), BUFLEN, stdin);
			if(n == 0) break;
			for(i=0; i<n; i++)
				buf2[i] = (float)buf1[i][0] - 127.4f
					+((float)buf1[i][1] - 127.4f)*I;
		} else if(inputformat == FORMAT_CS16) {
			cs16_t buf1[BUFLEN];
			n = fread(buf1, sizeof(cs16_t), BUFLEN, stdin);
			if(n == 0) break;
			for(i=0; i<n; i++)
				buf2[i] = (float)buf1[i][0]
					+((float)buf1[i][1])*I;
		} else {
			n = fread(buf2, sizeof(sample_t), BUFLEN, stdin);
			if(n == 0) break;
		}
		self->receiver->execute(self->receiver_arg, buf2, n, timestamp);
		timestamp += 1e9 * n / self->conf.samplerate;
	}

	return 0;
}


const struct file_io_conf file_io_defaults = {
	.samplerate = 1e6,
	.input = NULL,
	.output = NULL
};

CONFIG_BEGIN(file_io)
CONFIG_F(samplerate)
CONFIG_C(input)
CONFIG_C(output)
CONFIG_END()


const struct signal_io_code file_io_code = { "file_io", init, destroy, init_conf, set_conf, set_callbacks, execute };
