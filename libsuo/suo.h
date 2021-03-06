#ifndef LIBSUO_SUO_H
#define LIBSUO_SUO_H
#include <stdlib.h>
#include <stdint.h>
#include <complex.h>
#include <math.h>
#include <stdbool.h>

/* -----------------
 * Common data types
 * ----------------- */

// Data type to represent I/Q samples in most calculations
typedef float complex sample_t;

// Fixed-point I/Q samples
typedef uint8_t cu8_t[2];
typedef int16_t cs16_t[2];

// Data type to represent single bits. Contains a value 0 or 1.
typedef uint8_t bit_t;

/* Data type to represent soft decision bits.
 * 0 = very likely '0', 255 = very likely '1'.
 * Exact mapping to log-likelihood ratios is not specified yet. */
typedef uint8_t softbit_t;

// Data type to represent timestamps. Unit is nanoseconds.
typedef uint64_t timestamp_t;

/* All categories have these functions at the beginning of the struct,
 * so configuration and initialization code can be shared among
 * different categories by casting them to struct any_code.
 *
 * Maybe there would be a cleaner way to do this, such as having this
 * as a member of every struct. Let's think about that later. */
struct any_code {
	// Name of the module
	const char *name;

	// Initialize an instance based on a configuration struct
	void *(*init)      (const void *conf);

	/* Destroy an instance and free all memory allocated.
	 * Not always supported. */
	int   (*destroy)   (void *);

	/* Allocate a configuration struct, fill it with the default values
	 * and return a pointer to it. */
	void *(*init_conf) (void);

	// Set a configuration parameter
	int   (*set_conf)  (void *conf, const char *parameter, const char *value);
};


/* Flags to indicate that a metadata field is used */
#define METADATA_ID 1
#define METADATA_FLAGS 2
#define METADATA_TIME 4
#define METADATA_MODE 0x10
#define METADATA_POWER 0x20
#define METADATA_CFO 0x40
#define METADATA_BER 0x80
#define METADATA_SER 0x100
#define METADATA_0 0x200
#define METADATA_1 0x400
#define METADATA_2 0x800
#define METADATA_3 0x1000
#define METADATA_4 0x2000
#define METADATA_5 0x4000
#define METADATA_LEN 0x8000

/* Flag to prevent transmission of a frame if it's too late,
 * i.e. if the timestamp is already in the past */
#define METADATA_NO_LATE 0x40000

/* Metadata for received and transmitted frames */
struct metadata {
	/* Arbitrary identifier. Possible uses for it:
	 * - As a topic over a ZeroMQ Pub-Sub socket
	 * - Identifying a receiver or demodulator, in case frames from multiple
	 *   receivers are sent to a single protocol stack or decoder */
	uint32_t id;
	/* Flag bits, as defined by macros starting with METADATA_.
	 * Indicates which other fields are used or valid. */
	uint32_t flags;
	timestamp_t time; /* Timestamp (ns) */
	uint32_t mode; /* Modem-specific modulation and coding flags */
	float power; /* Received signal strength or transmit power (dB?) */
	float cfo; /* Frequency offset (Hz) */
	float ber; /* Bit error rate of decoded frame */
	float ser; /* Octet or symbol error rate of decoded frame */
	/* Other modem- or coding-specific metadata */
	float extra[6];
	/* Length of the data field */
	uint32_t len;
};


// Frame together with metadata
struct frame {
	struct metadata m; // Metadata
	//uint32_t len; // Length of the data field
	uint8_t data[]; // Data (can be bytes, bits, symbols or soft bits)
};


// Timing and control messages
struct timing {
	uint32_t id;      // Arbitrary identifier
	uint32_t flags;   // Unused
	timestamp_t time; // Current time
};


/* -----------------------------------------
 * Receive related interfaces and data types
 * ----------------------------------------- */

/* Interface to a frame decoder module */
struct decoder_code {
	const char *name;
	void *(*init)      (const void *conf);
	int   (*destroy)   (void *);
	void *(*init_conf) (void);
	int   (*set_conf)  (void *conf, const char *parameter, const char *value);

	/* Decode a frame.
	 * Input frame data is soft decision bits,
	 * output frame data is decoded data bytes.
	 * Return negative value if decoding failed. */
	int   (*decode)  (void *, const struct frame *in, struct frame *out, size_t maxlen);
};


/* Interface to a receiver output module.
 * A receiver calls one when it has received a frame. */
struct rx_output_code {
	const char *name;
	void *(*init)      (const void *conf);
	int   (*destroy)   (void *);
	void *(*init_conf) (void);
	int   (*set_conf)  (void *conf, const char *parameter, const char *value);

	// Set callback to a decoder which is used to decode a frame
	int   (*set_callbacks) (void *, const struct decoder_code *, void *decoder_arg);

	// Called by a receiver when a frame has been received
	int   (*frame) (void *, const struct frame *frame);

	// Called regularly with the time where reception is going
	int   (*tick)      (void *, timestamp_t timenow);
};


