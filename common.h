#ifndef MM_COMMON_H
#define MM_COMMON_H
#include <stdlib.h>
#include <stdint.h>
#include <complex.h>

typedef float complex sample_t;
typedef int bit_t; // TODO: choose type and representation for soft decisions

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

struct decoder_code {
	void *(*init)    (const void *conf);
	int   (*decode)  (void *, bit_t *bits, size_t nbits);
};

struct output_code {
	void *(*init)    (const void *conf);
	int   (*packet)  (void *, uint8_t *bytes, size_t nbytes);
};


struct receiver_conf {
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
};

void *receiver_init(struct receiver_conf *conf);
int receiver_execute(void *state, sample_t *samp, size_t nsamp);
//typedef struct receiver_conf receiver_conf_t;

#endif
