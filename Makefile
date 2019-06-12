CC=gcc
#CFLAGS=-Wall -Wextra -Wdouble-promotion -pedantic -std=c99 -O3
CFLAGS=-Wall -Wextra -Wdouble-promotion -std=c99 -O3
LIBS=-lliquid -lm
MAINOBJS=main.o receiver.o preamble_acq.o fsk_demod.o syncword_deframer.o efrk7_decoder.o
SIMPLEOBJS=main.o simple_receiver.o efrk7_decoder.o

simple: $(SIMPLEOBJS)
	$(CC) $(SIMPLEOBJS) -o $@ $(LIBS)

all: main

main: $(MAINOBJS)
	$(CC) $(MAINOBJS) -o $@ $(LIBS)

clean:
	rm main $(MAINOBJS) $(SIMPLEOBJS)

%.o: %.c *.h
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: all clean
