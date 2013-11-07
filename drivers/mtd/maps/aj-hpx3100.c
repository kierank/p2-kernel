/*
 * Description: Handle mapping of the flash on AJ-HPX3100 board
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


/* AJ-HPX3100 flash layout : 64MB
0xf000_0000                   mtd0 :  HRCW & 1st bootloader   :    0.250MB(256KB)
0xf004_0000                   mtd1 :  2nd bootloader1         :    0.500MB(512KB)
0xf00c_0000                   mtd2 :  2nd bootloader2         :    0.500MB(512KB)
0xf014_0000                   mtd3 :  BootLoaderEnv           :    0.250MB(256KB)
0xf018_0000                   mtd4 :  OF tree 1(dtb)          :    0.250MB(256KB)
0xf01c_0000                   mtd5 :  OF tree 2(dtb)          :    0.250MB(256KB)
0xf020_0000                   mtd6 :  backup(app save)        :    8.0MB
0xf0A0_0000                   mtd7 :  backup2(reserved)       :    8.0MB
0xf120_0000                   mtd8 :  PQCNT_FPGA1             :    2.0MB
0xf140_0000                   mtd9 :  PQCNT_FPGA1             :    2.0MB
0xf160_0000                   mtd10:  vup kernel              :    2.0MB
0xf180_0000                   mtd11:  vup rootfs              :    4.0MB
0xf1c0_0000                   mtd12:  kernel(Normal)          :    4.0MB
0xf200_0000                   mtd13:  kernel(WLAN)            :    8.0MB
0xf280_0000    0xf3ff_ffff    mtd14:  rootfs & application    :   24.0MB
 */

#define WINDOW_ADDR 0xF0000000  /*NOR_Flash=0xf0000000- */
#define WINDOW_SIZE 0x04000000  /*NOR_Flash=64MB*/

/* partition_info gives details on the logical partitions that the split the
 * single flash device into. If the size if zero we use up to the end of the
 * device. */
static struct mtd_partition partition_info[]={
	{
		.name		= "HRCW+1stBoot",
		.offset 	= 0,
		.size		= 0x40000,
	},
	{
		.name		= "2ndBoot1",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x80000,
	},
	{
		.name		= "2ndBoot2",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x80000,
	},
	{
		.name		= "uboot_env",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x40000,
	},
	{
		.name		= "dtb1",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x40000,
	},
	{
		.name		= "dtb2",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x40000,
	},
	{
		.name		= "backup",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x800000,
	},
	{
		.name		= "backup2",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x800000,
	},
	{
		.name		= "fpga1",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x200000,
	},
	{
		.name		= "fpga2",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x200000,
	},
	{
		.name		= "vup kernel",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x200000,
	},
	{
		.name		= "vup rootfs",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x400000,
	},
	{
		.name		= "kernel",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x400000,
	},
	{
		.name		= "kernel2",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x800000,
	},
	{
		.name		= "rootfs+App",  
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x1800000,
    },
};
#define PARTITION_NUM (sizeof(partition_info)/sizeof(struct mtd_partition))

static struct mtd_info *mymtd;


struct map_info aj_hpx3100_map = {
	.name		= "AJ-HPX3100 Flash Map Info",
	.size		= WINDOW_SIZE,
	.phys		= WINDOW_ADDR,
	.bankwidth	= 2,
};

int __init init_aj_hpx3100_mtdmap(void)
{
	printk(KERN_NOTICE"Panasonic AJ-HPX3100 flash device: %x at %x Partition number %d\n",
			WINDOW_SIZE, WINDOW_ADDR, PARTITION_NUM);
	aj_hpx3100_map.virt = ioremap(WINDOW_ADDR, WINDOW_SIZE);

	if (!aj_hpx3100_map.virt) {
		printk("Failed to ioremap\n");
		return -EIO;
	}
	simple_map_init(&aj_hpx3100_map);
	
	mymtd = do_map_probe("cfi_probe", &aj_hpx3100_map);
	if (mymtd) {
		mymtd->owner = THIS_MODULE;
                add_mtd_partitions(mymtd, partition_info, PARTITION_NUM);
		printk(KERN_NOTICE "AJ-HPX3100 flash device (%s) initialized\n",mymtd->name);
		return 0;
	}

	iounmap((void *)aj_hpx3100_map.virt);
	return -ENXIO;
}

static void __exit cleanup_aj_hpx3100_mtdmap(void)
{
	if (mymtd) {
		del_mtd_device(mymtd);
		map_destroy(mymtd);
	}
	if (aj_hpx3100_map.virt) {
		iounmap((void *)aj_hpx3100_map.virt);
		aj_hpx3100_map.virt = 0;
	}
}
module_init(init_aj_hpx3100_mtdmap);
module_exit(cleanup_aj_hpx3100_mtdmap);

MODULE_AUTHOR("Li Yawei");
MODULE_DESCRIPTION("MTD map driver for Panasonic AJ-HPX3100 board");
MODULE_LICENSE("GPL");
