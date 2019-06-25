#ifndef SUO_H
#define SUO_H
#include <stdlib.h>
#include <stdint.h>
#include <complex.h>
#include <math.h>
#include <stdbool.h>

typedef float complex sample_t;
typedef uint8_t bit_t; // TODO: choose type and representation for soft decisions
typedef uint64_t timestamp_t;

/* RX */

struct rx_output_code {
	void *(*init)    (const void *conf);
	int   (*destroy) (void *);
	int   (*frame)   (void *, const bit_t *bits, size_t nbits);
};

struct receiver_code {
	void *(*init)          (const void *conf);
	int   (*destroy)       (void *);
	int   (*set_callbacks) (void *, const struct rx_output_code *, void *rx_output_arg);
	int   (*execute)       (void *, const sample_t *samp, size_t nsamp);
};

struct decoder_code {
	void *(*init)    (const void *conf);
	int   (*destroy) (void *);
	int   (*decode)  (void *, const bit_t *bits, size_t nbits, uint8_t *decoded, size_t nbytes);
};


/* TX */

struct transmitter_metadata {
	timestamp_t timestamp;
};

struct tx_input_code {
	void *(*init)      (const void *conf);
	int   (*destroy)   (void *);
	int   (*get_frame) (void *, bit_t *bits, size_t nbits, struct transmitter_metadata *);
};

struct transmitter_code {
	void *(*init)          (const void *conf);
	int   (*destroy)       (void *);
	int   (*set_callbacks) (void *, const struct tx_input_code *, void *tx_input_arg);
	int   (*execute)       (void *, sample_t *samples, size_t nsamples, struct transmitter_metadata *);
};

struct encoder_code {
	void *(*init)    (const void *conf);
	int   (*destroy) (void *);
	int   (*encode)  (void *, bit_t *bits, size_t max_nbits, const uint8_t *input, size_t nbytes);
};

#endif
