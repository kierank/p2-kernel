#
# drivers/p2pf/msudev/keyev/Makefile
# Makefile for keyev
# $Id: Makefile 5088 2010-02-09 02:49:16Z Sawada Koji $
#

## key event driver
obj-$(CONFIG_MSUDEV_KEYEV)	+= p2msudev-keyev.o

p2msudev-keyev-obj-$(CONFIG_MSUDEV_KEYEV_DEV_GPIO) += keyev-gpio.o

p2msudev-keyev-objs := keyev-core.o $(p2msudev-keyev-obj-y)
