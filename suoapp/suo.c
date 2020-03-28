#include "suo.h"
#include "configure.h"

#include <string.h>
#include <stdio.h>
#include <signal.h>

static struct suo suo1;

int main(int argc, char *argv[])
{
	configure(&suo1, argc, argv);
	return suo1.signal_io->execute(suo1.signal_io_arg);
}
