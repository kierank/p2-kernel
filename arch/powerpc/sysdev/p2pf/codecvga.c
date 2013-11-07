/*
 * CODEC-VGA setup
 */
/* $Id: codecvga.c 6363 2010-04-13 09:19:35Z Noguchi Isao $ */

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

static int __init setup_codecvga(void)
{
    int retval = 0;
    int i;
    struct resource *res=NULL;
    int res_num=0;
    int sprite_num=0, dma_chan=-1;
    struct platform_device *pdev=NULL;
    struct codecvga_platform_data *pdata = NULL;
#ifdef CONFIG_CODEC_VGA_OF
	struct device_node *np=NULL;
    const void *prop=NULL;
    struct device_node *fbnp=NULL, *spnp=NULL;
#endif  /* CONFIG_CODEC_VGA_OF */

#ifdef CONFIG_CODEC_VGA_OF

    /*  */
    pr_info("cpufpga: **** setup from device tree\n");

    /* get node */
	np=of_find_compatible_node(NULL, "display", "p2pf,codecvga");
    if(!np){
        pr_err("A node for \"p2pf,codecvga\" is NOT found.\n");
        return 0;
    }

    /* fb-handle */
    prop = of_get_property(np, "fb-handle", NULL);
    if (!prop){
        retval = -ENOENT;
        pr_err("%s: property \"fb-handle\" is NOT found.\n",np->full_name);
        goto err;
    }
    fbnp = of_find_node_by_phandle(*(const phandle*)prop);
    if(!fbnp){
        retval = -ENOENT;
        pr_err("%s: node by \"fb-handle\" is NOT found.\n",np->full_name);
        goto err;
    }
    prop = of_get_property(np, "dma-chan", NULL);
    if(prop)
        dma_chan = *(u32*)prop;

    /* sprite-handle */
    prop = of_get_property(np, "sprite-handle", NULL);
    if (prop){
        spnp = of_find_node_by_phandle(*(const phandle*)prop);
        if(spnp){
            prop = of_get_property(spnp, "sprite-num", NULL);
            if(prop)
                sprite_num = *(u32*)prop;
        } else {
            pr_warning("%s: node by \"sprite-handle\" is NOT found.\n",np->full_name);
        }
    } else {
        pr_warning("%s: property \"sprite-handle\" is NOT found.\n",np->full_name);
    }

#else  /* ! CONFIG_CODEC_VGA_OF */

    sprite_num = CONFIG_CODEC_VGA_SPRITE_NUM;
    dma_chan = CONFIG_CODEC_VGA_DMA_CH;

#endif  /* CONFIG_CODEC_VGA_OF */

    /* allocte resources */
    res_num = sprite_num + 2;
    res = (struct resource *)kzalloc(sizeof(struct resource)*res_num, GFP_KERNEL);
    if(!res){
        retval = -ENOMEM;
        pr_err("no memory for resources\n");
        goto err;
    }

    /* get address map for control register */
#ifdef CONFIG_CODEC_VGA_OF
    retval = of_address_to_resource(np, 0, &res[0]);
    if(retval){
        pr_err("%s: failed to get address map\n",np->full_name);
        goto err;
    }
#else  /* ! CONFIG_CODEC_VGA_OF */
    res[0].name = "codecvga";
    res[0].flags = IORESOURCE_MEM;
    res[0].start = CONFIG_CODEC_VGA_REG_ADDR;
    res[0].end = CONFIG_CODEC_VGA_REG_SIZE + CONFIG_CODEC_VGA_REG_ADDR - 1;
#endif  /* CONFIG_CODEC_VGA_OF */
    pr_info("cpufpga: resource reg = [ 0x%08x - 0x%08x ]\n", res[0].start,res[0].end);

    /* get address map for fram-buffer memory */
#ifdef CONFIG_CODEC_VGA_OF
    retval = of_address_to_resource(fbnp, 0, &res[1]);
    if(retval){
        pr_err("%s: failed to get address map\n",fbnp->full_name);
        goto err;
    }
#else  /* ! CONFIG_CODEC_VGA_OF */
    res[1].name = "fb-handle";
    res[1].flags = IORESOURCE_MEM;
    res[1].start = CONFIG_CODEC_VGA_FB_ADDR;
    res[1].end = CONFIG_CODEC_VGA_FB_SIZE + CONFIG_CODEC_VGA_FB_ADDR - 1;
#endif  /* CONFIG_CODEC_VGA_OF */
    pr_info("cpufpga: resource fb = [ 0x%08x - 0x%08x ]\n", res[1].start,res[1].end);

    /* get address map for sprite-buffer memory */
    for(i=0; i<sprite_num; i++){
#ifdef CONFIG_CODEC_VGA_OF
        retval = of_address_to_resource(spnp, i, &res[2+i]);
        if(retval){
            pr_err("%s,i=%d: failed to get address map\n",spnp->full_name,i);
            goto err;
        }
#else  /* ! CONFIG_CODEC_VGA_OF */
    res[2+i].name = "sprite-handle";
    res[2+i].flags = IORESOURCE_MEM;
    res[2+i].start = CONFIG_CODEC_VGA_SPRITE_START_ADDR + ( i * CONFIG_CODEC_VGA_SPRITE_SIZE);
    res[2+i].end = res[2+i].start + CONFIG_CODEC_VGA_SPRITE_SIZE  - 1;
#endif  /* CONFIG_CODEC_VGA_OF */
        pr_info("cpufpga: resource sprite#%d = [ 0x%08x - 0x%08x ]\n",
                 i,res[2+i].start,res[2+i].end);
    }

    /* allocte platform data for codecvga */
    pdata = (struct codecvga_platform_data *)kzalloc(sizeof(struct codecvga_platform_data),GFP_KERNEL);
    if(!pdata){
        retval = -ENOMEM;
        pr_err("no memory for platform data\n");
        goto err;
    }

    /* setting platform data */
    pdata->sprite_num = sprite_num;
    pdata->dma_chan = dma_chan;
    pr_info("cpufpga: sprite number=%d, dma cahnnel=%d\n",
             pdata->sprite_num, pdata->dma_chan);


    /* allocte private_device */
    pdev = platform_device_alloc("codec_vga", 0);
    if (!pdev){
        pr_err("can't allocate for platform device\n");
        retval=-ENOMEM;
        goto err;
    }

    /* add resources to platform device */
    retval = platform_device_add_resources(pdev, res, res_num);
    if(retval){
        pr_err("failed to add resources to platform device\n");
        platform_device_del(pdev);
        goto err;
    }

    /* add platform data to platform device */
    retval = platform_device_add_data(pdev, pdata, sizeof(struct codecvga_platform_data));
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
    if(res)
        kfree(res);

    /* complete */
    return retval;
}

arch_initcall(setup_codecvga);
