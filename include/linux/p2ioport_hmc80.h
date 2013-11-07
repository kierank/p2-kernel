/*****************************************************************************
 *  linux/include/linux/p2ioport_hmc80.h
 *
 *   Header file of P2PF I/O port and GPIO access driver for AG-HMC80
 *     
 *     Copyright (C) 2010 Matsushita Electric Industrial, Co.,Ltd.
 *     All Rights Reserved.
 *
 *****************************************************************************/

#ifndef _P2IOPORT_HMC80_H_
#define _P2IOPORT_HMC80_H_

 /** special commands for each boards **/
 /* AG-HMC80 */
#define P2IOPORT_IOC_HMC80_MAGIC	(0xD5)
#define P2IOPORT_IOC_HMC80_TOGGLE_DEBUGMODE	_IO(P2IOPORT_IOC_HMC80_MAGIC, 100)


/*
 * PORT virtual definition
 */

#define P2IOPORT_PORT_SM331_RST_N       (1<<0)


#endif /* _P2IOPORT_HMC80_H_ */

/******************** the end of the file "p2ioport_hmc80.h" ********************/
