CC=gcc
CFLAGS=-Wall -Wextra -Wdouble-promotion -O3 -I.. -g
LIBS=-L../libsuo -lsuo -lliquid -lm
OBJS=zmq_interface.o configure.o test_interface.o

all: suo_soapy #suo_file

suo_soapy: suo_soapy.o $(OBJS) ../libsuo/libsuo.a
	$(CC) suo_soapy.o $(OBJS) -o $@ $(LIBS) -lSoapySDR -lzmq -lpthread

suo_file: suo_file.o $(OBJS) ../libsuo/libsuo.a
	$(CC) suo_file.o $(OBJS) -o $@ $(LIBS) -lzmq -lpthread

clean:
	rm -f suo_soapy suo_file ./*.o

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

../libsuo/libsuo.a: ../libsuo/*.c ../libsuo/*.h
	$(MAKE) -C ../libsuo

.PHONY: all clean
