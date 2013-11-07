/* $Id: debug.h 5088 2010-02-09 02:49:16Z Sawada Koji $
 * debug.h --- MACRO for debug message
 *
 * Copyright (C) 2010 Panasonic Co.,LTD.
 */

#ifndef __MSUDEV_DEBUG_H__
#define __MSUDEV_DEBUG_H__


#include <linux/kernel.h>

#ifndef MODULE_NAME
#define MODULE_NAME "MSUDEV"
#endif

#define _MSG(level, fmt, args...) \
    printk( level  "[%s] " fmt, MODULE_NAME, ## args )

/* #define _MSG_LOCATION(level, fmt, args...) \ */
/*     printk( level  "[%s] %s(%d): " fmt, MODULE_NAME, __FILE__, __LINE__, ## args ) */
#define _MSG_LOCATION _MSG

#if defined(MSUDEV_DEBUG)
#   define _DEBUG(fmt, args...) _MSG(KERN_DEBUG,"DEBUG: " fmt, ## args)
#else  /* ! defined(MSUDEV_DEBUG)  */
#   define _DEBUG(fmt, args...)
#endif /* defined(MSUDEV_DEBUG) */

#define _INFO(fmt, args...)  _MSG(KERN_INFO, "INFO: "  fmt, ## args)

#define _WARN(fmt, args...)  _MSG_LOCATION(KERN_WARNING, "WARNING: " fmt, ## args)

#define _ERR(fmt, args...)   _MSG_LOCATION(KERN_ERR,  "ERROR: " fmt, ## args )

#define _CRIT(fmt, args...)  _MSG_LOCATION(KERN_CRIT, "CRIT: " fmt, ## args )

#endif /* __MSUDEV_DEBUG_H__ */
