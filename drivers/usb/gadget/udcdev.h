/* -*- C -*-
 *  udcdev.h
 *
 * Copyright (C) 2011 Panasonic Co.,LTD.
 *
 * $Id: udcdev.h 14816 2011-06-08 01:02:14Z Noguchi Isao $
 */

#ifndef __UDCDEV_H__
#define __UDCDEV_H__

#include <linux/device.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>

#ifdef CONFIG_USB_GADGET_UDCDEV
void udcdev_change_connection(void);
int __init udcdev_init (struct usb_gadget *);
void __init udcdev_cleanup (void);
#else  /* ! CONFIG_USB_GADGET_UDCDEV */
#define udcdev_change_connection()
#define udcdev_init(g) 0
#define udcdev_cleanup()
#endif

#endif  /* __UDCDEV_H__ */
