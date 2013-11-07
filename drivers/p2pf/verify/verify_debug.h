/* $Id: verify_debug.h 8573 2010-07-30 10:46:09Z Noguchi Isao $
 * debug.h --- MACRO for debug message
 *
 * Copyright (C) 2005-2010 Panasonic Co.,LTD.
 */
/* $Id: verify_debug.h 8573 2010-07-30 10:46:09Z Noguchi Isao $ */

#ifndef __VERIFY_DEBUG_H__
#define __VERIFY_DEBUG_H__

#include <linux/kernel.h>
#include "verify.h"

#define _MSG(level, fmt, args...) \
    printk( level  "[" MODULE_NAME "] " fmt, ## args )

#define _MSG_LOCATION(level, fmt, args...) \
    printk( level  "[" MODULE_NAME "] %s(%d): " fmt, __FILE__, __LINE__, ## args )

#if defined(DEBUG)
#   define _DEBUG(fmt, args...) _MSG(KERN_DEBUG,"DEBUG: " fmt, ## args)
#else  /* ! defined(DEBUG)  */
#   define _DEBUG(fmt, args...)
#endif /* defined(DEBUG) */

#define _INFO(fmt, args...)  _MSG(KERN_INFO, "INFO: "  fmt, ## args)

#define _WARN(fmt, args...)  _MSG_LOCATION(KERN_WARNING, "WARNING: " fmt, ## args)

#define _ERR(fmt, args...)   _MSG_LOCATION(KERN_ERR,  "ERROR: " fmt, ## args )

#define _CRIT(fmt, args...)  _MSG_LOCATION(KERN_CRIT, "CRIT: " fmt, ## args )

#endif /* __VERIFY_DEBUG_H__ */
