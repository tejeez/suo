CC=gcc
SRCS=suo.c configure.c
CFLAGS=-Wall -Wextra -Wdouble-promotion -O3 -fcx-limited-range -march=native -I../libsuo -g
LIBS=-L../libsuo/build -lsuo-dsp -lliquid -lm
IO_LIBS=-lsuo-io -lSoapySDR -lzmq -lpthread
BUILD=build
OBJS = $(addprefix $(BUILD)/,$(SRCS:.c=.o))

all: $(BUILD)/suo

$(BUILD)/suo: $(OBJS) ../libsuo/build/*.a
	$(CC) $(OBJS) -o $@ $(LIBS) $(IO_LIBS)

$(BUILD)/%.o: %.c *.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean
