/*
 * GPIO for FPGA
 *
 */
/* $Id: fpga_gpio.c 5761 2010-03-17 05:23:37Z Noguchi Isao $ */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>

static const char compatible[]="p2pf,fpga-gpio";

//#define SAVEREGS    1

struct fpga_gpio_chip {
	struct of_mm_gpio_chip mm_gc;
	spinlock_t lock;

#ifdef SAVEREGS
	/* shadowed data register to clear/set bits safely */
	union {
        u8 b;
        u16 w;
        u32 l;
    } data;
#endif  /* SAVEREGS */

    u32 mask;
    u32 dir;

};

static inline struct fpga_gpio_chip *
to_fpga_gpio_chip(struct of_mm_gpio_chip *mm_gc)
{
	return container_of(mm_gc, struct fpga_gpio_chip, mm_gc);
}

#ifdef SAVEREGS
static void fpga_gpio_save_regs8(struct of_mm_gpio_chip *mm_gc)
{
	struct fpga_gpio_chip *fpga_gc = to_fpga_gpio_chip(mm_gc);
    fpga_gc->data.b = in_8((u8*)mm_gc->regs);
}
static void fpga_gpio_save_regs16(struct of_mm_gpio_chip *mm_gc)
{
	struct fpga_gpio_chip *fpga_gc = to_fpga_gpio_chip(mm_gc);
    fpga_gc->data.w = in_be16((u16*)mm_gc->regs);
}
static void fpga_gpio_save_regs32(struct of_mm_gpio_chip *mm_gc)
{
	struct fpga_gpio_chip *fpga_gc = to_fpga_gpio_chip(mm_gc);
    fpga_gc->data.l = in_be32((u32*)mm_gc->regs);
}
#endif  /* SAVEREGS */

static int fpga_gpio_get8(struct gpio_chip *gc, unsigned int gpio)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	u8 pin_mask = 1 << (gc->ngpio - 1 - gpio);
	return (in_8((u8*)mm_gc->regs) & pin_mask)?1:0;
}
static int fpga_gpio_get16(struct gpio_chip *gc, unsigned int gpio)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	u16 pin_mask = 1 << (gc->ngpio - 1 - gpio);
	return (in_be16((u16*)mm_gc->regs) & pin_mask)?1:0;
}
static int fpga_gpio_get32(struct gpio_chip *gc, unsigned int gpio)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	u32 pin_mask = 1 << (gc->ngpio - 1 - gpio);
	return (in_be32((u32*)mm_gc->regs) & pin_mask)?1:0;
}

static void fpga_gpio_set8(struct gpio_chip *gc, unsigned int gpio, int val)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct fpga_gpio_chip *fpga_gc = to_fpga_gpio_chip(mm_gc);
	unsigned long flags;
	u8 pin_mask = 1 << (gc->ngpio - 1 - gpio);
    u8 __iomem *reg = (u8 __iomem *)mm_gc->regs;

	spin_lock_irqsave(&fpga_gc->lock, flags);

#ifdef SAVEREGS
	if (val)
		fpga_gc->data.b |= pin_mask;
	else
		fpga_gc->data.b &= ~pin_mask;
	out_8(reg, fpga_gc->data.b);
#else  /* ! SAVEREGS */
	if (val)
		out_8(reg, in_8(reg)|pin_mask);
	else
		out_8(reg, in_8(reg)&~pin_mask);
#endif  /* SAVEREGS */

	spin_unlock_irqrestore(&fpga_gc->lock, flags);
}
static void fpga_gpio_set16(struct gpio_chip *gc, unsigned int gpio, int val)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct fpga_gpio_chip *fpga_gc = to_fpga_gpio_chip(mm_gc);
	unsigned long flags;
	u16 pin_mask = 1 << (gc->ngpio - 1 - gpio);
    u16 __iomem *reg = (u16 __iomem *)mm_gc->regs;

	spin_lock_irqsave(&fpga_gc->lock, flags);

#ifdef SAVEREGS
	if (val)
		fpga_gc->data.w |= pin_mask;
	else
		fpga_gc->data.w &= ~pin_mask;
	out_be16(reg, fpga_gc->data.w);
#else  /* ! SAVEREGS */
	if (val)
		out_be16(reg, in_be16(reg)|pin_mask);
	else
		out_be16(reg, in_be16(reg)&~pin_mask);
#endif  /* SAVEREGS */

	spin_unlock_irqrestore(&fpga_gc->lock, flags);
}
static void fpga_gpio_set32(struct gpio_chip *gc, unsigned int gpio, int val)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct fpga_gpio_chip *fpga_gc = to_fpga_gpio_chip(mm_gc);
	unsigned long flags;
	u32 pin_mask = 1 << (gc->ngpio - 1 - gpio);
    u32 __iomem *reg = (u32 __iomem *)mm_gc->regs;

	spin_lock_irqsave(&fpga_gc->lock, flags);

#ifdef SAVEREGS
	if (val)
		fpga_gc->data.l |= pin_mask;
	else
		fpga_gc->data.l &= ~pin_mask;
	out_be32(reg, fpga_gc->data.l);
#else  /* ! SAVEREGS */
	if (val)
		out_be32(reg, in_be32(reg)|pin_mask);
	else
		out_be32(reg, in_be32(reg)&~pin_mask);
#endif  /* SAVEREGS */

	spin_unlock_irqrestore(&fpga_gc->lock, flags);
}

static int fpga_gpio_dir(struct gpio_chip *gc, unsigned int gpio)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct fpga_gpio_chip *fpga_gc = to_fpga_gpio_chip(mm_gc);
	u32 pin_mask = 1 << (gc->ngpio - 1 - gpio);
    return (fpga_gc->dir&pin_mask)?1:0;
}

