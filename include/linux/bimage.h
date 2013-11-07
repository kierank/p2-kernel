/* 
 * linux/bimage.h  --- define bootloder image format
 * $Id: bimage.h 1285 2009-01-26 08:24:33Z isao $
 */

#ifndef __LINUX_BIMAGE_H__
#define __LINUX_BIMAGE_H__

#include <linux/bimage_common.h>

/**
 * Structure of bimage header
 */
struct bimage_header {
    unsigned char magic_no[BIMAGE_MAGIC_SIZE];
    unsigned char fmt_ver;
    unsigned long image_sz;     /* Byte size of this file */
    unsigned long verinfo_off;  /* Offset of version information characters (if verinfo_sz is zero then non-mean) */
    unsigned long verinfo_sz;   /* Byte size of version information characters (if zero then non-mean) */
    unsigned long dhdr_off;     /* Offset of data header table */
    unsigned long dhdr_sz;      /* Byte size of data header table */
};

/**
 * Structure of the entry in data header table (Ver.2 or later)
 */
struct bimage_dhdr_entry {
    unsigned long type;         /* Type of data */
    unsigned long off;          /* Offset of data */
    unsigned long sz;           /* Byte size of data */
    unsigned long load;         /* Physical address to load (if zero then non-mean) */
    unsigned long start;        /* Physical address to start (if zero then non-mean) */
    unsigned long reserve[3];   /* Reserved for the future */
};

/**
 * Structure of the entry in data header table (Ver.1)
 */
struct bimage_dhdr_entry_v1 {
    unsigned long type;         /* Type of data */
    unsigned long off;          /* Offset of data */
    unsigned long sz;           /* Byte size of data */
    unsigned long reserve[1];   /* Reserved for the future */
};


struct bimage_header_ppc {
	unsigned long	ih_magic;	/* Image Header Magic Number	*/
	unsigned long	ih_hcrc;	/* Image Header CRC Checksum	*/
	unsigned long	ih_time;	/* Image Creation Timestamp	*/
	unsigned long   ih_size;	/* Image Data Size		*/
	unsigned long	ih_load;	/* Data	 Load  Address		*/
	unsigned long	ih_ep;		/* Entry Point Address		*/
	unsigned long	ih_dcrc;	/* Image Data CRC Checksum	*/
	unsigned char	ih_os;		/* Operating System		*/
	unsigned char   ih_arch;	/* CPU architecture		*/
	unsigned char   ih_type;	/* Image Type			*/
	unsigned char   ih_comp;	/* Compression Type		*/
	unsigned char   ih_name[32];	/* Image Name		*/
};




/*
 * endian
 */

#if defined(__BIG_ENDIAN__)

#ifdef BIMAGE_CPU_ENDIAN
#undef BIMAGE_CPU_ENDIAN
#endif  /* BIMAGE_CPU_ENDIAN */
#define BIMAGE_CPU_ENDIAN 1     /* big-endian */

#elif defined(__LITTLE_ENDIAN__)

#ifdef BIMAGE_CPU_ENDIAN
#undef BIMAGE_CPU_ENDIAN
#endif  /* BIMAGE_CPU_ENDIAN */
#define BIMAGE_CPU_ENDIAN 0     /* little-endian */

#elif ! defined(BIMAGE_CPU_ENDIAN)

#define BIMAGE_CPU_ENDIAN 0     /* little-endian */

#endif

#define __bimage_swab32(x) ((((x)>>24) & 0xff) |          \
                            (((x)>>8) & 0xff00) |         \
                            (((x)<<8) & 0xff0000) |       \
                            (((x)<<24) & 0xff000000))
#if BIMAGE_CPU_ENDIAN != 0      /* CPU is big-endian */
#define _bimage_swab32(x,e) ((e)?(x):__bimage_swab32(x))
#else  /* CPU is little-endian */
#define _bimage_swab32(x,e) ((!e)?(x):__bimage_swab32(x))
#endif


/**
 *  @brief  check magic number for PPC boot image
 *  @param   start_image  start address casted by long-word
 *  @return     0:      matched magic number
 *              non-0:  unmached magic number
 */
static inline int bimage_check_magic_ppc(const unsigned long start_image)
{
    int n;
    volatile const unsigned char *p1,*p2;

    for(n=0, p1=(const unsigned char *)(start_image+BIMAGE_MAGIC_PPC_POS),
            p2=(const unsigned char*)BIMAGE_MAGIC_PPC;
        n<BIMAGE_MAGIC_PPC_SIZE; n++,p1++,p2++){
        if((*p1) != (*p2))
            return -1;
    }
    return 0;
}


/**
 *  @brief  check magic number for U-BOOT
 *  @param   start_image  start address casted by long-word
 *  @return     0:      matched magic number
 *              non-0:  unmached magic number
 */
static inline int bimage_check_magic_uboot(const unsigned long start_image)
{
    int n;
    volatile const unsigned char *p1,*p2;

    for(n=0, p1=(const unsigned char *)(start_image+BIMAGE_MAGIC_UBOOT_POS),
            p2=(const unsigned char*)BIMAGE_MAGIC_UBOOT;
        n<BIMAGE_MAGIC_UBOOT_SIZE; n++,p1++,p2++){
        if((*p1) != (*p2))
            return -1;
    }
    return 0;
}


/**
 * @brief   get bimage header pointer
 * @param   start_image  start address casted by long-word
 * @return  bimage header pointer
 */
static inline struct bimage_header *bimage_get_header(const unsigned long start_image)
{
    if(bimage_check_magic_uboot(start_image))
        return (struct bimage_header*)(start_image + BIMAGE_HDR_POS);
    else
        return (struct bimage_header*)(start_image + BIMAGE_HDR_UBOOT_POS);
}

/**
 * @brief   get bimage header pointer for PPC bimage
 * @param   start_image  start address casted by long-word
 * @return  bimage header pointer
 */
static inline struct bimage_header_ppc *bimage_get_header_ppc(const unsigned long start_image)
{
    return (struct bimage_header_ppc*)(start_image + BIMAGE_HDR_PPC_POS);
}

/**
 *  @brief  check magic number and return
 *  @param   start_image  start address casted by long-word
 *  @return Version number of boot_image format (>=0). 
 *          If unmached magic number, then return -1. 
 */
static inline int bimage_check_magic(const unsigned long start_image)
{
    int n;
    struct bimage_header *hdr;
    volatile const unsigned char *p1,*p2;

    hdr = bimage_get_header(start_image);
    for(n=0, p1=(const unsigned char *)hdr->magic_no, p2=(const unsigned char*)BIMAGE_MAGIC_NO;
        n<BIMAGE_MAGIC_SIZE; n++,p1++,p2++){
        if((*p1) != (*p2))
            return -1;
    }
    return (int)(hdr->fmt_ver & BIMAGE_FMT_MASK);
}

/**
 *  @brief  get endian info.
 *  @param   start_image  start address casted by long-word
 *  @return  0      : little-endian
 *           non-0  : big-endian
 */
static inline int bimage_get_endian(const unsigned long start_image)
{
    struct bimage_header *hdr = bimage_get_header(start_image);
    return (hdr->fmt_ver & BIMAGE_BIT_ENDIAN);
}

/**
 * @brief   get bimage size
 * @param   start_image  start address casted by long-word
 * @return  bimage size
 */
static inline unsigned int bimage_get_size(const unsigned long start_image)
{
    struct bimage_header *hdr = bimage_get_header(start_image);
    return (unsigned int)_bimage_swab32(hdr->image_sz, hdr->fmt_ver&BIMAGE_BIT_ENDIAN);
}

/**
 * @brief   get bimage size for PPC boot image
 * @param   start_image  start address casted by long-word
 * @return  bimage size
 */
static inline unsigned int bimage_get_size_ppc(const unsigned long start_image)
{
    struct bimage_header_ppc *hdr = bimage_get_header_ppc(start_image);
    return (unsigned int)_bimage_swab32(hdr->ih_size, 1) + sizeof(struct bimage_header_ppc);
}

/**
 * @brief   get version infomation block address
 * @param   start_image  start address casted by long-word
 * @return  version infomation block address
 */
static inline unsigned long bimage_get_verinfo_addr(const unsigned long start_image)
{
    struct bimage_header *hdr = bimage_get_header(start_image);
    return start_image + _bimage_swab32(hdr->verinfo_off, hdr->fmt_ver&BIMAGE_BIT_ENDIAN);
}

/**
 * @brief   get version infomation block address for PPC
 * @param   start_image  start address casted by long-word
 * @return  version infomation block address
 */
static inline unsigned long bimage_get_verinfo_addr_ppc(const unsigned long start_image)
{
    return start_image + BIMAGE_PPC_VERINFO_OFF;
}

/**
 * @brief   get version infomation block size
 * @param   start_image  start address casted by long-word
 * @return  version infomation block size
 */
static inline unsigned int bimage_get_verinfo_size(const unsigned long start_image)
{
    struct bimage_header *hdr = bimage_get_header(start_image);
    return (unsigned int)_bimage_swab32(hdr->verinfo_sz, hdr->fmt_ver&BIMAGE_BIT_ENDIAN);
}

