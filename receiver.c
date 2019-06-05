#include "common.h"

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


void *receiver_init(struct receiver_conf *conf) {
	struct receiver_state *st = malloc(sizeof(struct receiver_state));
	if(st == NULL) return NULL;

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

	return st;
}


int receiver_execute(void *state, sample_t *samp, size_t nsamp) {
	struct receiver_state *st = state;
#if 0
	st->conf->acq.execute(, samp, nsamp);
#endif
	return 0;
}

