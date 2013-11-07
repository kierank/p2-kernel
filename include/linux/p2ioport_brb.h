/*****************************************************************************
 *  linux/include/linux/p2ioport_brb.h
 *
 *   Header file of P2PF I/O port and GPIO access driver for BRBs
 *     
 *     Copyright (C) 2008-2010 Matsushita Electric Industrial, Co.,Ltd.
 *     All Rights Reserved.
 *
 *****************************************************************************/

#ifndef _P2IOPORT_BRB_H_
#define _P2IOPORT_BRB_H_

 /** special commands for each boards **/
 /* SAV8313BRB1 */
#define P2IOPORT_IOC_SAV8313BRB1_MAGIC	(0xD1)
#define P2IOPORT_IOC_SAV8313BRB1_TOGGLE_DEBUGMODE	_IO(P2IOPORT_IOC_SAV8313BRB1_MAGIC, 100)

 /* MPC837xERDB */
#define P2IOPORT_IOC_MPC837XERDB_MAGIC	P2IOPORT_IOC_SAV8313BRB1_MAGIC
#define P2IOPORT_IOC_MPC837XERDB_TOGGLE_DEBUGMODE	P2IOPORT_IOC_SAV8313BRB1_TOGGLE_DEBUGMODE


/*
 * PORT virtual definition
 */



#endif /* _P2IOPORT_BRB_H_ */

/******************** the end of the file "p2ioport_brb.h" ********************/
