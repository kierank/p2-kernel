/*
 * Copyright (c) 2005 MontaVista Software
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Ported to 834x by Randy Vinson <rvinson@mvista.com> using code provided
 * by Hunter Wu.
 */
/* $Id: ehci-fsl.c 21891 2013-04-16 05:23:30Z Masaki Sato $ */

#include <linux/platform_device.h>
#include <linux/fsl_devices.h>

#include "ehci-fsl.h"

/* FIXME: Power Management is un-ported so temporarily disable it */
#undef CONFIG_PM

#ifdef CONFIG_USB_EHCI_FSL_ULPI_PHY_SUPPORT

/*!
 *  Read register of ULPI-PHY
 */
static u8 mpc83xx_ulpi_read_phy(struct usb_hcd *hcd, const u8 port, const u8 addr)
{
    u32 val;
    u32 timeout = 1000*300; // timeout = 300msec
    struct ehci_hcd *ehci = hcd_to_ehci (hcd);
    u32 __iomem *ulpivp = hcd->regs + FSL_SOC_USB_ULPIVP;
    ehci_writel(ehci, ULPIRUN|ULPIPORT(port)|ULPIADDR(addr), ulpivp);
    while(1){
       val =  ehci_readl(ehci,ulpivp);
       if(!(val&ULPIRUN))
           break;
       timeout--;
       if(!timeout){
	 	printk(KERN_DEBUG "mp83xx_ulpi_read_phy timeout!\n");
		break;
       }
       udelay(1);
    }
    return (val&ULPIDATRD_MSK)>>ULPIDATRD_SHIFT;
}

/*!
 *  Write register of ULPI-PHY
 */
static void mpc83xx_ulpi_write_phy(struct usb_hcd *hcd, const u8 port, const u8 addr, const u8 val)
{
    struct ehci_hcd *ehci = hcd_to_ehci (hcd);
    u32 __iomem *ulpivp = hcd->regs + FSL_SOC_USB_ULPIVP;
    u32 timeout = 1000*300; // timeout = 300msec
    ehci_writel(ehci, ULPIRUN|ULPIRW|ULPIPORT(port)|ULPIADDR(addr)|ULPIDATWR(val), ulpivp);
    while(1){
        if(!(ehci_readl(ehci,ulpivp)&ULPIRUN))
            break;
       timeout--;
       if(!timeout){
	 	printk(KERN_DEBUG "mp83xx_ulpi_write_phy timeout!\n");
		break;
       }
       udelay(1);
    }
}

#endif  /* CONFIG_USB_EHCI_FSL_ULPI_PHY_SUPPORT */

/* configure so an HC device and id are always provided */
/* always called with process context; sleeping is OK */

/**
 * usb_hcd_fsl_probe - initialize FSL-based HCDs
 * @drvier: Driver to be used for this HCD
 * @pdev: USB Host Controller being probed
 * Context: !in_interrupt()
 *
 * Allocates basic resources for this USB host controller.
 *
 */
int usb_hcd_fsl_probe(const struct hc_driver *driver,
		      struct platform_device *pdev)
{
	struct fsl_usb2_platform_data *pdata;
	struct usb_hcd *hcd;
	struct resource *res;
	int irq;
	int retval;
	unsigned int temp;

	pr_debug("initializing FSL-SOC USB Controller\n");

	/* Need platform data for setup */
	pdata = (struct fsl_usb2_platform_data *)pdev->dev.platform_data;
	if (!pdata) {
		dev_err(&pdev->dev,
			"No platform data for %s.\n", dev_name(&pdev->dev));
		return -ENODEV;
	}

	/*
	 * This is a host mode driver, verify that we're supposed to be
	 * in host mode.
	 */
	if (!((pdata->operating_mode == FSL_USB2_DR_HOST) ||
	      (pdata->operating_mode == FSL_USB2_MPH_HOST) ||
	      (pdata->operating_mode == FSL_USB2_DR_OTG))) {
		dev_err(&pdev->dev,
			"Non Host Mode configured for %s. Wrong driver linked.\n",
			dev_name(&pdev->dev));
		return -ENODEV;
	}

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(&pdev->dev,
			"Found HC with no IRQ. Check %s setup!\n",
			dev_name(&pdev->dev));
		return -ENODEV;
	}
	irq = res->start;

	hcd = usb_create_hcd(driver, &pdev->dev, dev_name(&pdev->dev));
	if (!hcd) {
		retval = -ENOMEM;
		goto err1;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev,
			"Found HC with no register addr. Check %s setup!\n",
			dev_name(&pdev->dev));
		retval = -ENODEV;
		goto err2;
	}
	hcd->rsrc_start = res->start;
	hcd->rsrc_len = res->end - res->start + 1;
	if (!request_mem_region(hcd->rsrc_start, hcd->rsrc_len,
				driver->description)) {
		dev_dbg(&pdev->dev, "controller already in use\n");
		retval = -EBUSY;
		goto err2;
	}
	hcd->regs = ioremap(hcd->rsrc_start, hcd->rsrc_len);

	if (hcd->regs == NULL) {
		dev_dbg(&pdev->dev, "error mapping memory\n");
		retval = -EFAULT;
		goto err3;
	}

	/* Enable USB controller */
	temp = in_be32(hcd->regs + 0x500);
	out_be32(hcd->regs + 0x500, temp | 0x4);

	/* Set to Host mode */
	temp = in_le32(hcd->regs + 0x1a8);
	out_le32(hcd->regs + 0x1a8, temp | 0x3);

	retval = usb_add_hcd(hcd, irq, IRQF_DISABLED | IRQF_SHARED);
	if (retval != 0)
		goto err4;
	return retval;

      err4:
	iounmap(hcd->regs);
      err3:
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
      err2:
	usb_put_hcd(hcd);
      err1:
	dev_err(&pdev->dev, "init %s fail, %d\n", dev_name(&pdev->dev), retval);
	return retval;
}

/* may be called without controller electrically present */
/* may be called with controller, bus, and devices active */

/**
 * usb_hcd_fsl_remove - shutdown processing for FSL-based HCDs
 * @dev: USB Host Controller being removed
 * Context: !in_interrupt()
 *
 * Reverses the effect of usb_hcd_fsl_probe().
 *
 */
void usb_hcd_fsl_remove(struct usb_hcd *hcd, struct platform_device *pdev)
{
	usb_remove_hcd(hcd);
	iounmap(hcd->regs);
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
	usb_put_hcd(hcd);
}

static void mpc83xx_setup_phy(struct ehci_hcd *ehci,
			      enum fsl_usb2_phy_modes phy_mode,
			      unsigned int port_offset)
{
	u32 portsc = 0;
	switch (phy_mode) {
	case FSL_USB2_PHY_ULPI:
		portsc |= PORT_PTS_ULPI;
		break;
	case FSL_USB2_PHY_SERIAL:
		portsc |= PORT_PTS_SERIAL;
		break;
	case FSL_USB2_PHY_UTMI_WIDE:
		portsc |= PORT_PTS_PTW;
		/* fall through */
	case FSL_USB2_PHY_UTMI:
		portsc |= PORT_PTS_UTMI;
		break;
	case FSL_USB2_PHY_NONE:
		break;
	}
	ehci_writel(ehci, portsc, &ehci->regs->port_status[port_offset]);

#ifdef CONFIG_USB_EHCI_FSL_ULPI_PHY_SUPPORT

    /*
     *  setup register of ULPI-PHY
     */
    if(phy_mode==FSL_USB2_PHY_ULPI){
        struct usb_hcd *hcd = ehci_to_hcd(ehci);

        printk(KERN_DEBUG "Select EXTBUS indicator for RXCMD VbusValid\n");
#ifdef CONFIG_USB_EHCI_FSL_ULPI_PHY_EXTVBUS
        /* set UseExternalVbusIndicator */
        mpc83xx_ulpi_write_phy(hcd,port_offset,0x0B,0x80);
  #ifdef CONFIG_USB_EHCI_FSL_ULPI_PHY_EXTVBUS_HIGH
        /* set IndicatorPassThru */
        mpc83xx_ulpi_write_phy(hcd,port_offset,0x08,0x40);
        /* clear IndicatorComplement */
        mpc83xx_ulpi_write_phy(hcd,port_offset,0x09,0x20);
  #endif  /* CONFIG_USB_EHCI_FSL_ULPI_PHY_EXTVBUS_HIGH */
  #ifdef CONFIG_USB_EHCI_FSL_ULPI_PHY_EXTVBUS_LOW
        /* set IndicatorComplement & IndicatorPassThru */
        mpc83xx_ulpi_write_phy(hcd,port_offset,0x08,0x60);
  #endif  /* CONFIG_USB_EHCI_FSL_ULPI_PHY_EXTVBUS_LOW */
#endif  /* CONFIG_USB_EHCI_FSL_ULPI_PHY_EXTVBUS */
/*         { */
/*             int i; */
/*             for(i=0;i<=0x18;i++) */
/*                 printk(KERN_INFO "**** PHY[0x%02X]=0x%02X\n", */
/*                        i,mpc83xx_ulpi_read_phy(hcd,port_offset,i)); */
/*         } */
    }

#endif  /* CONFIG_USB_EHCI_FSL_ULPI_PHY_SUPPORT */

}

