/*
 * Description: Handle mapping of the flash on AJ-HPM200 board
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


/* AJ-HPM200 flash layout 
 *-Flash1-----
 * 0 : 0xF000 0000 - 0xF003 FFFF : U-Boot 1st   (256KB)
 * 1 : 0xF004 0000 - 0xF00B FFFF : U-Boot 2nd-1 (512KB)
 * 2 : 0xF00C 0000 - 0xF013 FFFF : U-Boot 2nd-2 (512KB)
 * 3 : 0xF014 0000 - 0xF017 FFFF : U-Boot Env   (256KB)
 * 4 : 0xF018 0000 - 0xF01B FFFF : OF tree 1    (256KB)
 * 5 : 0xF01C 0000 - 0xF01F FFFF : OF tree 2    (256KB)
 * 6 : 0xF020 0000 - 0xF05F FFFF : Kernel       (4MB)
 * 7 : 0xF060 0000 - 0xF3AF FFFF : RootFS       (55.5MB)
 * 8 : 0xF3B0 0000 - 0xF3EF FFFF : Backup#1     (4MB)
 * 9 : 0xF3F0 0000 - 0xF3FF FFFF : Backup#2     (1MB)
 *-Flash2-----
 *10 : 0xF400 0000 - 0xF40F FFFF : VUP Kernel   (1MB)
 *11 : 0xF410 0000 - 0xF44F FFFF : VUP RAMDISK  (4MB)
 *12 : 0xF450 0000 - 0xF4CF FFFF : Backup#3     (8MB)
 *13 : 0xF4D0 0000 - 0xF4E5 FFFF : CODECFPGA cfg1 (1.4MB)
 *14 : 0xF4E6 0000 - 0xF4FB FFFF : CODECFPGA cfg2 (1.4MB)
 *15 : 0xF4FC 0000 - 0xF539 FFFF : AVIO cfg1    (3.8MB)
 *16 : 0xF53A 0000 - 0xF577 FFFF : AVIO cfg2    (3.8MB)
 *17 : 0xF578 0000 - 0xF577 FFFF : SDI cfg1     (1MB)
 *18 : 0xF588 0000 - 0xF587 FFFF : (SDI cfg2)   (1MB)
 *19 : 0xF598 0000 - 0xF5CF FFFF : R/W filesystem area(JFFS2)  (3.5MB)
 *20 : 0xF5D0 0000 - 0xF5FF FFFF : (reserved)   (3.0MB)
 */

/* Flash1 - 32bit bus width , 64MB*/
#define WINDOW_ADDR1 0xF0000000  /*NOR_Flash=0xf0000000- */
#define WINDOW_SIZE1 0x04000000  /*NOR_Flash=64MB*/

/* Flash1 - 16bit bus width , 32MB*/
#define WINDOW_ADDR2 0xF4000000  /*NOR_Flash=0xf4000000- */
#define WINDOW_SIZE2 0x02000000  /*NOR_Flash=32MB*/

/* partition_info gives details on the logical partitions that the split the
 * single flash device into. If the size if zero we use up to the end of the
 * device. */

/* flash1 */
static struct mtd_partition partition_info1[]={
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
		.name		= "kernel",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x400000,
	},
	{
		.name		= "rootfs+App",  
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x3500000,
	},
	{
		.name		= "backup1",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x400000,
	},
	{
		.name		= "backup2",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x100000,
	},
};

/* flash2 */
static struct mtd_partition partition_info2[]={
	{
		.name		= "vup kernel",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x100000,
	},
	{
		.name		= "vup rootfs",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x400000,
	},
	{
		.name		= "backup3",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x800000,
	},
	{
		.name		= "codecfpga1",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x160000,
	},
	{
		.name		= "codecfpga2",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x160000,
	},
	{
		.name		= "avio1",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x3E0000,
	},
	{
		.name		= "avio2",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x3E0000,
	},
	{
		.name		= "sdi1",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x100000,
	},
	{
		.name		= "sdi2",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x100000,
	},
	{
		.name		= "jffs",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x380000,
	},
	{
		.name		= "reserved",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 0x400000,
	}


};

