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


## Using libsuo

To use libsuo as a part of your own application, link code under
`libsuo/` into your application and call the functions as needed.
For some documentation of the different module categories and their
interfaces, see the file `libsuo/suo.h`.

Compilation into a dynamic library is not implemented yet and not
considered really necessary. By running `make` under `libsuo/`,
`libsuo-dsp.a` and `libsuo-io.a` files for static linking are created.
If that does not work for your usecase, use your preferred way to
add the necessary source files into your application.

Note that there are dependencies on other libraries.
Most of the modem code depends on
[liquid-dsp](https://github.com/jgaeddert/liquid-dsp/).


## Using the Suo application

To run Suo modems on general-purpose operating systems,
the program under `suoapp/` can be used. In most cases, this is
probably easier than using libsuo directly.

There is an ugly configuration file system, which might change
to something better some day.

For some examples on running and interfacing Suo,
see the files under `examples/`.
Note that Suo is meant to be used as a part of other systems,
so alone it doesn't do that much.


## I/O interfaces implemented at the moment

With SoapySDR, Suo can be easily used with common SDR hardware.
It has been tested on USRP, rtl-sdr, LimeSDR and xtrx.

File I/O is also supported, which is useful for testing with
recorded or simulated signals read from a file, or for piping
the signals from some other program.

Higher protocol layers should run in a separate process, and
ZeroMQ sockets are used to transfer frames between processes.
ZeroMQ messages are prefixed with a frame metadata struct.


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


## Installation on Ubuntu or Debian

If you have a LimeSDR, I recommend using Ubuntu 18.04 and following
[instructions for MyriadRF PPA](https://wiki.myriadrf.org/Lime_Suite#Ubuntu_PPA)
first.

For other devices, use your preferred distribution and install a SoapySDR
module for your device. Sometimes it's convenient to use an Ubuntu-based
Docker, nspawn or lxd container. Installation of these is outside
of the scope of this document.

Next, install some dependencies:

    sudo apt install git gcc make automake libsoapysdr-dev libzmq3-dev python3 python3-zmq

    cd
    git clone https://github.com/jgaeddert/liquid-dsp
    cd liquid-dsp
    ./bootstrap.sh
    ./configure
    make -j4
    sudo make install
    sudo ldconfig

Compile Suo:

    cd
    git clone https://github.com/tejeez/suo  # Do this only if it wasn't cloned yet
    cd suo/libsuo
    make -j4
    cd ../suoapp
    make -j4

Run it:

    ~/suo/suoapp/build/suo configuration_file.txt


## Licensing

Code in this repository is under the MIT license.

Note that it's possible to have Suo modules and Suo based applications
under different licenses as well.
