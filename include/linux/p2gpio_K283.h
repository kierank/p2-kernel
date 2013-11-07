/*
 *  linux/include/linux/p2gpio_user.h
 *
 *   Header file of P2GPIO driver for user applications for K283
 *     
 *     Copyright (C) 2010 Panasonic, Co.,Ltd. All Rights Reserved.
 */
/* $Id: p2gpio_K283.h 14402 2011-05-18 02:49:52Z Noguchi Isao $ */

#ifndef __LINUX_P2GPIO_K283_H__
#define __LINUX_P2GPIO_K283_H__


/*
 * PORT virtual definition for K283 only
 */
#define P2GPIO_VPORT_VBUS_USBHOST   __P2GPIO_VPORT_VALUE(K283, 0, 1)
#define P2GPIO_VPORT_VBUS_USBDEV    __P2GPIO_VPORT_VALUE(K283, 0, 2)
#define P2GPIO_VPORT_USB_OCI        __P2GPIO_VPORT_VALUE(K283, P2GPIO_VPORT_FLAG_RONLY, 3)
/* #define P2GPIO_VPORT_LOG_EVENT      __P2GPIO_VPORT_VALUE(K283, P2GPIO_VPORT_FLAG_RONLY, 4) */


#endif /* __LINUX_P2GPIO_K283_H__ */

