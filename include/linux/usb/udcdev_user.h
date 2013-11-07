/*
 *  linux/usb/udcdev_user.h
 */
/* $Id: udcdev_user.h 14816 2011-06-08 01:02:14Z Noguchi Isao $ */

#ifndef __LINUX_USB_UDCDEV_USER_H__
#define __LINUX_USB_UDCDEV_USER_H__

#include <linux/ioctl.h>

/* magic number for ioctl */
#define UDCDEV_IOC_MAGIC    0xEF

/* ioctl-command */
#define UDCDEV_IOC_CHK_CONNECT  _IOR(UDCDEV_IOC_MAGIC, 1, int)


#endif  /* __LINUX_USB_UDCDEV_USER_H__ */
