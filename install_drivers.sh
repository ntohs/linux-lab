#/bin/bash

sudo rmmod button_interrupt
sudo rmmod led_gpio

sudo insmod drivers/button_interrupt.ko
sudo insmod drivers/led_gpio.ko
