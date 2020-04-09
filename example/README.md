# Some examples on using the modem application

Example for LimeSDR:

Start `../suoapp/build/suo example-config.txt`
and `./zmq_example.py`
and try chatting between two computers.

Example on demodulating TETRA bursts from a file:

	wget "https://prkele.prk.tky.fi/~peltolt2/TETRA_434412500Hz_250ksps.cu8.bz2"
	bunzip2 *.bz2
	../suoapp/build/suo tetra-demodulator.txt

At the same time, run `./zmq_dump.py` to see the bits.
