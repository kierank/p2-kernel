/*
 * P2MSU watchdog setup
 */
/* $Id: p2msu_wdt.c 6388 2010-04-14 06:20:30Z Noguchi Isao $ */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/major.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>

#include <linux/p2pf_fpga_devices.h>

static const char name[]="p2msu-wdt";

static int __init setup_p2msu_wdt(void)
{
    int retval = 0;
    struct resource res;
	u32 period=0, ticks=0;
    struct platform_device *pdev=NULL;
    struct p2msu_wdt_platform_data *pdata = NULL;
	struct device_node *np=NULL;
    const void *prop=NULL;

    /*  */
    pr_info("[%s] **** setup from device tree\n",name);

    /* get node */
	np=of_find_compatible_node(NULL, "watchdog", "p2pf,p2msu-wdt");
    if(!np){
        pr_err("A node for \"p2pf,msudev-fpga-wdt\" is NOT found.\n");
        return 0;
    }

    /* Watchdog timeout in ticks */
    prop = of_get_property(np, "ticks", NULL);
    if(prop){
        ticks = *(u32*)prop;
        pr_info("[%s] ticks=%d\n", name, ticks);
    }

    /* Period in ticks [msec] */
    prop = of_get_property(np, "period", NULL);
    if(!prop){
        retval = -ENOENT;
        pr_err("[%s] property \"period\" is NOT found.\n",np->full_name);
        goto err;
    }
    period = *(u32*)prop;
    pr_info("[%s] period=%d\n", name, period);


    /* get address map for control register */
    memset(&res,0,sizeof(struct resource));
    retval = of_address_to_resource(np, 0, &res);
    if(retval){
        pr_err("[%s] failed to get address map\n",np->full_name);
        goto err;
    }
    pr_info("[%s] resource reg = [ 0x%08x - 0x%08x ]\n", name, res.start,res.end);


    /* allocte platform data */
    pdata = (struct p2msu_wdt_platform_data *)kzalloc(sizeof(struct p2msu_wdt_platform_data),GFP_KERNEL);
    if(!pdata){
        retval = -ENOMEM;
        pr_err("no memory for platform data\n");
        goto err;
    }

    /* setting platform data */
    pdata->ticks = ticks;
    pdata->period = period;

    /* allocte private_device */
    pdev = platform_device_alloc(name, 0);
    if (!pdev){
        pr_err("can't allocate for platform device\n");
        retval=-ENOMEM;
        goto err;
    }

    /* add resources to platform device */
    retval = platform_device_add_resources(pdev, &res, 1);
    if(retval){
        pr_err("failed to add resources to platform device\n");
        platform_device_del(pdev);
        goto err;
    }

    /* add platform data to platform device */
    retval = platform_device_add_data(pdev, pdata, sizeof(struct p2msu_wdt_platform_data));
    if (retval){
        pr_err("failed to add platform data to platform device\n");
        platform_device_del(pdev);
        goto err;
    }

    /* regiter platform device */
    retval = platform_device_add(pdev);
    if (retval){
        pr_err("failed to register platform device\n");
        platform_device_del(pdev);
        goto err;
    }

 err:
    if(pdata)
        kfree(pdata);

    /* complete */
    return retval;
}

arch_initcall(setup_p2msu_wdt);
