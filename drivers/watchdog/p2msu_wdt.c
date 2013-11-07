/*
 * p2msu_wdt.c - P2MSU watchdog userspace interface
 *
 */
/* $Id: p2msu_wdt.c 6697 2010-04-27 06:31:05Z Noguchi Isao $ */

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/watchdog.h>
#include <linux/io.h>
#include <linux/uaccess.h>

#include <linux/platform_device.h>
#include <linux/p2pf_fpga_devices.h>

struct p2msu_wdt {
	__be16 wcrr;                /* System watchdog control register */
#define BIT_WCRR_SWEN 0x00000001 /* Watchdog Enable bit. */
	__be16 wcnr;                 /* System watchdog count register */
#define SHIFT_WCNR_WTC  8        /*  */
#define MASK_WCNR_WTC (0xff<<SHIFT_WCNR_WTC) /* Mask of Software Watchdog Time Count. */
#define SHIFT_WCNR_WCN  0        /*  */
#define MASK_WCNR_WCN (0xff<<SHIFT_WCNR_WCN) /* Mask of Software Watchdog Counter. */
	__be16 wsrr; /* System watchdog service register */
};

#define drvname "p2msu-wdt"

static struct p2msu_wdt __iomem *wd_base;
static int p2msu_wdt_init_late(void);

static unsigned int ticks = 0xff;
module_param(ticks, uint, 0444);
MODULE_PARM_DESC(ticks,
	"Watchdog timeout in ticks. (0<ticks<256, default=255");

static int nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, int, 0444);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started "
		 "(default=" __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static unsigned int timeout_msec;
static unsigned int period;

static unsigned long wdt_is_open;
static DEFINE_SPINLOCK(wdt_spinlock);

static void p2msu_wdt_keepalive(void)
{
    unsigned long flags;
	/* Ping the WDT */
    pr_debug("[%s] Watchdog is keeped alive.\n",drvname);
	spin_lock_irqsave(&wdt_spinlock,flags);
	out_be16(&wd_base->wsrr, 0x5032);
	spin_unlock_irqrestore(&wdt_spinlock,flags);
}

static void p2msu_wdt_timer_ping(unsigned long arg);
static DEFINE_TIMER(wdt_timer, p2msu_wdt_timer_ping, 0, 0);

static void p2msu_wdt_timer_ping(unsigned long arg)
{
	p2msu_wdt_keepalive();
	/* We're pinging it twice faster than needed, just to be sure. */
	mod_timer(&wdt_timer, jiffies + HZ * timeout_msec / 2000);
}

static void p2msu_wdt_setcnt(unsigned int ticks)
{
    unsigned long flags;
	spin_lock_irqsave(&wdt_spinlock,flags);
	out_be16(&wd_base->wcnr, ticks<<SHIFT_WCNR_WTC);
	spin_unlock_irqrestore(&wdt_spinlock,flags);
}

static void p2msu_wdt_start(unsigned int ticks)
{
    unsigned long flags;
	spin_lock_irqsave(&wdt_spinlock,flags);
	out_be16(&wd_base->wcnr, ticks<<SHIFT_WCNR_WTC);
	out_be16(&wd_base->wcrr, in_be16(&wd_base->wcrr)|BIT_WCRR_SWEN);
	spin_unlock_irqrestore(&wdt_spinlock,flags);
}

static void p2msu_wdt_stop(void)
{
    unsigned long flags;
	spin_lock_irqsave(&wdt_spinlock,flags);
	out_be16(&wd_base->wcrr, in_be16(&wd_base->wcrr)&~BIT_WCRR_SWEN);
	spin_unlock_irqrestore(&wdt_spinlock,flags);
}

static void p2msu_wdt_pr_warn(const char *msg)
{
	pr_crit("[%s] %s, expect the %s soon!\n", drvname, msg, "reset");
}

static ssize_t p2msu_wdt_write(struct file *file, const char __user *buf,
				 size_t count, loff_t *ppos)
{
	if (count)
		p2msu_wdt_keepalive();
	return count;
}

static int p2msu_wdt_open(struct inode *inode, struct file *file)
{
	if (test_and_set_bit(0, &wdt_is_open))
		return -EBUSY;

	/* Once we start the watchdog we can't stop it */
	if (nowayout)
		__module_get(THIS_MODULE);

	p2msu_wdt_start(ticks);

	del_timer_sync(&wdt_timer);

	return nonseekable_open(inode, file);
}

static int p2msu_wdt_release(struct inode *inode, struct file *file)
{
	if (!nowayout)
		p2msu_wdt_timer_ping(0);
	else
		p2msu_wdt_pr_warn("watchdog closed");
	clear_bit(0, &wdt_is_open);
	return 0;
}

