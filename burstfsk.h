typedef struct {
	float input_sample_rate;
	float symbol_rate;
} burstfsk_config_t;

typedef float complex sample_t;

void *burstfsk_init(burstfsk_config_t *conf);
void burstfsk_execute(void *state, sample_t *insamp, unsigned n_insamp);