static void mpc83xx_usb_setup(struct usb_hcd *hcd)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	struct fsl_usb2_platform_data *pdata;
	void __iomem *non_ehci = hcd->regs;
	u32 temp;

	pdata =
	    (struct fsl_usb2_platform_data *)hcd->self.controller->
	    platform_data;
	/* Enable PHY interface in the control reg. */
	temp = in_be32(non_ehci + FSL_SOC_USB_CTRL);
	out_be32(non_ehci + FSL_SOC_USB_CTRL, temp | 0x00000004);
	out_be32(non_ehci + FSL_SOC_USB_SNOOP1, 0x0000001b);

#if defined(CONFIG_PPC32) && !defined(CONFIG_NOT_COHERENT_CACHE)
	/*
	 * Turn on cache snooping hardware, since some PowerPC platforms
	 * wholly rely on hardware to deal with cache coherent
	 */

	/* Setup Snooping for all the 4GB space */
	/* SNOOP1 starts from 0x0, size 2G */
	out_be32(non_ehci + FSL_SOC_USB_SNOOP1, 0x0 | SNOOP_SIZE_2GB);
	/* SNOOP2 starts from 0x80000000, size 2G */
	out_be32(non_ehci + FSL_SOC_USB_SNOOP2, 0x80000000 | SNOOP_SIZE_2GB);
#endif

	if ((pdata->operating_mode == FSL_USB2_DR_HOST) ||
			(pdata->operating_mode == FSL_USB2_DR_OTG))
		mpc83xx_setup_phy(ehci, pdata->phy_mode, 0);

	if (pdata->operating_mode == FSL_USB2_MPH_HOST) {
		unsigned int chip, rev, svr;

		svr = mfspr(SPRN_SVR);
		chip = svr >> 16;
		rev = (svr >> 4) & 0xf;

		/* Deal with USB Erratum #14 on MPC834x Rev 1.0 & 1.1 chips */
		if ((rev == 1) && (chip >= 0x8050) && (chip <= 0x8055))
			ehci->has_fsl_port_bug = 1;

		if (pdata->port_enables & FSL_USB2_PORT0_ENABLED)
			mpc83xx_setup_phy(ehci, pdata->phy_mode, 0);
		if (pdata->port_enables & FSL_USB2_PORT1_ENABLED)
			mpc83xx_setup_phy(ehci, pdata->phy_mode, 1);
	}

	/* put controller in host mode. */
	ehci_writel(ehci, 0x00000003, non_ehci + FSL_SOC_USB_USBMODE);
#ifdef CONFIG_PPC_85xx
	out_be32(non_ehci + FSL_SOC_USB_PRICTRL, 0x00000008);
	out_be32(non_ehci + FSL_SOC_USB_AGECNTTHRSH, 0x00000080);
#else
	out_be32(non_ehci + FSL_SOC_USB_PRICTRL, 0x0000000c);
	out_be32(non_ehci + FSL_SOC_USB_AGECNTTHRSH, 0x00000040);
#endif
	out_be32(non_ehci + FSL_SOC_USB_SICTRL, 0x00000001);
}

/* called after powerup, by probe or system-pm "wakeup" */
static int ehci_fsl_reinit(struct ehci_hcd *ehci)
{
	mpc83xx_usb_setup(ehci_to_hcd(ehci));
	ehci_port_power(ehci, 0);

	return 0;
}

/* called during probe() after chip reset completes */
static int ehci_fsl_setup(struct usb_hcd *hcd)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	int retval;

	/* EHCI registers start at offset 0x100 */
	ehci->caps = hcd->regs + 0x100;
	ehci->regs = hcd->regs + 0x100 +
	    HC_LENGTH(ehci_readl(ehci, &ehci->caps->hc_capbase));
	dbg_hcs_params(ehci, "reset");
	dbg_hcc_params(ehci, "reset");

	/* cache this readonly data; minimize chip reads */
	ehci->hcs_params = ehci_readl(ehci, &ehci->caps->hcs_params);

	retval = ehci_halt(ehci);
	if (retval)
		return retval;

	/* data structure init */
	retval = ehci_init(hcd);
	if (retval)
		return retval;

	hcd->has_tt = 1;

	ehci->sbrn = 0x20;

	ehci_reset(ehci);

	retval = ehci_fsl_reinit(ehci);
	return retval;
}

