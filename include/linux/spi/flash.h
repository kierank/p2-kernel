/* $Id: flash.h 11136 2010-12-14 05:30:50Z Noguchi Isao $ */
#ifndef LINUX_SPI_FLASH_H
#define LINUX_SPI_FLASH_H

/* 2010/12/13, added by Panasonic (SAV) ---> */
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
/* <--- 2010/12/13, added by Panasonic (SAV) */


struct mtd_partition;

/**
 * struct flash_platform_data: board-specific flash data
 * @name: optional flash device name (eg, as used with mtdparts=)
 * @parts: optional array of mtd_partitions for static partitioning
 * @nr_parts: number of mtd_partitions for static partitoning
 * @type: optional flash device type (e.g. m25p80 vs m25p64), for use
 *	with chips that can't be queried for JEDEC or other IDs
 *
 * Board init code (in arch/.../mach-xxx/board-yyy.c files) can
 * provide information about SPI flash parts (such as DataFlash) to
 * help set up the device and its appropriate default partitioning.
 *
 * Note that for DataFlash, sizes for pages, blocks, and sectors are
 * rarely powers of two; and partitions should be sector-aligned.
 */
struct flash_platform_data {
	char		*name;
	struct mtd_partition *parts;
	unsigned int	nr_parts;

	char		*type;

	/* we'll likely add more ... use JEDEC IDs, etc */

    void *private;          /* 2010/12/13, added by Panasonic (SAV) */

};

#endif
