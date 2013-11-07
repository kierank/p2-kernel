/* 
 * linux/bimage_common.h  --- define bootloder image format
 * $Id: bimage_common.h 1326 2009-01-28 23:19:39Z isao $
 */

#ifndef __LINUX_BIMAGE_COMMON_H__
#define __LINUX_BIMAGE_COMMON_H__

/**
 *   Offset posision of bimage header from the top of bimage file
 */
#define BIMAGE_HDR_POS      (512L)
#define BIMAGE_HDR_UBOOT_POS    (0x80)
#define BIMAGE_HDR_PPC_POS    (0x00)

/**
 * For magic_no member in struct bimage_header
 */
#define BIMAGE_MAGIC_SIZE       (7)
#define BIMAGE_MAGIC_NO         "P2Store"

/**
 * U-boot magic number
 */
#define BIMAGE_MAGIC_UBOOT_SIZE (4)
#define BIMAGE_MAGIC_UBOOT      "\x27\x05\x19\x56"
#define BIMAGE_MAGIC_UBOOT_POS  (0x40)

/**
 * PPC boot image magic number
 */
#define BIMAGE_MAGIC_PPC_SIZE    BIMAGE_MAGIC_UBOOT_SIZE
#define BIMAGE_MAGIC_PPC         BIMAGE_MAGIC_UBOOT
#define BIMAGE_MAGIC_PPC_POS     0x00
#define BIMAGE_PPC_HEADER_SIZE   64
#define BIMAGE_PPC_VERINFO_OFF   32
#define BIMAGE_PPC_VERINFO_LEN   12
#define BIMAGE_PPC_DATASIZE_OFF  12

/**
 * Value of version of this format
 *   ( for fmt_ver  member in struct bimage_header )
 */
#define BIMAGE_FMT_MASK     0x7F
#define BIMAGE_BIT_ENDIAN   0x80
/* #define BIMAGE_FMT_VER_VALUE    (0x00) */
#define BIMAGE_FMT_VERLEVEL_1   0x00 /* Ver.1 */
#define BIMAGE_FMT_VERLEVEL_2   0x01 /* Ver.1 */

/** For type member in struct bimage_dhdr_entry */
#define BIMAGE_DHDR_TYPE_UNDEFINED              0
#define BIMAGE_DHDR_TYPE_KERNEL_COMPRESSED      1
#define BIMAGE_DHDR_TYPE_KERNEL_UNCOMPRESSED    2
#define BIMAGE_DHDR_TYPE_INITRD                 3
#define BIMAGE_DHDR_TYPE_FPGA                   4
#define BIMAGE_DHDR_TYPE_BOOTLOADER             5


#endif /* __LINUX_BIMAGE_COMMON_H__ */
