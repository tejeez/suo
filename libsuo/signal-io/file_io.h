#ifndef LIBSUO_FILE_IO_H
#define LIBSUO_FILE_IO_H
#include "suo.h"

struct file_io_conf {
	// Sample rate of the files
	double samplerate;
	// File name of input file containing received signal
	const char *input;
	// File name for output file containing transmitted signal
	const char *output;
};

extern const struct file_io_conf file_io_defaults;

extern const struct signal_io_code file_io_code;

#endif
