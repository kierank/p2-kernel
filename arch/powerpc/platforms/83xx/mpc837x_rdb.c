/*
 * arch/powerpc/platforms/83xx/mpc837x_rdb.c
 *
 * Copyright (C) 2007 Freescale Semicondutor, Inc. All rights reserved.
 *
 * MPC837x RDB board specific routines
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
/* $Id: mpc837x_rdb.c 11136 2010-12-14 05:30:50Z Noguchi Isao $ */

#include <linux/pci.h>
#include <linux/of_platform.h>

#include <asm/time.h>
#include <asm/ipic.h>
#include <asm/udbg.h>
#include <sysdev/fsl_pci.h>
#include <sysdev/fsl_soc.h>

/* 2009/12/7, added by Panasonic >>>> */
#include <linux/gpio.h>
#include <linux/spi/spi.h>
/* <<<< 2009/12/7, added by Panasonic */

#include "mpc83xx.h"
#if defined(CONFIG_HEART_BEAT)
# include <linux/heartbeat.h>
#endif /* CONFIG_HEART_BEAT */

/* 2010/1/12, added by panasonic >>>>  */
#ifdef CONFIG_P2PF_PM
#include <linux/p2pf_pm.h>
#endif  /* CONFIG_P2PF_PM */
/* <<<< 2010/1/12, added by panasonic */

extern int mpc837x_usb_cfg(void);

#if defined(CONFIG_MMC_ESDHC) || defined(CONFIG_MMC_ESDHC_MODULE)
static int mpc837xrdb_sd_cfg(void)
{
	void __iomem *immap = NULL;
	unsigned long sicrl, sicrh;

	/* Configure the multiplexing of pins */
	immap = ioremap(get_immrbase(), 0x1000);
	if (!immap)
		return -ENOMEM;

	sicrl = in_be32(immap + MPC83XX_SICRL_OFFS) & ~MPC837X_SICRL_USBB_MASK;
	sicrh = in_be32(immap + MPC83XX_SICRH_OFFS) & ~MPC837X_SICRH_SPI_MASK;

	out_be32(immap + MPC83XX_SICRL_OFFS, (sicrl | MPC837X_SICRL_SD));
	out_be32(immap + MPC83XX_SICRH_OFFS, (sicrh | MPC837X_SICRH_SD));
	udelay(100);
	iounmap(immap);
	return 0;
}
#endif

/* 2010/1/12, added by panasonic >>>>  */
static void mpc837x_rdb_power_off(void)
{
#ifdef CONFIG_P2PF_PM_POFF
    p2pm_power_off();
#endif  /* CONFIG_P2PF_PM_POFF */
}
/* <<<< 2010/1/12, added by panasonic */

/* 2010/3/1, added by panasonic >>>>  */
static void mpc837x_rdb_halt(void)
{
#ifdef CONFIG_P2PF_PM_POFF
    p2pm_power_off();
#endif  /* CONFIG_P2PF_PM_POFF */
}
/* <<<< 2010/3/1, added by panasonic */

/* 2009/12/7, added by Panasonic >>>> */
#ifdef CONFIG_SPI_MPC83xx

static void mpc837x_spi_activate_cs(u8 cs, u8 polarity)
{
    gpio_set_value(cs, polarity?1:0);
}


static void mpc837x_spi_deactivate_cs(u8 cs, u8 polarity)
{
    gpio_set_value(cs, polarity?0:1);
}


static int __init mpc837x_spi_init(void)
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

    num_board_infos = fsl_spi_board_info(board_infos,max_infos);
    if(num_board_infos<=0){
        retval = (num_board_infos==0)? -ENODEV: num_board_infos;
        goto exit;
    }

    retval = fsl_spi_init(board_infos, num_board_infos,
                          mpc837x_spi_activate_cs,
                          mpc837x_spi_deactivate_cs);

 exit:
    if(NULL!=board_infos)
        kfree(board_infos);

    return retval;
}

machine_device_initcall(mpc837x_rdb, mpc837x_spi_init);

#endif  /* CONFIG_SPI_MPC83xx */

/* <<<<< 2009/12/7, added by Panasonic */

/* ************************************************************************
 *
 * Setup the architecture
 *
 */
static void __init mpc837x_rdb_setup_arch(void)
{
#ifdef CONFIG_PCI
	struct device_node *np;
#endif

	if (ppc_md.progress)
		ppc_md.progress("mpc837x_rdb_setup_arch()", 0);

#ifdef CONFIG_PCI
	for_each_compatible_node(np, "pci", "fsl,mpc8349-pci")
		mpc83xx_add_bridge(np);

    /* 2010/7/26, added by Panasonic (SAV) */
#ifdef CONFIG_PPC_MPC83XX_PCIE
	for_each_compatible_node(np, "pci", "fsl,mpc83xx-pcie")
		mpc83xx_add_bridge(np);
#endif  /* CONFIG_PPC_MPC83XX_PCIE */

#endif
	mpc837x_usb_cfg();

#if defined(CONFIG_MMC_ESDHC) || defined(CONFIG_MMC_ESDHC_MODULE)
	 mpc837xrdb_sd_cfg();
#endif
}

static struct of_device_id mpc837x_ids[] = {
	{ .type = "soc", },
	{ .compatible = "soc", },
	{ .compatible = "simple-bus", },
	{},
};

static int __init mpc837x_declare_of_platform_devices(void)
{
	/* Publish of_device */
	of_platform_bus_probe(NULL, mpc837x_ids, NULL);

	return 0;
}
machine_device_initcall(mpc837x_rdb, mpc837x_declare_of_platform_devices);

static void __init mpc837x_rdb_init_IRQ(void)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "fsl,ipic");
	if (!np)
		return;

	ipic_init(np, 0);

	/* Initialize the default interrupt mapping priorities,
	 * in case the boot rom changed something on us.
	 */
	ipic_set_default_priority();
}

/* Panasonic Original */
#if defined(CONFIG_HEART_BEAT)
static void __init mpc837x_rdb_heartbeat_init(void)
{
	unsigned long interval, offset;
	int ncpus;
	
	interval = HZ / 80;
	offset = HZ;
	ncpus = num_online_cpus();
	interval *= ncpus;
	
#if ! defined(CONFIG_DISABLE_INIT_MESSAGE)
	udbg_printf(" CPU : %d\n", ncpus);
#endif /* ! CONFIG_DISABLE_INIT_MESSAGE */

	setup_heartbeat(interval, offset, ncpus);

#if ! defined(CONFIG_DISABLE_INIT_MESSAGE)
	udbg_printf("Event Scan Rate: (%lu jiffies)\n", interval);
#endif /* ! CONFIG_DISABLE_INIT_MESSAGE */
}
#endif /* CONFIG_HEART_BEAT */

static void __init mpc837x_rdb_init(void)
{
	if (ppc_md.progress)
		ppc_md.progress("mpc837x_rdb_init()", 0);

#if defined(CONFIG_HEART_BEAT)
    /* initialize hertbeat */
    mpc837x_rdb_heartbeat_init();
#endif /* CONFIG_HEART_BEAT */
}

/*--------------------*/

/*
 * Called very early, MMU is off, device-tree isn't unflattened
 */
static int __init mpc837x_rdb_probe(void)
{
	unsigned long root = of_get_flat_dt_root();

	return of_flat_dt_is_compatible(root, "fsl,mpc8377rdb") ||
	       of_flat_dt_is_compatible(root, "fsl,mpc8378rdb") ||
	       of_flat_dt_is_compatible(root, "fsl,mpc8379rdb");
}

define_machine(mpc837x_rdb) {
	.name			= "MPC837x RDB",
	.probe			= mpc837x_rdb_probe,
	.setup_arch		= mpc837x_rdb_setup_arch,
	/* Panasonic Original */
	.init			= mpc837x_rdb_init,
	/*--------------------*/
	.init_IRQ		= mpc837x_rdb_init_IRQ,
	.get_irq		= ipic_get_irq,
    /* 2010/1/12, added by panasonic >>>>  */
        .power_off      = mpc837x_rdb_power_off,
    /* <<<< 2010/1/12, added by panasonic */
    /* 2010/3/1, added by panasonic >>>>  */
        .power_off      = mpc837x_rdb_halt,
    /* <<<< 2010/3/1, added by panasonic */
	.restart		= mpc83xx_restart,
	.time_init		= mpc83xx_time_init,
	.calibrate_decr		= generic_calibrate_decr,
#if ! defined(CONFIG_DISABLE_INIT_MESSAGE) /* Added by Panasonic for fast bootup */
	.progress		= udbg_progress,
#endif /* ! CONFIG_DISABLE_INIT_MESSAGE */
};
