/*****************************************************************************
 *  linux/include/linux/p2ioport_pcd2.h
 *
 *   Header file of P2PF I/O port and GPIO access driver for AJ-PCD2
 *     
 *     Copyright (C) 2010 Matsushita Electric Industrial, Co.,Ltd.
 *     All Rights Reserved.
 *
 *****************************************************************************/

#ifndef _P2IOPORT_PCD2_H_
#define _P2IOPORT_PCD2_H_

 /** special commands for each boards **/
 /* AJ-PCD2 */
#define P2IOPORT_IOC_PCD2_MAGIC	(0xD3)
#define P2IOPORT_IOC_PCD2_TOGGLE_DEBUGMODE	_IO(P2IOPORT_IOC_PCD2_MAGIC, 100)


/*
 * PORT virtual definition
 */


#endif /* _P2IOPORT_PCD2_H_ */

/******************** the end of the file "p2ioport_pcd2.h" ********************/
