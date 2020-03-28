#ifndef PREAMBLE_ACQ_H
#define PREAMBLE_ACQ_H
#include "suo.h"

struct preamble_acq_conf {
	float input_sample_rate, center_freq, symbol_rate;
	// preamble detector parameters
	float pd_max_freq_offset, pd_power_bandwidth;
	unsigned pd_window_symbols;
};

#if 0
void *preamble_acq_init(preamble_acq_conf_t *conf);
void preamble_acq_execute(void *state, sample_t *insamp, unsigned n_insamp);
#endif

extern const struct acq_code preamble_acq_code;
#endif
