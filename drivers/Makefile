obj-m := button_interrupt.o led_gpio.o

HOST_ARCH := $(shell uname -m)

ifeq ($(HOST_ARCH),x86_64)
	export ARCH := arm64
	export CROSS_COMPILE := aarch64-linux-gnu-
	KERNELDIR ?= $(HOME)/linux
else ifeq ($(HOST_ARCH),aarch64)
	export ARCH := arm64
	export CROSS_COMPILE :=
	KERNELDIR ?= /lib/modules/$(shell uname -r)/build
endif

all default: modules
install: modules_install

modules:
	$(MAKE) -C $(KERNELDIR) M=$(shell pwd) modules

modules_install help clean:
	$(MAKE) -C $(KERNELDIR) M=$(shell pwd) $@

.PHONY: nfs
nfs:
	cp button_interrupt.ko ~/nfs