/* 2009/5/22, Added by Panasonic >>>> */
static int ehci_fsl_start (struct usb_hcd *hcd)
{
    int retval=0;

    retval=ehci_run (hcd);
    if(retval!=0)
        return retval;

#ifdef CONFIG_USB_EHCI_FSL_OPTION

    /* set/clear SDIS bit in USBMODE */
    {
        struct ehci_hcd		*ehci = hcd_to_ehci (hcd);
        void __iomem *non_ehci = hcd->regs;
        unsigned long data = ehci_readl(ehci,non_ehci + FSL_SOC_USB_USBMODE);
#ifdef CONFIG_USB_EHCI_FSL_STREAM_DISABLE
        data |= 0x10; 
#else  /* CONFIG_USB_EHCI_FSL_STREAM_DISABLE */
        data &= ~0x10; 
#endif  /* CONFIG_USB_EHCI_FSL_STREAM_DISABLE */
        ehci_writel(ehci, data, non_ehci + FSL_SOC_USB_USBMODE);

/*         printk(KERN_INFO ">>>>>>>>>>>>>>>>>>> USBMODE=0x%08X\n", */
/*                ehci_readl(ehci,non_ehci+FSL_SOC_USB_USBMODE) ); */
    }

#ifdef CONFIG_USB_EHCI_FSL_TXFIFOTHRES
    /* set TXFILLTUNING register */
    {
        struct ehci_hcd		*ehci = hcd_to_ehci (hcd);
        void __iomem *non_ehci = hcd->regs;
        int thres = CONFIG_USB_EHCI_FSL_TXFIFOTHRES;
        int bursize = (ehci_readl(ehci,non_ehci+FSL_SOC_USB_BURSTSIZE)>>8)&0xff;
        /* TXSCHOH = TXFIFOTHRES * (BURSTSIZE * 4 bytes-per-word) / (40 * 1.267) */
        int schoh = ((thres * bursize * 4 * 1000) + (40*1267) - 1)  / (40*1267);
        unsigned long data = ((thres&0x3f)<<16) | (schoh & 0xff);
        ehci_writel(ehci, data, non_ehci + FSL_SOC_USB_TXFILLTUNING);

/*         printk(KERN_INFO ">>>>>>>>>>>>>>>>>>> TXFILLTUNING=0x%08X\n", */
/*                ehci_readl(ehci,non_ehci+FSL_SOC_USB_TXFILLTUNING) ); */
    }
#endif  /* CONFIG_USB_EHCI_FSL_TXFIFOTHRES */

#endif  /* CONFIG_USB_EHCI_FSL_OPTION */

    return retval;
}
/* <<<< 2009/5/22, Added by Panasonic */

static const struct hc_driver ehci_fsl_hc_driver = {
	.description = hcd_name,
	.product_desc = "Freescale On-Chip EHCI Host Controller",
	.hcd_priv_size = sizeof(struct ehci_hcd),

	/*
	 * generic hardware linkage
	 */
	.irq = ehci_irq,
	.flags = HCD_USB2,

	/*
	 * basic lifecycle operations
	 */
	.reset = ehci_fsl_setup,
    /* 2009/5/22, Modified by Panasonic >>>> */
    //	.start = ehci_run,
	.start = ehci_fsl_start,
    /* <<<< 2009/5/22, Modified by Panasonic */
	.stop = ehci_stop,
	.shutdown = ehci_shutdown,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue = ehci_urb_enqueue,
	.urb_dequeue = ehci_urb_dequeue,
	.endpoint_disable = ehci_endpoint_disable,
	.endpoint_reset = ehci_endpoint_reset,

	/*
	 * scheduling support
	 */
	.get_frame_number = ehci_get_frame,

	/*
	 * root hub support
	 */
	.hub_status_data = ehci_hub_status_data,
	.hub_control = ehci_hub_control,
	.bus_suspend = ehci_bus_suspend,
	.bus_resume = ehci_bus_resume,
	.relinquish_port = ehci_relinquish_port,
	.port_handed_over = ehci_port_handed_over,

	.clear_tt_buffer_complete = ehci_clear_tt_buffer_complete,

#ifdef CONFIG_USB_EHCI_FSL_ULPI_PHY_SUPPORT
    /* ULPI i/f */
    .ulpi_read = mpc83xx_ulpi_read_phy,
    .ulpi_write = mpc83xx_ulpi_write_phy,
#endif  /* CONFIG_USB_EHCI_FSL_ULPI_PHY_SUPPORT */
};

static int ehci_fsl_drv_probe(struct platform_device *pdev)
{
	if (usb_disabled())
		return -ENODEV;

	/* FIXME we only want one one probe() not two */
	return usb_hcd_fsl_probe(&ehci_fsl_hc_driver, pdev);
}

static int ehci_fsl_drv_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	/* FIXME we only want one one remove() not two */
	usb_hcd_fsl_remove(hcd, pdev);
	return 0;
}

MODULE_ALIAS("platform:fsl-ehci");

static struct platform_driver ehci_fsl_driver = {
	.probe = ehci_fsl_drv_probe,
	.remove = ehci_fsl_drv_remove,
	.shutdown = usb_hcd_platform_shutdown,
	.driver = {
		   .name = "fsl-ehci",
	},
};
