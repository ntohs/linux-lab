#!/bin/bash
set -x
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

if ! grep -q "^dtparam=spi=on" /boot/firmware/config.txt; then
    echo "dtparam=spi=on이 /boot/firmware/config.txt에 없습니다."
    echo "  sudo sed -i 's/.*dtparam=spi.*/dtparam=spi=on/' /boot/firmware/config.txt"
    echo "  sudo reboot"
    exit 1
fi

if [ ! -f drivers/disable_spidev.dtbo ]; then
    echo "drivers/disable_spidev.dtbo가 없습니다. 먼저 빌드하세요:"
    echo "  make -C drivers/"
    exit 1
fi

sudo rmmod sensor_spi
sudo rmmod button_interrupt
sudo rmmod led_gpio

sudo dtoverlay -r disable_spidev
sudo dtoverlay -d drivers/ disable_spidev

sudo insmod drivers/sensor_spi.ko
sudo insmod drivers/button_interrupt.ko
sudo insmod drivers/led_gpio.ko
