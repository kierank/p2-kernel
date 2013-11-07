/*****************************************************************************
 * linux/include/linux/rtctrl.h
 *
 *   Header file of RT Control Driver for kernel 2.6
 *     
 *     Copyright (C) 2003-2010 Panasonic corp.
 *     All Rights Reserved.
 *
 *****************************************************************************/

#ifndef _RTCTRL_H_
#define _RTCTRL_H_

/*** I/F structure definitions ***/
struct rtctrl_status_s
{
  unsigned int minor;	/* [in] device number for checking */
  unsigned char status;	/* [out] status */
};

struct delayproc_param_s
{
  unsigned int minor;		/* [in] device number of this parameters */
  unsigned long size_sys;	/* [in] size of I/O block at system area[sector] */
  unsigned long size_usr;	/* [in] size of I/O block at user area[sector] */
  int nr_sys;			/* [in] number of I/O block at system area */
  int nr_usr;			/* [in] number of I/O block at user area */
};


/*** macro ***/
 /** ioctl commands **/
#define RTCTRL_IOC_MAGIC	(0xEC)

 /* for RT control */
#define RTCTRL_IOC_RTON		_IO(RTCTRL_IOC_MAGIC, 0)
#define RTCTRL_IOC_RTOFF	_IO(RTCTRL_IOC_MAGIC, 1)
#define RTCTRL_IOC_GET_RTSTATUS	_IOR(RTCTRL_IOC_MAGIC, 2, unsigned char)

/* for RT control [user-space filesystem] */
#define RTCTRL_IOC_LOCK_RTON		_IO(RTCTRL_IOC_MAGIC, 10)
#define RTCTRL_IOC_UNLOCK_RTON		_IO(RTCTRL_IOC_MAGIC, 11)

 /* for delayproc */
#define RTCTRL_IOC_INIT_DELAYPROC	_IOW(RTCTRL_IOC_MAGIC, 20, struct delayproc_param_s)
#define RTCTRL_IOC_EXEC_DELAYPROC	_IOW(RTCTRL_IOC_MAGIC, 21, unsigned int)
#define RTCTRL_IOC_SYNC_DELAYPROC	_IO(RTCTRL_IOC_MAGIC, 22)
#define RTCTRL_IOC_SYNC_SYS_DELAYPROC	_IO(RTCTRL_IOC_MAGIC, 23)
#define RTCTRL_IOC_CHECK_DELAYPROC_DIRTY	_IOWR(RTCTRL_IOC_MAGIC, 24, struct rtctrl_status_s)
#define RTCTRL_IOC_CHECK_ALL_DELAYPROC_DIRTY	_IOR(RTCTRL_IOC_MAGIC, 25, unsigned char)
#define RTCTRL_IOC_GET_DELAYPROC_STATUS	_IOR(RTCTRL_IOC_MAGIC, 26, unsigned char)

#define RTCTRL_IOC_CLEAR_DELAYPROC	_IOW(RTCTRL_IOC_MAGIC, 27, unsigned int)
#define RTCTRL_IOC_CLEAR_ALL_DELAYPROC	_IO(RTCTRL_IOC_MAGIC, 28)

/* for delayproc [user-space filesystem] */
#define RTCTRL_IOC_CHECK_RT_STATUS	_IOR(RTCTRL_IOC_MAGIC, 30, unsigned char)
#define RTCTRL_IOC_CHECK_DELAY_STATUS	_IOR(RTCTRL_IOC_MAGIC, 31, unsigned char)
#define RTCTRL_IOC_IS_RTON4WAIT		_IOR(RTCTRL_IOC_MAGIC, 32, unsigned char)

 /* for TEST and DEBUG */
#define RTCTRL_IOC_TOGGLE_DEBUGMODE	_IO(RTCTRL_IOC_MAGIC, 100)
#define RTCTRL_IOC_TOGGLE_TESTMODE	_IOW(RTCTRL_IOC_MAGIC, 101, int)
#define RTCTRL_IOC_PRINT_TESTMESSAGE	_IO(RTCTRL_IOC_MAGIC, 102)
#define RTCTRL_IOC_TOGGLE_PRINT_ONOFF_MODE	_IO(RTCTRL_IOC_MAGIC, 103)


/** constants **/
enum DELAYPROC_STATUS_ENUM { /* delayproc status numbers */
  DELAYPROC_STATUS_SLEEP   = 0,
  DELAYPROC_STATUS_STANDBY = 1,
  DELAYPROC_STATUS_RUN     = 2,
};


#ifdef __KERNEL__  /* for Kernel ---------------> */

/*** header include ***/
#include <linux/blkdev.h> /* request queue */

/*** macro ***/

/* version numbers */
#define RTCTRL_DRV_VERSION	"2.1.0"
#define RTCTRL_CORE_VERSION	"2.2.0"

enum RTCTRL_CONST_ENUM {
  RTCTRL_MAJOR		= 63,		/* RT-Ctrl driver's device number */
  RTONARRAY_SIZE	= (1 << 12),	/* array size(major number=12bit) */
  DELAYPROC_MAXNR	= 256,		/* the max number of delayproc */
};

enum RTCTRL_FLAGS {	/* RT-Control's status flags(bit) */
  RTCTRL_RTON		= 0x01,	/* RT_ON/OFF bit */
  RTCTRL_RTLOCK		= 0x02,	/* RT_ON lock/unlock bit */
  RTCTRL_SUSPEND	= 0x04,	/* RT_ON suspend/not suspend bit */
  RTCTRL_LOCKCNT_SHIFT	= 4,	/* RT_ON lock counter shift value */
  RTCTRL_LOCKCNT_MASK	= 0xF0,	/* RT_ON lock counter mask */
  RTCTRL_LOCKCNT_MAX	= 0x0F,	/* the max number of RT_ON lock counter */
};

