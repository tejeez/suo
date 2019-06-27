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

/* Should the RX frame metadata be a common struct defined here
 * or should it be modem-specific? A common definition makes it easier
 * to interface to decoders and protocols stacks.
 * For now, there are extra fields reserved for modem-specific metadata. */

struct rx_metadata {
	uint32_t fields; /* Bitmap to indicate which fields are valid */
	timestamp_t timestamp;
	float cfo;  /* Frequency offset at start of the frame (Hz) */
	float cfod; /* Drift of CFO during the frame (Hz) */
	float rssi; /* Received signal strength. Unit TBD */
	float snr;  /* Signal-to-noise ratio. Definition TBD */
	float ber;  /* Bit error rate */
	uint32_t mode; /* Modem-specific modulation and coding flags */
	uint32_t reserved[7];
};

struct decoder_code {
	void *(*init)    (const void *conf);
	int   (*destroy) (void *);
	int   (*decode)  (void *, const bit_t *bits, size_t nbits, uint8_t *decoded, size_t nbytes);
};

struct rx_output_code {
	void *(*init)    (const void *conf);
	int   (*destroy) (void *);
	int   (*set_callbacks) (void *, const struct decoder_code *, void *decoder_arg);
	int   (*frame)   (void *, const bit_t *bits, size_t nbits, struct rx_metadata *);
};

struct receiver_code {
	void *(*init)          (const void *conf);
	int   (*destroy)       (void *);
	int   (*set_callbacks) (void *, const struct rx_output_code *, void *rx_output_arg);
	int   (*execute)       (void *, const sample_t *samp, size_t nsamp, timestamp_t timestamp);
};


/* TX */

#define TX_FLAG_USE_CSMA 1
#define TX_FLAG_USE_TIMESTAMP 2
struct tx_metadata {
	uint32_t flags;
	timestamp_t timestamp;
	float cfo; /* Frequency offset */
	float amp; /* Amplitude */
	uint32_t mode; /* Modem-specific modulation and coding flags */
	uint32_t reserved[2];
};

struct encoder_code {
	void *(*init)    (const void *conf);
	int   (*destroy) (void *);
	int   (*encode)  (void *, bit_t *bits, size_t max_nbits, const uint8_t *input, size_t nbytes);
};

struct tx_input_code {
	void *(*init)      (const void *conf);
	int   (*destroy)   (void *);
	int   (*set_callbacks) (void *, const struct encoder_code *, void *encoder_arg);
	int   (*get_frame) (void *, bit_t *bits, size_t nbits, timestamp_t timestamp, struct tx_metadata *);
};

struct transmitter_code {
	void *(*init)          (const void *conf);
	int   (*destroy)       (void *);
	int   (*set_callbacks) (void *, const struct tx_input_code *, void *tx_input_arg);
	int   (*execute)       (void *, sample_t *samples, size_t nsamples, timestamp_t *timestamp);
};

#endif
