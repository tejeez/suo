CC=gcc
CFLAGS=-Wall -Wextra -Wdouble-promotion -std=gnu11 -O3 -g
CFLAGS += -I.

BUILD=build
DSP_SRCS = $(wildcard modem/*.c coding/*.c)
DSP_OBJS = $(addprefix $(BUILD)/,$(DSP_SRCS:.c=.o))

IO_SRCS = $(wildcard frame-io/*.c signal-io/*.c)
IO_OBJS = $(addprefix $(BUILD)/,$(IO_SRCS:.c=.o))

DEPS=Makefile $(wildcard *.h */*.h)

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
