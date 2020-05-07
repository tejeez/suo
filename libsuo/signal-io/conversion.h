#ifndef LIBSUO_CONVERSION_H
#define LIBSUO_CONVERSION_H
#include "suo.h"

static inline size_t cs16_to_cf(cs16_t *in, sample_t *out, size_t n)
{
	size_t i;
	const float scale = 1.0f / 0x8000;
	for (i = 0; i < n; i++)
		out[i] =
		((float)in[i][0] * scale) +
		((float)in[i][1] * scale) * I;
	return n;
}


static inline size_t cu8_to_cf(cu8_t *in, sample_t *out, size_t n)
{
	size_t i;
	const float dc = -127.4f;
	const float scale = 1.0f / 127.6f;
	for (i = 0; i < n; i++)
		out[i] =
		(((float)in[i][0] + dc) * scale) +
		(((float)in[i][1] + dc) * scale) * I;
	return n;
}


static inline size_t cf_to_cs16(sample_t *in, cs16_t *out, size_t n)
{
	size_t i;
	const float scale = 0x8000;
	// TODO: clip
	for (i = 0; i < n; i++) {
		out[i][0] = crealf(in[i]) * scale;
		out[i][1] = cimagf(in[i]) * scale;
	}
	return n;
}

#endif
