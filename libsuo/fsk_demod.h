#ifndef FSKDEMOD_H
#define FSKDEMOD_H
#include "suo.h"

struct fsk_demod_conf {
	unsigned id, sps;
	unsigned corr_len, corr_num;
	const sample_t *corr_taps;
	struct deframer_code deframer;
	void *deframer_arg;
};

#if 0
void fskdemod_conf_default(fskdemod_conf_t *configuration);
void *fskdemod_init(fskdemod_conf_t *configuration);
void fskdemod_start(void *state, float freqoffset);
void fskdemod_execute(void *state, sample_t *signal, unsigned nsamples);
int fskdemod_is_free(void *state);
#endif

extern const struct demod_code fsk_demod_code;

#endif
