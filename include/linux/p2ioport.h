/*****************************************************************************
 * llinux/drivers/p2pf/p2ioport/p2ioport.h
 *
 *   Header file of P2PF I/O port and GPIO access driver
 *     
 *     Copyright (C) 2008-2010 Panasonic corp.
 *     All Rights Reserved.
 *
 *****************************************************************************/
/* $Id: p2ioport.h 17953 2011-12-13 04:25:46Z Yoshioka Masaki $ */

#ifndef _P2IOPORT_H_
#define _P2IOPORT_H_

#ifdef __KERNEL__

/*** header include ***/
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/p2ioport_user.h>

/*** macro ***/

/* constants */

/** structure **/
struct p2ioport_operations {
  int (*ioctl)(unsigned, unsigned long);
  unsigned int (*poll) (struct file *, struct poll_table_struct *);
  int (*get_dipsw)(int, unsigned long *);
  int (*get_led)(int, unsigned long *);
  int (*set_led)(int, unsigned long);
  int (*clr_led)(int);
  int (*toggle_led)(int, unsigned long);
  int (*heartbeat_led)(int); /* kernel use only */
  int (*get_rotarysw)(int, unsigned long *);
  int (*get_version)(int, unsigned long long *);
  int (*get_vport)(int, int *);
  int (*set_vport)(int, int);
    int (*lock_vport)(int, int *);
  int (*unlock_vport)(int);
  int (*notify_int)(unsigned long); /* p2ioport int-driver use only */
  //  int (*set_int_callback)(p2ioport_cb_t *);
};

struct p2ioport_info_s {
  int nr_dipsw;
  int nr_led;
  int nr_rotarysw;
  int nr_device;
  struct p2ioport_operations *ops;
  char name[256];
  struct list_head cb_list[32];
};

struct p2ioport_cbentry_s
{
  p2ioport_cbfunc_t func;
  void *data;
  struct list_head cbentry_list;
};


/** function **/
extern int p2ioport_get_dipsw( int num, unsigned long *val );
extern int p2ioport_get_led( int num, unsigned long *val );
extern int p2ioport_set_led( int num, unsigned long val );
extern int p2ioport_clr_led( int num );
extern int p2ioport_toggle_led( int num, unsigned long val );
extern int p2ioport_get_rotarysw( int num, unsigned long *val );
extern int p2ioport_get_version( int num, unsigned long long *val );
extern int p2ioport_get_vport( int port, int *val );
extern int p2ioport_set_vport( int port, int val );
extern void p2ioport_get_operations( struct p2ioport_operations *ops );

/* CAUTION: need to implement for each low-level drivers. */
void __p2ioport_get_ops( struct p2ioport_operations *ops );
int __p2ioport_init_info( struct p2ioport_info_s *info );
void __p2ioport_cleanup_info( struct p2ioport_info_s *info );


/** include machine-depended header file. **/

#if defined(CONFIG_P2IOPORT_HPM200) || defined(CONFIG_P2IOPORT_K286)
# include <linux/p2ioport_hpm200.h>
#elif defined(CONFIG_P2IOPORT_K298)
# include <linux/p2ioport_K298.h>
#elif defined(CONFIG_P2IOPORT_K301)
# include <linux/p2ioport_K301.h>
#elif defined(CONFIG_P2IOPORT_K302)
# include <linux/p2ioport_K302.h>
#elif defined(CONFIG_P2IOPORT_PCD2)
# include <linux/p2ioport_pcd2.h>
#elif defined(CONFIG_P2IOPORT_HMC80)
# include <linux/p2ioport_hmc80.h>
#elif defined(CONFIG_P2IOPORT_HPX3100)
# include <linux/p2ioport_hpx3100.h>
#elif defined(CONFIG_P2IOPORT_K283)
# include <linux/p2ioport_K283.h>
#elif defined(CONFIG_P2IOPORT_K318)
# include <linux/p2ioport_K318.h>
#else
# include <linux/p2ioport_brb.h>
#endif /* CONFIG_P2IOPORT_XXX */

#endif /* __KERNEL__ */

#endif /* _P2IOPORT_H_ */

/******************** the end of the file "p2ioport.h" ********************/
