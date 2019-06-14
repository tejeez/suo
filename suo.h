#ifndef SUO_H
#define SUO_H
#include <stdlib.h>
#include <stdint.h>
#include <complex.h>
#include <math.h>
#include <stdbool.h>

typedef float complex sample_t;
typedef uint8_t bit_t; // TODO: choose type and representation for soft decisions

/* RX */

struct frame_output_code {
	void *(*init)   (const void *conf);
	int   (*frame)  (void *arg, bit_t *bits, size_t nbits);
};

struct receiver_code {
	void *(*init)        (const void *conf);
	int (*set_callbacks) (void *, const struct frame_output_code *, void *frame_output_arg);
	int (*execute)       (void *, sample_t *samp, size_t nsamp);
};

struct decoder_code {
	void *(*init)    (const void *conf);
	int   (*decode)  (void *, bit_t *bits, size_t nbits, uint8_t *decoded, size_t nbytes);
};


/* TX */

struct transmitter_metadata {
	uint64_t timestamp;
};

struct framer_code {
	/* Maybe this should called some kind of a buffer instead */
	void *(*init)   (const void *conf);
	int   (*get_frame)  (void *arg, bit_t *bits, size_t nbits, struct transmitter_metadata *);
};

struct transmitter_code {
	void *(*init)        (const void *conf);
	int (*set_callbacks) (void *, const struct framer_code *, void *framer_arg);
	int (*execute)       (void *, sample_t *samples, size_t nsamples, struct transmitter_metadata *);
};

#endif
