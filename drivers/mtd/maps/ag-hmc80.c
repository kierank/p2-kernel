/*
 * Description: Handle mapping of the flash on AG-HMC80 board
 *
 * Copyright (C) 2006 Freescale Semiconductor, Inc.
 * Author: Li yawei(r66110@freescale.com)
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <asm/io.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>


/* AG-HMC80 flash layout 
0xf000_0000                   mtd0 :  HRCW & 1st bootloader   :    0.125MB(128KB)
0xf002_0000                   mtd1 :  2nd bootloader1         :    0.250MB(256KB)
0xf006_0000                   mtd2 :  2nd bootloader2         :    0.250MB(256KB)
0xf00a_0000                   mtd3 :  BootLoaderEnv           :    0.125MB(128KB)
0xf00c_0000                   mtd4 :  OF tree 1(dtb)          :    0.125MB(128KB)
0xf00e_0000                   mtd5 :  OF tree 2(dtb)          :    0.125MB(128KB)
0xf010_0000                   mtd6 :  backup(app save)        :    4.0MB
0xf050_0000                   mtd7 :  vup kernel              :    1.5MB
0xf068_0000                   mtd8 :  vup rootfs              :    3.0MB
0xf098_0000                   mtd9 :  kernel                  :    4.0MB
0xf0d8_0000                   mtd10:  rootfs & application    :   18.5MB
 */

#define WINDOW_ADDR 0xF0000000  /*NOR_Flash=0xf0000000- */
#define WINDOW_SIZE 0x02000000  /*NOR_Flash=32MB*/

/* partition_info gives details on the logical partitions that the split the
 * single flash device into. If the size if zero we use up to the end of the
 * device. */
static struct mtd_partition partition_info[]={
	{
		.name		= "HRCW+1stBoot",
		.offset 	= 0,
		.size		= 0x20000,
	},
	{
		.name		= "2ndBoot1",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x40000,
	},
	{
		.name		= "2ndBoot2",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x40000,
	},
	{
		.name		= "uboot_env",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x20000,
	},
	{
		.name		= "dtb1",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x20000,
	},
	{
		.name		= "dtb2",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x20000,
	},
	{
		.name		= "backup",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x400000,
	},
	{
		.name		= "vup kernel",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x180000,
	},
	{
		.name		= "vup rootfs",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x300000,
	},
	{
		.name		= "kernel",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x400000,
	},
	{
		.name		= "rootfs+App",  
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x1280000,
    },
};
#define PARTITION_NUM (sizeof(partition_info)/sizeof(struct mtd_partition))

static struct mtd_info *mymtd;


struct map_info ag_hmc80_map = {
	.name		= "AG-HMC80 Flash Map Info",
	.size		= WINDOW_SIZE,
	.phys		= WINDOW_ADDR,
	.bankwidth	= 2,
};

int __init init_ag_hmc80_mtdmap(void)
{
	printk(KERN_NOTICE"Panasonic AG-HMC80 flash device: %x at %x Partition number %d\n",
			WINDOW_SIZE, WINDOW_ADDR, PARTITION_NUM);
	ag_hmc80_map.virt = ioremap(WINDOW_ADDR, WINDOW_SIZE);

	if (!ag_hmc80_map.virt) {
		printk("Failed to ioremap\n");
		return -EIO;
	}
	simple_map_init(&ag_hmc80_map);
	
	mymtd = do_map_probe("cfi_probe", &ag_hmc80_map);
	if (mymtd) {
		mymtd->owner = THIS_MODULE;
                add_mtd_partitions(mymtd, partition_info, PARTITION_NUM);
		printk(KERN_NOTICE "AG-HMC80 flash device (%s) initialized\n",mymtd->name);
		return 0;
	}

	iounmap((void *)ag_hmc80_map.virt);
	return -ENXIO;
}

static void __exit cleanup_ag_hmc80_mtdmap(void)
{
	if (mymtd) {
		del_mtd_device(mymtd);
		map_destroy(mymtd);
	}
	if (ag_hmc80_map.virt) {
		iounmap((void *)ag_hmc80_map.virt);
		ag_hmc80_map.virt = 0;
	}
}
module_init(init_ag_hmc80_mtdmap);
module_exit(cleanup_ag_hmc80_mtdmap);

MODULE_AUTHOR("Li Yawei");
MODULE_DESCRIPTION("MTD map driver for Panasonic AG-HMC80 board");
MODULE_LICENSE("GPL");