static int fpga_gpio_is_valid(struct gpio_chip *gc, unsigned int gpio)
{
	struct of_mm_gpio_chip *mm_gc = to_of_mm_gpio_chip(gc);
	struct fpga_gpio_chip *fpga_gc = to_fpga_gpio_chip(mm_gc);
	u32 pin_mask = 1 << (gc->ngpio - 1 - gpio);
    return (fpga_gc->mask&pin_mask)?0:1;
}

static int __init fpga_add_gpiochips(void)
{
	struct device_node *np;
    int no=0;

    pr_info("%s: **** setup from device tree\n", compatible);

	for_each_compatible_node(np, NULL, compatible) {
		int ret;
		struct fpga_gpio_chip *fpga_gc=NULL;
		struct of_mm_gpio_chip *mm_gc;
		struct of_gpio_chip *of_gc;
		struct gpio_chip *gc;
        const void *prop;

        pr_info("%s#%d: node = \"%s\"\n",
                compatible, no, np->full_name);

		fpga_gc = kzalloc(sizeof(*fpga_gc), GFP_KERNEL);
		if (!fpga_gc) {
			ret = -ENOMEM;
			goto err;
		}

		spin_lock_init(&fpga_gc->lock);

		mm_gc = &fpga_gc->mm_gc;
		of_gc = &mm_gc->of_gc;
		gc = &of_gc->gc;

        prop = of_get_property(np, "#gpio-cells", NULL);
        if(prop)
            of_gc->gpio_cells = *(u32*)prop;
        else
            of_gc->gpio_cells = 2;
        pr_info("%s#%d: cells = %d\n", compatible, no, of_gc->gpio_cells);

        prop = of_get_property(np, "base", NULL);
        if(prop)
            gc->base = *(u32*)prop;
        else
            gc->base = -1; /* dynamic allocation of GPIOs */
        pr_info("%s#%d: base = %d\n", compatible, no, gc->base);

        prop = of_get_property(np, "pins", NULL);
        if(!prop){
            pr_err("%s#%d: ERROR: NOT found property \"pins\" in %s\n",
                   compatible, no, np->full_name);
            goto err;
        }
        gc->ngpio = *(u32*)prop;
        pr_info("%s#%d: pins = %d\n", compatible, no, gc->ngpio);

        switch (gc->ngpio) {
        case 8:
#ifdef SAVEREGS
            mm_gc->save_regs = fpga_gpio_save_regs8;
#endif  /* SAVEREGS */
            gc->get = fpga_gpio_get8;
            gc->set = fpga_gpio_set8;
            break;
        case 16:
#ifdef SAVEREGS
            mm_gc->save_regs = fpga_gpio_save_regs16;
#endif  /* SAVEREGS */
            gc->get = fpga_gpio_get16;
            gc->set = fpga_gpio_set16;
            break;
        case 32:
#ifdef SAVEREGS
            mm_gc->save_regs = fpga_gpio_save_regs32;
#endif  /* SAVEREGS */
            gc->get = fpga_gpio_get32;
            gc->set = fpga_gpio_set32;
            break;
        default:
            pr_err("%s#%d: ERROR: invalid pins property = %d\n",compatible, no, gc->ngpio);
            goto err;
        }
        gc->direction = fpga_gpio_dir;
        gc->is_valid = fpga_gpio_is_valid;

        {
            const u32 *prop;
            int len;
            prop = of_get_property(np, "mask-pins", &len);
            len /= sizeof(u32);
            if(prop){
                int i;
                for(i=0;i<len;i++){
                    u32 gpio = prop[i];
                    if(gpio>=gc->ngpio){
                        pr_err("%s#%d: ERROR: invalid mask pin number = %d\n",
                               compatible, no, gpio);
                        goto err;
                    }
                    fpga_gc->mask |= 1 << (gc->ngpio - 1 - gpio);;
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
            while(maplen>=3) {
                if(gpio_map[1]) /* OUT */
                    fpga_gc->dir |= (1 << (gc->ngpio - 1 - gpio_map[0]));
                else            /* IN */
                    fpga_gc->dir &= ~(1 << (gc->ngpio - 1 - gpio_map[0]));
                gpio_map += 3;
                maplen -= 3;
            }
        }

		ret = of_mm_gpiochip_add(np, mm_gc);
		if (ret){
            pr_err("%s#%d: ERROR: registration failed with status %d in \"%s\"\n",
                   compatible, no, ret, np->full_name);
			goto err;
        }
        pr_info("%s#%d: range=[%d-%d]\n", compatible, no,
                gc->base,gc->ngpio+gc->base-1);

        /* gpio-map(data) */
        {
            const u32 *gpio_map;
            u32 maplen;
            gpio_map = of_get_property(np, "gpio-map", &maplen);
            if(!gpio_map)
                break;
            maplen /= sizeof(u32);
            while(maplen>=3) {
                pr_info("%s#%d: pin=%d,dir=%s,dat=%s\n",
                        compatible,no,
                        (unsigned int)gpio_map[0],        /* pin */
                        (int)gpio_map[1]?"OUT":"IN",      /* dir */
                        (int)gpio_map[2]?"H":"L");        /* dat */
                if(gpio_map[1])
                    gc->set(gc,gpio_map[0],gpio_map[2]);
                gpio_map += 3;
                maplen -= 3;
            }
        }

		goto next;
    err:
		kfree(fpga_gc);
		/* try others anyway */

    next:
        no++;
	}
	return 0;
}
core_initcall(fpga_add_gpiochips);
