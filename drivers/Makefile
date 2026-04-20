obj-m := sensor_spi.o button_interrupt.o led_gpio.o

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

all default: modules dt test
install: modules_install

modules:
	$(MAKE) -C $(KERNELDIR) M=$(shell pwd) modules

modules_install help:
	$(MAKE) -C $(KERNELDIR) M=$(shell pwd) $@

clean:
	$(MAKE) -C $(KERNELDIR) M=$(shell pwd) $@
	$(MAKE) -C test_apps $@

dt: disable_spidev.dts
	dtc -@ -I dts -O dtb -o disable_spidev.dtbo disable_spidev.dts

test:
	$(MAKE) -C test_apps

.PHONY: nfs
nfs:
	cp sensor_spi.ko ~/nfs
	cp button_interrupt.ko ~/nfs
	cp led_gpio.ko ~/nfs
