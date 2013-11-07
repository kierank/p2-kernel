/*
 * linux/drivers/spi/of_gpio_spi.h
 *
 * Copyright 2010 Panasonic, Inc
 */
/* $Id: of_gpio_spi.c 11201 2010-12-15 23:57:24Z Noguchi Isao $ */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/major.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/of_platform.h>
#include <linux/of_spi.h>
#include <linux/of_gpio.h>

#include "of_gpio_spi.h"

static int __init of_gpio_spi_probe(char *type, char *compatible, 
				   struct spi_board_info *board_infos,
                                    unsigned int num_board_infos)
{
	struct device_node *np;
	unsigned int i = 0;

	for_each_compatible_node(np, type, compatible) {
		int ret;
		unsigned int j;
		const void *prop;
        int gpio;
		struct platform_device *pdev;
		struct gpio_spi_platform_data pdata;

        memset(&pdata,0,sizeof(pdata));

		prop = of_get_property(np, "reg", NULL);
		if (!prop)
			goto err;
		pdata.bus_num = *(u32 *)prop;

		prop = of_get_property(np, "cell-index", NULL);
		if (prop)
			i = *(u32 *)prop;

        /* All output pins are configured to open drain mode */
        if(of_get_property(np, "open-drain", NULL))
            pdata.od_mode = 1;

		for (j = 0; j < num_board_infos; j++) {
			if (board_infos[j].bus_num == pdata.bus_num){
                int index = board_infos[j].chip_select;
                if(index>=sizeof(pdata.cs2gpio)){
                    ret = -EINVAL;
                    goto err;
                }
                gpio = of_get_gpio(np,index);
                if(gpio>=0 && !gpio_is_valid_port(gpio)){
                    pr_err("[gpio-spi-master.%d] *** ERROR: GPIO-%d is NOT avilable. :\"%s\"\n",
                           i,gpio,np->full_name);
                    goto err;
                }
                pdata.cs2gpio[index] = (gpio<0)?index:gpio;
				pdata.max_chipselect++;
            }
		}

		if (!pdata.max_chipselect)
			continue;

        prop = of_get_property(np, "gpio-clk", NULL);
		if (!prop || (gpio = of_get_gpio(np,*(u32*)prop))<0 ) {
            pr_err("[gpio-spi-master.%d] *** ERROR: property \"%s\" is NOT found or invalid GPIO:\"%s\"\n",
                   i, "gpio-clk",np->full_name);
			goto err;
        }
        if(gpio>=0 && !gpio_is_valid_port(gpio)){
            pr_err("[gpio-spi-master.%d] *** ERROR: GPIO-%d is NOT avilable. :\"%s\"\n",
                   i,gpio,np->full_name);
            goto err;
        }
        pdata.port_clk = gpio;

        prop = of_get_property(np, "gpio-mosi", NULL);
		if (!prop || (gpio = of_get_gpio(np,*(u32*)prop))<0 ) {
            pr_err("[gpio-spi-master.%d] *** ERROR: property \"%s\" is NOT found or invalid GPIO:\"%s\"\n",
                   i, "gpio-mosi",np->full_name);
			goto err;
        }
        if(gpio>=0 && !gpio_is_valid_port(gpio)){
            pr_err("[gpio-spi-master.%d] *** ERROR: GPIO-%d is NOT avilable. :\"%s\"\n",
                   i,gpio,np->full_name);
            goto err;
        }
        pdata.port_mosi = gpio;

        prop = of_get_property(np, "gpio-miso", NULL);
		if (!prop || (gpio = of_get_gpio(np,*(u32*)prop))<0 ) {
            pr_err("[gpio-spi-master.%d] *** ERROR: property \"%s\" is NOT found or invalid GPIO:\"%s\"\n",
                   i, "gpio-miso",np->full_name);
			goto err;
        }
        if(gpio>=0 && !gpio_is_valid_port(gpio)){
            pr_err("[gpio-spi-master.%d] *** ERROR: GPIO-%d is NOT avilable. :\"%s\"\n",
                   i,gpio,np->full_name);
            goto err;
        }
        pdata.port_miso = gpio;

		pdev = platform_device_alloc("gpio_spi", i);
		if (!pdev)
			goto err;

		ret = platform_device_add_data(pdev, &pdata, sizeof(pdata));
		if (ret)
			goto unreg;

		ret = platform_device_add(pdev);
		if (ret)
			goto unreg;

        pr_info("[gpio-spi-master.%d] \"%s\": bus_num=0x%08x,max_chipselect=%d,"
                "port_clk=%d,port_mosi=%d,port_miso=%d\n",
                i,np->full_name,pdata.bus_num,pdata.max_chipselect,
                pdata.port_clk,pdata.port_mosi,pdata.port_miso); 

 		goto next;
unreg:
		platform_device_del(pdev);
err:
		pr_err("%s: registration failed\n", np->full_name);
next:
		i++;
	}

	return i;
}


static int __init gpio_spi_init(void)
{
    int retval = 0;
    struct spi_board_info *board_infos = NULL;
    unsigned int num_board_infos = 0;
    const int max_infos = 128;

    board_infos =
        (struct spi_board_info *)kmalloc(sizeof(struct spi_board_info) * max_infos, GFP_KERNEL);
    if(NULL==board_infos){
        retval = -ENOMEM;
        goto exit;
    }
    memset(board_infos, 0, sizeof(struct spi_board_info) * max_infos);

    num_board_infos = of_spi_device_probe("spi", "gpio,spi", board_infos,max_infos);
    if(num_board_infos<=0){
        retval = (num_board_infos==0)? -ENODEV: num_board_infos;
        goto exit;
    }

	if( !of_gpio_spi_probe(NULL, "gpio,spi", board_infos, num_board_infos) )
		of_gpio_spi_probe("spi", "gpio_spi", board_infos, num_board_infos);

	retval =spi_register_board_info(board_infos, num_board_infos);

 exit:

    if(NULL!=board_infos)
        kfree(board_infos);

    return retval;
}

//device_initcall(gpio_spi_init);
arch_initcall(gpio_spi_init);

