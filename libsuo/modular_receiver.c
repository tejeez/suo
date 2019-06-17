#include "modular_receiver.h"

struct receiver_state {
#if 0
	const struct acq_code       acq;
	void                       *acq_arg;
	const struct demod_code     demod;
	void                       *demod_arg;
	const struct deframer_code  deframer;
	void                       *deframer_arg;
	const struct decoder_code   decoder;
	void                       *decoder_arg;
#endif
};


void *receiver_init(struct basic_receiver_conf *conf) {
	struct receiver_state *self = malloc(sizeof(struct receiver_state));
	if(self == NULL) return NULL;

	/* TODO everything */
	struct receiver_conf conf = {
		.acq      = preamble_acq_code,
		.acq_conf = &(const struct preamble_acq_conf){
		},
		.demod      = fsk_demod_code,
		.demod_conf = &(const struct fsk_demod_conf){
			.id = 4,
			.sps = 4
		},
		.deframer      = syncword_deframer_code,
		.deframer_conf = &(const struct syncword_deframer_conf){
		},

		.decoder      = efrk7_decoder_code,
		.decoder_conf = &(const struct efrk7_decoder_conf){
		},
		.output      = printf_output_code,
		.output_conf = NULL
	};
#if 0	
	st->acq       = conf->acq;
	st->demod     = conf->demod;
	st->deframer  = conf->deframer;
	st->decoder   = conf->decoder;

	st->acq      .init(conf->acq_conf);
	st->demod    .init(conf->demod_conf);
	st->deframer .init(conf->deframer_conf);
	st->decoder  .init(conf->decoder_conf);
	st->acq.init(st->acq.arg
#endif

	return self;
}


int receiver_execute(void *arg, sample_t *samp, size_t nsamp) {
	struct receiver_state *self = arg;
#if 0
	st->conf->acq.execute(, samp, nsamp);
#endif
	return 0;
}

