/* $Id: p2verify.h 8600 2010-08-03 01:05:58Z Noguchi Isao $
 * linux/p2verify.h -- definitions for the char module
 *
 * Copyright (C) 2006-2010 Panasonic Co.,LTD
 *
 * Programmed by I.Noguchi
 */

#ifndef __LINUX_P2VERIFY_H__
#define __LINUX_P2VERIFY_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <linux/ioctl.h> /* needed for the _IOW etc stuff used later */


/*******************************************************************************
 * structure type definitions
 ******************************************************************************/

/*
 * VERIFY
 */

/* Control parameter */
struct p2verify_start {
    unsigned long sa;           /* source address */
    unsigned long da;           /* destination address */
    unsigned long len;          /* byte length */

    int addrtype;                 /* type of address */
#define P2VERIFY_ADDRTYPE_BUS   0 /* bus address (change to virtal
                                     address by using bus_to_virt() */
#define P2VERIFY_ADDRTYPE_MEM   1 /* physical address of main memory
                                     (change to virtal address by
                                     using __va() */
#define P2VERIFY_ADDRTYPE_IOMEM 2 /* physical address of io memory
                                     (change to virtal address by
                                     using ioremap() */
};

/* status */
struct p2verify_status {
    int state;                  /* state of verify */
    int result;                 /* The result of last comparison */
    int handle;                 /* The handle of last comparison */
    unsigned long nr_end;       /* The number of last comparison ends */
    unsigned short src_err,dst_err; /* The error data of last comparison */
};



/*******************************************************************************
 * MACROs definitions
 ******************************************************************************/

/*
 *  VERIFY
 */

/* State of verify */
#define ST_P2VERIFY_IDLE     0 /* idle */
#define ST_P2VERIFY_BUSY     1 /* under verify */
#define ST_P2VERIFY_DONE     2 /* complete */


/*******************************************************************************
 * ioctl definitions
 ******************************************************************************/

/* Magic number */
#define P2VERIFY_IOC_MAGIC  (0xD0) /* Same to 'spdcpufpga' */

/*****************  Serial number ***********************/

/*
  VERIFY
*/
#define P2VERIFY_IOC_START  _IOW(P2VERIFY_IOC_MAGIC, 0, struct p2verify_start )
#define P2VERIFY_IOC_DONE   _IO(P2VERIFY_IOC_MAGIC, 1)
#define P2VERIFY_IOC_STOP   _IO(P2VERIFY_IOC_MAGIC, 2)
#define P2VERIFY_IOC_STATUS _IOR(P2VERIFY_IOC_MAGIC, 3, struct p2verify_status)

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* __LINUX_P2VERIFY_H__ */

