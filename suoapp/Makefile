CC = gcc
CFLAGS =
CFLAGS += -g
CFLAGS += -Wall -Wextra -Wdouble-promotion
CFLAGS += -std=gnu11 -fcx-limited-range
CFLAGS += -O3 -march=native
CFLAGS += -I../libsuo

SRCS = suo.c configure.c
OBJS = $(addprefix $(BUILD)/,$(SRCS:.c=.o))

LIBDIR = -L../libsuo/build
LIBS = -lsuo-dsp -lliquid -lm
IO_LIBS = -lsuo-io -lSoapySDR -lzmq -lpthread

BUILD = build
DEPS = Makefile $(wildcard *.h) ../libsuo/suo.h

ifeq ($(OS),Windows_NT)
	# Kludge to compile on 64-bit Windows using MSYS2
	PATH += :/mingw64/bin
	CFLAGS += "-I/c/Program Files/PothosSDR/include"
	LIBDIR += "-L/c/Program Files/PothosSDR/bin"
	CC = x86_64-w64-mingw32-gcc
endif

all: $(BUILD)/suo

$(BUILD)/suo: $(OBJS) ../libsuo/build/*.a
	$(CC) $(OBJS) -o $@ $(LIBDIR) $(LIBS) $(IO_LIBS)

$(BUILD)/%.o: %.c $(DEPS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean
