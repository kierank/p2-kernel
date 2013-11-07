/*
 * arch/powerpc/platforms/83xx/mpc831x_rdb.c
 *
 * Description: MPC831x RDB board specific routines.
 * This file is based on mpc834x_sys.c
 * Author: Lo Wlison <r43300@freescale.com>
 *
 * Copyright (C) Freescale Semiconductor, Inc. 2006. All rights reserved.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
/* $Id: mpc831x_rdb.c 11136 2010-12-14 05:30:50Z Noguchi Isao $ */

#include <linux/pci.h>
#include <linux/of_platform.h>

#include <asm/time.h>
#include <asm/ipic.h>
#include <asm/udbg.h>
#include <sysdev/fsl_pci.h>

/* 2009/12/7, added by Panasonic >>>> */
#include <sysdev/fsl_soc.h>

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

/* 2010/1/12, added by panasonic >>>>  */
static void mpc831x_rdb_power_off(void)
{
#ifdef CONFIG_P2PF_PM_POFF
    p2pm_power_off();
#endif  /* CONFIG_P2PF_PM_POFF */
}
/* <<<< 2010/1/12, added by panasonic */

/* 2010/3/1, added by panasonic >>>>  */
static void mpc831x_rdb_halt(void)
{
#ifdef CONFIG_P2PF_PM_POFF
    p2pm_power_off();
#endif  /* CONFIG_P2PF_PM_POFF */
}
/* <<<< 2010/3/1, added by panasonic */

/* 2009/12/7, added by Panasonic >>>> */
#ifdef CONFIG_SPI_MPC83xx

static void mpc831x_spi_activate_cs(u8 cs, u8 polarity)
{
    gpio_set_value(cs, polarity?1:0);
}


static void mpc831x_spi_deactivate_cs(u8 cs, u8 polarity)
{
    gpio_set_value(cs, polarity?0:1);
}


static int __init mpc831x_spi_init(void)
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
                          mpc831x_spi_activate_cs,
                          mpc831x_spi_deactivate_cs);

 exit:
    if(NULL!=board_infos)
        kfree(board_infos);

    return retval;
}

machine_device_initcall(mpc831x_rdb, mpc831x_spi_init);

#endif  /* CONFIG_SPI_MPC83xx */
/* <<<<< 2009/12/7, added by Panasonic */


/*
 * Setup the architecture
 */
static void __init mpc831x_rdb_setup_arch(void)
{
#ifdef CONFIG_PCI
	struct device_node *np;
#endif

	if (ppc_md.progress)
		ppc_md.progress("mpc831x_rdb_setup_arch()", 0);

#ifdef CONFIG_PCI
	for_each_compatible_node(np, "pci", "fsl,mpc8349-pci")
		mpc83xx_add_bridge(np);

    /* 2010/7/26, added by Panasonic (SAV) */
#ifdef CONFIG_PPC_MPC83XX_PCIE
	for_each_compatible_node(np, "pci", "fsl,mpc83xx-pcie")
		mpc83xx_add_bridge(np);
#endif  /* CONFIG_PPC_MPC83XX_PCIE */

#endif  /* CONFIG_PCI */
	mpc831x_usb_cfg();
}

void __init mpc831x_rdb_init_IRQ(void)
{
	struct device_node *np;

	np = of_find_node_by_type(NULL, "ipic");
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
static void __init mpc831x_rdb_heartbeat_init(void)
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

static void __init mpc831x_rdb_init(void)
{
	if (ppc_md.progress)
		ppc_md.progress("mpc831x_rdb_init()", 0);

#if defined(CONFIG_HEART_BEAT)
    /* initialize heartbeat */
    mpc831x_rdb_heartbeat_init();
#endif /* CONFIG_HEART_BEAT */
}

/*--------------------*/

/*
 * Called very early, MMU is off, device-tree isn't unflattened
 */
static int __init mpc831x_rdb_probe(void)
{
	unsigned long root = of_get_flat_dt_root();

	return of_flat_dt_is_compatible(root, "MPC8313ERDB") ||
	       of_flat_dt_is_compatible(root, "fsl,mpc8315erdb");
}

static struct of_device_id __initdata of_bus_ids[] = {
	{ .compatible = "simple-bus" },
	{},
};

static int __init declare_of_platform_devices(void)
{
	of_platform_bus_probe(NULL, of_bus_ids, NULL);
	return 0;
}
machine_device_initcall(mpc831x_rdb, declare_of_platform_devices);

define_machine(mpc831x_rdb) {
	.name			= "MPC831x RDB",
	.probe			= mpc831x_rdb_probe,
	.setup_arch		= mpc831x_rdb_setup_arch,
	/* Panasonic Original */
	.init			= mpc831x_rdb_init,
	/*--------------------*/
	.init_IRQ		= mpc831x_rdb_init_IRQ,
	.get_irq		= ipic_get_irq,
    /* 2010/1/12, added by panasonic >>>>  */
        .power_off      = mpc831x_rdb_power_off,
    /* <<<< 2010/1/12, added by panasonic */
    /* 2010/3/1, added by panasonic >>>>  */
        .halt      = mpc831x_rdb_halt,
    /* <<<< 2010/3/1, added by panasonic */
	.restart		= mpc83xx_restart,
	.time_init		= mpc83xx_time_init,
	.calibrate_decr		= generic_calibrate_decr,
#if ! defined(CONFIG_DISABLE_INIT_MESSAGE)
	.progress		= udbg_progress,
#endif /* ! CONFIG_DISABLE_INIT_MESSAGE */
};