#define PARTITION_NUM1 (sizeof(partition_info1)/sizeof(struct mtd_partition))
#define PARTITION_NUM2 (sizeof(partition_info2)/sizeof(struct mtd_partition))

static struct mtd_info *mymtd1;
static struct mtd_info *mymtd2;


struct map_info aj_hpm200_map1 = {
	.name		= "AJ-HPM200 Flash 1 Map Info",
	.size		= WINDOW_SIZE1,
	.phys		= WINDOW_ADDR1,
	.bankwidth	= 4,
};

struct map_info aj_hpm200_map2 = {
	.name		= "AJ-HPM200 Flash 2 Map Info",
	.size		= WINDOW_SIZE2,
	.phys		= WINDOW_ADDR2,
	.bankwidth	= 2,
};

int __init init_aj_hpm200_mtd(void)
{

  /* flash1 */
	printk(KERN_NOTICE"Panasonic AJ-HPM200 flash1 device: %x at %x Partition number %d\n",
			WINDOW_SIZE1, WINDOW_ADDR1, PARTITION_NUM1);
	aj_hpm200_map1.virt = ioremap(WINDOW_ADDR1, WINDOW_SIZE1);
	if (!aj_hpm200_map1.virt) {
		printk("Failed to Flash1 ioremap\n");
		return -EIO;
	}

	simple_map_init(&aj_hpm200_map1);
	
	mymtd1 = do_map_probe("cfi_probe", &aj_hpm200_map1);
	if (mymtd1) {
		mymtd1->owner = THIS_MODULE;
                add_mtd_partitions(mymtd1, partition_info1, PARTITION_NUM1);
		printk(KERN_NOTICE "Panasonic AJ-HPM200 flash1 device (%s) initialized\n",mymtd1->name);
	} else {
	  iounmap((void *)aj_hpm200_map1.virt);
	  return -ENXIO;
	}

	/* flash2 */
	printk(KERN_NOTICE"Panasonic AJ-HPM200 flash2 device: %x at %x Partition number %d\n",
			WINDOW_SIZE2, WINDOW_ADDR2, PARTITION_NUM2);

	aj_hpm200_map2.virt = ioremap(WINDOW_ADDR2, WINDOW_SIZE2);
	if (!aj_hpm200_map2.virt) {
		printk("Failed to Flash2 ioremap\n");
		return -EIO;
	}
	simple_map_init(&aj_hpm200_map2);

	mymtd2 = do_map_probe("cfi_probe", &aj_hpm200_map2);
	if (mymtd2) {
		mymtd2->owner = THIS_MODULE;
                add_mtd_partitions(mymtd2, partition_info2, PARTITION_NUM2);
		printk(KERN_NOTICE "Panasonic AJ-HPM200 flash2 device (%s) initialized\n",mymtd2->name);
	} else {
	  iounmap((void *)aj_hpm200_map2.virt);
	  return -ENXIO;
	}

	return 0;
}

static void __exit cleanup_aj_hpm200_mtd(void)
{
  /* flash1 */
	if (mymtd1) {
		del_mtd_device(mymtd1);
		map_destroy(mymtd1);
	}
	if (aj_hpm200_map1.virt) {
		iounmap((void *)aj_hpm200_map1.virt);
		aj_hpm200_map1.virt = 0;
	}

  /* flash2 */
	if (mymtd2) {
		del_mtd_device(mymtd2);
		map_destroy(mymtd2);
	}
	if (aj_hpm200_map2.virt) {
		iounmap((void *)aj_hpm200_map2.virt);
		aj_hpm200_map2.virt = 0;
	}

}
module_init(init_aj_hpm200_mtd);
module_exit(cleanup_aj_hpm200_mtd);

MODULE_AUTHOR("Li Yawei");
MODULE_DESCRIPTION("MTD map driver for Panasonic AJ-HPM200 board");
MODULE_LICENSE("GPL");
