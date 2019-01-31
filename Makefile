CC=gcc
#CFLAGS=-Wall -Wextra -Wdouble-promotion -pedantic -std=c99 -O3
CFLAGS=-Wall -Wextra -Wdouble-promotion -std=c99 -O3
LIBS=-lliquid -lm
MAINOBJS=burstfsk.o fskdemod.o main.o deframer.o efrk7.o

all: main

main: $(MAINOBJS)
	$(CC) $(MAINOBJS) -o $@ $(LIBS)

clean:
	rm main $(MAINOBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: all clean
