/*****************************************************************************
 * linux/drivers/p2pf/rtctrl/rtctrl_drv.c
 *
 *   RT Control Driver for kernel 2.6
 *   
 *     Copyright (C) 2003-2010 Panasonic corp.
 *     All Rights Reserved.
 *
 *****************************************************************************/

/***** include header files & define macros *****/
/** header files **/
#include <linux/module.h>		/* for module */
#include <linux/kernel.h>		/* for kernel module */
#include <linux/fs.h>			/* for filesystem operations */
#include <asm/uaccess.h>		/* for verity_area etc */
#include <linux/init.h>			/* for init */
#include <linux/cdev.h>			/* for character driver */
#include <linux/blkdev.h>		/* for block device */
#include <linux/proc_fs.h>		/* for proc filesystem */

/** RT Control header **/
#include <linux/rtctrl.h>

/** module infomation **/
#if defined(MODULE)
MODULE_AUTHOR("Panasonic");
MODULE_SUPPORTED_DEVICE("rtctrl");
MODULE_LICENSE("GPL");
#endif /* MODULE */

/***** definitions *****/
/** constants **/
#define RTCTRL_DEVNAME  "rtctrl"
#define RTCTRL_PROCNAME "fs/ebio" /* for compatibility */

/** variables **/
static int rtctrl_major = RTCTRL_MAJOR;
static struct cdev rtctrl_cdev;
extern unsigned char RTCTRL_WARNING;
extern unsigned char RTCTRL_DEBUG;
extern unsigned char RTCTRL_TEST;
extern unsigned char RTCTRL_PRINT_ONOFF;

/** print messages **/
#define PINFO( fmt, args... )	printk( KERN_INFO "[rtctrl] " fmt, ## args)
#define PERROR( fmt, args... )	printk( KERN_ERR "[rtctrl](E)" fmt, ## args)
#define PWARNING( fmt, args... )	do { if (RTCTRL_WARNING) printk( KERN_WARNING "[rtctrl](W)" fmt, ## args); } while(0)
#define PDEBUG( fmt, args... )	do { if (RTCTRL_DEBUG) printk( KERN_INFO "[rtctrl:l.%d] " fmt, __LINE__, ## args); } while(0)
/** prototype **/


/********************************* function *********************************/

/***************************************************************************
 * rtctrl_ioctl
 *
 * @mfunc
 *		RT Control ioctl
 * @args
 *		struct inode	*inode	[in]	:	devicefile's inode
 *		struct file		*filp	[in]	:	struct file
 *		unsigned int	cmd		[in]	:	ioctl command
 *		unsigned long	arg	[in&out]	:	argument
 * @return
 * 		int
 * 		   0		:	success
 * 		   -EINVAL	:	invalid argument
 * @comment
 *
 **************************************************************************/
static int rtctrl_ioctl( struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg )
{
  int retval = 0;	/* return value */
  dev_t dev = 0;  /* device number */
  unsigned int minor = 0; /* minor device number */

  /* Get device number. */
  dev = inode->i_rdev;
  minor = MINOR( dev );

  /* Check minor device number */
  if ( 0 == minor || RTONARRAY_SIZE <= minor ) {
    PERROR( "Invalid device number(%d)!\n", minor );
    return (-EINVAL);
  }

  /*** main routine ***/
  switch ( cmd ) {

  case RTCTRL_IOC_RTON:
    {
      prepare_rton( minor );
      set_rton( minor );
      PDEBUG( "## RT_ON(dev:%d)\n", minor ); /* for DEBUG */
      break;
    }

  case RTCTRL_IOC_RTOFF:
    {
      clr_rton( minor );
      if (RTCTRL_PRINT_ONOFF) printk( "## RT_OFF\n" ); /* for DEBUG */
      PDEBUG( "## RT_OFF(dev:%d)\n", minor ); /* for DEBUG */
      break;
    }

  case RTCTRL_IOC_GET_RTSTATUS:
    {
      unsigned char status = chk_rton( minor );

      if ( put_user(status, (int __user *)arg) ) {
	PERROR( "put_user failed at RTCTRL_IOC_GET_RTSTATUS!\n" );
	return (-EFAULT);
      }
      PDEBUG( "Get RT_ON status(dev:%d) = %d\n", minor, status ); /* for DEBUG */
      break;
    }

#if defined(CONFIG_DELAYPROC)
  case RTCTRL_IOC_INIT_DELAYPROC:	/* InitDelayProc */
    {
      struct delayproc_param_s params;

      if ( copy_from_user((void *)&params,
			  (void *)arg,
			  sizeof(struct delayproc_param_s)) ) {
	PERROR( "copy_from_user failed at RTCTRL_IOC_INIT_DELAYPROC!\n" );
	return (-EFAULT);
      }

      return init_delayproc( MKDEV(minor, params.minor), &params );
    }

  case RTCTRL_IOC_EXEC_DELAYPROC:	/* ExecDelayProc */
    {
      unsigned int dev_minor = 0;

      /* Get slot number(argument). */
      if ( get_user(dev_minor, (unsigned int __user *)arg) ) {
	PERROR( "get_user failed at RTCTRL_IOC_EXEC_DELAYPROC!\n" );
	return (-EFAULT);
      }

      exec_delayproc( MKDEV(minor, dev_minor) );
      PDEBUG( "*** END ExecDelayProc(%d:%d) ***\n", minor, dev_minor );
      break;
    }

  case RTCTRL_IOC_SYNC_DELAYPROC:	/* SyncDelayProc */
    {
      sync_delayproc( minor, 0 );
      PDEBUG( "*** END SyncDelayProc ***\n" );
      break;
    }

  case RTCTRL_IOC_SYNC_SYS_DELAYPROC:	/* SyncSystemDelayProc */
    {
      sync_delayproc( minor, 1 );
      PDEBUG( "*** END SyncSystemDelayProc ***\n" );
      break;
    }

  case RTCTRL_IOC_CHECK_DELAYPROC_DIRTY:	/* CheckDirty */
    {
      struct rtctrl_status_s info;

      if ( copy_from_user((void *)&info,
			  (void *)arg,
			  sizeof(struct rtctrl_status_s)) ) {
	PERROR( "copy_from_user failed at RTCTRL_IOC_CHECK_DELAYPROC_DIRTY!\n" );
	return (-EFAULT);
      }

      /* Get dirty info. */
      info.status = chk_delayproc_dirty( MKDEV(minor, info.minor) );

      if ( copy_to_user((void *)arg,
			(void *)&info,
			sizeof(struct rtctrl_status_s)) ){
	PERROR( "copy_to_user() failed at RTCTRL_IOC_CHECK_DELAYPROC_DIRTY!\n" );
	return (-EFAULT);
      }
      break;
    }

  case RTCTRL_IOC_CHECK_ALL_DELAYPROC_DIRTY:	/* CheckAllDelayProcDirty */
    {
      unsigned char dirty = 0;

      /* Get dirty info. */
      dirty = chk_all_delayproc_dirty( minor );
      if ( put_user(dirty, (unsigned char __user *)arg) ){
	PERROR( "put_user() failed at RTCTRL_IOC_CHECK_ALL_DELAYPROC_DIRTY!\n" );
	return (-EFAULT);
      }
      break;
    }

  case RTCTRL_IOC_GET_DELAYPROC_STATUS:	/* GetDelayProcStatus */
    {
      unsigned char status = 0;

      /* Get delayproc status. */
      status = get_delayproc_status_dev( MKDEV(minor, 0) );
      if ( put_user(status, (unsigned char __user *)arg) ){
	PERROR( "put_user() failed at RTCTRL_IOC_GET_DELAYPROC_STATUS!\n" );
	return (-EFAULT);
      }
      break;
    }

  case RTCTRL_IOC_CLEAR_DELAYPROC:
    {
      unsigned int dev_minor = 0;

      if ( get_user(dev_minor, (unsigned int __user *)arg) ){
	PERROR( "get_user failed at RTCTRL_IOC_CLEAR_DELAYPROC!\n" );
	return (-EFAULT);
      }

      clr_delayproc_params( MKDEV(minor, dev_minor) );
      break;
    }

  case RTCTRL_IOC_CLEAR_ALL_DELAYPROC:
    {
      clr_all_delayproc_params( minor );
      break;
    }

    /* ioctl for DEBUG and TEST */
  case RTCTRL_IOC_PRINT_TESTMESSAGE:
    {
      print_delayproc_testmessage();
      break;
    }
#endif /* CONFIG_DELAYPROC */

  case RTCTRL_IOC_TOGGLE_DEBUGMODE:
    {
      RTCTRL_DEBUG ^= 1;
      PINFO( "%s mode\n", RTCTRL_DEBUG?"DEBUG":"No-DEBUG" );
      break;
    }

  case RTCTRL_IOC_TOGGLE_TESTMODE:
    {
      RTCTRL_TEST = (unsigned char)arg;
      break;
    }

  case RTCTRL_IOC_TOGGLE_PRINT_ONOFF_MODE:
    {
      RTCTRL_PRINT_ONOFF ^= 1;
      PDEBUG( "%s print On/Off mode\n", RTCTRL_PRINT_ONOFF?"Enable":"Disable" );
      break;
    }

    /* for RT control [user-space filesystem] */
  case RTCTRL_IOC_LOCK_RTON:
    {
      lock_rton( minor );
      break;
    }

  case RTCTRL_IOC_UNLOCK_RTON:
    {
      unlock_rton( minor );
      break;
    }

    /* for delayproc [user-space filesystem] */
  case RTCTRL_IOC_CHECK_RT_STATUS:
    {
      unsigned char retval = 0;
      unsigned char rt_stat = chk_rton(minor);
#if defined(CONFIG_DELAYPROC)
      unsigned char dp_stat = get_delayproc_status_dev( MKDEV(minor,0) );
#else /* !CONFIG_DELAYPROC */
      unsigned char dp_stat = 0;
#endif /* CONFIG_DELAYPROC */

      /* RT_ON -> 0, RT_OFF -> 1 */
      retval = (!rt_stat && !dp_stat) ? 1 : 0;
      PDEBUG( "check RT status(%d:%d) -> %d\n", rt_stat, dp_stat, retval );

      /* Copy to argument from user. */
      if ( put_user(retval, (unsigned char __user *)arg) ){
	PERROR( "put_user() failed at RTCTRL_IOC_CHECK_RT_STATUS!\n" );
	return (-EFAULT);
      }
      break;
    }

  case RTCTRL_IOC_CHECK_DELAY_STATUS:
    {
      unsigned char retval = 0;
      unsigned char rt_stat = chk_rton(minor);
#if defined(CONFIG_DELAYPROC)
      unsigned char dp_stat = get_delayproc_status_dev( MKDEV(minor,0) );
#else /* !CONFIG_DELAYPROC */
      unsigned char dp_stat = 0;
#endif /* CONFIG_DELAYPROC */

      /* RT_OFF and delayproc -> 0, RT_ON or normal -> 1 */
      retval = (rt_stat || (!rt_stat && !dp_stat)) ? 1 : 0;
      PDEBUG( "check delay status(%d:%d) -> %d\n", rt_stat, dp_stat, retval );

      /* Copy to argument from user. */
      if ( put_user(retval, (unsigned char __user *)arg) ){
	PERROR( "put_user() failed at RTCTRL_IOC_CHECK_DELAY_STATUS!\n" );
	return (-EFAULT);
      }
      break;
    }

  case RTCTRL_IOC_IS_RTON4WAIT:
    {
      unsigned char status = 0;

#if defined(CONFIG_DELAYPROC)
      /* delayproc status != SLEEP --> RT_ON for waiting */
      status = get_delayproc_status_dev( MKDEV(minor,0) );

#else /* !CONFIG_DELAYPROC */

      /* RT status == 1 --> RT_ON for waiting */
      status = chk_rton( minor );
#endif /* CONFIG_DELAYPROC */

      PDEBUG( "status = %d\n", status );
      
      /* Copy to argument from user. */
      if ( put_user(status, (unsigned char __user *)arg) ){
	PERROR( "put_user() failed at RTCTRL_IOC_IS_RTON4WAIT!\n" );
	return (-EFAULT);
      }
      break;
    }

  default:
    {
    PERROR( "Unknown ioctl command!(0x%X)\n", cmd );
    retval = -EINVAL;
    }
  } /* the end of switch(cmd) */

  return (retval);
}


/***************************************************************************
 * rtctrl_open
 *
 * @mfunc
 *		Open RT Control driver
 * @args
 *		struct inode	*inode	[in]	:	devicefile's inode
 *		struct file		*filp	[in]	:	struct file
 * @return
 * 		int
 * 		   0		:	success
 * @comment
 *
 **************************************************************************/
static int rtctrl_open( struct inode *inode, struct file *filp )
{
  PDEBUG( "Open(%d:%d)\n", rtctrl_major, MINOR(inode->i_rdev) );
  return (0);
}


/***************************************************************************
 * rtctrl_release
 *
 * @mfunc
 *		Release RT Control driver
 * @args
 *		struct inode	*inode	[in]	:	devicefile's inode
 *		struct file		*filp	[in]	:	struct file
 * @return
 * 		int
 * 		   0		:	success
 * @comment
 *
 **************************************************************************/
static int rtctrl_release( struct inode *inode, struct file *filp )
{
  PDEBUG( "Close(%d:%d)\n", rtctrl_major, MINOR(inode->i_rdev) );
  return (0);
}


/***** file operations *****/
static struct file_operations rtctrl_fops = {
  .owner	= THIS_MODULE,
  .ioctl	= rtctrl_ioctl,		/* rtctrl_ioctl   */
  .open		= rtctrl_open,		/* rtctrl_open    */
  .release	= rtctrl_release,	/* rtctrl_release */
  /* nothing more, fill with NULLs */
};


