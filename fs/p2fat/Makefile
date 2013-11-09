#
# Makefile for the Linux p2fat filesystem support.
#

obj-$(CONFIG_P2FAT_FS) += p2fat.o p2fat2.o

#ifeq ($(CONFIG_KGDB),y)
#EXTRA_CFLAGS	+=	-O0
#endif

p2fat-objs := cache.o dir.o file.o inode.o misc.o reservoir.o mpage.o fatent.o
p2fat2-objs := namei.o
