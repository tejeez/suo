ENABLE_ALSA ?= 1

CC = gcc
CFLAGS =
CFLAGS += -g
CFLAGS += -Wall -Wextra -Wdouble-promotion
CFLAGS += -std=gnu11 -fcx-limited-range
CFLAGS += -O3 -march=native
CFLAGS += -I.

DSP_SRCS = dsp_modules.c $(wildcard modem/*.c coding/*.c)
DSP_OBJS = $(addprefix $(BUILD)/,$(DSP_SRCS:.c=.o))

IO_SRCS = io_modules.c $(wildcard frame-io/*.c signal-io/*.c)
IO_OBJS = $(addprefix $(BUILD)/,$(IO_SRCS:.c=.o))

BUILD = build
DEPS = Makefile $(wildcard *.h */*.h)

ifeq ($(OS),Windows_NT)
	# Kludge to compile on 64-bit Windows using MSYS2
	PATH += :/mingw64/bin
	CFLAGS += "-I/c/Program Files/PothosSDR/include"
	CC = x86_64-w64-mingw32-gcc
	ENABLE_ALSA = 0
endif

ifeq ($(ENABLE_ALSA),1)
	CFLAGS += -DENABLE_ALSA=1
endif

all: $(BUILD)/libsuo-dsp.a $(BUILD)/libsuo-io.a

$(BUILD)/libsuo-dsp.a: $(DSP_OBJS)
	ar rcs $@ $(DSP_OBJS)

$(BUILD)/libsuo-io.a: $(IO_OBJS)
	ar rcs $@ $(IO_OBJS)

$(BUILD)/%.o: %.c $(DEPS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean
