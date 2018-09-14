CC=gcc
#CFLAGS=-Wall -Wextra -Wdouble-promotion -pedantic -std=c99 -O3
CFLAGS=-Wall -Wextra -Wdouble-promotion -std=c99 -O3
LIBS=-lliquid -lm
MAINOBJS=burstfsk.o main.o
BITOBJS=efr32bits.o pos.o

all: main efr32bits

main: $(MAINOBJS)
	$(CC) $(MAINOBJS) -o $@ $(LIBS)

efr32bits: $(BITOBJS)
	$(CC) $(BITOBJS) -o $@ $(LIBS)

clean:
	rm main efr32bits $(MAINOBJS) $(BITOBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: all clean
