/*****************************************************************************
 *  linux/include/linux/p2ioport_K301.h
 *
 *   Header file of P2PF I/O port and GPIO access driver for K301
 *     
 *     Copyright (C) 2010 Matsushita Electric Industrial, Co.,Ltd.
 *     All Rights Reserved.
 *
 *****************************************************************************/
/* $Id: p2ioport_K301.h 9140 2010-09-14 01:48:36Z myoshioka $ */

#ifndef _P2IOPORT_K301_H_
#define _P2IOPORT_K301_H_

 /*** I/F structure definitions ***/
struct p2ioport_rwval_s
{
  int dir;           /* [in] 0=Read, 1=Write */
  unsigned char val; /* [in/out] value */
};

 /** special commands for each boards **/
 /* AG-3DP1(K301) */
#define P2IOPORT_IOC_3DP1_MAGIC	(0xD7)
#define P2IOPORT_IOC_3DP1_TOGGLE_DEBUGMODE	_IO(P2IOPORT_IOC_3DP1_MAGIC, 100)
#define P2IOPORT_IOC_3DP1_JTAGCTRL		_IOWR(P2IOPORT_IOC_3DP1_MAGIC, 101, struct p2ioport_rwval_s)
#define P2IOPORT_IOC_3DP1_PQ_MODE_DET	_IOR(P2IOPORT_IOC_3DP1_MAGIC, 102, unsigned char)

/*
 * PORT virtual definition for K301
 */

/*
 * PORT virtual definition
 */
enum HPX3100_IOPORT_VPORTDEF {
  /* VUP ctrl */
  P2IOPORT_VPORT_VUP_MODE_N    = 0x037,

  /* UART select */
  P2IOPORT_VPORT_AERO_L_RST    = 0x050,
  P2IOPORT_VPORT_AERO_L_W      = 0x051,
  P2IOPORT_VPORT_AERO_R_RST    = 0x052,
  P2IOPORT_VPORT_AERO_R_W      = 0x053,
  P2IOPORT_VPORT_CAM_L_RST     = 0x054,
  P2IOPORT_VPORT_CAM_L_W       = 0x055,
  P2IOPORT_VPORT_CAM_R_RST     = 0x056,
  P2IOPORT_VPORT_CAM_R_W       = 0x057,

  /* VUP route select */
  P2IOPORT_VPORT_ROUTE_AERO_L  = 0x075,
  P2IOPORT_VPORT_ROUTE_AERO_R  = 0x076,
  P2IOPORT_VPORT_ROUTE_AVIO    = 0x077,
  P2IOPORT_VPORT_ROUTE_CAMU_L  = 0x079,
  P2IOPORT_VPORT_ROUTE_CAMU_R  = 0x07A,
  P2IOPORT_VPORT_ROUTE_CAMIO   = 0x07B,
  P2IOPORT_VPORT_ROUTE_CAMF_L  = 0x07C,
  P2IOPORT_VPORT_ROUTE_CAMF_R  = 0x07D,

  /* USB VBUS control */
  P2IOPORT_VPORT_USB_VBUS      = 0x090,

  /* PQ INFO reg */
  P2IOPORT_VPORT_PQ_INFO_SND   = 0x0A0,
  P2IOPORT_VPORT_PQ_INFO_RCV   = 0x0A1,
};

#endif /* _P2IOPORT_K301_H_ */

/******************** the end of the file "p2ioport_K301.h" ********************/
