#ifndef LIBSUO_MODULAR_RECEIVER_H
#define LIBSUO_MODULAR_RECEIVER_H
#include "suo.h"

struct acq_code {
	void *(*init)    (const void *conf);
	int   (*execute) (void *, sample_t *samp, size_t nsamp);
};

struct demod_code {
	void *(*init)    (const void *conf);
	int   (*execute) (void *, sample_t *samp, size_t nsamp);
	int   (*reset)   (void *, float freqoffset);
};

struct deframer_code {
	void *(*init)    (const void *conf);
	int   (*reset)   (void *);
	int   (*bit)     (void *, bit_t bit);
};

struct receiver_conf {
#if 0
	struct acq_code             acq;
	const void                 *acq_conf;
	struct demod_code           demod;
	const void                 *demod_conf;
	struct deframer_code        deframer;
	const void                 *deframer_conf;
	struct decoder_code         decoder;
	const void                 *decoder_conf;
	struct output_code          output;
	const void                 *output_conf;
#endif
};

/*void *receiver_init(struct receiver_conf *conf);
int receiver_execute(void *state, sample_t *samp, size_t nsamp);*/
//typedef struct receiver_conf receiver_conf_t;

#endif
