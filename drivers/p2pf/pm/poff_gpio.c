/**
 * linux/drivers/p2pf/pm/poff_gpio.c
 *
 *     Copyright (C) 2010, Panasonic Co.,LTD.
 *     All Rights Reserved.
 */
/* $Id: poff_gpio.c 5088 2010-02-09 02:49:16Z Sawada Koji $ */

#include <linux/kernel.h>  /* for kernel module */
#include <linux/init.h>    /* for init */
#include <linux/gpio.h>
#include <linux/string.h>

#ifdef CONFIG_P2PF_PM_POFF_GPIO_OF
#include <linux/of.h>
#include <linux/of_gpio.h>
#define compatible "p2pf,pm"
#endif  /* CONFIG_P2PF_PM_POFF_GPIO_OF */

#define DEVNAME     "p2pm_poff"

#define DEBUG 0
#define WARNING 0
#define PEMERG( fmt, args... )  pr_emerg("[%s] EMERG: " fmt, DEVNAME, ## args)
#define PINFO( fmt, args... )	printk( KERN_INFO "[%s] INFO: " fmt, DEVNAME, ## args)
#define PERROR( fmt, args... )	printk( KERN_ERR "[%s] ERROR: " fmt, DEVNAME, ## args)
#define PWARNING( fmt, args... )	do { if (WARNING) printk( KERN_WARNING "[%s] WARNING: " fmt, DEVNAME, ## args); } while(0)
#define PDEBUG( fmt, args... )	do { if (DEBUG) printk( KERN_INFO "[%s] " fmt, DEVNAME_, ## args); } while(0)


static struct {
    int gpio;                   /* gpio port number */
    int pol;                    /* porarity of gpio port */
} param;


int __init __p2pm_poff_init(void)
{
    int retval = 0;

    PINFO("initialize is started\n");

    memset(&param,0,sizeof(param));

#ifdef CONFIG_P2PF_PM_POFF_GPIO_OF

    {
        struct device_node *np;
        const u32 *prop;
        u32 len;

        np = of_find_compatible_node(NULL,NULL,compatible);
        if(!np){
            PWARNING("NOT found a node for \"%s\"",compatible);
            goto fail;
        }
        prop = of_get_property(np,"poff",&len);
        len /= sizeof(u32);
        if(!prop||len<2){
            PERROR("NOT found \"poff\" property or too few cells in this property\n");
            retval = -EINVAL;
            goto fail;
        }
        param.gpio = of_get_gpio(np,prop[0]);
        if(param.gpio<0){
            PERROR("NOT foud gpio port\n");
            retval = param.gpio;
            goto fail;
        }
        param.pol = prop[1]?1:0;
    }


#else  /* ! CONFIG_P2PF_PM_POFF_GPIO_OF */

    param.gpio = CONFIG_P2PF_PM_POFF_GPIO_PORT;
#ifdef CONFIG_P2PF_PM_POFF_GPIO_POL_HIGH
    param.pol = 1;
#else  /* ! CONFIG_P2PF_PM_POFF_GPIO_POL_HIGH */
    param.pol = 0;
#endif  /* CONFIG_P2PF_PM_POFF_GPIO_POL_HIGH */

#endif  /* CONFIG_P2PF_PM_POFF_GPIO_OF */

    PINFO("gpio=%d,pol=Active-%s\n",param.gpio,param.pol?"HIGH":"LOW");

 fail:
    return retval;
}

void __exit __p2pm_poff_cleanup(void)
{
    /* nothing to do */
}

void __p2pm_do_poff(void)
{
    /* check GPIO port */
    if(!gpio_is_valid_port(param.gpio)){
        PWARNING("can't power-off in order to invalid gpio port=%d\n",param.gpio);
        return;
    }
    PEMERG("power-off by GPIO port = %d\n",param.gpio);
    gpio_set_value(param.gpio,param.pol?1:0);
}
