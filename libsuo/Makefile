CC=gcc
CFLAGS=-Wall -Wextra -Wdouble-promotion -std=c99 -O3

#OBJS=modular_receiver.o preamble_acq.o fsk_demod.o syncword_deframer.o efrk7_decoder.o basic_decoder.o
OBJS=simple_receiver.o efrk7_decoder.o basic_decoder.o simple_transmitter.o

libsuo.a: ${OBJS}
	ar rcs $@ ${OBJS}

clean:
	rm -f libsuo.a ./*.o

%.o: %.c *.h
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: all clean
