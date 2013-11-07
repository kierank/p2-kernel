/*
 * arch/powerpc/sysdev/mpc83xx_gpio.c
 *      GPIOs in mpc831x/mpc837x
 *
 */
/* $Id: mpc83xx_gpio.c 11201 2010-12-15 23:57:24Z Noguchi Isao $ */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#ifdef CONFIG_GPIO_IRQ
#include <linux/interrupt.h>
#endif  /* CONFIG_GPIO_IRQ */

//#define SAVEREGS    1

#define MPC83XX_GPIO_PINS 32
static const char compatible[] = "fsl,mpc83xx-gpio";

struct mpc83xx_gpio_regs {
    __be32 gpdir;               /* direction register */
    __be32 gpodr;               /* open drain register */
    __be32 gpdat;               /* data register */
    __be32 gpier;               /* interrupt event register */
    __be32 gpimr;               /* interrupt mask register */
    __be32 gpicr;               /* external interrupt control register */
};


struct mpc83xx_gpio_chip {
	struct of_mm_gpio_chip mm_gc;
	spinlock_t lock;

#ifdef SAVEREGS
	/* shadowed data register to clear/set bits safely */
	u32 data;
#endif  /* SAVEREGS */

#ifdef CONFIG_GPIO_IRQ
    int irqno;
    void (*handler[MPC83XX_GPIO_PINS])(unsigned,void*);
    void *data[MPC83XX_GPIO_PINS];
#endif  /* CONFIG_GPIO_IRQ */

    u32 mask;
    u32 dir;

};

static inline struct mpc83xx_gpio_chip *
to_mpc83xx_gpio_chip(struct of_mm_gpio_chip *mm_gc)
{
	return container_of(mm_gc, struct mpc83xx_gpio_chip, mm_gc);
}

static void gpio_init_port(struct of_mm_gpio_chip *mm_gc, unsigned int gpio,
                           int dir, int odr, int dat, int icr)
{
	struct mpc83xx_gpio_chip *mpc83xx_gc = to_mpc83xx_gpio_chip(mm_gc);
	struct mpc83xx_gpio_regs __iomem *regs = mm_gc->regs;
	unsigned long flags;
	u32 pin_mask = 1 << (MPC83XX_GPIO_PINS - 1 - gpio);

	spin_lock_irqsave(&mpc83xx_gc->lock, flags);

    if(icr)
        out_be32(&regs->gpicr, in_be32(&regs->gpicr)|pin_mask);
    else
        out_be32(&regs->gpicr, in_be32(&regs->gpicr)&~pin_mask);
    mb();

    if(dat)
        out_be32(&regs->gpdat, in_be32(&regs->gpdat)|pin_mask);
    else
        out_be32(&regs->gpdat, in_be32(&regs->gpdat)&~pin_mask);
    mb();

    if(odr)
        out_be32(&regs->gpodr, in_be32(&regs->gpodr)|pin_mask);
    else
        out_be32(&regs->gpodr, in_be32(&regs->gpodr)&~pin_mask);
    mb();

    if(dir)
        out_be32(&regs->gpdir, in_be32(&regs->gpdir)|pin_mask);
    else
        out_be32(&regs->gpdir, in_be32(&regs->gpdir)&~pin_mask);
    mb();

	spin_unlock_irqrestore(&mpc83xx_gc->lock, flags);
}


#ifdef SAVEREGS
static void mpc83xx_gpio_save_regs(struct of_mm_gpio_chip *mm_gc)
{
	struct mpc83xx_gpio_chip *mpc83xx_gc = to_mpc83xx_gpio_chip(mm_gc);
	struct mpc83xx_gpio_regs __iomem *regs = mm_gc->regs;

	mpc83xx_gc->data = in_be32(&regs->gpdat);
}
#endif  /* SAVEREGS */

static int mpc83xx_gpio_get(struct gpio_chip *gc, unsigned int gpio)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct mpc83xx_gpio_regs __iomem *regs = mm_gc->regs;
	u32 pin_mask = 1 << (MPC83XX_GPIO_PINS - 1 - gpio);

	return (in_be32((u32*)&regs->gpdat) & pin_mask)?1:0;
}

static void mpc83xx_gpio_set(struct gpio_chip *gc, unsigned int gpio, int val)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct mpc83xx_gpio_chip *mpc83xx_gc = to_mpc83xx_gpio_chip(mm_gc);
	struct mpc83xx_gpio_regs __iomem *regs = mm_gc->regs;
	unsigned long flags;
	u32 pin_mask = 1 << (MPC83XX_GPIO_PINS - 1 - gpio);

	spin_lock_irqsave(&mpc83xx_gc->lock, flags);

#ifdef SAVEREGS
	if (val)
		mpc83xx_gc->data |= pin_mask;
	else
		mpc83xx_gc->data &= ~pin_mask;
	out_be32(&regs->gpdat, mpc83xx_gc->data);
#else  /* ! SAVEREGS */
	if (val)
		out_be32(&regs->gpdat, in_be32(&regs->gpdat)|pin_mask);
	else
		out_be32(&regs->gpdat, in_be32(&regs->gpdat)&~pin_mask);
#endif  /* SAVEREGS */

	spin_unlock_irqrestore(&mpc83xx_gc->lock, flags);
}

static int mpc83xx_gpio_dir(struct gpio_chip *gc, unsigned int gpio)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct mpc83xx_gpio_chip *mpc83xx_gc = to_mpc83xx_gpio_chip(mm_gc);
    //	struct mpc83xx_gpio_regs __iomem *regs = mm_gc->regs;
	u32 pin_mask = 1 << (MPC83XX_GPIO_PINS - 1 - gpio);

    return (mpc83xx_gc->dir&pin_mask)?1:0;
    //	return (in_be32(&regs->gpdir)&pin_mask)?1:0;
}

static int mpc83xx_gpio_dir_in(struct gpio_chip *gc, unsigned int gpio)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct mpc83xx_gpio_chip *mpc83xx_gc = to_mpc83xx_gpio_chip(mm_gc);
	struct mpc83xx_gpio_regs __iomem *regs = mm_gc->regs;
	unsigned long flags;
	u32 pin_mask = 1 << (MPC83XX_GPIO_PINS - 1 - gpio);

	spin_lock_irqsave(&mpc83xx_gc->lock, flags);

	out_be32(&regs->gpdir,in_be32(&regs->gpdir)&~pin_mask);

	spin_unlock_irqrestore(&mpc83xx_gc->lock, flags);

	return 0;
}

static int mpc83xx_gpio_dir_out(struct gpio_chip *gc, unsigned int gpio, int val)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct mpc83xx_gpio_chip *mpc83xx_gc = to_mpc83xx_gpio_chip(mm_gc);
	struct mpc83xx_gpio_regs __iomem *regs = mm_gc->regs;
	unsigned long flags;
	u32 pin_mask = 1 << (MPC83XX_GPIO_PINS - 1 - gpio);

	mpc83xx_gpio_set(gc, gpio, val);

	spin_lock_irqsave(&mpc83xx_gc->lock, flags);

	out_be32(&regs->gpdir,in_be32(&regs->gpdir)|pin_mask);

	spin_unlock_irqrestore(&mpc83xx_gc->lock, flags);

	return 0;
}


static void mpc83xx_gpio_opendrain(struct gpio_chip *gc, unsigned int gpio, int val)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct mpc83xx_gpio_chip *mpc83xx_gc = to_mpc83xx_gpio_chip(mm_gc);
	struct mpc83xx_gpio_regs __iomem *regs = mm_gc->regs;
	unsigned long flags;
	u32 pin_mask = 1 << (MPC83XX_GPIO_PINS - 1 - gpio);

	spin_lock_irqsave(&mpc83xx_gc->lock, flags);

	if (val)
		out_be32(&regs->gpodr, in_be32(&regs->gpodr)|pin_mask);
	else
		out_be32(&regs->gpodr, in_be32(&regs->gpodr)&~pin_mask);

	spin_unlock_irqrestore(&mpc83xx_gc->lock, flags);
}



#ifdef CONFIG_GPIO_IRQ

static irqreturn_t mpc83xx_gpio_irq_handler(int irq, void *dev_id)
{
    struct gpio_chip *gc =   (struct gpio_chip *)dev_id; 
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct mpc83xx_gpio_chip *mpc83xx_gc = to_mpc83xx_gpio_chip(mm_gc);
	struct mpc83xx_gpio_regs __iomem *regs = mm_gc->regs;
    u32 mask, event;
    unsigned int gpio;

	spin_lock(&mpc83xx_gc->lock);

    mask = in_be32(&regs->gpimr);
    event = in_be32(&regs->gpier) & mask;
    out_be32(&regs->gpier, event);

	spin_unlock(&mpc83xx_gc->lock);

    for(gpio=0; gpio<MPC83XX_GPIO_PINS; gpio++){
        u32 pin_mask = 1 << (MPC83XX_GPIO_PINS - 1 - gpio);
        if( (event & pin_mask) && mpc83xx_gc->handler[gpio])
            (* mpc83xx_gc->handler[gpio])(gc->base+gpio,mpc83xx_gc->data[gpio]);
    }

    return IRQ_HANDLED;
} 

static int mpc83xx_gpio_init_irq(struct gpio_chip *gc)
{
    int retval=0;
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct mpc83xx_gpio_chip *mpc83xx_gc = to_mpc83xx_gpio_chip(mm_gc);
	struct mpc83xx_gpio_regs __iomem *regs = mm_gc->regs;
	unsigned long flags;

    if(mpc83xx_gc->irqno == NO_IRQ)
        goto fail;

	spin_lock_irqsave(&mpc83xx_gc->lock, flags);

	out_be32(&regs->gpimr,0);
    mb();
    out_be32(&regs->gpier, 0xffffffff);
    mb();

    memset((void*)mpc83xx_gc->handler,0,sizeof(mpc83xx_gc->handler));
    memset((void*)mpc83xx_gc->data,0,sizeof(mpc83xx_gc->data));

	spin_unlock_irqrestore(&mpc83xx_gc->lock, flags);

    retval = request_irq(mpc83xx_gc->irqno,mpc83xx_gpio_irq_handler,0,gc->label,(void*)gc);
    if(retval<0){
        pr_err("%s: ERROR: can NOT register interrupt handler. (retval=%d) @ %s(%d)\n",
               compatible, retval,__FILE__,__LINE__);
        goto fail;
    }

 fail:
    return retval;
}

static int mpc83xx_gpio_clean_irq(struct gpio_chip *gc)
{
    int retval=0;
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct mpc83xx_gpio_chip *mpc83xx_gc = to_mpc83xx_gpio_chip(mm_gc);
	struct mpc83xx_gpio_regs __iomem *regs = mm_gc->regs;
	unsigned long flags;

    if(mpc83xx_gc->irqno == NO_IRQ)
        goto fail;

    free_irq(mpc83xx_gc->irqno,(void*)gc);

	spin_lock_irqsave(&mpc83xx_gc->lock, flags);

	out_be32(&regs->gpimr,0);
    mb();
    out_be32(&regs->gpier, 0xffffffff);
    mb();

    memset((void*)mpc83xx_gc->handler,0,sizeof(mpc83xx_gc->handler));
    memset((void*)mpc83xx_gc->data,0,sizeof(mpc83xx_gc->data));

	spin_unlock_irqrestore(&mpc83xx_gc->lock, flags);


 fail:
    return retval;
}

static int mpc83xx_gpio_request_irq(struct gpio_chip *gc, unsigned int gpio,
                                    void (*handler)(unsigned,void*),void *data)
{
    int retval=0;
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct mpc83xx_gpio_chip *mpc83xx_gc = to_mpc83xx_gpio_chip(mm_gc);
	struct mpc83xx_gpio_regs __iomem *regs = mm_gc->regs;
    u32 pin_mask = 1<<(MPC83XX_GPIO_PINS - 1 - gpio);
	unsigned long flags;

    if(mpc83xx_gc->irqno == NO_IRQ)
        goto fail;

	spin_lock_irqsave(&mpc83xx_gc->lock, flags);

    mpc83xx_gc->handler[gpio] = handler;
    mpc83xx_gc->data[gpio] = data;

    out_be32(&regs->gpier, pin_mask);
    mb();
	out_be32(&regs->gpimr, in_be32(&regs->gpimr)|pin_mask);
    mb();

	spin_unlock_irqrestore(&mpc83xx_gc->lock, flags);

 fail:
    return retval;
}

static int mpc83xx_gpio_free_irq(struct gpio_chip *gc, unsigned int gpio)
{
    int retval=0;
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct mpc83xx_gpio_chip *mpc83xx_gc = to_mpc83xx_gpio_chip(mm_gc);
	struct mpc83xx_gpio_regs __iomem *regs = mm_gc->regs;
    u32 pin_mask = 1<<(MPC83XX_GPIO_PINS - 1 - gpio);
	unsigned long flags;

    if(mpc83xx_gc->irqno == NO_IRQ)
        goto fail;

	spin_lock_irqsave(&mpc83xx_gc->lock, flags);

	out_be32(&regs->gpimr, in_be32(&regs->gpimr)&~pin_mask);
    mb();
    out_be32(&regs->gpier, pin_mask);
    mb();

    mpc83xx_gc->handler[gpio] = NULL;
    mpc83xx_gc->data[gpio] = NULL;

	spin_unlock_irqrestore(&mpc83xx_gc->lock, flags);

 fail:
    return retval;
}


