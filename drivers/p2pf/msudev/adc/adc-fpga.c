/*
 *  adc_dummy.c  --- dummy lower driver for AD/C
 */
/* $Id: adc-fpga.c 5088 2010-02-09 02:49:16Z Sawada Koji $ */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#ifdef CONFIG_MSUDEV_ADC_DEV_FPGA_OF
#include <linux/of.h>
#endif  /* CONFIG_MSUDEV_ADC_DEV_FPGA_OF */

#include <linux/p2msudev_user.h>
#include "adc.h"

#define MODULE_NAME "msudev-adc-fpga"
#include "../debug.h"


#define BIT_WIDTH 10              /* 10 bit */
#define MAX_CHANNEL 8

struct adc_regs {
#define MASK_PORT_VALUE ((1<<BIT_WIDTH)-1) 
    __be16  port[MAX_CHANNEL];
    __be16  dummy[7];
#define FLAG_RESET  (1<<13)
    __be16  ctrl;
};

static struct adc_param {
 	spinlock_t lock;
    unsigned long paddr, psize;
    struct adc_regs __iomem *regs;
    unsigned int nr_chan;
} adc_param;


/* initilazie */
static int init_adc(struct msudev_adc_ops *ops)
{
    int retval = 0;
    struct adc_param *ap = (struct adc_param *)ops->data;

    memset(ap,0,sizeof(struct adc_param));

    spin_lock_init(&ap->lock);

#ifdef CONFIG_MSUDEV_ADC_DEV_FPGA_OF
    {
        struct device_node *np=NULL;
        const u32 *prop=NULL;
        static const char devtype[] = "adc";
        static const char compatible[] = "p2pf,fpga-adc";
        struct resource res;

        _INFO("%s: *** setup from device tree\n", compatible);
        
        /* get node */
        np=of_find_compatible_node(NULL, devtype, compatible);
        if(!np){
            retval = -ENODEV;
            _ERR("A node for \"%s\" is NOT found.\n", compatible);
            goto err;
        }

        /* get address map for control register */
        retval = of_address_to_resource(np, 0, &res);
        if(retval){
            _ERR("%s: failed to get address map\n",compatible);
            goto err;
        }
        _INFO("%s : I/O space = [ 0x%08x - 0x%08x ]\n",
              compatible, res.start, res.end);
        ap->paddr = res.start;
        ap->psize = res.end - res.start + 1;

        /* number of ADCs */
        prop = of_get_property(np, "num", NULL);
        ap->nr_chan = prop? (unsigned int)*prop: 1;
        _INFO("%s : ADC port number = %d\n", compatible, ap->nr_chan);
            
    }

#else  /* !CONFIG_MSUDEV_ADC_DEV_FPGA_OF */

    ap->paddr = CONFIG_MSUDEV_ADC_DEV_FPGA_PADDR;
    ap->psize = CONFIG_MSUDEV_ADC_DEV_FPGA_PSIZE;
    _INFO("I/O space = [ 0x%08lx - 0x%08lx ]\n",
          ap->paddr, ap->paddr + ap->psize -1);

    ap->nr_chan = CONFIG_MSUDEV_ADC_DEV_FPGA_NUMBER;
    _INFO("ADC port number = %d\n", ap->nr_chan);

#endif  /* CONFIG_MSUDEV_ADC_DEV_FPGA_OF */

    /* check resource */
    if(!ap->paddr || !ap->psize || ap->nr_chan>=MAX_CHANNEL){
        retval = -EINVAL;
        goto err;
    }


    /* i/o re-mapping */
    ap->regs = (struct adc_regs __iomem *)ioremap(ap->paddr,ap->psize);
    if(!ap->regs){
        _ERR("failed to ioremap: [ 0x%08lx - 0x%08lx ]\n",
             ap->paddr, ap->paddr + ap->psize -1);
        retval = -ENXIO;
        goto err;
    }
/*     pr_info("******* regs=%p\n",ap->regs); */

 err:

    if(retval<0){

        /* i/o un-remap */
        if(ap->regs)
            iounmap(ap->regs);

    }

    /* complete */
    return retval;
}

/* cleanup */
static void cleanup_adc(struct msudev_adc_ops *ops)
{
    struct adc_param *ap = (struct adc_param *)ops->data;

    /* i/o un-remap */
    if(ap->regs)
        iounmap(ap->regs);
}

/* get value */
static int get_value(struct msudev_adc_ops *ops, int chan, unsigned long *p_value)
{
    struct adc_param *ap = (struct adc_param *)ops->data;
    if(chan<0 || chan>=ap->nr_chan){
        _ERR("invalid channel = %d\n",chan);
        return -EINVAL;
    }
    *p_value = in_be16((u16*)&(ap->regs->port[chan])) & MASK_PORT_VALUE;
    return 0;
}

/* reset cntrol */
static int do_reset(struct msudev_adc_ops *ops, int reset_on)
{
    struct adc_param *ap = (struct adc_param *)ops->data;
    u16 reg;
    unsigned long flags;

    spin_lock_irqsave(&ap->lock,flags);
    reg = in_be16((u16*)&(ap->regs->ctrl));
    if(reset_on)
        reg |= FLAG_RESET;
    else
        reg &= ~FLAG_RESET;
    out_be16((u16*)&(ap->regs->ctrl), reg);
    spin_unlock_irqrestore(&ap->lock,flags);

    return 0;
}

/* check reset status */
static int check_reset(struct msudev_adc_ops *ops, int *check)
{
    struct adc_param *ap = (struct adc_param *)ops->data;
    u16 reg = in_be16((u16*)&(ap->regs->ctrl));
    if(!check)
        return -EINVAL;
    *check = (reg&FLAG_RESET)?1:0;
    return 0;
}

/* chanel number */
static unsigned int nr_chan(struct msudev_adc_ops *ops)
{
    struct adc_param *ap = (struct adc_param *)ops->data;
    return ap->nr_chan;
}


/*
 *  structure of operational functions
 */
static struct msudev_adc_ops adc_ops = {
    .init_adc    = init_adc,
    .cleanup_adc = cleanup_adc,
    .get_value  = get_value,
    .do_reset   = do_reset,
    .check_reset = check_reset,
    .nr_chan = nr_chan,
    .data           = (void*)&adc_param,
};

/*
 *  get operational functions
 */
struct msudev_adc_ops *  __adc_get_ops(void)
{
    return &adc_ops;
}

