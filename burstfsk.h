typedef struct {
	float input_sample_rate, center_freq, symbol_rate;
	// preamble detector parameters
	float pd_max_freq_offset, pd_power_bandwidth;
	unsigned pd_window_symbols;
} burstfsk_config_t;

void *burstfsk_init(burstfsk_config_t *conf);
void burstfsk_execute(void *state, sample_t *insamp, unsigned n_insamp);
