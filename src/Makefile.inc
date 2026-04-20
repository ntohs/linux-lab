DIR = ..

COMMON = ./common
CORE = ./core
IPC = ./ipc
UTILS = ./utils

DIRS = ${COMMON} ${CORE} ${IPC} ${UTILS}

INC = -I${COMMON} -I${CORE} -I${IPC} -I${UTILS}

CROSS ?=

ifneq ($(CROSS), )
CROSS_COMPILE = ~/buildroot/output/host/bin/aarch64-buildroot-linux-gnu-
else
CROSS_COMPILE =
endif

CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall ${INC}
CXX = $(CROSS_COMPILE)g++
CXXLIBS = -lpthread -lm -lrt -Wl,--no-as-needed -ldl
CXXFLAGS = ${INC} -g -O0 -std=c++14