static int mpc83xx_gpio_enable_irq(struct gpio_chip *gc, unsigned int gpio)
{
    int retval=0;
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct mpc83xx_gpio_chip *mpc83xx_gc = to_mpc83xx_gpio_chip(mm_gc);
	struct mpc83xx_gpio_regs __iomem *regs = mm_gc->regs;
    u32 pin_mask = 1<<(MPC83XX_GPIO_PINS - 1 - gpio);
	unsigned long flags;

    if(mpc83xx_gc->irqno == NO_IRQ)
        goto fail;

	spin_lock_irqsave(&mpc83xx_gc->lock, flags);

	out_be32(&regs->gpimr, in_be32(&regs->gpimr)|pin_mask);

	spin_unlock_irqrestore(&mpc83xx_gc->lock, flags);

 fail:
    return retval;
}

static int mpc83xx_gpio_disable_irq(struct gpio_chip *gc, unsigned int gpio)
{
    int retval=0;
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct mpc83xx_gpio_chip *mpc83xx_gc = to_mpc83xx_gpio_chip(mm_gc);
	struct mpc83xx_gpio_regs __iomem *regs = mm_gc->regs;
    u32 pin_mask = 1<<(MPC83XX_GPIO_PINS - 1 - gpio);
	unsigned long flags;

    if(mpc83xx_gc->irqno == NO_IRQ)
        goto fail;

	spin_lock_irqsave(&mpc83xx_gc->lock, flags);

	out_be32(&regs->gpimr, in_be32(&regs->gpimr)&~pin_mask);

	spin_unlock_irqrestore(&mpc83xx_gc->lock, flags);

 fail:
    return retval;
}


static int mpc83xx_gpio_is_enabled_irq(struct gpio_chip *gc, unsigned int gpio)
{
    int retval=0;
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct mpc83xx_gpio_chip *mpc83xx_gc = to_mpc83xx_gpio_chip(mm_gc);
	struct mpc83xx_gpio_regs __iomem *regs = mm_gc->regs;
    u32 pin_mask = 1<<(MPC83XX_GPIO_PINS - 1 - gpio);
	unsigned long flags;

    if(mpc83xx_gc->irqno == NO_IRQ)
        goto fail;

	spin_lock_irqsave(&mpc83xx_gc->lock, flags);

	retval = (in_be32(&regs->gpier)&pin_mask)?1:0;

	spin_unlock_irqrestore(&mpc83xx_gc->lock, flags);

 fail:
    return retval;
}


#endif  /* CONFIG_GPIO_IRQ */

static int mpc83xx_gpio_is_valid(struct gpio_chip *gc, unsigned int gpio)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct mpc83xx_gpio_chip *mpc83xx_gc = to_mpc83xx_gpio_chip(mm_gc);
	u32 pin_mask = 1 << (MPC83XX_GPIO_PINS - 1 - gpio);
    return (mpc83xx_gc->mask&pin_mask)?0:1;
}

static int __init mpc83xx_add_gpiochips(void)
{
	struct device_node *np;
    int no=0;

    pr_info("%s: **** setup from device tree\n", compatible);

	for_each_compatible_node(np, NULL, compatible) {
		int ret;
		struct mpc83xx_gpio_chip *mpc83xx_gc;
		struct of_mm_gpio_chip *mm_gc;
		struct of_gpio_chip *of_gc;
		struct gpio_chip *gc;

        pr_info("%s#%d: node=\"%s\".\n",compatible,no,np->full_name);

		mpc83xx_gc = kzalloc(sizeof(*mpc83xx_gc), GFP_KERNEL);
		if (!mpc83xx_gc) {
            pr_err("%s#%d: ERROR: no memory\n", compatible, no);
			goto err;
		}

		spin_lock_init(&mpc83xx_gc->lock);

		mm_gc = &mpc83xx_gc->mm_gc;
		of_gc = &mm_gc->of_gc;
		gc = &of_gc->gc;

#ifdef SAVEREGS
		mm_gc->save_regs = mpc83xx_gpio_save_regs;
#endif  /* SAVEREGS */

        {
            const u32 *gpio_cells;
            gpio_cells = of_get_property(np, "#gpio-cells", NULL);
            if(gpio_cells)
                of_gc->gpio_cells = *gpio_cells;
            else
                of_gc->gpio_cells = 2;
            pr_info("%s#%d: cells=%d\n",compatible,no,of_gc->gpio_cells);
        }

        gc->ngpio = MPC83XX_GPIO_PINS;
        pr_info("%s#%d: pins=%d\n",compatible,no,gc->ngpio);

		gc->direction = mpc83xx_gpio_dir;
		gc->direction_input = mpc83xx_gpio_dir_in;
		gc->direction_output = mpc83xx_gpio_dir_out;
		gc->get = mpc83xx_gpio_get;
		gc->set = mpc83xx_gpio_set;
        gc->is_valid = mpc83xx_gpio_is_valid;
        gc->opendrain = mpc83xx_gpio_opendrain; /* 2010/12/15, added by Panasonic (SAV) */

        {
            const u32 *gpio_base;
            gpio_base = of_get_property(np, "base", NULL);
            if(gpio_base)
                gc->base = *gpio_base;
            else
                gc->base = -1; /* dynamic allocation of GPIOs */
            pr_info("%s#%d: base=%d\n",compatible,no,gc->base);
        }

#ifdef CONFIG_GPIO_IRQ
        mpc83xx_gc->irqno = irq_of_parse_and_map(np, 0);
        pr_info("%s#%d: irqno=%d\n",compatible,no,mpc83xx_gc->irqno);
        if(mpc83xx_gc->irqno!=NO_IRQ){
            gc->init_irq = mpc83xx_gpio_init_irq;
            gc->clean_irq = mpc83xx_gpio_clean_irq;
            gc->request_irq = mpc83xx_gpio_request_irq;
            gc->free_irq = mpc83xx_gpio_free_irq;
            gc->enable_irq = mpc83xx_gpio_enable_irq;
            gc->disable_irq = mpc83xx_gpio_disable_irq;
            gc->is_enabled_irq = mpc83xx_gpio_is_enabled_irq;
            gc->can_irq=1;
        }
#endif  /* CONFIG_GPIO_IRQ */

        {
            const u32 *prop;
            int len;
            prop = of_get_property(np, "mask-pins", &len);
            len /= sizeof(u32);
            if(prop){
                int i;
                for(i=0;i<len;i++){
                    u32 gpio = prop[i];
                    if(gpio>=MPC83XX_GPIO_PINS){
                        pr_err("%s#%d: ERROR: invalid mask pin number = %d\n",
                               compatible, no, gpio);
                        goto err;
                    }
                    mpc83xx_gc->mask |= 1 << (MPC83XX_GPIO_PINS - 1 - gpio);;
                    pr_info("%s#%d: masked pin number = %d\n",
                            compatible, no, gpio);
                }
            }
        }

        /* gpio-map (dir) */
        {
            const u32 *gpio_map;
            u32 maplen;
            gpio_map = of_get_property(np, "gpio-map", &maplen);
            if(!gpio_map)
                break;
            maplen /= sizeof(u32);
            while(maplen>=5) {
                if(gpio_map[1]) /* OUT */
                    mpc83xx_gc->dir |= (1 << (gc->ngpio - 1 - gpio_map[0]));
                else            /* IN */
                    mpc83xx_gc->dir &= ~(1 << (gc->ngpio - 1 - gpio_map[0]));
                gpio_map += 5;
                maplen -= 5;
            }
        }

		ret = of_mm_gpiochip_add(np, mm_gc);
		if (ret){
            pr_err("%s#%d: ERROR: registration failed with status %d\n", compatible, no, ret);
			goto err;
        }
        pr_info("%s#%d: range=[%d-%d]\n",compatible,no,
                gc->base,gc->ngpio+gc->base-1);

        /* gpio-map */
        {
            const u32 *gpio_map;
            u32 maplen;
            gpio_map = of_get_property(np, "gpio-map", &maplen);
            if(!gpio_map)
                break;
            maplen /= sizeof(u32);
            while(maplen>=5) {
                pr_info("%s#%d: pin=%d,dir=%s,odr=%s,dat=%s,icr=%d\n",
                        compatible,no,
                        (unsigned int)gpio_map[0],        /* pin */
                        (int)gpio_map[1]?"OUT":"IN",      /* dir */
                        (int)gpio_map[2]?"ON":"OFF",      /* odr */
                        (int)gpio_map[3]?"H":"L",          /* dat */
                        (int)gpio_map[4]);                 /* icr */
                gpio_init_port(mm_gc,
                               (unsigned int)gpio_map[0], /* pin */
                               (int)gpio_map[1],          /* dir */
                               (int)gpio_map[2],          /* odr */
                               (int)gpio_map[3],          /* dat */
                               (int)gpio_map[4]);         /* icr */
                gpio_map += 5;
                maplen -= 5;
            }
        }

		goto next;
    err:
		kfree(mpc83xx_gc);

    next:
        no++;
		/* try others anyway */
	}
	return 0;
}
core_initcall(mpc83xx_add_gpiochips);
