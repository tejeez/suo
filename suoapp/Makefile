CC=gcc
CFLAGS=-Wall -Wextra -Wdouble-promotion -O3 -I..
LIBS=-L../libsuo -lsuo -lliquid -lm

all: suo_soapy suo_file

suo_soapy: suo_soapy.o ../libsuo/libsuo.a
	$(CC) suo_soapy.o -o $@ $(LIBS) -lSoapySDR

suo_file: suo_file.o ../libsuo/libsuo.a
	$(CC) suo_file.o -o $@ $(LIBS)

clean:
	rm -f suo_soapy suo_file ./*.o

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

../libsuo/libsuo.a: ../libsuo/*.c ../libsuo/*.h
	$(MAKE) -C ../libsuo

.PHONY: all clean
