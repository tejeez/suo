#ifndef EFRK7_DECODER_H
#define EFRK7_DECODER_H
#include "common.h"

struct efrk7_decoder_conf {
	/* Callbacks */
	struct output_code output;
	void *output_arg;
};

extern const struct decoder_code efrk7_decoder_code;

#endif
