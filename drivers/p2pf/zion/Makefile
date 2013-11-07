#
# Makefile for the kernel ZION device drivers.
#

# Object files in subdirectories

ziondrv-objs	+= zion_core.o zion_interrupt.o zion_syms.o zion_init_list.o

ziondrv-objs	+= common/zion_common.o

ifneq ($(CONFIG_ZION_AUDIODSP),)
ziondrv-objs	+= AudioDSP/zion_audio_dsp.o
endif
ifneq ($(CONFIG_ZION_AUDIOPROC),)
ziondrv-objs	+= AudioProc/audio_proc.o
endif
ifneq ($(CONFIG_ZION_DMAIF),)
ziondrv-objs	+= DMAIF/dmaif.o
endif
ifneq ($(CONFIG_ZION_DUELCORE),)
ziondrv-objs	+= DUELcore/zion_duel.o
endif
ifneq ($(CONFIG_ZION_DVCIF),)
ziondrv-objs	+= DVCIF/dvcif.o
endif
ifneq ($(CONFIG_ZION_HOSTIF),)
ziondrv-objs	+= HOSTIF/zion_hostif.o
endif
ifneq ($(CONFIG_ZION_MATRIX),)
ziondrv-objs	+= Matrix/matrix_common.o
endif
ifneq ($(CONFIG_ZION_NEOCTRL),)
ziondrv-objs	+= NEOctrl/neoctrl.o
endif
ifneq ($(CONFIG_ZION_PCI),)
ziondrv-objs	+= PCI/zion_pci.o PCI/direct_bus_access.o
endif
ifneq ($(CONFIG_ZION_ROMIF),)
ziondrv-objs	+= ROMIF/romif.o
endif
ifneq ($(CONFIG_ZION_VGA),)
ziondrv-objs	+= VGA/zion_vga.o
endif

obj-$(CONFIG_ZION)	+= ziondrv.o
