#ifndef LIBSUO_DDC_H
#define LIBSUO_DDC_H

struct suo_ddc;

#define DDC_DOWN 0
#define DDC_UP 1

struct suo_ddc *suo_ddc_init(float fs_in, float fs_out, float cf, unsigned flags);
size_t suo_ddc_out_size(struct suo_ddc *ddc, size_t inlen);
size_t suo_ddc_execute(struct suo_ddc *self, const sample_t *in, size_t inlen, sample_t *out, timestamp_t *timestamp);
size_t suo_duc_in_size(struct suo_ddc *ddc, size_t outlen, timestamp_t *timestamp);
tx_return_t suo_duc_execute(struct suo_ddc *self, const sample_t *in, size_t inlen, sample_t *out);

#endif