/* Interface to a receiver module, which typically performs
 * demodulation, synchronization and deframing.
 * When a frame is received, a receiver calls a given rx_output.
 */
struct receiver_code {
	const char *name;
	void *(*init)          (const void *conf);
	int   (*destroy)       (void *);
	void *(*init_conf)     (void);
	int   (*set_conf)      (void *conf, const char *parameter, const char *value);

	// Set callback to an rx_output module which is called when a frame has been received
	int   (*set_callbacks) (void *, const struct rx_output_code *, void *rx_output_arg);

	// Execute the receiver for a buffer of input signal
	int   (*execute)       (void *, const sample_t *samp, size_t nsamp, timestamp_t timestamp);
};


/* ------------------------------------------
 * Transmit related interfaces and data types
 * ------------------------------------------ */


/* Interface to a frame encoder module */
struct encoder_code {
	const char *name;
	void *(*init)      (const void *conf);
	int   (*destroy)   (void *);
	void *(*init_conf) (void);
	int   (*set_conf)  (void *conf, const char *parameter, const char *value);

	/* Encode a frame.
	 * Input is an array of data bytes,
	 * output is an array of encoded symbols.
	 * Return the number of symbols in the encoded frame. */
	int   (*encode)  (void *, const struct frame *in, struct frame *out, size_t maxlen);
};


/* Interface to a transmitter input module.
 * A transmitter calls one to request a frame to be transmitted. */
struct tx_input_code {
	const char *name;
	void *(*init)      (const void *conf);
	int   (*destroy)   (void *);
	void *(*init_conf) (void);
	int   (*set_conf)  (void *conf, const char *parameter, const char *value);

	// Set callback to a encoder which is used to encode a frame
	int   (*set_callbacks) (void *, const struct encoder_code *, void *encoder_arg);

	/* Called by a transmitter to request the next frame to be transmitted.
	 * time_dl is a "deadline": if there is a frame to transmit
	 * before time_dl, it should be returned in this call, since in the
	 * next call it may be too late. Returning a frame to transmit after
	 * time_dl is also accepted though. */
	int   (*get_frame) (void *, struct frame *frame, size_t maxlen, timestamp_t time_dl);

	// Called regularly with the time where transmit signal generation is going
	int   (*tick)      (void *, timestamp_t timenow);
};


// Return value of transmitter execute
typedef struct {
	/* Number of the samples produced, also including the time
	 * outside of a burst. Transmitter code should usually
	 * aim to produce exactly the number of samples requested
	 * and some I/O drivers may assume this is the case,
	 * but exceptions are possible. */
	int len;
	// Index of the sample where transmit burst starts
	int begin;
	/* Index of the sample where transmit burst ends.
	 * If the transmission lasts the whole buffer,
	 * end is equal to len and begin is 0.
	 * If there's nothing to transmit, end is equal to begin. */
	int end;
} tx_return_t;

/* Interface to a transmitter module.
 * These perform modulation of a signal. */
struct transmitter_code {
	const char *name;
	void *(*init)      (const void *conf);
	int   (*destroy)   (void *);
	void *(*init_conf) (void);
	int   (*set_conf)  (void *conf, const char *parameter, const char *value);

	// Set callback to a tx_input module which provides frames to be transmitted
	int   (*set_callbacks) (void *, const struct tx_input_code *, void *tx_input_arg);

	/* Generate a buffer of signal to be transmitted.
	 * Timestamp points to the first sample in the buffer. */
	tx_return_t (*execute) (void *, sample_t *samples, size_t nsamples, timestamp_t timestamp);
};


/* --------------------------------
 * Common interfaces and data types
 * -------------------------------- */

/* Interface to an I/O implementation
 * which typically controls some SDR hardware.
 * Received signal is passed to a given receiver.
 * Signal to be transmitted is asked from a given transmitter.
 */
struct signal_io_code {
	const char *name;
	void *(*init)      (const void *conf);
	int   (*destroy)   (void *);
	void *(*init_conf) (void);
	int   (*set_conf)  (void *conf, const char *parameter, const char *value);

	// Set callbacks to a receiver and a transmitter
	int   (*set_callbacks)(void *, const struct receiver_code *, void *receiver_arg, const struct transmitter_code *, void *transmitter_arg);

	// The I/O "main loop"
	int   (*execute)    (void *);
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

// List of all receivers
extern const struct receiver_code *suo_receivers[];
// List of all transmitters
extern const struct transmitter_code *suo_transmitters[];
// List of all decoders
extern const struct decoder_code *suo_decoders[];
// List of all encoders
extern const struct encoder_code *suo_encoders[];
// List of all RX outputs
extern const struct rx_output_code *suo_rx_outputs[];
// List of all TX inputs
extern const struct tx_input_code *suo_tx_inputs[];
// List of all signal I/Os
extern const struct signal_io_code *suo_signal_ios[];


/* Write samples somewhere for testing purposes.
 * Something like this could be useful for showing diagnostics
 * such as constellations in end applications as well,
 * so some more general way would be nice. */
void print_samples(unsigned stream, sample_t *samples, size_t len);

#endif
