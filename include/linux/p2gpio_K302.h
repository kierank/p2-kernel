/*
 *  linux/include/linux/p2gpio_user.h
 *
 *   Header file of P2GPIO driver for user applications for K302
 *     
 *     Copyright (C) 2010 Panasonic, Co.,Ltd. All Rights Reserved.
 */
/* $Id: p2gpio_K302.h 11456 2010-12-28 03:06:29Z Noguchi Isao $ */

#ifndef __LINUX_P2GPIO_K302_H__
#define __LINUX_P2GPIO_K302_H__


/*
 * PORT virtual definition for K302 only
 */
#define P2GPIO_VPORT_VBUS_3D_OFF        __P2GPIO_VPORT_VALUE(K302, 0, 1)
#define P2GPIO_VPORT_VBUS_KBD_OFF       __P2GPIO_VPORT_VALUE(K302, 0, 2)
#define P2GPIO_VPORT_VBUS_USB3_OFF      __P2GPIO_VPORT_VALUE(K302, 0, 3)
#define P2GPIO_VPORT_VBUS_USBDEV_OFF    __P2GPIO_VPORT_VALUE(K302, 0, 4)
#define P2GPIO_VPORT_REF5V_OCI          __P2GPIO_VPORT_VALUE(K302, P2GPIO_VPORT_FLAG_RONLY, 5)


#endif /* __LINUX_P2GPIO_K302_H__ */

