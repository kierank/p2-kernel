/*
 *  include/linux/rtc_user.h
 *
 */
/* $Id: rtc_user.h 6584 2010-04-22 04:22:53Z Noguchi Isao $ */

#ifndef __LINUX_RTC_USER_H__
#define __LINUX_RTC_USER_H__

#include <linux/rtc.h>

/* ioctl command */
#define RTC_PON_STATUS   _IOR('p', 0xff, unsigned long)
#define RTC_PON_CLEAR    _IO('p', 0xfe)

/* Power-ON status flag using in ioctl(RTC_GET_PON_STATUS) */
#define RTC_STAT_ZERO_VOLT  (1<<0) /* voltage-zero detected */
#define RTC_STAT_LOW_VOLT   (1<<1) /* voltage-low detected */
#define RTC_STAT_OSC_STOP   (1<<2) /* oscillator-stop detected */

#endif  /* __LINUX_RTC_USER_H__ */
