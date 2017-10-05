#include <complex.h>
#include "burstfsk.h"
#include <stdio.h>

typedef unsigned char sample1_t[2];

#define BUFLEN 4096
int main() {
	void *fsk;
	sample1_t buf1[BUFLEN];
	sample_t buf2[BUFLEN];
	burstfsk_config_t fskconf = { 300000, -8000, 10000, 2000, 20000, 64 };
	fsk = burstfsk_init(&fskconf);
	for(;;) {
		size_t n, i;
		n = fread(buf1, sizeof(sample1_t), BUFLEN, stdin);
		if(n == 0) break;
		for(i=0; i<n; i++)
			buf2[i] = (float)buf1[i][0] - 127.4f
			        +((float)buf1[i][1] - 127.4f)*I;
		burstfsk_execute(fsk, buf2, n);
	}
}
