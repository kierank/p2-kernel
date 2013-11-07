/*
 * Description: Handle mapping of the flash on Panasonic AJ-PCD2 board
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

/* AJ-PCD2 flash layout 
 * 0 : 0xF000 0000 - 0xF001 FFFF : U-Boot 1st   (128KB)
 * 1 : 0xF002 0000 - 0xF005 FFFF : U-Boot 2nd-1 (256KB)
 * 2 : 0xF006 0000 - 0xF009 FFFF : U-Boot 2nd-2 (256KB)
 * 3 : 0xF00A 0000 - 0xF00B FFFF : U-Boot Env   (128KB)
 * 4 : 0xF00C 0000 - 0xF00D FFFF : OF tree 1    (128KB)
 * 5 : 0xF00E 0000 - 0xF00F FFFF : OF tree 2    (128KB)
 * 6 : 0xF010 0000 - 0xF07F FFFF : Backup       (7MB)
 * 7 : 0xF080 0000 - 0xF0BF FFFF : VUP Kernel   (4MB)
 * 8 : 0xF0C0 0000 - 0xF13F FFFF : VUP RootFS   (8MB)
 * 9 : 0xF140 0000 - 0xF17F FFFF : Kernel       (4MB)
 * 10: 0xF180 0000 - 0xF1FF FFFF : RootFS+App   (8MB)
 */


#define WINDOW_ADDR 0xF0000000  /*NOR_Flash=0xf0000000- */
#define WINDOW_SIZE 0x02000000  /*NOR_Flash=32MB*/

/* partition_info gives details on the logical partitions that the split the
 * single flash device into. If the size if zero we use up to the end of the
 * device. */
static struct mtd_partition partition_info[]={ /*SAV*/
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
		.size		= 0x700000,
	},
	{
		.name		= "vup kernel",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x400000,
	},
	{
		.name		= "vup rootfs",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x800000,
	},
	{
		.name		= "kernel",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x400000,
	},
	{
		.name		= "rootfs+App",  
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x800000,
	},
};
#define PARTITION_NUM (sizeof(partition_info)/sizeof(struct mtd_partition))

static struct mtd_info *mymtd;


struct map_info mpc8313rdb_map = {
	.name		= "Panasonic AJ-PCD2 Flash Map Info",
	.size		= WINDOW_SIZE,
	.phys		= WINDOW_ADDR,
	.bankwidth	= 2,
};

int __init init_mpc8313rdb(void)
{
	printk(KERN_NOTICE"Panasonic AJ-PCD2 flash device: %x at %x Partition number %d\n",
			WINDOW_SIZE, WINDOW_ADDR, PARTITION_NUM); /*SAV*/
	mpc8313rdb_map.virt = ioremap(WINDOW_ADDR, WINDOW_SIZE);

	if (!mpc8313rdb_map.virt) {
		printk("Failed to ioremap\n");
		return -EIO;
	}
	simple_map_init(&mpc8313rdb_map);
	
	mymtd = do_map_probe("cfi_probe", &mpc8313rdb_map);
	if (mymtd) {
		mymtd->owner = THIS_MODULE;
                add_mtd_partitions(mymtd, partition_info, PARTITION_NUM);
		printk(KERN_NOTICE "Panasonic AJ-PCD2 flash device (%s) initialized\n",mymtd->name);
		return 0;
	}

	iounmap((void *)mpc8313rdb_map.virt);
	return -ENXIO;
}

static void __exit cleanup_mpc8313rdb(void)
{
	if (mymtd) {
		del_mtd_device(mymtd);
		map_destroy(mymtd);
	}
	if (mpc8313rdb_map.virt) {
		iounmap((void *)mpc8313rdb_map.virt);
		mpc8313rdb_map.virt = 0;
	}
}
module_init(init_mpc8313rdb);
module_exit(cleanup_mpc8313rdb);

MODULE_AUTHOR("Li Yawei");
MODULE_DESCRIPTION("MTD map driver for Panasonic AJ-PCD2 board");
MODULE_LICENSE("GPL");
