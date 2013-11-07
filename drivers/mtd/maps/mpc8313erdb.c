/*
 * Description: Handle mapping of the flash on MPC8313rdb board
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


/* MPC8313RDB flash layout 
 * 0 : 0xFE00 0000 - 0xFE0F FFFF : U-Boot (1M)
 * 1 : 0xFE10 0000 - 0xFE2F FFFF : Kernel (2M)
 * 2 : 0xFE30 0000 - 0xFE6F FFFF : Ramdisk/JFFS2/YAFFS file system (4MB)
 * 3 : 0xFE7F 0000 - 0xFE7F FFFF : OF tree (dtb) (1M), Vitesse 7385 image at 0xFE7F E000 - 0xFE7F FFFF
 */

/*SAV comment out
#define WINDOW_ADDR 0xFE000000
#define WINDOW_SIZE 0x00800000
  SAV*/

#define WINDOW_ADDR 0xF0000000  /*NOR_Flash=0xf0000000- */
#define WINDOW_SIZE 0x04000000  /*NOR_Flash=64MB*/

/* partition_info gives details on the logical partitions that the split the
 * single flash device into. If the size if zero we use up to the end of the
 * device. */
static struct mtd_partition partition_info[]={ /*SAV*/
	{
		.name		= "HRCW+1stBoot",
		.offset 	= 0,
		.size		= 0x20000,
//		.mask_flags     = MTD_WRITEABLE
	},
	{
		.name		= "2ndBoot1",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x40000,
//		.mask_flags     = MTD_WRITEABLE
	},
	{
		.name		= "2ndBoot2",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x40000,
//		.mask_flags     = MTD_WRITEABLE
	},
	{
		.name		= "uboot_env",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x20000,
//		.mask_flags     = MTD_WRITEABLE
	},
	{
		.name		= "dtb1",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x20000,
//		.mask_flags     = MTD_WRITEABLE
	},
	{
		.name		= "dtb2",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x20000,
//		.mask_flags     = MTD_WRITEABLE
	},
	{
		.name		= "backup",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x400000,
//		.mask_flags     = MTD_WRITEABLE
	},
	{
		.name		= "vup kernel",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x100000,
//		.mask_flags     = MTD_WRITEABLE
	},
	{
		.name		= "vup rootfs",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x300000,
//		.mask_flags     = MTD_WRITEABLE
	},
	{
		.name		= "kernel",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x400000,
//		.mask_flags     = MTD_WRITEABLE
	},
	{
		.name		= "rootfs+App",  
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x1300000,
    },
};
#define PARTITION_NUM (sizeof(partition_info)/sizeof(struct mtd_partition))

static struct mtd_info *mymtd;


struct map_info mpc8313rdb_map = {
	.name		= "MPC8313RDB Flash Map Info",
	.size		= WINDOW_SIZE,
	.phys		= WINDOW_ADDR,
	.bankwidth	= 2,
};

int __init init_mpc8313rdb(void)
{
	printk(KERN_NOTICE"MPC8313_SAV_BRB flash device: %x at %x Partition number %d\n",
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
		printk(KERN_NOTICE "MPC8313RDB flash device (%s) initialized\n",mymtd->name);
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
MODULE_DESCRIPTION("MTD map driver for Freescale MPC8313RDB board");
MODULE_LICENSE("GPL");
