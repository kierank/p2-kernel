/*
 *  led_dummy.c  --- lower driver for LED control by FPGA
 */
/* $Id: led-fpga.c 5088 2010-02-09 02:49:16Z Sawada Koji $ */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#ifdef CONFIG_MSUDEV_LED_DEV_FPGA_OF
#include <linux/of.h>
#endif  /* CONFIG_MSUDEV_LED_DEV_FPGA_OF */

#include <linux/p2msudev_user.h>
#include "led.h"

#define MODULE_NAME "msudev-led-fpga"
#include "../debug.h"

struct fpga_led_reg {
#define SHIFT_LED_TIMING    12
#define MASK_LED_TIMING     (3<<SHIFT_LED_TIMING)
#define SHIFT_LED_BRIGHT    8
#define MASK_LED_BRIGHT     (3<<SHIFT_LED_BRIGHT)
    __be16 led;
};

struct fpga_led_setting {
    int bright;
    int timing;
};

static struct led_param {

 	spinlock_t lock;
    unsigned long paddr, psize; 
    struct fpga_led_reg __iomem *regs;
    unsigned int nr_led;

} led_param;


static inline void get_setting(struct fpga_led_reg __iomem *regs, struct fpga_led_setting *setting)
{
    u16 val = in_be16((u16*)&(regs->led));
/*     pr_info("*********** reg(%p)=0x%04x\n",&(regs->led),val); */

    setting->bright = (val & MASK_LED_BRIGHT) >> SHIFT_LED_BRIGHT;
    setting->timing =  (val & MASK_LED_TIMING) >> SHIFT_LED_TIMING;

/*     pr_info("*********** bright=%d, timing=%d\n", setting->bright, setting->timing); */
}

static inline void set_setting(struct fpga_led_reg __iomem *regs, struct fpga_led_setting *setting)
{
    u16 val = in_be16((u16*)&(regs->led));

/*     pr_info("*********** bright=%d, timing=%d\n", setting->bright, setting->timing); */
/*     pr_info("*********** old:reg(%p)=0x%04x\n",&(regs->led),val); */

    val = (val & ~MASK_LED_BRIGHT) | ((setting->bright<<SHIFT_LED_BRIGHT) & MASK_LED_BRIGHT);
    val = (val & ~MASK_LED_TIMING) | ((setting->timing<<SHIFT_LED_TIMING) & MASK_LED_TIMING);

/*     pr_info("*********** new:reg(%p)=0x%04x\n",&(regs->led),val); */
    out_be16((u16*)&(regs->led), val);

}

/* initilazie */
static int init_led(struct msudev_led_ops *ops)
{
    int retval = 0;
    struct led_param *lp = (struct led_param *)ops->data;

    memset(lp,0,sizeof(struct led_param));

    spin_lock_init(&lp->lock);

#ifdef CONFIG_MSUDEV_LED_DEV_FPGA_OF
    {
        struct device_node *np=NULL;
        const u32 *prop=NULL;
        static const char devtype[] = "led";
        static const char compatible[] = "p2pf,fpga-led";
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
        lp->paddr = res.start;
        lp->psize = res.end - res.start + 1;

        /* number of LEDs */
        prop = of_get_property(np, "num", NULL);
        lp->nr_led = prop? (unsigned int)*prop: 1;
        _INFO("%s : LED number = %d\n", compatible, lp->nr_led);
            
    }

#else  /* !CONFIG_MSUDEV_LED_DEV_FPGA_OF */

    lp->paddr = CONFIG_MSUDEV_LED_DEV_FPGA_PADDR;
    lp->psize = CONFIG_MSUDEV_LED_DEV_FPGA_PSIZE;
    _INFO("I/O space = [ 0x%08lx - 0x%08lx ]\n",
          lp->paddr, lp->paddr + lp->psize -1);
    lp->nr_led = CONFIG_MSUDEV_LED_DEV_FPGA_NUMBER;
    _INFO("LED number = %d\n", lp->nr_led);

#endif  /* CONFIG_MSUDEV_LED_DEV_FPGA_OF */

    /* check resource */
    if(!lp->paddr || !lp->psize){
        retval = -EINVAL;
        goto err;
    }


    /* i/o re-mapping */
    lp->regs = (struct fpga_led_reg __iomem *)ioremap(lp->paddr,lp->psize);
    if(!lp->regs){
        _ERR("failed to ioremap: [ 0x%08lx - 0x%08lx ]\n",
             lp->paddr, lp->paddr + lp->psize -1);
        retval = -ENXIO;
        goto err;
    }
/*     pr_info("******* regs=%p\n",lp->regs); */

 err:

    if(retval<0){

        /* i/o un-remap */
        if(lp->regs)
            iounmap(lp->regs);

    }

    /* complete */
    return retval;
}

/* cleanup */
static void cleanup_led(struct msudev_led_ops *ops)
{
    struct led_param *lp = (struct led_param *)ops->data;

    /* i/o un-remap */
    if(lp->regs)
        iounmap(lp->regs);
}

/* setting */
static int set_led(struct msudev_led_ops *ops, struct p2msudev_ioc_led_ctrl *param)
{
    struct led_param *lp = (struct led_param *)ops->data;
    struct fpga_led_setting setting;
    unsigned long flags;

    _DEBUG("**** set LED[%d] : bright=%d, timing=%d\n",
           param->no, param->bright, param->timing);

    if(param->no>=lp->nr_led){
        _ERR("invalid LED number = %d\n", param->no);
        return -EINVAL;
    }

    setting.bright = param->bright;
    setting.timing = param->timing;

    spin_lock_irqsave(&lp->lock,flags);
    set_setting(&(lp->regs[param->no]), &setting);
    spin_unlock_irqrestore(&lp->lock,flags);

    return 0;
}

/* getting */
static int get_led(struct msudev_led_ops *ops, struct p2msudev_ioc_led_ctrl *param)
{
    struct led_param *lp = (struct led_param *)ops->data;
    struct fpga_led_setting setting;

    _DEBUG("**** get LED[%d] : bright=%d, timing=%d\n",
           param->no, param->bright, param->timing);

    if(param->no>=lp->nr_led){
        _ERR("invalid LED number = %d\n", param->no);
        return -EINVAL;
    }

    get_setting(&(lp->regs[param->no]), &setting);

    param->bright = setting.bright;
    param->timing = setting.timing;

    return 0;
}


/* LED number */
static unsigned int nr_led(struct msudev_led_ops *ops)
{
    struct led_param *lp = (struct led_param *)ops->data;
    return lp->nr_led;
}


/*
 *  structure of operational functions
 */
static struct msudev_led_ops msudev_led_ops = {
    .init_led    = init_led,
    .cleanup_led = cleanup_led,
    .set_led = set_led,
    .get_led = get_led,
    .nr_led = nr_led,
    .data   = (void*)&led_param,
};

/*
 *  get operational functions
 */
struct msudev_led_ops *  __led_get_ops(void)
{
    return &msudev_led_ops;
}

