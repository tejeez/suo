ENABLE_ALSA ?= 1

CC = gcc
CFLAGS =
CFLAGS += -g
CFLAGS += -Wall -Wextra -Wdouble-promotion
CFLAGS += -std=gnu11 -fcx-limited-range
CFLAGS += -O3 -march=native
CFLAGS += -I../libsuo

SRCS = suo.c configure.c
OBJS = $(addprefix $(BUILD)/,$(SRCS:.c=.o))
DEPS = Makefile $(wildcard *.h) ../libsuo/suo.h

LIBDIR = -L../libsuo/build
LIBS = -lsuo-dsp -lliquid -lm
IO_LIBS = -lsuo-io -lSoapySDR -lzmq -lpthread

BUILD = build
DESTDIR ?= /usr/local/bin

ifeq ($(OS),Windows_NT)
	# Kludge to compile on 64-bit Windows using MSYS2
	PATH += :/mingw64/bin
	CFLAGS += "-I/c/Program Files/PothosSDR/include"
	LIBDIR += "-L/c/Program Files/PothosSDR/bin"
	CC = x86_64-w64-mingw32-gcc
	ENABLE_ALSA = 0
endif

ifeq ($(ENABLE_ALSA),1)
	CFLAGS += -DENABLE_ALSA=1
	IO_LIBS += -lasound
endif

all: $(BUILD)/suo

$(BUILD)/suo: $(OBJS) ../libsuo/build/*.a
	$(CC) $(OBJS) -o $@ $(LIBDIR) $(LIBS) $(IO_LIBS)

$(BUILD)/%.o: %.c $(DEPS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf "$(BUILD)"

install: $(BUILD)/suo
	mkdir -p "$(DESTDIR)"
	cp "$(BUILD)/suo" "$(DESTDIR)/suo"

uninstall:
	rm -f "$(DESTDIR)/suo"

.PHONY: all clean install uninstall
