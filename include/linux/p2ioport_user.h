/*****************************************************************************
 *  linux/include/linux/p2ioport_user.h
 *
 *   Header file of P2PF I/O port and GPIO access driver for user applications
 *     
 *     Copyright (C) 2008-2010 Matsushita Electric Industrial, Co.,Ltd.
 *     All Rights Reserved.
 *
 *****************************************************************************/
/* $Id: p2ioport_user.h 6918 2010-05-14 10:28:21Z Noguchi Isao $ */

#ifndef _P2IOPORT_USR_H_
#define _P2IOPORT_USR_H_

/*** I/F structure definitions ***/
struct p2ioport_val_s
{
  int num;           /* [in] the number of device(DIPSW, LED...) */
  unsigned long val; /* [in/out] value */
};


struct p2ioport_version_s
{
  int num;                /* [in] the number of device(FPGA) */
  unsigned long long ver; /* [in/out] value(version number) */
};


typedef int (*p2ioport_cbfunc_t)(void *);
typedef struct p2ioport_cb_s
{
  int num;                /* [in] callback number */
  p2ioport_cbfunc_t func; /* [in] callback function */
  void *data;             /* [in] the argument of callback function */
} p2ioport_cb_t;


/*** macro ***/
 /** ioctl commands **/
#define P2IOPORT_IOC_MAGIC	(0xD0)

#define P2IOPORT_IOC_GET_DIPSW	_IOWR(P2IOPORT_IOC_MAGIC, 0, struct p2ioport_val_s)
#define P2IOPORT_IOC_SET_LED	_IOW (P2IOPORT_IOC_MAGIC, 1, struct p2ioport_val_s)
#define P2IOPORT_IOC_CLR_LED	_IOW (P2IOPORT_IOC_MAGIC, 2, int)
#define P2IOPORT_IOC_TOGGLE_LED	_IOW (P2IOPORT_IOC_MAGIC, 3, struct p2ioport_val_s)
#define P2IOPORT_IOC_GET_ROTARYSW	_IOWR(P2IOPORT_IOC_MAGIC, 4, struct p2ioport_val_s)
#define P2IOPORT_IOC_GET_VERSION	_IOWR(P2IOPORT_IOC_MAGIC, 5, struct p2ioport_version_s)
#define P2IOPORT_IOC_GET_LED	_IOWR(P2IOPORT_IOC_MAGIC, 6, struct p2ioport_val_s)
#define P2IOPORT_IOC_GET_PORTS	_IOWR(P2IOPORT_IOC_MAGIC, 7, struct p2ioport_val_s)
#define P2IOPORT_IOC_SET_PORTS	_IOW (P2IOPORT_IOC_MAGIC, 8, struct p2ioport_val_s)
#define P2IOPORT_IOC_GET_VPORT	_IOWR(P2IOPORT_IOC_MAGIC, 9, struct p2ioport_val_s)
#define P2IOPORT_IOC_SET_VPORT	_IOW (P2IOPORT_IOC_MAGIC, 10, struct p2ioport_val_s)
#define P2IOPORT_IOC_SET_INT_CB	_IOW (P2IOPORT_IOC_MAGIC, 11, p2ioport_cb_t)
#define P2IOPORT_IOC_LOCK_VPORT     _IOWR (P2IOPORT_IOC_MAGIC, 12, struct p2ioport_val_s)
#define P2IOPORT_IOC_UNLOCK_VPORT   _IOW (P2IOPORT_IOC_MAGIC, 13, struct p2ioport_val_s)


 /* for TEST and DEBUG */
#define P2IOPORT_IOC_TOGGLE_DEBUGMODE	_IO(P2IOPORT_IOC_MAGIC, 100)


/** [[for P2V3]] include machine-depended header file. **/



#endif /* _P2IOPORT_USR_H_ */

/******************** the end of the file "p2ioport_user.h" ********************/
