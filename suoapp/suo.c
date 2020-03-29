#include "suo.h"
#include "configure.h"

#include <string.h>
#include <stdio.h>
#include <signal.h>

static struct suo suo1;

#define N_TEST_FILES 1

FILE *test_files[N_TEST_FILES];

void print_samples(unsigned stream, sample_t *samples, size_t len)
{
	if (stream >= N_TEST_FILES)
		return;
	FILE *f = test_files[stream];
	if (f != NULL)
		fwrite(samples, sizeof(sample_t), len, f);
}


int main(int argc, char *argv[])
{
	test_files[0] = fopen("test/out0.cf32", "wb");

	configure(&suo1, argc, argv);

	fprintf(stderr, "Starting main loop\n");
	return -suo1.signal_io->execute(suo1.signal_io_arg);
}