/**
 * @brief   get version infomation block size for PPC
 * @param   start_image  start address casted by long-word
 * @return  version infomation block size
 */
static inline unsigned int bimage_get_verinfo_size_ppc(const unsigned long start_image)
{
    return BIMAGE_PPC_VERINFO_LEN;
}

/**
 * @brief   get bimage data header table pointer (Ver.2 or later)
 * @param   start_image  start address casted by long-word
 * @return  bimage data header table pointer
 */
static inline struct bimage_dhdr_entry *bimage_get_data_header(const unsigned long start_image)
{
    struct bimage_header *hdr = bimage_get_header(start_image);
    return (struct bimage_dhdr_entry *)(start_image +
                                        _bimage_swab32(hdr->dhdr_off, hdr->fmt_ver&BIMAGE_BIT_ENDIAN));
}

/**
 * @brief   get bimage data header table pointer (Ver.1)
 * @param   start_image  start address casted by long-word
 * @return  bimage data header table pointer
 */
static inline struct bimage_dhdr_entry_v1 
*bimage_get_data_header_v1(const unsigned long start_image)
{
    struct bimage_header *hdr = bimage_get_header(start_image);
    return (struct bimage_dhdr_entry_v1 *)(start_image +
                                           _bimage_swab32(hdr->dhdr_off, hdr->fmt_ver&BIMAGE_BIT_ENDIAN));
}

/**
 * @brief   get bimage data header table size
 * @param   start_image  start address casted by long-word
 * @return  bimage data header table size
 */
static inline unsigned int bimage_get_data_header_size(const unsigned long start_image)
{
    struct bimage_header *hdr = bimage_get_header(start_image);
    return (unsigned int)_bimage_swab32(hdr->dhdr_sz, hdr->fmt_ver&BIMAGE_BIT_ENDIAN);
}

/**
 * @brief   get max  index number of bimage data header table
 * @param   start_image  start address casted by long-word
 * @return  max index number of bimage header table
 */
static inline int bimage_get_nr_data_header_entry(const unsigned long start_image)
{
    struct bimage_header *hdr = bimage_get_header(start_image);
    if((hdr->fmt_ver&BIMAGE_FMT_MASK)>=BIMAGE_FMT_VERLEVEL_2) /* Ver.2 or later */
        return _bimage_swab32(hdr->dhdr_sz, hdr->fmt_ver&BIMAGE_BIT_ENDIAN)
            / sizeof(struct bimage_dhdr_entry);
    else                        /* Ver.1 */
        return _bimage_swab32(hdr->dhdr_sz, hdr->fmt_ver&BIMAGE_BIT_ENDIAN)
            / sizeof(struct bimage_dhdr_entry_v1);
}

/**
 *  @brief  get data type
 *  @param  start_image  start address casted by long-word
 *  @param  indx   index number of data heder table
 *  @return value of data type
 *      - BIMAGE_DHDR_TYPE_UNDEFINED            undefined type
 *      - BIMAGE_DHDR_TYPE_KERNEL_COMPRESSED    compressed kernel image
 *      - BIMAGE_DHDR_TYPE_KERNEL_UNCOMPRESSED  uncompressed kernel image
 *      - BIMAGE_DHDR_TYPE_INITRD               INITRD image
 *      - BIMAGE_DHDR_TYPE_FPGA                 FPGA data
 */
static inline unsigned int bimage_get_data_type(const unsigned long start_image, const int indx)
{
    struct bimage_header *hdr = bimage_get_header(start_image);
    if((hdr->fmt_ver&BIMAGE_FMT_MASK)>=BIMAGE_FMT_VERLEVEL_2){ /* Ver.2 or later */
        struct bimage_dhdr_entry *dhdr = bimage_get_data_header(start_image);
        return (unsigned int)_bimage_swab32(dhdr[indx].type, hdr->fmt_ver&BIMAGE_BIT_ENDIAN);
    }else{                      /* Ver.1 */
        struct bimage_dhdr_entry_v1 *dhdr = bimage_get_data_header_v1(start_image);
        return (unsigned int)_bimage_swab32(dhdr[indx].type, hdr->fmt_ver&BIMAGE_BIT_ENDIAN);
   }
}

/**
 *  @brief  get data start address
 *  @param  start_image  start address casted by long-word
 *  @param  indx   index number of data heder table
 *  @return pointer of data start address casted by long-word
 */
