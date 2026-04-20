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

TARGET = interrupt_test
objects = main.o

$(TARGET): $(objects) 
	@echo $(MSG)
	$(CXX) -o $(TARGET) $(objects)

main.o: main.c
	$(CC) -g $(INCLUDES) -c main.c

.PHONY: clean
clean:
	rm -rf *.o
	rm -rf $(TARGET)