enum DELAYPROC_TYPE_ENUM {	/* delayproc execute type numbers */
  DELAYPROC_TYPE_NONE	= 0,	/* Sleep */
  DELAYPROC_TYPE_NORMAL	= 1,	/* ExecDelayProc */
  DELAYPROC_TYPE_SYNC	= 2,	/* SyncDelayProc */
  DELAYPROC_TYPE_SYSSYNC= 3,	/* SyncSystemDelayProc */
};

#if defined(CONFIG_DELAYPROC_WRITE_ORDER)
enum DELAYPROC_DATTYPE_ENUM {
  DELAYPROC_DATTYPE_FILE = 0,
  DELAYPROC_DATTYPE_META,
  DELAYPROC_DATTYPE_MAXNR,
};

enum DELAYPROC_AREA_ENUM {
  DELAYPROC_AREA_SYS = 0,
  DELAYPROC_AREA_USR,
  DELAYPROC_AREA_MAXNR,
};

#define DELAYPROC_ORDER_MAXNR (DELAYPROC_DATTYPE_MAXNR * DELAYPROC_AREA_MAXNR)

#endif /* CONFIG_DELAYPROC_WRITE_ORDER */


/** structure and type **/
typedef int (*dp_except_inode_t)(struct inode *inode);

struct delayproc_maininfo_s 
{
  unsigned int id;		/* delayproc ID (Major number) */
  unsigned char status;		/* delayproc status */
  unsigned char type;		/* delayproc exec type */
  atomic_t use_cnt;		/* use count (the number of minor devices) */
  struct list_head minor_list;	/* the list of minor devices */
  spinlock_t lock;		/* spin lock */
  struct mutex exec_lock;	/* mutex lock for exec delayproc */
  struct workqueue_struct *works;	/* delayproc daemons */
  dp_except_inode_t *excp_list;	/* excepted inode list */
};

struct delayproc_info_s
{
  dev_t rdev;				/* device number(disk) */
  dev_t pdev;				/* device number(partition) */
  struct list_head dev_list;		/* the list of a device */
  struct list_head rq_list;		/* the list of request for waiting */
  unsigned short buf_cnt;		/* buffering counts */
  struct request_queue *q;		/* request queue */
  spinlock_t lock;			/* spin lock */
  struct delayproc_param_s params;	/* delayproc I/O parameters */
  struct delayproc_maininfo_s *main;	/* delayproc main info */
  struct work_struct delayprocd;	/* delayproc daemon */
#if defined(CONFIG_DELAYPROC_WRITE_ORDER)
  unsigned char order;			/* delayproc write order */
#endif /* CONFIG_DELAYPROC_WRITE_ORDER */
};


/** function **/
/* delayproc function */
extern int	init_delayproc( dev_t pdev, struct delayproc_param_s *params);
extern int	exec_delayproc( dev_t pdev );
extern void	sync_delayproc( unsigned int major, int meta );
extern unsigned char	chk_delayproc_dirty( dev_t pdev );
extern unsigned char	chk_all_delayproc_dirty( unsigned int major );
extern void	clr_delayproc_params( dev_t pdev );
extern void	clr_all_delayproc_params( unsigned int major );
extern void	destroy_delayproc( dev_t pdev );

extern void prepare_rton( unsigned int major );
extern unsigned char	get_rton_status( unsigned int major );

extern int	init_delayproc_info( struct delayproc_info_s **pdpinfo, dev_t rdev, dp_except_inode_t *excp_list );
extern void	exit_delayproc_info( struct delayproc_info_s *dpinfo );

extern int	is_delayproc_dev( dev_t dev );
extern int	is_delayproc_run_dev( dev_t dev );
extern unsigned char	get_delayproc_status( struct delayproc_info_s *dpinfo );
extern unsigned char	get_delayproc_status_dev( dev_t dev );
extern unsigned char	get_delayproc_type( struct delayproc_info_s *dpinfo );

extern void	add_delayproc_req_waitlist( struct delayproc_info_s *dpinfo,
					    struct request *req );
extern void	del_delayproc_req_waitlist( struct delayproc_info_s *dpinfo,
					    struct request *req );
extern void	wait_delayproc_req( struct delayproc_info_s *dpinfo );

extern void	inc_delayproc_buf_cnt( struct delayproc_info_s *dpinfo );
extern void	dec_delayproc_buf_cnt( struct delayproc_info_s *dpinfo );
extern unsigned short	get_delayproc_buf_cnt( struct delayproc_info_s *dpinfo );

extern void	set_delayproc_order( struct delayproc_info_s *dpinfo, unsigned char order );
extern unsigned char	get_delayproc_order( struct delayproc_info_s *dpinfo );

extern void	print_delayproc_testmessage( void );

static inline int task_is_delayprocd( struct task_struct *task )
{
  return (task->flags & PF_DELAYPROC);
}

static inline void set_task_delayprocd( struct task_struct *task )
{
  task->flags |= PF_DELAYPROC;
}

static inline void clr_task_delayprocd( struct task_struct *task )
{
  task->flags &= ~PF_DELAYPROC;
}

#define current_is_delayprocd() task_is_delayprocd( current )


#endif /* __KERNEL__ */ /* <--------------- for Kernel */

#endif /* _RTCTRL_H_ */

/******************** the end of the file "rtctrl.h" ********************/
