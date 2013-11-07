/*
 * include/linux/p2gpio.h
 *
 *   Header file of P2GPIO driver
 *     
 *     Copyright (C) 2010 Panasonic corp.
 *     All Rights Reserved.
 */
/* $Id: p2gpio.h 14402 2011-05-18 02:49:52Z Noguchi Isao $ */

#ifndef __LINUX_P2GPIO_H__
#define __LINUX_P2GPIO_H__

#ifdef __KERNEL__

#include <linux/p2gpio_user.h>

extern int p2gpio_get_dipsw( int num, unsigned long *val );
#define p2ioport_get_dipsw(num,val) p2gpio_get_dipsw(num,val)
extern int p2gpio_get_rotsw( int num, unsigned long *val );
#define p2ioport_get_rotarysw(num,val) p2gpio_get_rotsw(num,val)
extern int p2gpio_get_led( int num, unsigned long *val );
#define p2ioport_get_led(num,val)   p2gpio_get_led(num,val)
extern int p2gpio_set_led( int num, unsigned long val );
#define p2ioport_set_led(num, val)  p2gpio_set_led(num, val)
extern int p2gpio_clr_led( int num );
#define p2ioport_clr_led(num)       p2gpio_clr_led(num)
extern int p2gpio_toggle_led( int num, unsigned long val );
#define p2ioport_toggle_led(num, val)   p2gpio_toggle_led(num, val) 
extern int p2gpio_get_vport( int port, int *val );
#define p2ioport_get_vport(port, val)   p2gpio_get_vport(port, val)
extern int p2gpio_set_vport( int port, int val );
#define p2ioport_set_vport(port, val)   p2gpio_set_vport(port, val)

#endif /* __KERNEL__ */

#endif /* __LINUX_P2GPIO_H__ */

