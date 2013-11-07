#
# drivers/p2pf/msudev/adc/Makefile
# Makefile for adc
# $Id: Makefile 5088 2010-02-09 02:49:16Z Sawada Koji $
#

## A/D converter driver
obj-$(CONFIG_MSUDEV_ADC)	+= p2msudev-adc.o

p2msudev-adc-obj-$(CONFIG_MSUDEV_ADC_DEV_FPGA) +=  adc-fpga.o
p2msudev-adc-obj-$(CONFIG_MSUDEV_ADC_DEV_DUMMY) +=  adc-dummy.o

p2msudev-adc-objs := adc-core.o $(p2msudev-adc-obj-y)
