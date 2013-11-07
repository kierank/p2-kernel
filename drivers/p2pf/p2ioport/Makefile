# Makefile for P2PF I/O port access driver.
## $Id: Makefile 10661 2010-11-25 05:12:02Z Yoshioka Masaki $

p2ioportdrv-$(CONFIG_P2IOPORTDRV)		:= p2ioport.o

# Board-dependent function
p2ioportdrv-$(CONFIG_P2IOPORT_GPIO_LIB)		+= p2ioport_gpio_lib.o
p2ioportdrv-$(CONFIG_P2IOPORT_HPM200)	+=	hpm200.o
p2ioportdrv-$(CONFIG_P2IOPORT_K286)	+=	hpm200.o
p2ioportdrv-$(CONFIG_P2IOPORT_K298)	+=	K298.o
p2ioportdrv-$(CONFIG_P2IOPORT_PCD2)	+=	pcd2.o
p2ioportdrv-$(CONFIG_P2IOPORT_HMC80)	+=	hmc80.o
p2ioportdrv-$(CONFIG_P2IOPORT_HPX3100)	+=	hpx3100.o
p2ioportdrv-$(CONFIG_P2IOPORT_K301)		+=	3dp1.o
p2ioportdrv-$(CONFIG_P2IOPORT_SAV8313BRB1)	+=	sav8313brb1.o
p2ioportdrv-$(CONFIG_P2IOPORT_MPC8313ERDB)	+=	mpc8313erdb.o
p2ioportdrv-$(CONFIG_P2IOPORT_MPC837XERDB)	+=	mpc837xerdb.o

# INT function
p2ioportdrv-$(CONFIG_P2IOPORT_MPC83XXGPIOINT)	+=	mpc83xxgpioint.o


obj-$(CONFIG_P2IOPORTDRV)	:=	p2ioportdrv.o
