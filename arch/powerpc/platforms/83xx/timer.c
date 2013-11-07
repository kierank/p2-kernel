/*
 * MPC83xx Global Timer4 support
 *
 * This driver is currently specific to timer 4 in 16-bit mode,
 * as that is all that can be used as a wakeup source for deep sleep
 * on the MPC8313.
 *
 * Copyright (c) 2007 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/sysfs.h>
#include <linux/of_platform.h>

#include <linux/io.h>
#include <linux/irq.h>

#include <sysdev/fsl_soc.h>

#define MDR_ICLK_DIV16	0x0004

struct gtm_regs {
	u8    cfr1; /* Timer1/2 Configuration  */
	#define CFR1_PCAS 0x80 /* Pair Cascade mode  */
	#define CFR1_BCM  0x40  /* Backward compatible mode  */
	#define CFR1_STP2 0x20 /* Stop timer  */
	#define CFR1_RST2 0x10 /* Reset timer  */
	#define CFR1_GM2  0x08 /* Gate mode for pin 2  */
	#define CFR1_GM1  0x04 /* Gate mode for pin 1  */
	#define CFR1_STP1 0x02 /* Stop timer  */
	#define CFR1_RST1 0x01 /* Reset timer  */
	#define CFR1_RES ~(CFR1_PCAS | CFR1_STP2 | CFR1_RST2 | CFR1_GM2 |\
		CFR1_GM1 | CFR1_STP1 | CFR1_RST1)

	u8    res0[3];
	u8    cfr2; /* Timer3/4 Configuration  */
	#define CFR2_PCAS 0x80 /* Pair Cascade mode  */
	#define CFR2_SCAS 0x40 /* Super Cascade mode  */
	#define CFR2_STP4 0x20 /* Stop timer  */
	#define CFR2_RST4 0x10 /* Reset timer  */
	#define CFR2_GM4  0x08 /* Gate mode for pin 4  */
	#define CFR2_GM3  0x04 /* Gate mode for pin 3  */
	#define CFR2_STP3 0x02 /* Stop timer  */
	#define CFR2_RST3 0x01 /* Reset timer  */

	u8    res1[11];
	u16   mdr1; /* Timer1 Mode Register  */
	#define MDR_SPS  0xff00 /* Secondary Prescaler value (256) */
	#define MDR_CE   0x00c0 /* Capture edge and enable interrupt  */
	#define MDR_OM   0x0020 /* Output mode  */
	#define MDR_ORI  0x0010 /* Output reference interrupt enable  */
	#define MDR_FRR  0x0008 /* Free run/restart  */
	#define MDR_ICLK 0x0006 /* Input clock source for the timer */
	#define MDR_GE   0x0001 /* Gate enable  */

	u16   mdr2; /* Timer2 Mode Register  */
	u16   rfr1; /* Timer1 Reference Register  */
	u16   rfr2; /* Timer2 Reference Register  */
	u16   cpr1; /* Timer1 Capture Register  */
	u16   cpr2; /* Timer2 Capture Register  */
	u16   cnr1; /* Timer1 Counter Register  */
	u16   cnr2; /* Timer2 Counter Register  */
	u16   mdr3; /* Timer3 Mode Register  */
	u16   mdr4; /* Timer4 Mode Register  */
	u16   rfr3; /* Timer3 Reference Register  */
	u16   rfr4; /* Timer4 Reference Register  */
	u16   cpr3; /* Timer3 Capture Register  */
	u16   cpr4; /* Timer4 Capture Register  */
	u16   cnr3; /* Timer3 Counter Register  */
	u16   cnr4; /* Timer4 Counter Register  */
	u16   evr1; /* Timer1 Event Register  */
	u16   evr2; /* Timer2 Event Register  */
	u16   evr3; /* Timer3 Event Register  */
	u16   evr4; /* Timer4 Event Register  */
	#define GTEVR_REF 0x0002 /* Output reference event  */
	#define GTEVR_CAP 0x0001 /* Counter Capture event   */
	#define GTEVR_RES ~(EVR_CAP|EVR_REF)

	u16   psr1; /* Timer1 Prescaler Register  */
	u16   psr2; /* Timer2 Prescaler Register  */
	u16   psr3; /* Timer3 Prescaler Register  */
	u16   psr4; /* Timer4 Prescaler Register  */
	#define GTPSR_PPS  0x00FF /* Primary Prescaler Bits (256). */
	#define GTPSR_RES  ~(GTPSR_PPS)
};

struct gtm_priv {
	struct gtm_regs __iomem *regs;
	int irq;
	int ticks_per_sec;
	spinlock_t lock;
};

static irqreturn_t fsl_gtm_isr(int irq, void *dev_id)
{
	struct gtm_priv *priv = dev_id;
	unsigned long flags;
	u16 event;

	spin_lock_irqsave(&priv->lock, flags);

	event = in_be16(&priv->regs->evr4);
	out_be16(&priv->regs->evr4, event);

	if (event & GTEVR_REF)
		out_8(&priv->regs->cfr2, CFR2_STP4);

	spin_unlock_irqrestore(&priv->lock, flags);
	return event ? IRQ_HANDLED : IRQ_NONE;
}

static ssize_t gtm_timeout_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct gtm_priv *priv = dev_get_drvdata(dev);
	unsigned long interval = simple_strtoul(buf, NULL, 0);

	if (interval > 0xffff) {
		dev_dbg(dev, "gtm: interval %lu (in ns) too long\n", interval);
		return -EINVAL;
	}

	interval *= priv->ticks_per_sec;

	if (interval > 0xffff) {
		dev_dbg(dev, "gtm: interval %lu (in ticks) too long\n",
			interval);
		return -EINVAL;
	}

	spin_lock_irq(&priv->lock);

	/* reset timer 4 */
	out_8(&priv->regs->cfr2, CFR2_STP3 | CFR2_STP4);

	if (interval != 0) {
		out_8(&priv->regs->cfr2, CFR2_GM4 | CFR2_RST4 | CFR2_STP4);
		/* clear events */
		out_be16(&priv->regs->evr4, GTEVR_REF | GTEVR_CAP);
		/* maximum primary prescale (256) */
		out_be16(&priv->regs->psr4, GTPSR_PPS);
		/* clear current counter */
		out_be16(&priv->regs->cnr4, 0x0);
		/* set count limit */
		out_be16(&priv->regs->rfr4, interval);
		out_be16(&priv->regs->mdr4, MDR_SPS | MDR_ORI | MDR_FRR |
			 MDR_ICLK_DIV16);
		/* start timer */
		out_8(&priv->regs->cfr2, CFR2_GM4 | CFR2_STP3 | CFR2_RST4);
	}

	spin_unlock_irq(&priv->lock);
	return count;
}

static ssize_t gtm_timeout_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct gtm_priv *priv = dev_get_drvdata(dev);
	int timeout = 0;

	spin_lock_irq(&priv->lock);

	if (!(in_8(&priv->regs->cfr2) & CFR2_STP4)) {
		timeout = in_be16(&priv->regs->rfr4) -
			  in_be16(&priv->regs->cnr4);
		timeout += priv->ticks_per_sec - 1;
		timeout /= priv->ticks_per_sec;
	}

	spin_unlock_irq(&priv->lock);
	return sprintf(buf, "%u\n", timeout);
}

static DEVICE_ATTR(timeout, 0660, gtm_timeout_show, gtm_timeout_store);

static int __devinit gtm_probe(struct of_device *dev,
			       const struct of_device_id *match)
{
	struct device_node *np = dev->node;
	struct resource res;
	int ret = 0;
	u32 busfreq = fsl_get_sys_freq();
	struct gtm_priv *priv;

	if (busfreq == 0) {
		dev_err(&dev->dev, "gtm: No bus frequency in device tree.\n");
		return -ENODEV;
	}

	priv = kmalloc(sizeof(struct gtm_priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	spin_lock_init(&priv->lock);
	dev_set_drvdata(&dev->dev, priv);

	ret = of_address_to_resource(np, 0, &res);
	if (ret)
		goto out;

	priv->irq = irq_of_parse_and_map(np, 0);
	if (priv->irq == NO_IRQ) {
		dev_err(&dev->dev, "mpc83xx-gtm exists in device tree "
				   "without an IRQ.\n");
		ret = -ENODEV;
		goto out;
	}

	ret = request_irq(priv->irq, fsl_gtm_isr, 0, "gtm timer", priv);
	if (ret)
		goto out;

	priv->regs = ioremap(res.start, sizeof(struct gtm_regs));
	if (!priv->regs) {
		ret = -ENOMEM;
		goto out;
	}

	/* Disable the unused clocks to save power. */
	out_8(&priv->regs->cfr1, CFR1_STP1 | CFR1_STP2);
	out_8(&priv->regs->cfr2, CFR2_STP3 | CFR2_STP4);

	/*
	 * Maximum prescaling is used (input clock/16, 256 primary prescaler,
	 * 256 secondary prescaler) to maximize the timer's range.  With a
	 * bus clock of 133MHz, this yields a maximum interval of 516
	 * seconds while retaining subsecond precision.  Since only
	 * timer 4 is supported for wakeup on the 8313, and timer 4
	 * is the LSB when chained, we can't use chaining to increase
	 * the range.
	 */
	priv->ticks_per_sec = busfreq / (16*256*256);

	ret = device_create_file(&dev->dev, &dev_attr_timeout);
	if (ret)
		goto out;

	return 0;

out:
	kfree(priv);
	return ret;
}

static int __devexit gtm_remove(struct of_device *dev)
{
	struct gtm_priv *priv = dev_get_drvdata(&dev->dev);

	device_remove_file(&dev->dev, &dev_attr_timeout);
	free_irq(priv->irq, priv);
	iounmap(priv->regs);

	dev_set_drvdata(&dev->dev, NULL);
	kfree(priv);
	return 0;
}

static struct of_device_id gtm_match[] = {
	{
		.compatible = "fsl,mpc83xx-gtm",
	},
	{},
};

static struct of_platform_driver gtm_driver = {
	.name = "mpc83xx-gtm",
	.match_table = gtm_match,
	.probe = gtm_probe,
	.remove = __devexit_p(gtm_remove)
};

static int __init gtm_init(void)
{
	return of_register_platform_driver(&gtm_driver);
}

static void __exit gtm_exit(void)
{
	of_unregister_platform_driver(&gtm_driver);
}

module_init(gtm_init);
module_exit(gtm_exit);
