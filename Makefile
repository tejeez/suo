CC=gcc
#CFLAGS=-Wall -Wextra -Wdouble-promotion -pedantic -std=c99 -O3
CFLAGS=-Wall -Wextra -Wdouble-promotion -std=c99 -O3
LIBS=-lliquid -lm

#OBJS=modular_receiver.o preamble_acq.o fsk_demod.o syncword_deframer.o efrk7_decoder.o basic_decoder.o
OBJS=simple_receiver.o efrk7_decoder.o basic_decoder.o

all: soapy_main file_main

soapy_main: soapy_main.o $(OBJS)
	$(CC) soapy_main.o $(OBJS) -o $@ $(LIBS) -lSoapySDR

file_main: file_main.o $(OBJS)
	$(CC) file_main.o $(OBJS) -o $@ $(LIBS)

clean:
	rm file_main soapy_main ./*.o

%.o: %.c *.h
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: all clean
