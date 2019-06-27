#include "libsuo/suo.h"
#include "configure.h"
#include <stdio.h>


typedef uint8_t cu8_t[2];
typedef int16_t cs16_t[2];
enum inputformat { FORMAT_CU8, FORMAT_CS16, FORMAT_CF32 };

static struct suo suo1;

#define BUFLEN 4096
int main(int argc, char *argv[]) {
	struct suo *const suo = &suo1;
	enum inputformat inputformat = FORMAT_CF32;
	timestamp_t timestamp = 0;
	sample_t buf2[BUFLEN];

	configure(suo, argc, argv);

	for(;;) {
		size_t n, i;
		if(inputformat == FORMAT_CU8) {
			cu8_t buf1[BUFLEN];
			n = fread(buf1, sizeof(cu8_t), BUFLEN, stdin);
			if(n == 0) break;
			for(i=0; i<n; i++)
				buf2[i] = (float)buf1[i][0] - 127.4f
					+((float)buf1[i][1] - 127.4f)*I;
		} else if(inputformat == FORMAT_CS16) {
			cs16_t buf1[BUFLEN];
			n = fread(buf1, sizeof(cs16_t), BUFLEN, stdin);
			if(n == 0) break;
			for(i=0; i<n; i++)
				buf2[i] = (float)buf1[i][0]
					+((float)buf1[i][1])*I;
		} else {
			n = fread(buf2, sizeof(sample_t), BUFLEN, stdin);
			if(n == 0) break;
		}
		suo->receiver->execute(suo->receiver_arg, buf2, n, timestamp);
		timestamp += 1e9f * n / suo->radio_conf.samplerate;
	}

	deinitialize(suo);

	return 0;
}
