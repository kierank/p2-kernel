# Makefile for P2PF GPIO driver.
## $Id: Makefile 17953 2011-12-13 04:25:46Z Yoshioka Masaki $

obj-$(CONFIG_P2GPIODRV)	:=	p2gpiodrv.o
p2gpiodrv-$(CONFIG_P2GPIODRV)	:= p2gpio.o

# Board-dependent function
p2gpiodrv-$(CONFIG_P2GPIO_K283)	+=	K283.o
p2gpiodrv-$(CONFIG_P2GPIO_K298)	+=	K298.o
p2gpiodrv-$(CONFIG_P2GPIO_K302)	+=	K302.o
p2gpiodrv-$(CONFIG_P2GPIO_K301)	+=	K301.o
p2gpiodrv-$(CONFIG_P2GPIO_K318)	+=	K318.o
