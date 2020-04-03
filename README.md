# Suo
Suo is a modem library. Suo can be also seen as a framework for
implementing the physical layer of various communication systems.

Suo is designed to be modular and portable. Modules are divided into
a few different categories. For each category, a common interface
based on function calls and possibly callbacks is defined.
Calls between modules happen through function pointers placed
in structs, so that parts of the code can be changed by assigning
a different value to a function pointer struct.

Signal processing code is mostly platform independent and kept separate
from the platform-dependent parts related to interfacing SDR hardware,
operating systems and other programs.

For some documentation of the different module categories and their
interfaces, see the file `libsuo/suo.h`.


## I/O interfaces implemented at the moment

Suo includes a program to run its modems on systems supporting
SoapySDR, so it can be easily used with common SDR hardware.
It has been tested on USRP, rtl-sdr, LimeSDR and xtrx.

File I/O is also supported, which is useful for testing with
recorded or simulated signals read from a file, or for piping
the signals from some other program.

Output of decoded frames by ZeroMQ is supported, so these modems can
easily be interfaced to higher protocol layers running in a separate
process. ZeroMQ messages are prefixed with a frame metadata struct.


## Modems implemented at the moment

Currently, there is `simple_receiver` and `simple_transmitter`,
which are primarily made for FSK packet waveforms similar to
those used by typical small radio chips. It supports the common
preamble-syncword-payload format with 2-GFSK modulation.
It's not a very high performance modem, but it's already useful
and also helps test other parts of the software.

There's also a PI/4 DQPSK burst modem primarily made for TETRA
experimentation, but it could grow to a more general phase-shift
keying modem as well.

Other, better modems are being developed.


## Compiling it

- Install liquid-dsp from https://github.com/jgaeddert/liquid-dsp
- Install SoapySDR and ZeroMQ. On Ubuntu:
  `apt install libsoapysdr-dev libzmq3-dev`
- Install driver for your SDR device
- Run make under libsuo directory
- Run make under suoapp directory
