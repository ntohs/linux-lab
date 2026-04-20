HOST_ARCH := $(shell uname -m)

ifeq ($(HOST_ARCH),x86_64)
    CC  = /usr/bin/aarch64-linux-gnu-gcc
    CXX = /usr/bin/aarch64-linux-gnu-g++
    MSG = "--- Cross Compiling Application ---"
else
    CC  = gcc
    CXX = g++
    MSG = "--- Native Compiling Application ---"
endif

SRCS := $(wildcard *.c)
TARGETS := $(SRCS:.c=)

all: $(TARGETS)

%: %.c
	$(CC) -o $@ $<

.PHONY: clean
clean:
	rm -rf $(TARGETS)