/*
 *  buzzer_dummy.c  --- lower driver for BUZZER control by FPGA
 */
/* $Id: buzzer-fpga.c 5864 2010-03-24 00:39:07Z Noguchi Isao $ */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#ifdef CONFIG_MSUDEV_BUZZER_DEV_FPGA_OF
#include <linux/of.h>
#endif  /* CONFIG_MSUDEV_BUZZER_DEV_FPGA_OF */

#include <linux/p2msudev_user.h>
#include "buzzer.h"

#define MODULE_NAME "msudev-buzzer-fpga"
#include "../debug.h"

struct fpga_buzzer_reg {
#define BIT_BUZZER_START        (1<<0)
#define SHIFT_BUZZER_FMODE      2
#define MASK_BUZZER_FMODE       (3<<SHIFT_BUZZER_FMODE)
#define SHIFT_BUZZER_REPEAT    8
#define MASK_BUZZER_REPEAT      (15<<SHIFT_BUZZER_REPEAT)
    __be16 control;
#define SHIFT_BUZZER_BEEP_PERIOD    0
#define MASK_BUZZER_BEEP_PERIOD     (0xff<<SHIFT_BUZZER_BEEP_PERIOD)
#define SHIFT_BUZZER_SILENT_PERIOD  8
#define MASK_BUZZER_SILENT_PERIOD   (0xff<<SHIFT_BUZZER_SILENT_PERIOD)
    __be16 period;
};

static struct buzzer_param {

 	spinlock_t lock;
    unsigned long paddr, psize; 
    struct fpga_buzzer_reg __iomem *regs;

} buzzer_param;


static inline int get_setting(struct fpga_buzzer_reg __iomem *regs, struct p2msudev_ioc_buzzer_ctrl *setting)
{
    u16 control = in_be16((u16*)&(regs->control));
    u16 period = in_be16((u16*)&(regs->period));

    setting->start = (control&BIT_BUZZER_START)?1:0;
    setting->repeat = (control&MASK_BUZZER_REPEAT)>>SHIFT_BUZZER_REPEAT;
    setting->fmode = (control&MASK_BUZZER_FMODE)>>SHIFT_BUZZER_FMODE;
    setting->beep_cnt = (period&MASK_BUZZER_BEEP_PERIOD) >> SHIFT_BUZZER_BEEP_PERIOD;
    setting->silent_cnt = (period&MASK_BUZZER_SILENT_PERIOD) >> SHIFT_BUZZER_SILENT_PERIOD;

    return 0;
}

static inline int set_setting(struct fpga_buzzer_reg __iomem *regs, struct p2msudev_ioc_buzzer_ctrl *setting)
{
    u16 control = in_be16((u16*)&(regs->control));
    u16 period = in_be16((u16*)&(regs->period));

    if((control&BIT_BUZZER_START) && setting->start)
        return -EINVAL;

    if(setting->start)
        control |= BIT_BUZZER_START;
    else
        control &= ~BIT_BUZZER_START;

    control = (control&~MASK_BUZZER_REPEAT)
        | ((setting->repeat<<SHIFT_BUZZER_REPEAT)&MASK_BUZZER_REPEAT);

    control = (control&~MASK_BUZZER_FMODE)
        | ((setting->fmode<<SHIFT_BUZZER_FMODE)&MASK_BUZZER_FMODE);

    period = (period&~MASK_BUZZER_BEEP_PERIOD)
        | ((setting->beep_cnt<<SHIFT_BUZZER_BEEP_PERIOD)&MASK_BUZZER_BEEP_PERIOD);

    period = (period&~MASK_BUZZER_SILENT_PERIOD)
        | ((setting->silent_cnt<<SHIFT_BUZZER_SILENT_PERIOD)&MASK_BUZZER_SILENT_PERIOD);

    out_be16((u16*)&(regs->period), period);
    out_be16((u16*)&(regs->control), control);

    return 0;
}

/* setting */
static int set_buzzer(struct msudev_buzzer_ops *ops, struct p2msudev_ioc_buzzer_ctrl *param)
{
    int retval=0;
    unsigned long flags;
    struct buzzer_param *lp = (struct buzzer_param *)ops->data;

    spin_lock_irqsave(&lp->lock,flags);
    retval=set_setting(lp->regs, param);
    spin_unlock_irqrestore(&lp->lock,flags);

    return retval;
}

/* getting */
static int get_buzzer(struct msudev_buzzer_ops *ops, struct p2msudev_ioc_buzzer_ctrl *param)
{
    struct buzzer_param *lp = (struct buzzer_param *)ops->data;
    return get_setting(lp->regs, param);
}

/* initilazie */
static int init_buzzer(struct msudev_buzzer_ops *ops)
{
    int retval = 0;
    struct buzzer_param *lp = (struct buzzer_param *)ops->data;

    memset(lp,0,sizeof(struct buzzer_param));

    spin_lock_init(&lp->lock);

#ifdef CONFIG_MSUDEV_BUZZER_DEV_FPGA_OF
    {
        struct device_node *np=NULL;
        static const char devtype[] = "buzzer";
        static const char compatible[] = "p2pf,fpga-buzzer";
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
            _ERR("%s: faibuzzer to get address map\n",compatible);
            goto err;
        }
        _INFO("%s : I/O space = [ 0x%08x - 0x%08x ]\n",
              compatible, res.start, res.end);
        lp->paddr = res.start;
        lp->psize = res.end - res.start + 1;

    }

#else  /* !CONFIG_MSUDEV_BUZZER_DEV_FPGA_OF */

    lp->paddr = CONFIG_MSUDEV_BUZZER_DEV_FPGA_PADDR;
    lp->psize = CONFIG_MSUDEV_BUZZER_DEV_FPGA_PSIZE;
    _INFO("I/O space = [ 0x%08lx - 0x%08lx ]\n",
          lp->paddr, lp->paddr + lp->psize -1);

#endif  /* CONFIG_MSUDEV_BUZZER_DEV_FPGA_OF */

    /* check resource */
    if(!lp->paddr || !lp->psize){
        retval = -EINVAL;
        goto err;
    }


    /* i/o re-mapping */
    lp->regs = (struct fpga_buzzer_reg __iomem *)ioremap(lp->paddr,lp->psize);
    if(!lp->regs){
        _ERR("faibuzzer to ioremap: [ 0x%08lx - 0x%08lx ]\n",
             lp->paddr, lp->paddr + lp->psize -1);
        retval = -ENXIO;
        goto err;
    }

    /* initialize register */
    {
        struct p2msudev_ioc_buzzer_ctrl param;
        memset(&param,0,sizeof(struct p2msudev_ioc_buzzer_ctrl));
        set_buzzer(ops, &param);
    }

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
static void cleanup_buzzer(struct msudev_buzzer_ops *ops)
{
    struct buzzer_param *lp = (struct buzzer_param *)ops->data;

    /* i/o un-remap */
    if(lp->regs)
        iounmap(lp->regs);
}

/*
 *  structure of operational functions
 */
static struct msudev_buzzer_ops msudev_buzzer_ops = {
    .init_buzzer    = init_buzzer,
    .cleanup_buzzer = cleanup_buzzer,
    .set_buzzer = set_buzzer,
    .get_buzzer = get_buzzer,
    .data   = (void*)&buzzer_param,
};

/*
 *  get operational functions
 */
struct msudev_buzzer_ops *  __buzzer_get_ops(void)
{
    return &msudev_buzzer_ops;
}

