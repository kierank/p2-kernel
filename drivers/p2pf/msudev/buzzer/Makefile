#
# drivers/p2pf/msudev/buzzer/Makefile
# Makefile for buzzer
# $Id: Makefile 5608 2010-03-09 08:03:31Z Noguchi Isao $
#

## BUZZER control driver
obj-$(CONFIG_MSUDEV_BUZZER)	+= p2msudev-buzzer.o

p2msudev-buzzer-obj-$(CONFIG_MSUDEV_BUZZER_DEV_FPGA) +=  buzzer-fpga.o
#p2msudev-buzzer-obj-$(CONFIG_MSUDEV_BUZZER_DEV_DUMMY) +=  buzzer-dummy.o

p2msudev-buzzer-objs := buzzer-core.o $(p2msudev-buzzer-obj-y)