static long p2msu_wdt_ioctl(struct file *file, unsigned int cmd,
							unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int __user *p = argp;
	static struct watchdog_info ident = {
		.options = WDIOF_KEEPALIVEPING|WDIOF_SETTIMEOUT,
		.firmware_version = 1,
		.identity = drvname,
	};

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		return copy_to_user(argp, &ident, sizeof(ident)) ? -EFAULT : 0;
	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		return put_user(0, p);
	case WDIOC_KEEPALIVE:
		p2msu_wdt_keepalive();
		return 0;
	case WDIOC_GETTIMEOUT:
        {
            int tout = (timeout_msec+999)/1000;
            return put_user(tout, p);
        }
	case WDIOC_SETTIMEOUT:
        {
            int tout;
            int retval=get_user(tout, p);
            if(retval)
                return retval;
            timeout_msec = tout * 1000;
            ticks = timeout_msec / period;
            if(ticks<1||ticks>255){
                pr_err("[%s] ERROR: invalid value : ticks=%d\n", drvname, ticks);
                return -EINVAL;
            }
            p2msu_wdt_setcnt(ticks);
            printk(KERN_DEBUG "[%s] WDT driver for P2MSU initialized. timeout=%d ticks(%d seconds)\n", drvname,
                   ticks, (timeout_msec+999)/1000);
            return 0;
        }
	default:
		return -ENOTTY;
	}
}

static const struct file_operations p2msu_wdt_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.write		= p2msu_wdt_write,
	.unlocked_ioctl	= p2msu_wdt_ioctl,
	.open		= p2msu_wdt_open,
	.release	= p2msu_wdt_release,
};

static struct miscdevice p2msu_wdt_miscdev = {
	.minor	= WATCHDOG_MINOR,
	.name	= "watchdog",
	.fops	= &p2msu_wdt_fops,
};

static int __init p2msu_wdt_probe(struct platform_device *dev)
{
	bool enabled;
    struct p2msu_wdt_platform_data *pdata;

	pdata = (struct p2msu_wdt_platform_data *)dev->dev.platform_data;
    if (pdata == NULL) {
        pr_err("[%s] ERROR: platform data is NOT found.\n",drvname);
		return -EINVAL;
	}

    /* Watchdog timeout in ticks */
    if(pdata->ticks)
        ticks = pdata->ticks;
    if(ticks<1||ticks>255){
        pr_err("[%s] ERROR: invalid value : ticks=%d\n", drvname, ticks);
        return -EINVAL;
    }

    /* Period in ticks [msec] */
    if(!pdata->period){
        pr_err("[%s] ERROR: invalid value : period=%d\n", drvname, pdata->period);
        return -EINVAL;
    }
    period=pdata->period;

	/* Calculate the timeout in msec */
    timeout_msec = ticks * period;

    {
        struct resource *r = platform_get_resource(dev, IORESOURCE_MEM, 0);
        if(!r){
            pr_err("[%s] ERROR: Can NOT get resource\n", drvname);
            return -EINVAL;
        }
        
        wd_base = (struct p2msu_wdt __iomem *)ioremap(r->start, (r->end - r->start + 1));
        if (!wd_base){
            pr_err("[%s] ERROR: Can NOT ioremap: [0x%08x-0x%08x]\n",drvname, r->start,r->end);
            return -ENOMEM;
        }
    }

    /* check enable bit */
	enabled = in_be16(&wd_base->wcrr) & BIT_WCRR_SWEN;

#ifdef MODULE
    if(p2msu_wdt_init_late()){
        iounmap(wd_base);
        wd_base = NULL;
		return err_unmap;
    }
#endif

	printk(KERN_DEBUG "[%s] WDT driver for P2MSU initialized. timeout=%d ticks(%d seconds)\n", drvname,
            ticks, (timeout_msec+999)/1000);

	/*
	 * If the watchdog was previously enabled or we're running on
	 * P2MSU, we should ping the wdt from the kernel until the
	 * userspace handles it.
	 */
	if (enabled)
		p2msu_wdt_timer_ping(0);
	return 0;
}

static int __exit p2msu_wdt_remove(struct platform_device *dev)
{
	p2msu_wdt_pr_warn("watchdog removed");
    p2msu_wdt_stop();
	del_timer_sync(&wdt_timer);
	misc_deregister(&p2msu_wdt_miscdev);
	iounmap(wd_base);

	return 0;
}

static void p2msu_wdt_shutdown(struct platform_device *dev)
{
	p2msu_wdt_pr_warn("watchdog shutdown");
    p2msu_wdt_stop();
}

MODULE_ALIAS("platform:p2msu-wdt");
static struct platform_driver p2msu_wdt_driver = {
	.remove		= __exit_p(p2msu_wdt_remove),
	.driver		= {
		.name	= drvname,
		.owner	= THIS_MODULE,
	},
    .shutdown   = p2msu_wdt_shutdown,
};

/*
 * We do wdt initialization in two steps: arch_initcall probes the wdt
 * very early to start pinging the watchdog (misc devices are not yet
 * available), and later module_init() just registers the misc device.
 */
static int p2msu_wdt_init_late(void)
{
	int ret;

	if (!wd_base)
		return -ENODEV;

	ret = misc_register(&p2msu_wdt_miscdev);
	if (ret) {
		pr_err("cannot register miscdev on minor=%d (err=%d)\n",
			WATCHDOG_MINOR, ret);
		return ret;
	}
	return 0;
}
#ifndef MODULE
module_init(p2msu_wdt_init_late);
#endif  /* MODULE */

static int __init p2msu_wdt_init(void)
{
	return platform_driver_probe(&p2msu_wdt_driver, p2msu_wdt_probe);
}
arch_initcall(p2msu_wdt_init);

static void __exit p2msu_wdt_exit(void)
{
	platform_driver_unregister(&p2msu_wdt_driver);
}
module_exit(p2msu_wdt_exit);

MODULE_DESCRIPTION("Driver for watchdog timer in P2MSU");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
