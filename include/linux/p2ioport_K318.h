/*****************************************************************************
 *  linux/include/linux/p2ioport_K318.h
 *
 *   Header file of P2PF I/O port and GPIO access driver for AJ-HPX600
 *     
 *     Copyright (C) 2010 Matsushita Electric Industrial, Co.,Ltd.
 *     All Rights Reserved.
 *
 *****************************************************************************/
/* $Id: p2ioport_K318.h 17953 2011-12-13 04:25:46Z Yoshioka Masaki $ */

#ifndef _P2IOPORT_K318_H_
#define _P2IOPORT_K318_H_

 /*** I/F structure definitions ***/
struct p2ioport_rwval_s
{
  int dir;           /* [in] 0=Read, 1=Write */
  unsigned char val; /* [in/out] value */
};

 /** special commands for each boards **/
 /* AG-HPX3100 */
#define P2IOPORT_IOC_HPX3100_MAGIC	(0xD1)
#define P2IOPORT_IOC_HPX3100_TOGGLE_DEBUGMODE	_IO(P2IOPORT_IOC_HPX3100_MAGIC, 100)
#define P2IOPORT_IOC_HPX3100_JTAGCTRL	_IOWR(P2IOPORT_IOC_HPX3100_MAGIC, 101, struct p2ioport_rwval_s)
#define P2IOPORT_IOC_HPX3100_USB_VBUS_ON	_IOWR(P2IOPORT_IOC_HPX3100_MAGIC, 102, struct p2ioport_rwval_s)
#define P2IOPORT_IOC_HPX3100_USB_OCI	_IOR(P2IOPORT_IOC_HPX3100_MAGIC, 103, unsigned char)
#define P2IOPORT_IOC_HPX3100_PROXY_OPT_DETECTION	_IOR(P2IOPORT_IOC_HPX3100_MAGIC, 104, unsigned char)

/*
 * USB VBUS Port definition
 */
#define	P2IOPORT_IOC_HPX3100_USB_VBUS_ON_P0	(1<<0)
#define	P2IOPORT_IOC_HPX3100_USB_VBUS_ON_P1	(1<<1)

/*
 * PORT virtual definition
 */
enum HPX3100_IOPORT_VPORTDEF {
  /* VUP ctrl */
  P2IOPORT_VPORT_SYSCON_RST    = 0x030,
  P2IOPORT_VPORT_SYSCON_W      = 0x031,
  P2IOPORT_VPORT_CAM_RST       = 0x032,
  P2IOPORT_VPORT_CAM_W         = 0x033,
  P2IOPORT_VPORT_CAM_EXT_W     = 0x034,
  P2IOPORT_VPORT_LCD_RST       = 0x035,
  P2IOPORT_VPORT_LCD_W         = 0x036,
  P2IOPORT_VPORT_VUP_MODE_N    = 0x037,

  /* JTAG switch */
  P2IOPORT_VPORT_JTAG_FM       = 0x050,
  P2IOPORT_VPORT_JTAG_CHAR     = 0x051,
  P2IOPORT_VPORT_JTAG_PULSE    = 0x052,
  P2IOPORT_VPORT_JTAG_PXY_FPGA = 0x053,
  P2IOPORT_VPORT_JTAG_PXY_DSP  = 0x054,

  /* RESET ctrl */
  P2IOPORT_VPORT_RICHO_RST_N   = 0x060,
  P2IOPORT_VPORT_PXYDSP_RST_N  = 0x064,

  /* USB ctrl */
  P2IOPORT_VPORT_RST_USBHUB_N  = 0x070,
  P2IOPORT_VPORT_USB_OE_N      = 0x071,
  P2IOPORT_VPORT_USB_HOST_N    = 0x072,
};

#endif /* _P2IOPORT_K318_H_ */

/******************** the end of the file "p2ioport_K318.h" ********************/
