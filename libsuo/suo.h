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
 * to interface to decoders and protocol stacks.
 * For now, there are extra fields reserved for modem-specific metadata. */

struct rx_metadata {
	uint32_t fields; /* Bitmap to indicate which fields are valid */
	timestamp_t timestamp;
	float cfo;  /* Frequency offset at start of the frame (Hz) */
	float cfod; /* Drift of CFO during the frame (Hz) */
	float rssi; /* Received signal strength. Unit TBD */
	float snr;  /* Signal-to-noise ratio. Definition TBD */
	float ber;  /* Bit error rate */
	float oer;  /* Octet error rate */
	uint32_t mode; /* Modem-specific modulation and coding flags */
	uint32_t reserved[6];
};

struct decoder_code {
	void *(*init)    (const void *conf);
	int   (*destroy) (void *);
	void *(*init_conf)(void);
	int   (*set_conf) (void *conf, char *parameter, char *value);
	int   (*decode)  (void *, const bit_t *bits, size_t nbits, uint8_t *decoded, size_t nbytes, struct rx_metadata *);
};

struct rx_output_code {
	void *(*init)    (const void *conf);
	int   (*destroy) (void *);
	void *(*init_conf)     (void);
	int   (*set_conf)      (void *conf, char *parameter, char *value);
	int   (*set_callbacks) (void *, const struct decoder_code *, void *decoder_arg);
	int   (*frame)   (void *, const bit_t *bits, size_t nbits, struct rx_metadata *);
};

/* Interface to a receiver implementation, which typically performs
 * demodulation, synchronization and deframing.
 * When a frame is received, a receiver calls a given rx_output.
 */
struct receiver_code {
	// Initialize a receiver instance based on a configuration struct
	void *(*init)          (const void *conf);

	// Destroy the receiver instance. Free all memory allocated by init.
	int   (*destroy)       (void *);

	/* Allocate a configuration struct, fill it with the default values
	 * and return a pointer to it. */
	void *(*init_conf)     (void);

	// Set a configuration parameter
	int   (*set_conf)      (void *conf, char *parameter, char *value);

	// Set the interface to rx_output
	int   (*set_callbacks) (void *, const struct rx_output_code *, void *rx_output_arg);

	// Execute the receiver for a buffer of input signal
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
	void *(*init_conf)(void);
	int   (*set_conf) (void *conf, char *parameter, char *value);
	int   (*encode)  (void *, bit_t *bits, size_t max_nbits, const uint8_t *input, size_t nbytes);
};

struct tx_input_code {
	void *(*init)      (const void *conf);
	int   (*destroy)   (void *);
	void *(*init_conf)(void);
	int   (*set_conf) (void *conf, char *parameter, char *value);
	int   (*set_callbacks) (void *, const struct encoder_code *, void *encoder_arg);
	int   (*get_frame) (void *, bit_t *bits, size_t nbits, timestamp_t timestamp, struct tx_metadata *);
};

struct transmitter_code {
	void *(*init)          (const void *conf);
	int   (*destroy)       (void *);
	void *(*init_conf)(void);
	int   (*set_conf) (void *conf, char *parameter, char *value);
	int   (*set_callbacks) (void *, const struct tx_input_code *, void *tx_input_arg);
	int   (*execute)       (void *, sample_t *samples, size_t nsamples, timestamp_t *timestamp);
};


/* Other */

/* Interface to an I/O implementation
 * which typically controls some SDR hardware.
 * Received signal is passed to a given receiver.
 * Signal to be transmitted is asked from a given transmitter.
 */
struct signal_io_code {
	void *(*init)      (const void *conf);
	int   (*destroy)   (void *);
	void *(*init_conf) (void);
	int   (*set_conf)  (void *conf, char *parameter, char *value);
	int   (*set_callbacks)(void *, const struct receiver_code *, void *receiver_arg, const struct transmitter_code *, void *transmitter_arg);
	int   (*execute)    (void *);
};


/* All categories have these functions at the beginning of the struct,
 * so configuration and initialization code can be shared among
 * different categories by casting them to struct any_code.
 *
 * Maybe there would be a cleaner way to do this, such as having this
 * as a member of every struct. Let's think about that later. */
struct any_code {
	void *(*init)      (const void *conf);
	int   (*destroy)   (void *);
	void *(*init_conf) (void);
	int   (*set_conf)  (void *conf, char *parameter, char *value);
};


/* Everything combined */
struct suo {
	const struct receiver_code *receiver;
	void *receiver_arg;

	const struct transmitter_code *transmitter;
	void *transmitter_arg;

	const struct decoder_code *decoder;
	void *decoder_arg;

	const struct encoder_code *encoder;
	void *encoder_arg;

	const struct rx_output_code *rx_output;
	void *rx_output_arg;

	const struct tx_input_code *tx_input;
	void *tx_input_arg;

	const struct signal_io_code *signal_io;
	void *signal_io_arg;
};

#endif
