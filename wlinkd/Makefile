CC = gcc
CFLAGS = -Wall -g -I./common -I/usr/include/libnl3
LDFLAGS = -lnl-genl-3 -lnl-3

TARGET = wlinkd
SRCS = $(wildcard src/*.c)
SRCS += $(wildcard common/*.c)

OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS)