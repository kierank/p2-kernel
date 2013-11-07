/*****************************************************************************
 * linux/drivers/p2pf/rtctrl/rtctrl_core.c
 *
 *   RT Control core module for kernel 2.6
 *     
 *     Copyright (C) 2007-2010 Panasonic corp.
 *     All Rights Reserved.
 *
 *****************************************************************************/

/***** macro for DEBUG *****/
/* #define DBG_TRACE */
/* #define DBG_PRINT_WAITLIST */

/***** include header files *****/
/** system header files **/
#include <linux/kernel.h>	/* for kernel module */
#include <linux/fs.h>		/* for filesystem operations */
#include <asm/uaccess.h>	/* for verity_area etc */
#include <linux/blkdev.h>	/* for block device */
#include <linux/proc_fs.h>	/* for proc filesystem */

/* drivers/pcmcia/cs.c */
#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>

/** RT Control header **/
#include <linux/rtctrl.h>


/***** definitions *****/
/** constants **/
/* for delayproc */
enum DELAYPROC_DIRTY_ENUM {
  DELAYPROC_NOT_DIRTY = 0,
  DELAYPROC_DIRTY     = 1,
};

enum DELAYPROC_CONST_ENUM {
  DELAYPROC_NRWRITE	= ((4<<20) >> 9),	/* 4MB [sector] */
  DELAYPROC_WAITTIME	= (HZ >> 2),		/* 250msec */
  DELAYPROC_TIMEOUT	= (40),			/* 10sec */
  DELAYPROC_MAXLOOP	= (20),			/* the max number of retry */
};


/** structures and variables **/
unsigned char RTCTRL_WARNING     = 1;
unsigned char RTCTRL_DEBUG       = 0;
unsigned char RTCTRL_TEST        = 0;
unsigned char RTCTRL_PRINT_ONOFF = 0;

/* for RT-Control */
static unsigned char rton_array[RTONARRAY_SIZE];
static DEFINE_SPINLOCK(rtoninfo_lock);
static int dppid[2] = {0, 0}; /* for DEBUG */


/** macros **/

/* for delayproc */
#if defined(CONFIG_DELAYPROC)
static struct delayproc_maininfo_s *dp_info[RTONARRAY_SIZE];
# define DP_MAININFO(id)	dp_info[id]
# define DP_ID(minfo)		((minfo)->id)
# define DP_STATUS(minfo)	((minfo)->status)
# define DP_TYPE(minfo)		((minfo)->type)
# define DP_USECNT(minfo)	((minfo)->use_cnt)
# define DP_DEVLIST(minfo)	((minfo)->minor_list)

# define DELAYPROC_PROCNAME "fs/delayproc"
#endif /* CONFIG_DELAYPROC */


/* for messages */
#define PINFO( fmt, args... )	printk( KERN_INFO "[rtctrl] " fmt, ## args)
#define PERROR( fmt, args... )	printk( KERN_ERR "[rtctrl](E)" fmt, ## args)
#define PWARNING( fmt, args... )	do { if (RTCTRL_WARNING) printk( KERN_WARNING "[rtctrl](W)" fmt, ## args); } while(0)
#define PDEBUG( fmt, args... )	do { if (RTCTRL_DEBUG) printk( KERN_INFO "[rtctrl:l.%d] " fmt, __LINE__, ## args); } while(0)

#if defined(DBG_TRACE)
# define PTRACE(fmt, args...)  printk(KERN_INFO "[rtctrl]##%s" fmt "\n",\
           __FUNCTION__, ## args);
#else
# define PTRACE(fmt, args...)
#endif

/* for delayproc messages */
#define DP_TESTMESSAGE "****** DELAYPROC TEST ******"
#define PDPTEST( fmt, args... ) do { if (RTCTRL_TEST) printk( KERN_ERR "[delayprocd]" DP_TESTMESSAGE "\n" fmt, ## args ); } while(0)


/** prototype **/

/* RT-Control function for each device (CONFIG_RTCTRLDRV_DEV) */
inline unsigned char chk_rton_devall( unsigned int major );

/* delayproc function */
static inline struct delayproc_maininfo_s *delayproc_get_maininfo( dev_t dev );
static inline unsigned char delayproc_do_get_status_lock( struct delayproc_maininfo_s *maininfo );
static inline void test_and_set_delayproc_dev( dev_t rdev );
static inline void delayproc_test_and_clear_status( unsigned int major );
static int  delayproc_do_exec( struct delayproc_info_s *dpinfo );
static void delayproc_exec_lock( struct delayproc_maininfo_s *maininfo );
static void delayproc_exec_unlock( struct delayproc_maininfo_s *maininfo );


/* fs/fs-writeback.c (for delayproc) */
#if defined(CONFIG_DELAYPROC)
# include <linux/writeback.h>
extern void generic_sync_sb_inodes(struct super_block *sb, struct writeback_control *wbc);
extern struct super_block *blockdev_superblock;
static inline int sb_is_blkdev_sb(struct super_block *sb)
{
  return ( sb == blockdev_superblock );
}
#endif /* CONFIG_DELAYPROC */


/********************************* function *********************************/

/* Get RT status. */
inline unsigned char get_rton_status( unsigned int major )
{
  unsigned char status = 0;

  /* Check an argument: major */
  if ( RTONARRAY_SIZE <= major) {
    PERROR( "Argument(%d) exceeded!(%d)\n",
	    major, RTONARRAY_SIZE );
    return (0);
  }

  /* Get RT status. */
  spin_lock( &rtoninfo_lock ); /* Lock --> */
  status = rton_array[major];
  spin_unlock( &rtoninfo_lock ); /* <-- Unlock */

  return (status);
}


/* Check RT_ON or OFF. */
inline unsigned char chk_rton( unsigned int major )
{
  return (get_rton_status(major) & RTCTRL_RTON);
}


/* Set RT_ON. */
void set_rton( unsigned int major )
{
  unsigned char status = 0;
  PTRACE();

  /* Check an argument: major */
  if ( RTONARRAY_SIZE <= major ) {
    PERROR( "set_rton error. Argument(%d) exceeded!(%d)\n",
	    major, RTONARRAY_SIZE );
    return;
  }

  /* Set RT_ON or Suspend status. */
  spin_lock( &rtoninfo_lock ); /* Lock--> */
  status = rton_array[major];
  rton_array[major] |= (status & RTCTRL_RTLOCK) ? RTCTRL_SUSPEND : RTCTRL_RTON;
  spin_unlock( &rtoninfo_lock ); /* <--Unlock */

  /* Set delayproc status STANDBY. */
  /*  NOTICE: Error occurred if P2 card isn't mounted. */
  if ( ! (status & RTCTRL_RTLOCK) ) {
    PDEBUG( "Set delayproc status STANDBY\n" );
    test_and_set_delayproc_dev( MKDEV(major, 0) );
  }

  PDEBUG( "rton_array[%d] = 0x%02X\n", major, rton_array[major] );
  PDEBUG( "<<RT-%s>>\n", (rton_array[major]&RTCTRL_RTLOCK)?"Suspend":"ON" );
  if ( RTCTRL_PRINT_ONOFF ) {
    if ( rton_array[major]&RTCTRL_RTON ) printk( "## RT_ON\n" ); /* for DEBUG */
    if ( rton_array[major]&RTCTRL_RTLOCK ) printk( "## Sp\n" ); /* for DEBUG */
  }
}


/* Clear RT_ON. */
void clr_rton( unsigned int major )
{
  PTRACE();

  /* Check an argument: major */
  if ( RTONARRAY_SIZE <= major ) {
    PERROR( "clr_rton error. Argument(%d) exceeded!(%d)\n",
	    major, RTONARRAY_SIZE );
    return;
  }

  /* Clear RT_ON and Suspend status. */
  spin_lock( &rtoninfo_lock ); /* Lock--> */
  rton_array[major] &= ~(RTCTRL_RTON | RTCTRL_SUSPEND);
  spin_unlock( &rtoninfo_lock ); /* <--Unlock */

  /* Check and clear delayproc status. */
  delayproc_test_and_clear_status( major );

  PDEBUG( "rton_array[%d] = 0x%02X\n", major, rton_array[major] );
  PDEBUG( "## RT_OFF\n" );
}


/* Lock RT_ON/OFF */
void lock_rton( unsigned int major )
{
  unsigned char count = 0;
#if defined(CONFIG_DELAYPROC)
  struct delayproc_maininfo_s *maininfo = delayproc_get_maininfo(MKDEV(major, 0));
#endif /* CONFIG_DELAYPROC */
  PTRACE();

  /* Check an argument: major */
  if ( RTONARRAY_SIZE <= major ) {
    PERROR( "lock_rton error. Argument(%d) exceeded!(%d)\n",
	    major, RTONARRAY_SIZE );
    return;
  }

  /* Lock RT_ON/OFF */
  spin_lock( &rtoninfo_lock ); /* Lock--> */

  count = (rton_array[major] & RTCTRL_LOCKCNT_MASK) >> RTCTRL_LOCKCNT_SHIFT;
#if defined(CONFIG_DELAYPROC)
  if (maininfo) PDEBUG("Lk[%d]<%d>%d", current->pid, major, count);
#else /* !CONFIG_DELAYPROC */
  PDEBUG("Lk[%d]<%d>%d", current->pid, major, count);
#endif /* CONFIG_DELAYPROC */

  /* Set RTLOCK flag */
  if ( ! (rton_array[major] & RTCTRL_RTON) ) {
    rton_array[major] |= RTCTRL_RTLOCK;
  }

  /* Check and increment lock counter */
  if ( count < RTCTRL_LOCKCNT_MAX ) {

    PDEBUG( "[%d]%d->%d (0x%02X -> 0x%02X)\n",
	    major, count, count+1, rton_array[major],
	    (rton_array[major] & ~RTCTRL_LOCKCNT_MASK)
	    | (((count+1) << RTCTRL_LOCKCNT_SHIFT) & RTCTRL_LOCKCNT_MASK) );

    rton_array[major] = (rton_array[major] & ~RTCTRL_LOCKCNT_MASK)
      | ((++count << RTCTRL_LOCKCNT_SHIFT) & RTCTRL_LOCKCNT_MASK);
  } else {
    PWARNING( "RTLOCK counter is overflow!(%d)\n", count );
  }

#if defined(CONFIG_DELAYPROC)
  if ( maininfo && !delayproc_do_get_status_lock(maininfo) && (1 == count) ){
    delayproc_exec_lock( maininfo ); /* MUTEX Lock --> */
  }
#endif /* CONFIG_DELAYPROC */
  spin_unlock( &rtoninfo_lock ); /* <--Unlock */

  PDEBUG( "rton_array[%d] = 0x%02X\n", major, rton_array[major] );
}


/* Unlock RT_ON/OFF */
void unlock_rton( unsigned int major )
{
  unsigned char status = 0;
  unsigned char count = 0;
#if defined(CONFIG_DELAYPROC)
  struct delayproc_maininfo_s *maininfo = delayproc_get_maininfo(MKDEV(major, 0));
  unsigned char old_dpstatus = -1;
#endif /* CONFIG_DELAYPROC */
  PTRACE();

  /* Check an argument: major */
  if ( RTONARRAY_SIZE <= major ) {
    PERROR( "unlock_rton error. Argument(%d) exceeded!(%d)\n",
	    major, RTONARRAY_SIZE );
    return;
  }

  /* Unlock RT_ON/OFF */
  spin_lock( &rtoninfo_lock ); /* Lock--> */

  count = (rton_array[major] & RTCTRL_LOCKCNT_MASK) >> RTCTRL_LOCKCNT_SHIFT;
#if defined(CONFIG_DELAYPROC)
  if (maininfo) PDEBUG("Ul(%d)<%d>%d", current->pid, major, count);
#else /* !CONFIG_DELAYPROC */
  PDEBUG("Ul(%d)<%d>%d", current->pid, major, count);
#endif /* CONFIG_DELAYPROC */

  if ( count > 0 ) {
    if ( 1 == count ) {
      /* Clear RTLOCK flag */
      if ( ! (rton_array[major] & RTCTRL_RTON) ) {
	rton_array[major] &= ~RTCTRL_RTLOCK;
      }
    }

    /* Decrement lock counter */
    PDEBUG( "[%d]%d->%d (0x%02X -> 0x%02X)\n",
	    major, count, count-1, rton_array[major],
	    (rton_array[major] & ~RTCTRL_LOCKCNT_MASK)
	    | (((count-1) << RTCTRL_LOCKCNT_SHIFT) & RTCTRL_LOCKCNT_MASK) );

    rton_array[major] = (rton_array[major] & ~RTCTRL_LOCKCNT_MASK)
      | ((--count << RTCTRL_LOCKCNT_SHIFT) & RTCTRL_LOCKCNT_MASK);
  } else {
    PWARNING( "RTLOCK counter is underflow!(%d)\n", count );
  }
  status = rton_array[major];

  if ( (status & RTCTRL_SUSPEND) && (0 == count) ) {
    rton_array[major] |= RTCTRL_RTON;
    rton_array[major] &= ~RTCTRL_SUSPEND;
    if ( RTCTRL_PRINT_ONOFF ) printk( "## Sp-ON\n" ); /* for DEBUG */
  }

  PDEBUG( "rton_array[%d] = 0x%02X\n", major, rton_array[major] );

#if defined(CONFIG_DELAYPROC)
  if ( maininfo ) {
    old_dpstatus = delayproc_do_get_status_lock( maininfo );
  }
#endif /* CONFIG_DELAYPROC */

  /* Set delayproc status STANDBY. */
  /*  NOTICE: Error occurred if P2 card isn't mounted. */
  if ( (status & RTCTRL_SUSPEND) && (0 == count) ) {
    PDEBUG( "Set delayproc status STANDBY\n" );
    test_and_set_delayproc_dev( MKDEV(major, 0) );
  }

#if defined(CONFIG_DELAYPROC)
  if ( maininfo && !old_dpstatus && (0 == count) ) {
    delayproc_exec_unlock( maininfo ); /* <-- MUTEX Unlock */
  }
#endif /* CONFIG_DELAYPROC */

  spin_unlock( &rtoninfo_lock ); /* <--Unlock */
}


/* Init RT_ON info. */
inline void init_rton_info( void )
{
  spin_lock( &rtoninfo_lock ); /* Lock--> */
  memset( rton_array, 0, RTONARRAY_SIZE );

#if defined(CONFIG_DELAYPROC)
  memset( dp_info, 0, sizeof(dp_info) );
#endif /* CONFIG_DELAYPROC */

  spin_unlock( &rtoninfo_lock ); /* <--Unlock */
}


/* Clear RT_ON info. */
inline void clr_rton_info( void )
{
  init_rton_info();
}


#if defined(CONFIG_DELAYPROC) /* ----------- delayproc function ----------- */


/* Print a delayproc test message for DEBUG and TEST. */
inline void print_delayproc_testmessage( void )
{
  PDPTEST();
}


/* Convert the device number of partition to the device number of disk. */
static inline dev_t delayproc_pdev2rdev( dev_t pdev )
{
  /* CAUTION: this function is only for SINGLE PARTITION devices! */
  PTRACE( " : %X->%X", pdev, pdev-1 );
  return (pdev - 1);
}


#if defined(CONFIG_DELAYPROC_WRITE_ORDER)
/* Set delayproc order. */
inline void set_delayproc_order( struct delayproc_info_s *dpinfo, unsigned char order )
{
  unsigned long irq_flg = 0L;

  /* Check an argument: dpinfo */
  if ( unlikely(NULL == dpinfo) ) {
    PERROR( "Invalid an argument: dpinfo!\n" );
    return;
  }

  spin_lock_irqsave( &dpinfo->main->lock, irq_flg ); /* Lock--> */
  dpinfo->order = order;
  spin_unlock_irqrestore( &dpinfo->main->lock, irq_flg ); /* <--Unlock */

  PTRACE( "(%d)", dpinfo->order );
}


/* Get delayproc order. */
inline unsigned char get_delayproc_order( struct delayproc_info_s *dpinfo )
{
  unsigned long irq_flg = 0L;
  unsigned char order = 0;

  /* Check an argument: dpinfo */
  if ( unlikely(NULL == dpinfo) ) {
    PERROR( "Invalid an argument: dpinfo!\n" );
    return (unsigned char)-1;
  }

  spin_lock_irqsave( &dpinfo->main->lock, irq_flg ); /* Lock--> */
  order = dpinfo->order;
  spin_unlock_irqrestore( &dpinfo->main->lock, irq_flg ); /* <--Unlock */

  return (order);
}


/* Init delayproc order. */
static inline void delayproc_init_order( struct delayproc_info_s *dpinfo )
{
  set_delayproc_order( dpinfo, 0 );
}

#else /* ! CONFIG_DELAYPROC_WRITE_ORDER */

inline void set_delayproc_order( struct delayproc_info_s *dpinfo, unsigned char order ) {return;}
inline unsigned char get_delayproc_order( struct delayproc_info_s *dpinfo ) {return 0;}
static inline void delayproc_init_order( struct delayproc_info_s *dpinfo ) {return;}
#endif /* CONFIG_DELAYPROC_WRITE_ORDER */


/* Convert the device number of disk to the device number of partition. */
static inline dev_t delayproc_rdev2pdev( dev_t rdev )
{
  /* CAUTION: this function is only for SINGLE PARTITION devices! */
  PTRACE( " : %X->%X", rdev, rdev+1 );
  return (rdev + 1);
}


/* Get a delayproc main info from device number. */
inline struct delayproc_maininfo_s *delayproc_get_maininfo( dev_t dev )
{
  return DP_MAININFO( MAJOR(dev) );
}


/* Get a delayproc info (minor element). */
static struct delayproc_info_s *delayproc_get_info( dev_t rdev )
{
  unsigned int major = MAJOR(rdev);
  unsigned int minor = MINOR(rdev);
  struct list_head *head = NULL;
  struct delayproc_info_s *walk = NULL;
  struct delayproc_maininfo_s *maininfo = delayproc_get_maininfo(rdev);
  unsigned long irq_flg = 0L;
  PTRACE();

  /* Check delayproc main info. */
  if ( unlikely(NULL == maininfo) ) {
    return (NULL);
  }

  /* Get the head of minor list and check minor list is empty or not.*/
  spin_lock_irqsave( &maininfo->lock, irq_flg ); /* Lock--> */
  head = &DP_DEVLIST(maininfo);
  if ( list_empty(head) ) {
    PDEBUG( "dpinfo[%d]->minor_list is empty.\n", major );
    spin_unlock_irqrestore( &maininfo->lock, irq_flg ); /* <--Unlock */
    return (NULL);
  }

  /** main loop **/
  list_for_each_entry( walk, head, dev_list ) {
    if ( walk->rdev == rdev ) { /* Find minor info. */
      spin_unlock_irqrestore( &maininfo->lock, irq_flg ); /* <--Unlock */
      return (walk);
    }
  }

  PDEBUG( "dpinfo[%d]->minor_list(%d) doesn't exist.\n", major, minor );
  spin_unlock_irqrestore( &maininfo->lock, irq_flg );	/* <--Unlock */
  return (NULL);
}


/* Get device queue from device number. */
static inline struct request_queue *delayproc_get_queue( dev_t rdev )
{
  struct delayproc_info_s *dpinfo = delayproc_get_info( rdev );

  if ( unlikely(NULL == dpinfo) ) {
    PERROR( "Getting dpinfo is failed!\n" );
    return (NULL);
  }
  return (dpinfo->q);
}


/* init lock of changing status and exec delayproc */
static inline void delayproc_init_exec_lock( struct delayproc_maininfo_s *maininfo )
{
  /* Check an argument: maininfo */
  if ( unlikely(NULL == maininfo) ) {
    PERROR( "Invalid an argument: maininfo!\n" );
    return;
  }
  mutex_init( &(maininfo->exec_lock) );
}


/* lock of changing status and exec delayproc */
static void delayproc_exec_lock( struct delayproc_maininfo_s *maininfo )
{
  /* Check an argument: maininfo */
  if ( unlikely(NULL == maininfo) ) {
    PERROR( "Invalid an argument: maininfo!\n" );
    return;
  }
  PDEBUG("[%d]<%d>L\n", current->pid, DP_ID(maininfo));
  mutex_lock( &(maininfo->exec_lock) );

  /* for DEBUG */
  dppid[0]  = current->pid;
  /* ---- */
}


/* unlock of changing status and exec delayproc */
static void delayproc_exec_unlock( struct delayproc_maininfo_s *maininfo )
{
  /* Check an argument: maininfo */
  if ( unlikely(NULL == maininfo) ) {
    PERROR( "Invalid an argument: maininfo!\n" );
    return;
  }
  mutex_unlock( &(maininfo->exec_lock) );
  PDEBUG("[%d]<%d>U\n", current->pid, DP_ID(maininfo));

  /* for DEBUG */
  dppid[1]  = current->pid;
  /* ---- */
}


/* Get delayproc status (internal function). */
inline unsigned char delayproc_do_get_status_lock( struct delayproc_maininfo_s *maininfo )
{
  unsigned long irq_flg = 0L;
  unsigned char status = 0;

  spin_lock_irqsave( &maininfo->lock, irq_flg ); /* Lock--> */
  status = DP_STATUS( maininfo );
  spin_unlock_irqrestore( &maininfo->lock, irq_flg ); /* <--Unlock */

/*   PTRACE( " = %d", status ); */
  return (status);
}


/* Get delayproc status (arg: dpinfo). */
inline unsigned char get_delayproc_status( struct delayproc_info_s *dpinfo )
{
  /* Check an argument: dpinfo */
  if ( unlikely(NULL == dpinfo) ) {
    PERROR( "Invalid an argument: dpinfo!\n" );
    return (0);
  }
  return delayproc_do_get_status_lock( dpinfo->main );
}


/* Get delayproc status (arg: device number). */
inline unsigned char get_delayproc_status_dev( dev_t dev )
{
  struct delayproc_maininfo_s *maininfo = delayproc_get_maininfo( dev );

  if ( NULL == maininfo ) {
    return (DELAYPROC_STATUS_SLEEP);
  }
  return delayproc_do_get_status_lock( maininfo );
}


/* Set delayproc status (internal function). */
static inline void delayproc_do_set_status_lock( struct delayproc_maininfo_s *maininfo, unsigned char status )
{
  unsigned long irq_flg = 0L;
  PTRACE( " = %d", status );

  spin_lock_irqsave( &maininfo->lock, irq_flg ); /* Lock--> */
  DP_STATUS(maininfo) = status;
  spin_unlock_irqrestore( &maininfo->lock, irq_flg ); /* <--Unlock */
}


/* Set delayproc status (arg: dpinfo). */
static inline void delayproc_set_status( struct delayproc_info_s *dpinfo, unsigned char status )
{
  /* Check an argument: dpinfo */
  if ( unlikely(NULL == dpinfo) ) {
    PERROR( "Invalid an argument: dpinfo!\n" );
    return;
  }
  delayproc_do_set_status_lock( dpinfo->main, status );
}


/* Set delayproc status (arg: device number). */
static inline void delayproc_set_status_dev( dev_t dev, unsigned char status )
{
  struct delayproc_maininfo_s *maininfo = delayproc_get_maininfo( dev );

  if ( unlikely(NULL == maininfo) ) {
    return;
  }
  delayproc_do_set_status_lock( maininfo, status );
}


/* Check executing delayproc [STANDBY] (arg: dpinfo). */
static inline int is_delayproc( struct delayproc_info_s *dpinfo )
{
  return ( DELAYPROC_STATUS_STANDBY == get_delayproc_status(dpinfo) );
}


/* Check delayproc status [STANDBY] (arg: device number). */
inline int is_delayproc_dev( dev_t dev )
{
  return ( DELAYPROC_STATUS_STANDBY == get_delayproc_status_dev(dev) );
}


/* Check delayproc status [RUN] (arg: device number). */
inline int is_delayproc_run_dev( dev_t dev )
{
  return ( DELAYPROC_STATUS_RUN == get_delayproc_status_dev(dev) );
}


/* Set delayproc status [STANDBY] (arg: dpinfo). */
static inline void set_delayproc( struct delayproc_info_s *dpinfo )
{
  delayproc_set_status( dpinfo, DELAYPROC_STATUS_STANDBY );
}


/* Set delayproc status [STANDBY] (arg: device number). */
static inline void set_delayproc_dev( dev_t dev )
{
  delayproc_set_status_dev( dev, DELAYPROC_STATUS_STANDBY );
}


/* Check and set delayproc status [STANDBY] (arg: device number). */
static void test_and_set_delayproc_dev( dev_t dev )
{
  struct delayproc_maininfo_s *maininfo = delayproc_get_maininfo( dev );

  if ( unlikely(NULL == maininfo) ) {
    return;
  }

  /* Set delayproc status when SLEEP */
  if ( DELAYPROC_STATUS_SLEEP == delayproc_do_get_status_lock(maininfo) ) {
    delayproc_do_set_status_lock( maininfo, DELAYPROC_STATUS_STANDBY );
  }
}


/* Clear delayproc status [SLEEP] (arg: dpinfo). */
static inline void clr_delayproc( struct delayproc_info_s *dpinfo )
{
  delayproc_set_status( dpinfo, DELAYPROC_STATUS_SLEEP );
}


/* Clear delayproc status [SLEEP] (arg: device number). */
static inline void clr_delayproc_dev( dev_t dev )
{
  delayproc_set_status_dev( dev, DELAYPROC_STATUS_SLEEP );
}


/* Set delayproc exec type (internal function). */
static inline void delayproc_do_set_type_lock( struct delayproc_maininfo_s *maininfo, unsigned char type )
{
  unsigned long irq_flg = 0L;
  PTRACE( " = %d", type );

  spin_lock_irqsave( &maininfo->lock, irq_flg ); /* Lock-->*/
  DP_TYPE(maininfo) = type;
  spin_unlock_irqrestore( &maininfo->lock, irq_flg ); /* <--Unlock */
}


/* Set delayproc exec type (arg: dpinfo). */
static inline void delayproc_set_type( struct delayproc_info_s *dpinfo, unsigned char type )
{
  /* Check an argument: dpinfo */
  if ( unlikely(NULL == dpinfo) ) {
    PERROR( "Invalid an argument: dpinfo!\n" );
    return;
  }
  delayproc_do_set_type_lock( dpinfo->main, type );
}


/* Set delayproc exec type (arg: device number). */
static inline void delayproc_set_type_dev( dev_t dev, unsigned char type )
{
  struct delayproc_maininfo_s *maininfo = delayproc_get_maininfo( dev );

  if ( unlikely(NULL == maininfo) ) {
    return;
  }
  delayproc_do_set_type_lock( maininfo, type );
}


/* Get delayproc exec type (internal function). */
static inline unsigned char delayproc_do_get_type_lock( struct delayproc_maininfo_s *maininfo )
{
  unsigned char type = 0;
  unsigned long irq_flg = 0L;

  spin_lock_irqsave( &maininfo->lock, irq_flg ); /* Lock-->*/
  type = DP_TYPE( maininfo );
  spin_unlock_irqrestore( &maininfo->lock, irq_flg ); /* <--Unlock */

/*   PTRACE( " = %d", type ); */
  return (type);
}


/* Get delayproc exec type (arg: dpinfo). */
inline unsigned char get_delayproc_type( struct delayproc_info_s *dpinfo )
{
  /* Check an argument: dpinfo */
  if ( unlikely(NULL == dpinfo) ) {
    PERROR( "Invalid an argument: dpinfo!\n" );
    return (DELAYPROC_TYPE_NONE);
  }
  return delayproc_do_get_type_lock( dpinfo->main );
}


/* Get delayproc exec type (arg: device number). */
static inline unsigned char delayproc_get_type_dev( dev_t dev )
{
  struct delayproc_maininfo_s *maininfo = delayproc_get_maininfo( dev );

  if ( NULL == maininfo ) {
    return (DELAYPROC_TYPE_NONE);
  }
  return delayproc_do_get_type_lock( maininfo );
}


/* Clear delayproc exec type. */
static inline void clr_delayproc_type( struct delayproc_info_s *dpinfo )
{
  delayproc_set_type( dpinfo, DELAYPROC_TYPE_NONE );
}


/* Clear delayproc exec type (arg: device number). */
static inline void clr_delayproc_type_dev( dev_t dev )
{
  delayproc_set_type_dev( dev, DELAYPROC_TYPE_NONE );
}


/* Check exec type is SysSync or not (arg: device number). */
static inline unsigned char is_delayproc_syssync_dev( dev_t dev )
{
  return (DELAYPROC_TYPE_SYSSYNC == delayproc_get_type_dev(dev));
}


/* Wakeup pccardmgr. */
static inline void delayproc_cont_cardmgr( void )
{
  PTRACE();
  SET_PCMCIA_INSERT_EVENT_MUTE( 0 );
}


/* Check dirty and RT_ON, and change status and type. */
static void delayproc_change_status( struct delayproc_maininfo_s *maininfo, unsigned int major )
{
  unsigned long irq_flg = 0L;
  PTRACE();

  if ( ! chk_rton(major) && ! chk_all_delayproc_dirty(major) ) {
    spin_lock_irqsave( &maininfo->lock, irq_flg ); /* Lock--> */
    PDEBUG( "status: RUN -> SLEEP\n" );
    DP_STATUS(maininfo) = DELAYPROC_STATUS_SLEEP;

    PDEBUG( "type -> NONE\n" );
    DP_TYPE(maininfo) = DELAYPROC_TYPE_NONE;
    spin_unlock_irqrestore( &maininfo->lock, irq_flg ); /* <--Unlock */

    /* Wakeup pccardmgr. */
    delayproc_cont_cardmgr();
  } else {
    spin_lock_irqsave( &maininfo->lock, irq_flg ); /* Lock--> */
    PDEBUG( "status: RUN -> STANDBY\n" );
    DP_STATUS(maininfo) = DELAYPROC_STATUS_STANDBY;

    PDEBUG( "type -> NONE\n" );
    DP_TYPE(maininfo) = DELAYPROC_TYPE_NONE;
    spin_unlock_irqrestore( &maininfo->lock, irq_flg ); /* <--Unlock */
  }
}


/* Increment delayproc buffering counts. */
inline void inc_delayproc_buf_cnt( struct delayproc_info_s *dpinfo )
{
  unsigned long irq_flg = 0L;

  /* Check an argument: dpinfo */
  if ( unlikely(NULL == dpinfo) ) {
    PERROR( "Invalid an argument: dpinfo!\n" );
    return;
  }

  spin_lock_irqsave( &dpinfo->main->lock, irq_flg ); /* Lock--> */
  dpinfo->buf_cnt++;
  spin_unlock_irqrestore( &dpinfo->main->lock, irq_flg ); /* <--Unlock */

  PTRACE( "(%d)", dpinfo->buf_cnt );
}


/* Decrement delayproc buffering counts. */
inline void dec_delayproc_buf_cnt( struct delayproc_info_s *dpinfo )
{
  unsigned long irq_flg = 0L;

  /* Check an argument: dpinfo */
  if ( unlikely(NULL == dpinfo) ) {
    PERROR( "Invalid an argument: dpinfo!\n" );
    return;
  }

  spin_lock_irqsave( &dpinfo->main->lock, irq_flg ); /* Lock--> */
  if ( dpinfo->buf_cnt < 1 ) {
    if ( dpinfo->params.size_usr ) {
      /* Not called clr_delayproc_params() */
      PERROR( "buffer count panic(%d)!\n", dpinfo->buf_cnt );
    }
    dpinfo->buf_cnt = 0;
  } else {
    dpinfo->buf_cnt--;
  }
  spin_unlock_irqrestore( &dpinfo->main->lock, irq_flg ); /* <--Unlock */

  PTRACE( "(%d)", dpinfo->buf_cnt );
}


/* Clear delayproc buffering counts. */
static inline void delayproc_clear_buf_cnt( struct delayproc_info_s *dpinfo )
{
  unsigned long irq_flg = 0L;

  /* Check an argument: dpinfo */
  if ( unlikely(NULL == dpinfo) ) {
    PERROR( "Invalid an argument: dpinfo!\n" );
    return;
  }

  spin_lock_irqsave( &dpinfo->main->lock, irq_flg ); /* Lock--> */
  dpinfo->buf_cnt = 0;
  spin_unlock_irqrestore( &dpinfo->main->lock, irq_flg ); /* <--Unlock */
}


/* Get delayproc buffering counts. */
inline unsigned short get_delayproc_buf_cnt( struct delayproc_info_s *dpinfo )
{
  unsigned long irq_flg = 0L;
  unsigned short buf_cnt = 0;

  /* Check an argument: dpinfo */
  if ( unlikely(NULL == dpinfo) ) {
    PERROR( "Invalid an argument: dpinfo!\n" );
    return (0);
  }

  spin_lock_irqsave( &dpinfo->main->lock, irq_flg ); /* Lock--> */
  buf_cnt = dpinfo->buf_cnt;
  spin_unlock_irqrestore( &dpinfo->main->lock, irq_flg ); /* <--Unlock */

  return (buf_cnt);
}


/* Wait on delayproc requests (arg: delayproc info). */
void wait_delayproc_req( struct delayproc_info_s *dpinfo )
{
  unsigned char type = 0;
  unsigned long count = 0;
  unsigned long timeout = DELAYPROC_WAITTIME;
  PTRACE( " timeout=%ld", timeout );

  /* for TEST */
  PDPTEST( "%s start %u\n", __FUNCTION__, jiffies_to_msecs(jiffies) );

  /* Check an argument: dpinfo */
  if ( unlikely(NULL == dpinfo) ) {
    PERROR( "Invalid an argument: dpinfo!\n" );
    return;
  }

  /* Get type. */
  type = get_delayproc_type( dpinfo );

  /* Check waitlist. */
  if ( DELAYPROC_TYPE_SYSSYNC != type ) {
    while ( !list_empty(&dpinfo->rq_list) ) {
      timeout = schedule_timeout( timeout );
      if ( timeout ) continue;
      count++;
      if ( count > DELAYPROC_TIMEOUT ) { /* About 10sec */
	goto TIMEOUT;
      }
      /* Reset timer. */
      timeout = DELAYPROC_WAITTIME;
    }
  } else {
    /* Wait SyncSystemDelayProc. */
    struct list_head *head = &dpinfo->rq_list;
    struct list_head *walk = NULL;
    list_for_each( walk, head ) {
      struct request *rq = list_entry( walk, struct request, waitlist );
      if ( rq_is_dirent(rq) ) {
	timeout = schedule_timeout( timeout );
	if ( !timeout ) {
	  count++;
	  if ( count > DELAYPROC_TIMEOUT ) { /* About 10sec */
	    goto TIMEOUT;
	  }

	  /* Reset timer. */
	  timeout = DELAYPROC_WAITTIME;
	}
	PDEBUG("Rewind waitlist\n");
	walk = head; /* Rewind list. */
      } else {
	PDEBUG( " ## NOT DIRENT rq found!\n" );
      }
    }
  }

  /* Wake up. */
  PDEBUG( "### WAKEUP! rq_list is empty. ###\n" );

  /* Check dirty and RT_ON, and change status and type.
   *  NOTICE: ExecDelayProc only. Otherwise, it's called at sync_delayproc(). */
  if ( DELAYPROC_TYPE_NORMAL == type ) {
    delayproc_change_status( dpinfo->main, MAJOR(dpinfo->rdev) );
  }

  /* for TEST */
  PDPTEST( "%s end %u\n", __FUNCTION__, jiffies_to_msecs(jiffies) );
  return;

 TIMEOUT:
  PERROR( "%s TIMEOUT!!(%ld-%u)\n", __FUNCTION__, count, jiffies_to_msecs(jiffies) );

  /* Clear wait list and status. */
  INIT_LIST_HEAD( &dpinfo->rq_list );
  if ( DELAYPROC_TYPE_NORMAL == type ) {
    delayproc_change_status( dpinfo->main, MAJOR(dpinfo->rdev) );
  }
}


/* Wait on delayproc requests (arg: device number). */
static inline void wait_delayproc_req_dev( dev_t pdev )
{
  dev_t rdev = delayproc_pdev2rdev( pdev );
  struct delayproc_info_s *dpinfo = delayproc_get_info( rdev );

  wait_delayproc_req( dpinfo );
}


/* Execute function of exception inode list. */
static inline unsigned char inode_is_exception( struct inode *inode, unsigned int major )
{
  int i = 0;
  unsigned char retval = 0;
  struct delayproc_maininfo_s *maininfo = DP_MAININFO(major);
  dp_except_inode_t *list = maininfo->excp_list;

  while ( list[i] != NULL ) {
    retval = list[i]( inode );
    PDEBUG( "excp_list[%d] = %d(%ld)\n", i, retval, inode->i_ino );
    if ( retval ) return retval;
    i++;
  }

  return (0);
}


/* Check inode by device MAJOR number. */
static inline unsigned char delayproc_chk_inode_majornum( struct list_head *sb_list, unsigned int major )
{
  struct inode *inode = NULL;
  PTRACE( " major=%d", major );

  list_for_each_entry( inode, sb_list, i_list ) {
    PDEBUG( " ** %d:%d(%s:%ld:%lX)\n", MAJOR(inode->i_rdev), MINOR(inode->i_rdev), (inode->i_state&I_LOCK)?"LOCK":"Unlock", inode->i_ino, inode->i_state );
    if ( inode_is_exception(inode, major) ) continue;
    if ( MAJOR(inode->i_rdev) == major ) {
      return (1);
    }
  }
  return (0);
}


/* Get inode by device number. */
static inline struct inode *delayproc_get_inode_devnum( struct list_head *sb_list, dev_t dev )
{
  struct inode *inode = NULL;
  unsigned int major = MAJOR(dev);
  PTRACE( " dev=%d:%d", major, MINOR(dev) );

  list_for_each_entry( inode, sb_list, i_list ) {
    PDEBUG( " ** %d:%d(%s:%ld:%lX)\n", MAJOR(inode->i_rdev), MINOR(inode->i_rdev), (inode->i_state&I_LOCK)?"LOCK":"Unlock", inode->i_ino, inode->i_state );
    if ( inode_is_exception(inode, major) ) continue;
    if ( inode->i_rdev == dev ) {
      return (inode);
    }
  }
  return (NULL);
}


/* Check that inode lists of super block have exception inode or not. */
static inline unsigned char delayproc_chk_except_inode( struct list_head *sb_list, unsigned int major )
{
  if ( !list_empty(sb_list) ) { /* Check inode list. */
    struct inode *inode = NULL;
    list_for_each_entry( inode, sb_list, i_list ) {
      if ( !inode_is_exception(inode, major) ) { /* Found an inode for delayproc. */
	PDEBUG( "** %d:%d(%ld)\n", MAJOR(inode->i_rdev), MINOR(inode->i_rdev), inode->i_ino );
	return (1);
      }
    }
  }
  return (0);
}


/* Check that inode lists of block device's super block have dirty inode(device number). */
static inline unsigned char delayproc_bdev_sblist_have_dirty_dev( struct list_head *sb_list, dev_t pdev )
{
  if ( !list_empty(sb_list) ) { /* Check inode list. */
    /* Check inode device number. */
    if ( delayproc_get_inode_devnum(sb_list, pdev) ) {
      return (1);
    }
  }
  return (0);
}


/* Check that inode lists of block device's super block have dirty inode(major number). */
static inline unsigned char delayproc_bdev_sblist_have_dirty_major( struct list_head *sb_list, unsigned int major )
{
  if ( !list_empty(sb_list) ) { /* Check inode list. */
    /* Check inode device MAJOR number. */
    if ( delayproc_chk_inode_majornum(sb_list, major) ) {
      return (1);
    }
  }
  return (0);
}


/* CheckDirty */
unsigned char chk_delayproc_dirty( dev_t pdev )
{
  dev_t rdev = delayproc_pdev2rdev( pdev );
  struct delayproc_info_s *dpinfo = delayproc_get_info( rdev );
  struct super_block *sb = NULL;
  unsigned long status = 0;
  unsigned short buf_cnt = 0;
  unsigned int major = MAJOR(pdev);
  PTRACE( " (%d:%d)", major, MINOR(pdev) );

  /* for TEST */
  PDPTEST( " ChkDirty start %u\n", jiffies_to_msecs(jiffies) );

  /* Check dpinfo. */
  if ( unlikely(NULL == dpinfo) ) {
    PWARNING( "Getting dpinfo is failed!\n" );
    goto NOT_DIRTY;
  }

  /* Get delayproc status. */
  status = get_delayproc_status( dpinfo );
  if ( DELAYPROC_STATUS_SLEEP == status ) {
    PDEBUG( "delayproc is SLEEPING. Return NOT_DIRTY(%d).\n", MINOR(pdev) );
    goto NOT_DIRTY;
  }

  /* Check buffering count. */
  /*  CAUTION: The case of "buf_cnt != 0 && status == SLEEP" exists! */
  buf_cnt = get_delayproc_buf_cnt( dpinfo );
  if ( 0 != buf_cnt ) {
    PDEBUG( "buf_cnt = %d. Return DIRTY(%d).\n", buf_cnt, MINOR(pdev) );
    goto DIRTY;
  }

  /* Check request list for waiting. */
  if ( !list_empty(&dpinfo->rq_list) ) {
    PDEBUG( "rq_list isn't empty. Return DIRTY(%d).\n", MINOR(pdev) );
    goto DIRTY;
  }

  /** Check inode(sb) dirty list. **/
  spin_lock( &sb_lock ); /* Lock--> */
  list_for_each_entry_reverse( sb, &super_blocks, s_list ) {
    /* Check the same device or not. */
    if ( pdev == sb->s_dev ) {
      /* Check s_io inode list. */
      if ( delayproc_chk_except_inode(&sb->s_io, major) ) {
	PDEBUG( "inode dirty(s_io)(%d)\n", MINOR(pdev) );
	goto DIRTY_LOCK;
      }

      /* Check s_dirty inode list. */
      if ( delayproc_chk_except_inode(&sb->s_dirty, major) ) {
	PDEBUG( "inode dirty(s_dirty)(%d)\n", MINOR(pdev) );
	goto DIRTY_LOCK;
      }
      /* Check block device's super block. */
    } else if ( sb_is_blkdev_sb(sb) ) {
      /* Check s_dirty inode list. */
      if ( delayproc_bdev_sblist_have_dirty_dev(&sb->s_dirty, pdev) ) {
	PDEBUG( "bdev inode dirty(s_dirty)(%d)\n", MINOR(pdev) );
	goto DIRTY_LOCK;
      }

      /* Check s_io inode list. */
      if ( delayproc_bdev_sblist_have_dirty_dev(&sb->s_io, pdev) ) {
	PDEBUG( "bdev inode dirty(s_io)(%d)\n", MINOR(pdev) );
	goto DIRTY_LOCK;
      }
    }
  }

  /* NOT dirty(and not error) */
  spin_unlock( &sb_lock ); /* <--Unlock */

 NOT_DIRTY:
  PDEBUG( "NOT dirty(%d)\n", MINOR(pdev) );
  PDPTEST( " ChkDirty end %u\n", jiffies_to_msecs(jiffies) ); /* for TEST */
  return (DELAYPROC_NOT_DIRTY);

  /* Dirty */
 DIRTY_LOCK:
  spin_unlock( &sb_lock ); /* <--Unlock */

 DIRTY:
  PDPTEST( " ChkDirty end %u\n", jiffies_to_msecs(jiffies) ); /* for TEST */
  return (DELAYPROC_DIRTY);
}


/* CheckAllDelayProcDirty */
unsigned char chk_all_delayproc_dirty( unsigned int major )
{
  struct delayproc_maininfo_s *maininfo = DP_MAININFO(major);
  struct list_head *head = NULL;
  struct delayproc_info_s *walk = NULL;
  struct super_block *sb = NULL;
  unsigned long irq_flg = 0L;
  PTRACE( " major=%d", major );

  /* for TEST */
  PDPTEST( " ChkAllDirty start %u\n", jiffies_to_msecs(jiffies) );

  /* Check delayproc main info. */
  if ( unlikely(NULL == maininfo) ) {
    PWARNING( "Getting main dpinfo is failed!\n" );
    goto NOT_DIRTY;
  }

  /* Check delayproc status. */
  if ( DELAYPROC_STATUS_SLEEP == delayproc_do_get_status_lock(maininfo) ) {
    PDEBUG( "delayproc is SLEEPING. Return NOT_DIRTY.\n" );
    goto NOT_DIRTY;
  }

  /** Check request list and buffering count. **/
  spin_lock_irqsave( &maininfo->lock, irq_flg ); /* Lock--> */
  head = &DP_DEVLIST(maininfo);
  if ( list_empty(head) ) { /* Check minor list. */
    PDEBUG( "dpinfo[%d]->minor_list is empty.\n", major );
    spin_unlock_irqrestore( &maininfo->lock, irq_flg ); /* <--Unlock */
    goto NOT_DIRTY;
  }

  list_for_each_entry( walk, head, dev_list ) {
    if ( !list_empty(&walk->rq_list) ) { /* Check request list. */
      PDEBUG( "rq_list isn't empty. Return DIRTY.\n" );
      spin_unlock_irqrestore( &maininfo->lock, irq_flg ); /* <--Unlock */
      goto DIRTY;
    }
    if ( 0 != walk->buf_cnt ) { /* Check buffer count. */
      PDEBUG( "buf_cnt = %d. Return DIRTY.\n", walk->buf_cnt );
      spin_unlock_irqrestore( &maininfo->lock, irq_flg ); /* <--Unlock */
      goto DIRTY;
    }
  }
  spin_unlock_irqrestore( &maininfo->lock, irq_flg ); /* <--Unlock */

  /** Check inode(sb) dirty list. **/
  spin_lock( &sb_lock ); /* Lock--> */
  list_for_each_entry_reverse( sb, &super_blocks, s_list ) { /* All super block entries */
    /* Check the same major number. */
    if ( major == MAJOR(sb->s_dev) ) {
      /* Check s_io inode list. */
      if ( delayproc_chk_except_inode(&sb->s_io, major) ) {
	PDEBUG( "inode dirty(s_io %d:%d)\n", major, MINOR(sb->s_dev) );
	goto DIRTY_LOCK;
      }

      /* Check s_dirty inode list. */
      if ( delayproc_chk_except_inode(&sb->s_dirty, major) ) {
	PDEBUG( "inode dirty(s_dirty %d:%d)\n", major, MINOR(sb->s_dev) );
	goto DIRTY_LOCK;
      }

      /* Check block device's super block. */
    } else if ( sb_is_blkdev_sb(sb) ) {
      /* Check s_dirty inode list. */
      if ( delayproc_bdev_sblist_have_dirty_major(&sb->s_dirty, major) ) {
	PDEBUG( "bdev inode dirty(s_dirty)\n" );
	goto DIRTY_LOCK;
      }

      /* Check s_io inode list. */
      if ( delayproc_bdev_sblist_have_dirty_major(&sb->s_io, major) ) {
	PDEBUG( "bdev inode dirty(s_io)\n" );
	goto DIRTY_LOCK;
      }
    }
  }

  /* NOT dirty(and not error) */
  spin_unlock( &sb_lock ); /* <--Unlock */

 NOT_DIRTY:
  PDEBUG( "NOT dirty\n" );
  PDPTEST( " ChkAllDirty end %u\n", jiffies_to_msecs(jiffies) ); /* for TEST */
  return (DELAYPROC_NOT_DIRTY);

  /* Dirty */
 DIRTY_LOCK:
  spin_unlock( &sb_lock ); /* <--Unlock */

 DIRTY:
  PDPTEST( " ChkAllDirty end %u\n", jiffies_to_msecs(jiffies) ); /* for TEST */
  return (DELAYPROC_DIRTY);
}


/* Print request list for waiting (for DEBUG). */
static inline void delayproc_print_req_waitlist( struct list_head *head )
{
#if defined(DBG_PRINT_WAITLIST)
  struct request *walk = NULL;
  int i = 0;

  list_for_each_entry( walk, head, waitlist ) {
    printk( "[%d]:%X ", i, (unsigned int)walk->sector );
    i++;
    if ( i > 5 ) {
      printk( "\n" );
      return;
    }
  }
  if ( i ) printk( "\n" );
#endif /* DBG_PRINT_WAITLIST */

  return;
}


/* Add a delayproc request to wait list. */
inline void add_delayproc_req_waitlist( struct delayproc_info_s *dpinfo, struct request *req )
{
  unsigned long irq_flg = 0L;

  /* Check arguments. */
  if ( unlikely(NULL == dpinfo || NULL == req) ) {
    PERROR( "Invalid arguments!\n" );
    return;
  }

  /* Add a delayproc request. */
  spin_lock_irqsave( &dpinfo->main->lock, irq_flg ); /* Lock--> */
  list_add_tail( &req->waitlist, &dpinfo->rq_list );
  spin_unlock_irqrestore( &dpinfo->main->lock, irq_flg ); /* <--Unlock */

  PTRACE( " sector=%X", (unsigned int)req->sector );
  delayproc_print_req_waitlist( &dpinfo->rq_list ); /* for DEBUG */

  return;
}


/* Delete a delayproc request from wait list. */
inline void del_delayproc_req_waitlist( struct delayproc_info_s *dpinfo, struct request *req )
{
  unsigned long irq_flg = 0L;

  /* Check arguments. */
  if ( unlikely(NULL == dpinfo || NULL == req) ) {
    PERROR( "Invalid arguments!\n" );
    return;
  }
  PTRACE( " sector=%X", (unsigned int)req->sector );

  /* Delete a delayproc request. */
  spin_lock_irqsave( &dpinfo->main->lock, irq_flg ); /* Lock--> */
  list_del( &req->waitlist );
  spin_unlock_irqrestore( &dpinfo->main->lock, irq_flg ); /* <--Unlock */

  delayproc_print_req_waitlist( &dpinfo->rq_list ); /* for DEBUG */

  return;
}


/* Core function of delayproc daemon */
static void delayprocd( struct work_struct *work )
{
  struct delayproc_info_s *dpinfo =
    container_of(work, struct delayproc_info_s, delayprocd);
  struct request_queue *q = NULL;
  PTRACE();

  /* Set task flag. */
  set_task_delayprocd( current );

  /* Check dpinfo. */
  if ( unlikely(NULL == dpinfo) ) {
    PERROR( "Getting dpinfo is failed!\n" );
    goto EXIT;
  }

  /* Get request queue. */
  q = dpinfo->q;

  /* Dispatch force. */
  if ( likely(q && q->elevator->ops->elevator_force_delayproc_fn) ) {
    q->elevator->ops->elevator_force_delayproc_fn( q );
    wait_delayproc_req( dpinfo );
  } else {
    PERROR( "I/O scheduler is invalid!\n" );
  }

 EXIT:
  PDEBUG( "<<<< END %s >>>>\n", __FUNCTION__ );

  /* Clear task flag. */
  clr_task_delayprocd( current );

  delayproc_exec_unlock( dpinfo->main ); /* <-- MUTEX Unlock @exec_delayproc() */
}


/* Wake up delayproc daemon. */
static inline void delayproc_wakeup_work( dev_t pdev )
{
  dev_t rdev = delayproc_pdev2rdev( pdev );
  struct delayproc_info_s *dpinfo = delayproc_get_info( rdev );
  PTRACE();

  /* Check dpinfo. */
  if ( unlikely(NULL == dpinfo) ) {
    PWARNING( "Getting dpinfo is failed!\n" );
    return;
  }

  /* Wakeup */
  queue_work( dpinfo->main->works, &dpinfo->delayprocd );
}


/* Call sync_inode of block device's super block. */
static inline void delayproc_bdev_sblist_sync_inode_dev( struct list_head *sb_list, struct writeback_control *wbc, dev_t pdev )
{
  if ( !list_empty(sb_list) ) {
    struct inode *tmp_inode = delayproc_get_inode_devnum( sb_list, pdev );

    while ( tmp_inode ) {
      PDEBUG( " blkdev_sb\n" );
      sync_inode( tmp_inode, wbc );

      if ( list_empty(sb_list) ) break;
      tmp_inode = delayproc_get_inode_devnum( sb_list, pdev );
    }
  }
}


/* Prepare real-time on: flush delayproc buffers. */
void prepare_rton( unsigned int major )
{
  struct delayproc_maininfo_s *maininfo = DP_MAININFO(major);
  struct list_head *head = NULL;
  struct list_head *walk = NULL;
  struct super_block *sb = NULL;
  struct writeback_control wbc = {
    .bdi = NULL,
    .sync_mode = WB_SYNC_ALL,
    .older_than_this = 0,
    .nr_to_write = 1024, /* 4MB XXX */
    .for_kupdate = 0,
    .range_cyclic = 0,
    .range_start = 0,
    .range_end = LLONG_MAX,
  };
  PTRACE( " (major=%d)", major );

  /* for TEST */
  PDPTEST( " prepare_rton start %u\n", jiffies_to_msecs(jiffies) );

  /* Check delayproc main info. */
  if ( unlikely(NULL == maininfo) ) {
    PWARNING( "Getting main dpinfo is failed!\n" );
    return;
  }

  lock_rton(major); /* RT_ON Lock --> */

  /* Check RT_ON or delayproc. */
  if ( chk_rton(major) || delayproc_do_get_status_lock(maininfo) ) {
    PDEBUG( "RT_ON or delayproc!\n" );
    unlock_rton(major); /* <-- RT_ON Unlock */
    return;
  }

  /** Execute delayproc for each device. **/
  head = &DP_DEVLIST(maininfo);
  list_for_each( walk, head ) {
    struct delayproc_info_s *dpinfo
      = list_entry( walk, struct delayproc_info_s, dev_list );
    struct request_queue *q = NULL;
    dev_t rdev = 0, pdev = 0;

    /* Check dpinfo. */
    if ( unlikely(NULL == dpinfo) ) {
      PERROR( "Minor list panic!\n" );
      break;
    }

    /* Set variables. */
    q = dpinfo->q;
    rdev = dpinfo->rdev;
    pdev = delayproc_rdev2pdev( rdev );

    /* Check request queue. */
    if ( unlikely(NULL == q) ) {
      PWARNING( "Getting request queue is failed!(%d:%d)\n", major, MINOR(rdev) );
      continue;
    }

    /** Make requests (ref. writeback_inodes()@fs/fs-writeback.c). **/
    spin_lock( &sb_lock ); /* Lock[sb]--> */
    list_for_each_entry_reverse( sb, &super_blocks, s_list ) {
      /* Check super block using delayproc or not. */
      if ( pdev != sb->s_dev && !sb_is_blkdev_sb(sb) )
	continue;

      if ( !list_empty(&sb->s_dirty) || !list_empty(&sb->s_io) ) {
	/* we're making our own get_super here */
	sb->s_count++;
	spin_unlock( &sb_lock ); /* <--Unlock[sb] */
	/*
	 * If we can't get the readlock, there's no sense in
	 * waiting around, most of the time the FS is going to
	 * be unmounted by the time it is released.
	 */
	if ( down_read_trylock(&sb->s_umount) ) { /* Lock[mount]--> */
	  if ( sb->s_root ) {
	    if ( sb_is_blkdev_sb(sb) ) {
	      /* ref. sync_sb_inodes()@2.6.23 */
	      if ( list_empty(&sb->s_io) || !delayproc_get_inode_devnum(&sb->s_io, pdev)) {
		PDEBUG( " =>>list_splice\n" );
		list_splice_init( &sb->s_dirty, &sb->s_io );
	      }
	      PDEBUG( "----- blkdev sync_inode(s_io) ------>>>>\n" );
	      delayproc_bdev_sblist_sync_inode_dev( &sb->s_io, &wbc, pdev );
	      PDEBUG( "<<<<----- blkdev sync_inode(s_io) ------\n" );
	    } else {
	      /* !sb_is_blkdev_sb(sb) */
	      spin_lock( &inode_lock ); /* Lock[inode]--> */
	      PDEBUG( "--------- generic_sync_sb_inodes --------->>>>\n" );
	      generic_sync_sb_inodes( sb, &wbc );
	      PDEBUG( "<<<<------- generic_sync_sb_inodes -----------\n" );
	      spin_unlock( &inode_lock ); /* <--Unlock[inode] */
	    }
	  }
	  up_read( &sb->s_umount ); /* <--Unlock[mount] */
	}
	spin_lock( &sb_lock ); /* Lock[sb]--> */
      }
      if ( wbc.nr_to_write <= 0 )
	break;
    }
    spin_unlock( &sb_lock ); /* <--Unlock[sb] */
  }

  unlock_rton(major); /* <-- RT_ON Unlock */

  /* for TEST */
  PDPTEST( " prepare_rton end %u\n", jiffies_to_msecs(jiffies) );
}


/* Core function of ExecDelayProc, SyncDelayProc and SyncSystemDelayProc. */
static int delayproc_do_exec( struct delayproc_info_s *dpinfo )
{
  dev_t pdev = 0;
  dev_t rdev = 0;
  struct super_block *sb = NULL;
  struct writeback_control wbc = {
    .bdi = NULL,
    .sync_mode = WB_SYNC_NONE,
    .older_than_this = 0,
    .nr_to_write = 1024, /* 4MB XXX */
    .for_kupdate = 0,
    .range_cyclic = 0,
    .range_start = 0,
    .range_end = LLONG_MAX,
    .for_delayproc = 1,
  };
  PTRACE();

  /* Check an argument: dpinfo */
  if ( unlikely(NULL == dpinfo) ) {
    PERROR( "Invalid an arugment: dpinfo!\n" );
    return (-EINVAL);
  }
  rdev = dpinfo->rdev;
  pdev = delayproc_rdev2pdev( rdev );

  /** Make requests (ref. writeback_inodes()@fs/fs-writeback.c). **/
  spin_lock( &sb_lock ); /* Lock[sb]--> */
  list_for_each_entry_reverse( sb, &super_blocks, s_list ) {
    /* Check super block using delayproc or not. */
    if ( pdev != sb->s_dev && !sb_is_blkdev_sb(sb) )
      continue;

    if ( !list_empty(&sb->s_dirty) || !list_empty(&sb->s_io) ) {
      /* we're making our own get_super here */
      sb->s_count++;
      spin_unlock( &sb_lock ); /* <--Unlock[sb] */
      /*
       * If we can't get the readlock, there's no sense in
       * waiting around, most of the time the FS is going to
       * be unmounted by the time it is released.
       */
      if ( down_read_trylock(&sb->s_umount) ) { /* Lock[mount]--> */
	if ( sb->s_root ) {
	  if ( sb_is_blkdev_sb(sb) ) {
	    /* ref. sync_sb_inodes()@2.6.23 */
	    if ( list_empty(&sb->s_io) || !delayproc_get_inode_devnum(&sb->s_io, pdev)) {
	      PDEBUG( " =>>list_splice\n" );
	      list_splice_init( &sb->s_dirty, &sb->s_io );
	    }

	    PDEBUG( "----- blkdev sync_inode(s_io) ------>>>>\n" );
	    delayproc_bdev_sblist_sync_inode_dev( &sb->s_io, &wbc, pdev );
	    PDEBUG( "<<<<----- blkdev sync_inode(s_io) ------\n" );
	  } else {
	    /* !sb_is_blkdev_sb(sb) */
	    spin_lock( &inode_lock ); /* Lock[inode]--> */
	    PDEBUG( "--------- generic_sync_sb_inodes --------->>>>\n" );
	    generic_sync_sb_inodes( sb, &wbc );
	    PDEBUG( "<<<<------- generic_sync_sb_inodes -----------\n" );
	    spin_unlock( &inode_lock ); /* <--Unlock[inode] */
	  }
	}
	up_read( &sb->s_umount ); /* <--Unlock[mount] */
      }
      spin_lock( &sb_lock ); /* Lock[sb]--> */
    }
    if ( wbc.nr_to_write <= 0 )
      break;
  }
  spin_unlock( &sb_lock ); /* <--Unlock[sb] */

  return (0);
}


/* ExecDelayProc */
int exec_delayproc( dev_t pdev )
{
  int retval = 0;
  dev_t rdev = delayproc_pdev2rdev(pdev);
  struct delayproc_info_s *dpinfo = NULL;
  struct delayproc_maininfo_s *maininfo = NULL;
  PTRACE( " (rdev=%d:%d)", MAJOR(rdev), MINOR(rdev) );

  /* for TEST */
  PDPTEST( " ExecDelayProc start %u\n", jiffies_to_msecs(jiffies) );

  /* Check RT_ON. */
  if ( chk_rton(MAJOR(rdev)) ) {
    PDPTEST( "RT_ON! ExecDelayProc is canceled!\n" );
    return (0);
  }

  /* Get a delayproc info (minor element). */
  dpinfo = delayproc_get_info( rdev );
  if ( unlikely(NULL == dpinfo) ) {
    PWARNING( "Getting dpinfo is failed!\n" );
    return (-EINVAL);
  }

  /* Get delayproc main info. */
  maininfo = dpinfo->main;
  if ( unlikely(NULL == maininfo) ) {
    PWARNING( "Getting dp maininfo is failed!\n" );
    return (-EINVAL);
  }

  /* Check already running delayproc. */
  if ( get_delayproc_status(dpinfo) == DELAYPROC_STATUS_RUN ) {
    PDPTEST("Already RUN or SYNC! ExecDelayProc is canceled!\n" );
    return (0);
  }

  delayproc_exec_lock( maininfo ); /* MUTEX Lock --> */

  /* Set status: RUN */
  PDEBUG( "<<<< status -> RUN >>>>\n" );
  delayproc_set_status( dpinfo, DELAYPROC_STATUS_RUN );

  /* Set delayproc exec type: NORMAL */
  PDEBUG( "<<<< type => Exec >>>>\n" );
  delayproc_set_type( dpinfo, DELAYPROC_TYPE_NORMAL );

  /* Set delayproc order. */
  delayproc_init_order( dpinfo );

  /* Execute delayproc. */
  retval = delayproc_do_exec( dpinfo );

  /* Start work queue (dispatch force, unplug device and wait requests). */
  delayproc_wakeup_work( pdev );

  /* for TEST */
  PDPTEST( " ExecDelayProc end %u\n", jiffies_to_msecs(jiffies) );
	
  /* CAUTION: wait_delayproc_req() changes status and type. */
  return (retval);
}


/* Core function of SyncDelayProc and SyncSystemDelayProc. */
static int delayproc_do_sync( struct delayproc_info_s *dpinfo, struct request_queue *q )
{
  int retval = 0;

  /* Check dpinfo. */
  if ( unlikely(NULL == dpinfo || NULL == q) ) {
    PERROR( "Getting dpinfo or request queue is failed!\n" );
    return (-EINVAL);
  }

  /* Execute delayproc. */
  retval = delayproc_do_exec( dpinfo );

  /* Dispatch force. */
  PDEBUG( "BEFORE dispatch force(%d)\n", retval );
  if ( likely(q->elevator->ops->elevator_force_delayproc_fn) ) {
    q->elevator->ops->elevator_force_delayproc_fn( q );
    wait_delayproc_req( dpinfo );
  }
  PDEBUG( "AFTER dispatch force - BEFORE unplug_device\n" );

  return (retval);
}


/* SyncDelayProc & SyncSystemDelayProc */
void sync_delayproc( unsigned int major, int meta_only )
{
  struct delayproc_maininfo_s *maininfo = NULL;
  struct list_head *head = NULL;
  struct list_head *walk = NULL;
  unsigned long loop = 0;
  PTRACE( " (major=%d, meta_only=%d)", major, meta_only );

  /* for TEST */
  PDPTEST( " SyncDelayProc start %u\n", jiffies_to_msecs(jiffies) );

  /* Check RT_ON. */
  if ( chk_rton(major) && !meta_only ) {
    PDPTEST( "RT_ON! %s is canceled!\n", __FUNCTION__ );
    return;
  }

  /* Get delayproc main info. */
  maininfo = DP_MAININFO( major );
  if ( unlikely(NULL == maininfo) ) {
    PWARNING( "Getting dp maininfo is failed!\n" );
    return;
  }

  delayproc_exec_lock( maininfo ); /* MUTEX Lock --> */

  /* Set task flag. */
  set_task_delayprocd( current );

  /* Set status: RUN */
  PDEBUG( "<<<< status -> RUN >>>>\n" );
  delayproc_do_set_status_lock( maininfo, DELAYPROC_STATUS_RUN );

  /* Set delayproc exec type: SYNC or SYSSYNC */
  if ( meta_only ) {
    PDEBUG( "<<<< type => SysSync >>>>\n" );
    delayproc_do_set_type_lock( maininfo, DELAYPROC_TYPE_SYSSYNC );
  } else {
    PDEBUG( "<<<< type => Sync >>>>\n" );
    delayproc_do_set_type_lock( maininfo, DELAYPROC_TYPE_SYNC );
  }

  /** Execute delayproc for each device. **/
  loop = 0;
  head = &DP_DEVLIST(maininfo);
 MAINLOOP:
  list_for_each( walk, head ) {
    struct delayproc_info_s *dpinfo
      = list_entry( walk, struct delayproc_info_s, dev_list );
    struct request_queue *q = NULL;
    dev_t rdev = 0, pdev = 0;
    unsigned long retry = 0;
    int retval = 0;

    /* Check dpinfo. */
    if ( unlikely(NULL == dpinfo) ) {
      PERROR( "Minor list panic!\n" );
      break;
    }

    /* Set variables. */
    q = dpinfo->q;
    rdev = dpinfo->rdev;
    pdev = delayproc_rdev2pdev( rdev );

    /* Set delayproc order. */
    if ( meta_only ) {
      set_delayproc_order( dpinfo, 2 );
    } else {
      delayproc_init_order( dpinfo );
    }

    /* Check request queue. */
    if ( unlikely(NULL == q) ) {
      PWARNING( "Getting request queue is failed!(%d:%d)\n",
		major, MINOR(rdev) );
      continue;
    }

    /* CheckDirty */
    if ( !chk_delayproc_dirty(pdev) ) continue;

  RETRY:
    retval = delayproc_do_sync( dpinfo, q );

    /* Check retry or not. */
    if ( !meta_only && chk_delayproc_dirty(pdev) ) {
      retry++;
      if ( retry > DELAYPROC_MAXLOOP ) {
	/* Switch context. */
	PWARNING( "retry count of exec delayproc is over %d(%ld)\n", DELAYPROC_MAXLOOP, retry );
	cond_resched( );
	continue; /* list_for_each(dev_list) */
      }
      PDEBUG("RETRY!\n");

      /* Reset delayproc order. */
#if defined(CONFIG_DELAYPROC_WRITE_ORDER)
      if ( get_delayproc_order(dpinfo) >= DELAYPROC_ORDER_MAXNR ) {
	if ( meta_only ) {
	  set_delayproc_order( dpinfo, 2 );
	} else {
	  delayproc_init_order( dpinfo );
	}
      }
#endif /* CONFIG_DELAYPROC_WRITE_ORDER */
      goto RETRY;
    }
  } /* the end of list_for_each */

  /* Paranoia */
  if ( !meta_only && chk_all_delayproc_dirty(major) ) {
    PDEBUG("Rewind dpinfo list\n");
    walk = head; /* Rewind list. */
    loop++;
    if ( loop > DELAYPROC_MAXLOOP ) {
      /* Timeout */
      PERROR( "SyncDelayProc TIMEOUT(CheckAllDirty)!\n" );
      cond_resched( );
    } else {
      goto MAINLOOP;
    }
  }

  /* Check dirty and RT_ON, and change status and type. */
  delayproc_change_status( maininfo, major );

  /* Clear task flag. */
  clr_task_delayprocd( current );

  delayproc_exec_unlock( maininfo ); /* <-- MUTEX Unlock */

  /* for TEST */
  PDPTEST( " SyncDelayProc end %u\n", jiffies_to_msecs(jiffies) );
  PDEBUG( "<<<< END %s(%d-%d) >>>>\n", __FUNCTION__, major, meta_only );
}


/* Test and clear delayproc status. */
static void delayproc_test_and_clear_status( unsigned int major )
{
  PTRACE();

  /* Check and clear status. */
  if ( ! chk_rton(major) && ! chk_all_delayproc_dirty(major) ) {
    dev_t rdev = MKDEV(major, 0);
    clr_delayproc_dev( rdev );
    clr_delayproc_type_dev( rdev );

    /* Wakeup pccardmgr. */
    delayproc_cont_cardmgr();
  }
}


/* Clear delayrproc parameters. */
static inline void delayproc_clr_params( struct delayproc_info_s *dpinfo )
{
  /* Check dpinfo. */
  if ( unlikely(NULL == dpinfo) ) {
    PERROR( "Getting dpinfo is failed!\n" );
    return;
  }

  /* Clear a device's delayproc parameters. */
  memset( &(dpinfo->params), 0, sizeof(struct delayproc_param_s) );

  /* Clear buffering counts. */
  delayproc_clear_buf_cnt( dpinfo );

  /* Clear delayproc status and type. */
  delayproc_do_set_status_lock( dpinfo->main, DELAYPROC_STATUS_SLEEP );
  delayproc_do_set_type_lock( dpinfo->main, DELAYPROC_TYPE_NONE );
}


/* Clear a device's delayproc parameters. */
inline void clr_delayproc_params( dev_t pdev )
{
  dev_t rdev = delayproc_pdev2rdev( pdev );
  struct delayproc_info_s *dpinfo = delayproc_get_info( rdev );
  PTRACE( " (%d:%d)", MAJOR(pdev), MINOR(pdev) );

  /* Check dpinfo. */
  if ( unlikely(NULL == dpinfo) ) {
    PERROR( "Getting dpinfo is failed!\n" );
    return;
  }

  /* Clear parameters. */
  delayproc_clr_params( dpinfo );

  /* Wakeup pccardmgr. */
  delayproc_cont_cardmgr();
}


/* Clear all devices' delayproc parameters. */
inline void clr_all_delayproc_params( unsigned int major )
{
  struct delayproc_maininfo_s *maininfo = DP_MAININFO(major);
  struct delayproc_info_s *walk = NULL;	
  PTRACE();

  /* Check maininfo. */
  if ( unlikely(NULL == maininfo) ) {
    PERROR( "Getting delayproc info is failed!\n" );
    return;
  }

  /* Clear all devices' dpinfo->params. */
  list_for_each_entry( walk, &DP_DEVLIST(maininfo), dev_list ) {
    delayproc_clr_params( walk );
    INIT_LIST_HEAD( &(walk->rq_list) );
  }

  /* Wakeup pccardmgr. */
  delayproc_cont_cardmgr();
}


/* Destroy delayproc. */
void destroy_delayproc( dev_t pdev )
{
  dev_t rdev = delayproc_pdev2rdev( pdev );
  struct delayproc_info_s *dpinfo = delayproc_get_info( rdev );
  PTRACE( " (%d:%d)", MAJOR(pdev), MINOR(pdev) );

  /* Check dpinfo. */
  if ( NULL == dpinfo ) {
    /* Don't print an error message for devices not using delayproc. */
    PDEBUG( "Getting dpinfo is failed.\n" );
    return;
  }

  /* Execute SyncDelayProc. */
  if ( chk_delayproc_dirty(pdev) ) {
    struct delayproc_maininfo_s *maininfo = dpinfo->main;

    /* Set status: RUN */
    PDEBUG( "<<<< status -> RUN >>>>\n" );
    delayproc_do_set_status_lock( maininfo, DELAYPROC_STATUS_RUN );

    /* Set delayproc exec type: SYNC or SYSSYNC */
    PDEBUG( "<<<< type => Sync >>>>\n" );
    delayproc_do_set_type_lock( maininfo, DELAYPROC_TYPE_SYNC );

    set_task_delayprocd( current );
    delayproc_do_sync( dpinfo, dpinfo->q );
    clr_task_delayprocd( current );
  }

  /* Clear parameters. */
  if ( chk_delayproc_dirty(pdev) ) {
    PDEBUG( "dirty -> clr_params\n" );
    delayproc_clr_params( dpinfo );
  }
}


/* Make proc filesystem entry. */
static int delayproc_read_proc( char *buf, char **start, off_t offset, int length, int *eof, void *data )
{
  int len = 0;
  struct delayproc_maininfo_s *maininfo = data;
  struct delayproc_info_s *walk = NULL;

  /* Check an argument: data */
  if ( NULL == maininfo ) {
    return (len);
  }

  /* Print version number. */
  len += sprintf( buf+len, "delayproc ver. %s\n", RTCTRL_CORE_VERSION );

  /* Print delayproc main info. */
  len += sprintf( buf+len, "id:\t%d\n", DP_ID(maininfo) );
  len += sprintf( buf+len, "status:\t%d\n", DP_STATUS(maininfo) );
  len += sprintf( buf+len, "type:\t%d\n", DP_TYPE(maininfo) );
  len += sprintf( buf+len, "lk:\t%d\n", (int)atomic_read(&(maininfo->exec_lock.count)) );

  /* for DEBUG */
  len += sprintf( buf+len, "lpid:\t%d\n", dppid[0]);
  len += sprintf( buf+len, "upid:\t%d\n", dppid[1]);
  /* ---- */

  /* Print minor list. */
  list_for_each_entry( walk, &DP_DEVLIST(maininfo), dev_list ) {
    len += sprintf( buf+len, "\n*card[%d:%d](%d:%d)\n",
		    MAJOR(walk->rdev), MINOR(walk->rdev),
		    MAJOR(walk->pdev), MINOR(walk->pdev) );
    len += sprintf( buf+len, " buf_cnt:\t%d\n", walk->buf_cnt );
    len += sprintf( buf+len, " size_sys:\t%ld\n", walk->params.size_sys );
    len += sprintf( buf+len, " size_usr:\t%ld\n", walk->params.size_usr );
    len += sprintf( buf+len, " nr_sys:\t%d\n", walk->params.nr_sys );
    len += sprintf( buf+len, " nr_usr:\t%d\n", walk->params.nr_usr );
#if defined(CONFIG_DELAYPROC_WRITE_ORDER)
    len += sprintf( buf+len, " order:\t%d\n", walk->order );
#endif /* CONFIG_DELAYPROC_WRITE_ORDER */
  }

  return (len);
}


/* InitDelayProc */
int init_delayproc( dev_t pdev, struct delayproc_param_s *params )
{
  dev_t rdev = delayproc_pdev2rdev( pdev );
  struct delayproc_info_s *dpinfo = delayproc_get_info( rdev );
  unsigned long irq_flg = 0L;
  PTRACE( " (%d:%d)", MAJOR(pdev), MINOR(pdev) );

  /* Check dpinfo. */
  if ( unlikely(NULL == dpinfo) ) {
    PERROR( "Getting dpinfo is failed!\n" );
    return (-EINVAL);
  }

  /* Check delayproc parameters */
  if ( params->size_sys > DELAYPROC_NRWRITE
       || params->size_usr > DELAYPROC_NRWRITE
       || params->size_usr < 1
       || params->nr_sys < 0
       || params->nr_usr <= 0
       || (params->size_sys < 1 && params->nr_sys > 0) ) {

    PERROR( "Invalid delayproc parameters!\n" );
    PDEBUG( "size_sys=%ld, size_usr=%ld, nr_sys=%d, nr_usr=%d\n",
	    params->size_sys, params->size_usr,
	    params->nr_sys, params->nr_usr );
    return (-EINVAL);
  }

  /* Set a device's delayproc parameters. */
  spin_lock_irqsave( &dpinfo->lock, irq_flg ); /* Lock--> */
  memcpy( &dpinfo->params, params, sizeof(struct delayproc_param_s) );
  spin_unlock_irqrestore( &dpinfo->lock, irq_flg ); /* <--Unlock */

  return (0);
}


/* Init delayproc info. */
int init_delayproc_info( struct delayproc_info_s **pdpinfo, dev_t rdev, dp_except_inode_t *excp_list )
{
  unsigned int major = MAJOR(rdev);
  unsigned int minor = MINOR(rdev);
  unsigned long irq_flg = 0L;
  struct delayproc_maininfo_s *maininfo = NULL;
  struct proc_dir_entry *pentry = NULL; /* proc entry */
  char procpath[20];
  PTRACE();

  /* Check an argument: pdpinfo */
  if ( unlikely(NULL == pdpinfo) ) {
    PERROR( "Invalid dpinfo at %s!\n", __FUNCTION__ );
    return (-EINVAL);
  }

  /* Malloc delayproc info. */
  if ( NULL == DP_MAININFO(major) ) {
    PDEBUG( "kmalloc dp_info[%d]\n", major );
    maininfo = kzalloc( sizeof(struct delayproc_maininfo_s), GFP_KERNEL );
    if ( NULL == maininfo ) {
      PERROR( "kmalloc failed at %s!(%d:%d)\n", __FUNCTION__, major, minor );
      return (-ENOMEM);
    }
    DP_MAININFO(major) = maininfo;

    /* Init main delayproc info. */
    DP_ID(maininfo) = major;
    DP_STATUS(maininfo) = DELAYPROC_STATUS_SLEEP;
    INIT_LIST_HEAD( &DP_DEVLIST(maininfo) );
    atomic_set( &DP_USECNT(maininfo), 1 );
    spin_lock_init( &maininfo->lock );
    maininfo->works = create_workqueue( "delayprocd" );
    delayproc_init_exec_lock( maininfo );
    maininfo->excp_list = excp_list;

    /* Make proc entry. */
    sprintf( procpath, "%s%d", DELAYPROC_PROCNAME, major );
    pentry = create_proc_entry( procpath, 0, 0 );
    if ( pentry ) {
      pentry->read_proc = delayproc_read_proc;
      pentry->data = (void *)maininfo;
    }
  } else { /* Main delayproc info is already malloced. */
    maininfo = DP_MAININFO( major );
    atomic_inc( &DP_USECNT(maininfo) );
    PDEBUG( "use_cnt = %d\n", atomic_read(&DP_USECNT(maininfo)) );
  }

  /** Init a element for minor device. **/
  /* Check already init or not. */
  if ( NULL != delayproc_get_info(rdev) ) {
    PERROR( "Already init minor info(%d:%d)!\n", major, minor );
    return (-EINVAL);
  }

  /* Malloc delayproc minor info. */
  *pdpinfo = kzalloc( sizeof(struct delayproc_info_s), GFP_KERNEL );
  if ( unlikely(NULL == *pdpinfo) ) {
    PERROR( "kmalloc failed at %s!(%d:%d)\n", __FUNCTION__, major, minor );
    return (-ENOMEM);
  }
  PDEBUG( "kmalloc dpinfo(%d)\n", minor );

  /* Init delayproc minor info.
   *  CAUTION: dpinfo->q is set after this function! */
  (*pdpinfo)->rdev = rdev;
  (*pdpinfo)->pdev = delayproc_rdev2pdev( rdev );
  INIT_LIST_HEAD( &((*pdpinfo)->dev_list) );
  INIT_LIST_HEAD( &((*pdpinfo)->rq_list ) );
  (*pdpinfo)->main = maininfo;
  spin_lock_init( &((*pdpinfo)->lock) );
  INIT_WORK( &((*pdpinfo)->delayprocd), delayprocd );

  /* Add delayproc minor info to minor device list. */
  spin_lock_irqsave( &maininfo->lock, irq_flg ); /* Lock--> */
  list_add_tail( &((*pdpinfo)->dev_list), &DP_DEVLIST(maininfo) );
  spin_unlock_irqrestore( &maininfo->lock, irq_flg ); /* <--Unlock */

  return (0);
}


/* Exit delayproc info. */
void exit_delayproc_info( struct delayproc_info_s *dpinfo )
{
  unsigned int major = 0;
  unsigned int minor = 0;
  unsigned long irq_flg = 0L;
  struct list_head *head = NULL;
  struct list_head *walk = NULL;
  struct delayproc_maininfo_s *maininfo = NULL;
  int use_cnt = 0;
  char procpath[20];
  PTRACE();

  /* Check an argument: dpinfo. */
  if ( unlikely(NULL == dpinfo) ) {
    PERROR( "Invalid dpinfo at %s!\n", __FUNCTION__ );
    return;
  }

  /** Delete the element of a minor device info. **/
  maininfo = dpinfo->main;
  major = MAJOR( dpinfo->rdev );
  minor = MINOR( dpinfo->rdev );
  BUG_ON( DP_MAININFO(major) != maininfo );

  /* main loop */
  spin_lock_irqsave( &maininfo->lock, irq_flg ); /* Lock--> */
  head = &DP_DEVLIST(maininfo);
  list_for_each( walk, head ) {
    struct delayproc_info_s *entry = list_entry( walk, struct delayproc_info_s, dev_list );

    /* Search the element from minor list. */
    PDEBUG( "entry->rdev = %d:%d\n", MAJOR(entry->rdev), MINOR(entry->rdev) );
    if ( entry == dpinfo ) {
      list_del( walk );
      kfree( entry );
      entry = NULL;
      PDEBUG( "delete(kfree) element(%d:%d)\n", major, minor );

      /* Decrement use count of delayproc info. */
      use_cnt = atomic_dec_return( &DP_USECNT(maininfo) );
      PDEBUG( "use_cnt = %d\n", use_cnt );

      if ( ! list_empty(head) ) {
	PDEBUG( "minor_list isn't empty\n" );
	spin_unlock_irqrestore( &maininfo->lock, irq_flg ); /* <--Unlock */
	return;
      }
      break;
    }
  } /* the end of list_for_each */

  spin_unlock_irqrestore( &maininfo->lock, irq_flg );	/* <--Unlock */
  PDEBUG( "use_cnt = %d\n", use_cnt );
  BUG_ON( 0 != use_cnt );
  BUG_ON( 0 == list_empty(head) );

  /* Kill delayprocd. */
  destroy_workqueue( maininfo->works );

  /* Remove proc entry. */
  sprintf( procpath, "%s%d", DELAYPROC_PROCNAME, major );
  remove_proc_entry( procpath, 0 );

  /* kfree dp_info. */
  PDEBUG( "kfree dp_info[%d]\n", major );
  kfree( maininfo );
  DP_MAININFO(major) = NULL;
}
#else /* !CONFIG_DELAYPROC */
static void test_and_set_delayproc_dev( dev_t rdev ) {return;}
static void delayproc_test_and_clear_status( unsigned int major ) {return;}
void prepare_rton( unsigned int major ) {return;}
static void delayproc_exec_lock( struct delayproc_maininfo_s *maininfo ) {return;}
static void delayproc_exec_unlock( struct delayproc_maininfo_s *maininfo ) {return;}

# define delayproc_get_maininfo(dev) (NULL)
# define delayproc_do_get_status_lock(maininfo) (0)
# define is_delayproc_dev(pdev)	(0)
# define is_delayproc_run_dev(pdev)	(0)
# define is_delayproc_syssync_dev(pdev)	(0)

#endif /* CONFIG_DELAYPROC */ /* ------ the end of delayproc function ------ */


/* ------------ the function for other modules ------------ */


/*
 * Choose device number for RT control
 *	dev_t 	s_dev	[in]	: device number in "super block"
 *	dev_t	r_dev	[in]	: device number in "device file's inode"
 *	
 * 	 CAUTION: Be careful to put arguments to this function!
 * 	  If you wrong to put them, we cannot get device number exactly!
 */
inline dev_t choose_rtctrl_dev( dev_t s_dev, dev_t r_dev )
{
  if ( 0 != r_dev ) {
    return (r_dev); /* device file's inode */
  }

  if ( unlikely(0 == s_dev) ) {
    PERROR( "Both device numbers are zero!\n" );
    return MKDEV(-1, -1);
  }

  return (s_dev); /* normal file or directory's inode */
}


/* Check RT_ON and delayproc for P2FAT-FS. */
/*  CAUTION: Need to call unlock_rton() after calling this function! */
inline int check_rt_status( struct super_block *sb )
{
  unsigned char av_check = 0;
  unsigned char status = 0;
  dev_t dev = sb->s_dev;
  PTRACE();

  av_check = chk_rton(MAJOR(dev));
#if defined(CONFIG_DELAYPROC)
  status = get_delayproc_status_dev(dev);
#endif /* CONFIG_DELAYPROC */

  PFSDBG( "check RT status(%d:%d) -> %d\n", av_check, status, ((!av_check && !status)?1:0) );

  /* RT_ON -> 0, RT_OFF -> 1 */
  return ( (!av_check && !status) ? 1 : 0 );
}


/* Check delayproc for P2FAT-FS(directory write-reserved). */
inline int check_delay_status( struct super_block *sb )
{
  unsigned char av_check = 0;
  unsigned char status = 0;

  av_check = chk_rton(MAJOR(sb->s_dev));
#if defined(CONFIG_DELAYPROC)
  status = get_delayproc_status_dev(sb->s_dev);
#endif /* CONFIG_DELAYPROC */

  PFSDBG( "check delay status(%d:%d) -> %d\n", av_check, status, ((av_check || (!av_check && !status))?1:0) );

  /* RT_OFF and delayproc -> 0, RT_ON or normal -> 1 */
  return ( (av_check || (!av_check && !status)) ? 1 : 0 );
}


/* Check RT_ON and delayproc for VFS(submit I/O). */
unsigned char is_rton( dev_t pdev, umode_t mode, int for_dp, int rt )
{
  /* RT file & delayproc context --> RT_ON */
  /* RT file & NOT delayproc context --> RT_OFF */
  if ( rt ) {
    return (for_dp);
  }

  /* delayproc status == STANDBY --> RT_ON */
  if ( !rt && is_delayproc_dev(pdev) ) {
    return (1);
  }

  /* delayproc status == RUN & NOT delayproc context --> RT_ON */
  if ( !rt && is_delayproc_run_dev(pdev) && 1 != for_dp ) {
    PDEBUG( "for_dp!\n" );
    return (1);
  }

  /* RT status == 1 & NOT executing SyncSystemDelayProc --> RT_ON */
  if ( !rt
       && chk_rton(MAJOR(pdev))
       && !is_delayproc_syssync_dev(pdev) ) {
    PDEBUG( "RT_ON and NOT SyncSystem!\n" );
    return (1);
  }

  /* Executing SyncSystemDelayProc
   * & NOT directory inode & NOT block device inode --> RT_ON */
  if ( !rt
       && is_delayproc_syssync_dev(pdev)
       && (!S_ISDIR(mode) && !S_ISBLK(mode)) ) {
    PDEBUG( "SyncSystem and NOT DIR!\n" );
    return (1);
  }

  /* Otherwise --> RT_OFF */
  return (0);
}


/* Check RT_ON and delayproc for VFS(wait I/O). */
inline unsigned char is_rton4wait( dev_t pdev )
{
  unsigned char status = 0;
	
#if defined(CONFIG_DELAYPROC)
  /* delayproc status != SLEEP --> RT_ON for waiting */
  status = get_delayproc_status_dev( pdev );

#else /* !CONFIG_DELAYPROC */

  /* RT status == 1 --> RT_ON for waiting */
  status = chk_rton( MAJOR(pdev) );
#endif /* CONFIG_DELAYPROC */

  PDEBUG( "status = %d\n", status );
  return (status);
}


/*** Export symbols ***/
EXPORT_SYMBOL(RTCTRL_WARNING);
EXPORT_SYMBOL(RTCTRL_DEBUG);

EXPORT_SYMBOL(choose_rtctrl_dev);
EXPORT_SYMBOL(is_rton);
EXPORT_SYMBOL(is_rton4wait);
EXPORT_SYMBOL(chk_rton);
EXPORT_SYMBOL(lock_rton);
EXPORT_SYMBOL(unlock_rton);

#if defined(CONFIG_DELAYPROC)
EXPORT_SYMBOL(init_delayproc_info);
EXPORT_SYMBOL(exit_delayproc_info);
EXPORT_SYMBOL(is_delayproc_dev);
EXPORT_SYMBOL(is_delayproc_run_dev);
EXPORT_SYMBOL(get_delayproc_status);
EXPORT_SYMBOL(get_delayproc_type);
EXPORT_SYMBOL(add_delayproc_req_waitlist);
EXPORT_SYMBOL(del_delayproc_req_waitlist);
EXPORT_SYMBOL(wait_delayproc_req);
EXPORT_SYMBOL(inc_delayproc_buf_cnt);
EXPORT_SYMBOL(dec_delayproc_buf_cnt);
EXPORT_SYMBOL(get_delayproc_buf_cnt);
#endif /* CONFIG_DELAYPROC */

#if defined(CONFIG_DELAYPROC_WRITE_ORDER)
EXPORT_SYMBOL(set_delayproc_order);
EXPORT_SYMBOL(get_delayproc_order);
#endif /* CONFIG_DELAYPROC_WRITE_ORDER */

/******************** the end of the file "rtctrl_core.c" ********************/
