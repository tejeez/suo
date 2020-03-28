CC=gcc
CFLAGS=-Wall -Wextra -Wdouble-promotion -std=gnu11 -O3 -g

#OBJS=simple_receiver.o simple_transmitter.o efrk7_decoder.o basic_decoder.o basic_encoder.o
OBJS=simple_receiver.o soapysdr_io.o basic_decoder.o

libsuo.a: ${OBJS}
	ar rcs $@ ${OBJS}

clean:
	rm -f libsuo.a ./*.o

%.o: %.c *.h
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: all clean