static inline unsigned long  
bimage_get_data_addr(const unsigned long start_image, const int indx)
{
    struct bimage_header *hdr = bimage_get_header(start_image);
    if((hdr->fmt_ver&BIMAGE_FMT_MASK)>=BIMAGE_FMT_VERLEVEL_2){ /* Ver.2 or later */
        struct bimage_dhdr_entry *dhdr = bimage_get_data_header(start_image);
        return _bimage_swab32(dhdr[indx].off, hdr->fmt_ver&BIMAGE_BIT_ENDIAN)
                + start_image;
    }else{                      /* Ver.1 */
        struct bimage_dhdr_entry_v1 *dhdr = bimage_get_data_header_v1(start_image);
        return _bimage_swab32(dhdr[indx].off, hdr->fmt_ver&BIMAGE_BIT_ENDIAN)
                + start_image;
    }
}

/**
 *  @brief  get size of data
 *  @param  start_image  start address casted by long-word
 *  @param  indx   index number of data heder table
 *  @return size of data
 */
static inline unsigned int bimage_get_data_size(const unsigned long start_image, const int indx)
{
    struct bimage_header *hdr = bimage_get_header(start_image);
    if((hdr->fmt_ver&BIMAGE_FMT_MASK)>=BIMAGE_FMT_VERLEVEL_2){ /* Ver.2 or later */
        struct bimage_dhdr_entry *dhdr = bimage_get_data_header(start_image);
        return _bimage_swab32(dhdr[indx].sz, hdr->fmt_ver&BIMAGE_BIT_ENDIAN);
    }else{                      /* Ver.1 */
        struct bimage_dhdr_entry_v1 *dhdr = bimage_get_data_header_v1(start_image);
        return _bimage_swab32(dhdr[indx].sz, hdr->fmt_ver&BIMAGE_BIT_ENDIAN);
    }
}

/**
 *  @brief  get data load address
 *  @param  start_image  start address casted by long-word
 *  @param  indx   index number of data heder table
 *  @return pointer of data load address casted by long-word
 */
static inline unsigned long
bimage_get_data_load(const unsigned long start_image, const int indx)
{
    struct bimage_header *hdr = bimage_get_header(start_image);
    if((hdr->fmt_ver&BIMAGE_FMT_MASK)>=BIMAGE_FMT_VERLEVEL_2){ /* Ver.2 or later */
        struct bimage_dhdr_entry *dhdr = bimage_get_data_header(start_image);
        return _bimage_swab32(dhdr[indx].load, hdr->fmt_ver&BIMAGE_BIT_ENDIAN);
    }else{                      /* Ver.1 */
        return 0L;
    }

}

/**
 *  @brief  get data start address
 *  @param  start_image  start address casted by long-word
 *  @param  indx   index number of data heder table
 *  @return pointer of data start address casted by long-word
 */
static inline unsigned long
bimage_get_data_start(const unsigned long start_image, const int indx)
{
    struct bimage_header *hdr = bimage_get_header(start_image);
    if((hdr->fmt_ver&BIMAGE_FMT_MASK)>=BIMAGE_FMT_VERLEVEL_2){         /* Ver.2 or later */
        struct bimage_dhdr_entry *dhdr = bimage_get_data_header(start_image);
        return _bimage_swab32(dhdr[indx].start, hdr->fmt_ver&BIMAGE_BIT_ENDIAN);
    }else{                      /* Ver.1 */
        return 0L;
    }
}

/**
 *  @brief  find index number of data by data type
 *  @param  start_image  start address casted by long-word
 *  @param  type        value of data type
 *  @return index number of data
 */
static inline int
bimage_find_data_by_type(const unsigned long start_image,const unsigned int type)
{
    unsigned int n;
    unsigned long nr_dhdr_entry = bimage_get_nr_data_header_entry(start_image);   
    
    for(n=0; n<nr_dhdr_entry; n++){
        unsigned int data_type;
        struct bimage_header *hdr = bimage_get_header(start_image);
        if((hdr->fmt_ver&BIMAGE_FMT_MASK)>=BIMAGE_FMT_VERLEVEL_2){ /* Ver.2 or later */
            struct bimage_dhdr_entry *dhdr = bimage_get_data_header(start_image);
            data_type=_bimage_swab32(dhdr[n].type, hdr->fmt_ver&BIMAGE_BIT_ENDIAN);
        }else{                      /* Ver.1 */
            struct bimage_dhdr_entry_v1 *dhdr = bimage_get_data_header_v1(start_image);
            data_type=_bimage_swab32(dhdr[n].type, hdr->fmt_ver&BIMAGE_BIT_ENDIAN);
        }
        if(data_type==type)
            return (int)n;
    }
    return -1;
}

#endif /* __LINUX_BIMAGE_H__ */
