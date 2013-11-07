#
# drivers/p2pf/msudev/led/Makefile
# Makefile for led
# $Id: Makefile 5088 2010-02-09 02:49:16Z Sawada Koji $
#

## LED control driver
obj-$(CONFIG_MSUDEV_LED)	+= p2msudev-led.o

p2msudev-led-obj-$(CONFIG_MSUDEV_LED_DEV_FPGA) +=  led-fpga.o
p2msudev-led-obj-$(CONFIG_MSUDEV_LED_DEV_DUMMY) +=  led-dummy.o

p2msudev-led-objs := led-core.o $(p2msudev-led-obj-y)
