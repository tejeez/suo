#include "modem/simple_receiver.h"
#include "modem/burst_dpsk_receiver.h"
#include "modem/simple_transmitter.h"
#include "coding/basic_decoder.h"
#include "coding/efrk7_decoder.h"
#include "coding/basic_encoder.h"

/* Tables to list all the modules implemented */

const struct receiver_code *suo_receivers[] = {
	&simple_receiver_code,
	&burst_dpsk_receiver_code,
	NULL
};

const struct transmitter_code *suo_transmitters[] = {
	&simple_transmitter_code,
	NULL
};

const struct decoder_code *suo_decoders[] = {
	&basic_decoder_code,
	NULL
};

const struct encoder_code *suo_encoders[] = {
	&basic_encoder_code,
	NULL,
};
