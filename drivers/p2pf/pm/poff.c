/**
 * linux/drivers/p2pf/pm/poff.c
 *
 *     Copyright (C) 2010, Panasonic Co.,LTD.
 *     All Rights Reserved.
 */
/* $Id: poff.c 5088 2010-02-09 02:49:16Z Sawada Koji $ */

#include <linux/module.h>  /* for module */
#include <linux/kernel.h>  /* for kernel module */
#include <linux/init.h>    /* for init */

extern int __init __p2pm_poff_init(void);
extern void __exit __p2pm_poff_cleanup(void);
extern void __p2pm_do_poff(void);

void p2pm_power_off(void)
{
    __p2pm_do_poff();
}
EXPORT_SYMBOL(p2pm_power_off);

static int __init p2pm_poff_init(void)
{
    return __p2pm_poff_init();
}
module_init(p2pm_poff_init);

static void __exit p2pm_poff_cleanup(void)
{
    __p2pm_poff_cleanup();
}
module_exit(p2pm_poff_cleanup);

/******************** the end of the file ********************/
