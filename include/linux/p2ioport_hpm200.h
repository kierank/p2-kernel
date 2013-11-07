/*****************************************************************************
 *  linux/include/linux/p2ioport_hpm200.h
 *
 *   Header file of P2PF I/O port and GPIO access driver for AJ-HPM200
 *     
 *     Copyright (C) 2009-2010 Matsushita Electric Industrial, Co.,Ltd.
 *     All Rights Reserved.
 *
 *****************************************************************************/
/* $Id:$ */

#ifndef _P2IOPORT_HPM200_H_
#define _P2IOPORT_HPM200_H_

 /** special commands for each boards **/
 /* AJ-HPM200 */
#define P2IOPORT_IOC_HPM200_MAGIC	(0xD2)
#define P2IOPORT_IOC_HPM200_TOGGLE_DEBUGMODE	_IO(P2IOPORT_IOC_HPM200_MAGIC, 100)

/* K286(AJ-HPD2500) */
#define P2IOPORT_IOC_K286_MAGIC             P2IOPORT_IOC_HPM200_MAGIC
#define P2IOPORT_IOC_K286_TOGGLE_DEBUGMODE  P2IOPORT_IOC_HPM200_TOGGLE_DEBUGMODE


/*
 * PORT virtual definition(old)
 */

 /* AJ-HPM200, K286(AJ-HPD2500) */
#define P2IOPORT_PORT_ZION_INIT_L       (1<<0)
#define P2IOPORT_PORT_FPGA_CONFDONE     (1<<1)
#define P2IOPORT_PORT_FPGA_NCONFIG      (1<<2)
#define P2IOPORT_PORT_FPGA_NSTATUS      (1<<3)
#define P2IOPORT_PORT_SDLED             (1<<15)

/*
 * PORT virtual definition
 */
#define P2IOPORT_VPORT_SDLED             0x001

#define P2IOPORT_VPORT_ZION_INIT_L       0x020
#define P2IOPORT_VPORT_FPGA_CONFDONE     0x021
#define P2IOPORT_VPORT_FPGA_NCONFIG      0x022
#define P2IOPORT_VPORT_FPGA_NSTATUS      0x023


#endif /* _P2IOPORT_HPM200_H_ */

/******************** the end of the file "p2ioport_hpm200.h" ********************/