/***************************************************************************
 * rtctrl_read_proc
 *
 * @mfunc
 *		/proc read function
 * @args
 *		--
 * @return
 * 		int
 * 		 length
 * @comment
 *
 **************************************************************************/
static int rtctrl_read_proc( char *buf, char **start, off_t offset, int length, int *eof, void *data )
{
  int len = 0;
  unsigned int i = 0;

  /* Print version number */
  len += sprintf( buf+len, "RT control driver ver. %s\n\n", RTCTRL_DRV_VERSION );

  /* Print RT_ON/OFF status */
  for ( i = 1; i < RTONARRAY_SIZE; i++ ) {
    unsigned char status = get_rton_status( i );

    if ( ! status ) {
      continue;
    }

    len += sprintf( buf+len, " MAJOR[%04d] = RT_%s(0x%02X)\n",
		    i, (status&RTCTRL_RTON)?"ON":"OFF", status );
  }

  *eof = 1;
  return (len);
}


/***************************************************************************
 * rtctrl_init
 *
 * @mfunc
 *		init RT Control driver
 * @args
 *		void
 * @return
 * 		int
 * 		  0		:	success
 * @comment
 *
 **************************************************************************/
static int __init rtctrl_init( void )
{
  int	retval = 0;	/* return value */
  dev_t devnum = 0;	/* device number */
  struct proc_dir_entry *pentry = NULL; /* proc entry */

  /* Print init Message. */
  PINFO( "RT Control driver ver. %s\n", RTCTRL_DRV_VERSION );

  /* Register major, and accept a dynamic number. */
  if ( rtctrl_major ) {
    devnum = MKDEV(rtctrl_major, 0);
    retval = register_chrdev_region( devnum, DELAYPROC_MAXNR, RTCTRL_DEVNAME );
    if ( unlikely(retval < 0) ) {
      PERROR( "Unable to register %d for rtctrl!\n", rtctrl_major );
      return (retval);
    }
  } else {
    retval = alloc_chrdev_region( &devnum, 0, DELAYPROC_MAXNR, RTCTRL_DEVNAME );
    if ( unlikely(retval < 0) ) {
      PERROR( "alloc_chrdev_region failed(%d)!\n", retval );
      return (retval);
    }

    /* Set dynamic major number. */
    rtctrl_major = MAJOR( devnum );
    PINFO( "Change device major number %d to %d!\n", RTCTRL_MAJOR, rtctrl_major );
  }

  /* Init cdev structure. */
  cdev_init( &rtctrl_cdev, &rtctrl_fops );
  rtctrl_cdev.owner = THIS_MODULE;
  retval = cdev_add( &rtctrl_cdev, devnum, DELAYPROC_MAXNR );
  if ( unlikely(retval < 0) ) {
    PERROR( "cdev_add failed(%d)!\n", retval );
  }

  /* Init rton_info. */
  init_rton_info();

  /* Init delayproc. */
#if defined(CONFIG_DELAYPROC)
  printk( KERN_INFO "[delayprocd] ver. %s\n", RTCTRL_CORE_VERSION );
#endif /* CONFIG_DELAYPROC */

  /* Init debug switch. */
  RTCTRL_DEBUG = 0;

  /* Make proc entry. */
  pentry = create_proc_entry( RTCTRL_PROCNAME, 0, 0 );
  if ( pentry ) {
    pentry->read_proc = rtctrl_read_proc;
  }

  return (0);
}


/***************************************************************************
 * rtctrl_cleanup
 *
 * @mfunc
 *		Clean up RT Control driver
 * @args
 *		void
 * @return
 * 		void
 * @comment
 *
 **************************************************************************/
static void __exit rtctrl_cleanup( void )
{
  PINFO( "Clean up RT Control driver\n" ); /* Message */

  /* Clear rton_info */
  clr_rton_info();

  /* Remove proc entry */
  remove_proc_entry( RTCTRL_PROCNAME, 0 );

  /* unregister the character device driver */
  cdev_del( &rtctrl_cdev );
  unregister_chrdev_region( MKDEV(rtctrl_major, 0), DELAYPROC_MAXNR );

  /* Clear debug switch */
  RTCTRL_DEBUG = 0;
}


/** Export symbols **/
module_init( rtctrl_init );
module_exit( rtctrl_cleanup );


/******************** the end of the file "rtctrl_drv.c" ********************/
