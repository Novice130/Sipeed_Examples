#!/bin/sh
# Run on the NanoKVM after each boot: power the OLED from GPIOA16 and route P22/P23 to I2C3.
# GPIOA16 is the UART0 TX pad, so the serial console stops until the next reboot.
[ -d /sys/class/gpio/gpio496 ] || echo 496 > /sys/class/gpio/export
echo high > /sys/class/gpio/gpio496/direction
devmem 0x03001040 32 0x3   # GPIOA16 -> GPIO
devmem 0x030010E0 32 0x2   # GPIOP22 -> IIC3 SCL
devmem 0x030010E4 32 0x2   # GPIOP23 -> IIC3 SDA
