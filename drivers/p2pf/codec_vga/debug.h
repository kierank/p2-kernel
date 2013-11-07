/* $Id: debug.h 5088 2010-02-09 02:49:16Z Sawada Koji $
 * debug.h --- MACRO for debug message
 *
 * Copyright (C) 2008-2009 Panasonic Co.,LTD.
 */

#ifndef __DEBUG_H__
#define __DEBUG_H__

#include <linux/kernel.h>

#define MODULE_NAME "CODEC-VGA"

#define _MSG(level, fmt, args...) \
    printk( level  "[" MODULE_NAME "] " fmt, ## args )

#if defined(DEBUG)
#   define _DEBUG(fmt, args...) _MSG(KERN_DEBUG,"DEBUG: " fmt, ## args)
#else  /* ! defined(DEBUG)  */
#   define _DEBUG(fmt, args...)
#endif /* defined(DEBUG) */

#define _INFO(fmt, args...)  _MSG(KERN_INFO, "INFO: "  fmt, ## args)

#define _WARN(fmt, args...)  _MSG(KERN_WARNING, "WARNING: " fmt, ## args)

#define _ERR(fmt, args...)   _MSG(KERN_ERR,  "ERROR: " fmt, ## args )

#define _CRIT(fmt, args...)  _MSG(KERN_CRIT, "CRIT: " fmt, ## args )

#endif /* __DEBUG_H__ */
