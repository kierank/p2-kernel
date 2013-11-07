/*****************************************************************************
 * linux/drivers/p2pf/p2ioport/p2ioport.c
 *
 *   P2PF I/O port and GPIO access driver
 *   
 *     Copyright (C) 2008-2010 Panasonic corp.
 *     All Rights Reserved.
 *
 *****************************************************************************/
/* $Id: p2ioport.c 10439 2010-11-16 04:47:46Z Noguchi Isao $ */

/***** include header files & define macros *****/
/** header files **/
#include <linux/module.h>  /* for module */
#include <linux/kernel.h>  /* for kernel module */
#include <asm/uaccess.h>   /* for verity_area etc */
#include <linux/init.h>    /* for init */
#include <linux/fs.h>
#include <linux/cdev.h>    /* for character driver */
#include <linux/proc_fs.h> /* for proc filesystem */

/** header **/
#include <linux/p2ioport.h>

/** module infomation **/
#if defined(MODULE)
MODULE_AUTHOR("Panasonic");
MODULE_SUPPORTED_DEVICE("p2ioport");
MODULE_LICENSE("GPL");
#endif /* MODULE */

/***** definitions *****/
/** constants **/
#define P2IOPORT_DEVNAME     "p2ioport"
#define P2IOPORT_DRV_VERSION "1.12"
#define P2IOPORT_PROCNAME    "driver/p2ioport"

enum P2IOPORT_CONST_ENUM {
  P2IOPORT_MAJOR = 245,  /* P2PF I/O port driver's device number */
};

/** variables **/
static int p2ioport_major = P2IOPORT_MAJOR;
static struct cdev p2ioport_cdev;
unsigned char P2IOPORT_WARNING;
unsigned char P2IOPORT_DEBUG;

struct p2ioport_info_s p2ioport_info;


/** print messages **/
#define PINFO( fmt, args... )	printk( KERN_INFO "[p2ioport] " fmt, ## args)
#define PERROR( fmt, args... )	printk( KERN_ERR "[p2ioport](E)" fmt, ## args)
#define PWARNING( fmt, args... )	do { if (P2IOPORT_WARNING) printk( KERN_WARNING "[p2ioport](W)" fmt, ## args); } while(0)
#define PDEBUG( fmt, args... )	do { if (P2IOPORT_DEBUG) printk( KERN_INFO "[p2ioport:l.%d] " fmt, __LINE__, ## args); } while(0)

/** prototype **/
extern void __p2ioport_get_ops( struct p2ioport_operations *ops );
extern int __p2ioport_init_info( struct p2ioport_info_s *info );
extern void __p2ioport_cleanup_info( struct p2ioport_info_s *info );


/********************************* function *********************************/

/***************************************************************************
 * p2ioport_get_dipsw
 **************************************************************************/
inline int p2ioport_get_dipsw( int num, unsigned long *val )
{
  struct p2ioport_operations *ops = p2ioport_info.ops;

  /* check get_dipsw operation. */
  if ( NULL == ops->get_dipsw ) {
    PERROR( "Undefined get_dipsw operation!\n" );
    return (-ENODEV);
  }

  /* check argument: num. */
  if ( num < 0 || p2ioport_info.nr_dipsw <= num ) {
    PERROR( "Invalid argument: num(%d)! (0 to %d)\n",
	    num, p2ioport_info.nr_dipsw );
    return (-EINVAL);
  }

  /* get DIPSW. */
  return ops->get_dipsw( num, val );
}


/***************************************************************************
 * p2ioport_get_led
 **************************************************************************/
inline int p2ioport_get_led( int num, unsigned long *val )
{
  struct p2ioport_operations *ops = p2ioport_info.ops;

  /* check get_led operation. */
  if ( NULL == ops->get_led ) {
    PERROR( "Undefined get_led operation!\n" );
    return (-ENODEV);
  }

  /* check argument: num. */
  if ( num < 0 || p2ioport_info.nr_led <= num ) {
    PERROR( "Invalid argument: num(%d)! (0 to %d)\n",
	    num, p2ioport_info.nr_led );
    return (-EINVAL);
  }

  /* get LED. */
  return ops->get_led( num, val );
}


/***************************************************************************
 * p2ioport_set_led
 **************************************************************************/
inline int p2ioport_set_led( int num, unsigned long val )
{
  struct p2ioport_operations *ops = p2ioport_info.ops;

  /* check set_led operation. */
  if ( NULL == ops->set_led ) {
    PERROR( "Undefined set_led operation!\n" );
    return (-ENODEV);
  }

  /* check argument: num. */
  if ( num < 0 || p2ioport_info.nr_led <= num ) {
    PERROR( "Invalid argument: num(%d)! (0 to %d)\n",
	    num, p2ioport_info.nr_led );
    return (-EINVAL);
  }

  /* set LED. */
  return ops->set_led( num, val );
}


/***************************************************************************
 * p2ioport_clr_led
 **************************************************************************/
inline int p2ioport_clr_led( int num )
{
  struct p2ioport_operations *ops = p2ioport_info.ops;

  /* check clr_led operation. */
  if ( NULL == ops->clr_led ) {
    PERROR( "Undefined clr_led operation!\n" );
    return (-ENODEV);
  }

  /* check argument: num. */
  if ( num < 0 || p2ioport_info.nr_led <= num ) {
    PERROR( "Invalid argument: num(%d)! (0 to %d)\n",
	    num, p2ioport_info.nr_led );
    return (-EINVAL);
  }

  /* clear LED. */
  return ops->clr_led( num );
}


/***************************************************************************
 * p2ioport_toggle_led
 **************************************************************************/
inline int p2ioport_toggle_led( int num, unsigned long val )
{
  struct p2ioport_operations *ops = p2ioport_info.ops;

  /* check toggle_led operation. */
  if ( NULL == ops->toggle_led ) {
    PERROR( "Undefined toggle_led operation!\n" );
    return (-ENODEV);
  }

  /* check argument: num. */
  if ( num < 0 || p2ioport_info.nr_led <= num ) {
    PERROR( "Invalid argument: num(%d)! (0 to %d)\n",
	    num, p2ioport_info.nr_led );
    return (-EINVAL);
  }

  /* toggle LED. */
  return ops->toggle_led( num, val );
}


/***************************************************************************
 * p2ioport_get_rotarysw
 **************************************************************************/
inline int p2ioport_get_rotarysw( int num, unsigned long *val )
{
  struct p2ioport_operations *ops = p2ioport_info.ops;

  /* check get_rotarysw operation. */
  if ( NULL == ops->get_rotarysw ) {
    PERROR( "Undefined get_rotarysw operation!\n" );
    return (-ENODEV);
  }

  /* check argument: num. */
  if ( num < 0 || p2ioport_info.nr_rotarysw <= num ) {
    PERROR( "Invalid argument: num(%d)! (0 to %d)\n",
	    num, p2ioport_info.nr_rotarysw );
    return (-EINVAL);
  }

  /* get rotary switch. */
  return ops->get_rotarysw( num, val );
}


/***************************************************************************
 * p2ioport_get_version
 **************************************************************************/
inline int p2ioport_get_version( int num, unsigned long long *val )
{
  struct p2ioport_operations *ops = p2ioport_info.ops;

  /* check get_version operation. */
  if ( NULL == ops->get_version ) {
    PERROR( "Undefined get_version operation!\n" );
    return (-ENODEV);
  }

  /* check argument: num. */
  if ( num < 0 || p2ioport_info.nr_device <= num ) {
    PERROR( "Invalid argument: num(%d)! (0 to %d)\n",
	    num, p2ioport_info.nr_device );
    return (-EINVAL);
  }

  /* get version info. */
  return ops->get_version( num, val );
}


/***************************************************************************
 * p2ioport_get_operations
 **************************************************************************/
void p2ioport_get_operations( struct p2ioport_operations *ops )
{
  if ( NULL == ops ) {
    PERROR("Invalid argument: p2ioport_operations is NULL!\n");
    return;
  }
  memset( ops, 0, sizeof(struct p2ioport_operations) );
  __p2ioport_get_ops(ops);
}


/***************************************************************************
 * p2ioport_get_vport
 **************************************************************************/
int p2ioport_get_vport( int vport, int *val )
{
  struct p2ioport_operations *ops = p2ioport_info.ops;

  /* check get_vport operation. */
  if ( NULL == ops->get_vport ) {
    PERROR( "Undefined get_vport operation!\n" );
    return (-ENODEV);
  }

  /* get vport */
  return ops->get_vport( vport, val );
}


/***************************************************************************
 * p2ioport_set_vport
 **************************************************************************/
int p2ioport_set_vport( int vport, int val )
{
  struct p2ioport_operations *ops = p2ioport_info.ops;

  /* check set_vport operation. */
  if ( NULL == ops->set_vport ) {
    PERROR( "Undefined set_vport operation!\n" );
    return (-ENODEV);
  }

  /* set ports */
  return ops->set_vport( vport, val );
}


/***************************************************************************
 * p2ioport_lock_vport
 **************************************************************************/
int p2ioport_lock_vport( int vport , int *val)
{
    struct p2ioport_operations *ops = p2ioport_info.ops;

    /* check lock_vport operation. */
    if ( NULL == ops->lock_vport ){
        *val = 0;               /* SUCCESS */
        return 0;
    }

    /* lock ports */
    return ops->lock_vport( vport, val);
}

/***************************************************************************
 * p2ioport_unlock_vport
 **************************************************************************/
int p2ioport_unlock_vport( int vport )
{
  struct p2ioport_operations *ops = p2ioport_info.ops;

  /* check unlock_vport operation. */
  if ( NULL == ops->unlock_vport )
    return 0;

  /* unlock ports */
  return ops->unlock_vport( vport );
}


/***************************************************************************
 * p2ioport_set_int_callback
 **************************************************************************/
int p2ioport_set_int_callback( p2ioport_cb_t *cbdat )
{
  struct p2ioport_cbentry_s *new = NULL;

  /* Malloc cb list entry. */
  new = kzalloc( sizeof(struct p2ioport_cbentry_s), GFP_KERNEL );
  if ( unlikely(NULL == new) ) {
    PERROR( "kmalloc failed at %s!\n", __FUNCTION__ );
    return (-ENOMEM);
  }

  /* Init and copy cb entry. */
  INIT_LIST_HEAD( &(new->cbentry_list) );
  new->func = cbdat->func;
  new->data = cbdat->data;

  /* Add cb list. */
  list_add_tail( &(new->cbentry_list), &(p2ioport_info.cb_list[cbdat->num]) );

  return (0);
}


/***************************************************************************
 * p2ioport_ioctl
 **************************************************************************/
static int p2ioport_ioctl( struct inode *inode, struct file *filp,
			   unsigned int cmd, unsigned long arg )
{
  int retval = 0;	/* return value */

  /*** main routine ***/
  switch ( cmd ) {

    /** get DIPSW. **/
  case P2IOPORT_IOC_GET_DIPSW:
    {
      struct p2ioport_val_s info;

      /* get and init values. */
      if ( copy_from_user((void *)&info,
			  (void *)arg,
			  sizeof(struct p2ioport_val_s)) ) {
	PERROR( "copy_from_user failed at P2IOPORT_IOC_GET_DIPSW!\n" );
	return (-EFAULT);
      }
      info.val = 0;

      /* get DIPSW. */
      retval = p2ioport_get_dipsw( info.num, &(info.val) );

      /* put values. */
      if ( copy_to_user((void *)arg,
			(void *)&info,
			sizeof(struct p2ioport_val_s)) ) {
	PERROR( "copy_to_user failed at P2IOPORT_IOC_GET_DIPSW!\n" );
	return (-EFAULT);
      }

      PDEBUG( "Get DIPSW num=%d, val=0x%lX\n", info.num, info.val );
      break;
    } /** the end of get DIPSW **/
    
    
    /** get LED. **/
  case P2IOPORT_IOC_GET_LED:
    {
      struct p2ioport_val_s info;

      /* get and init values. */
      if ( copy_from_user((void *)&info,
			  (void *)arg,
			  sizeof(struct p2ioport_val_s)) ) {
	PERROR( "copy_from_user failed at P2IOPORT_IOC_GET_LED!\n" );
	return (-EFAULT);
      }
      info.val = 0;

      /* get LED. */
      retval = p2ioport_get_led( info.num, &(info.val) );
      
      /* put values. */
      if ( copy_to_user((void *)arg,
			(void *)&info,
			sizeof(struct p2ioport_val_s)) ) {
	PERROR( "copy_to_user failed at P2IOPORT_IOC_GET_LED!\n" );
	return (-EFAULT);
      }

      PDEBUG( "Get LED num=%d, val=0x%lX\n", info.num, info.val );
      break;
    } /** the end of get LED **/


    /** set LED. **/
  case P2IOPORT_IOC_SET_LED:
    {
      struct p2ioport_val_s info;

      /* get and init values. */
      if ( copy_from_user((void *)&info,
			  (void *)arg,
			  sizeof(struct p2ioport_val_s)) ) {
	PERROR( "copy_from_user failed at P2IOPORT_IOC_SET_LED!\n" );
	return (-EFAULT);
      }

      /* set LED. */
      retval = p2ioport_set_led( info.num, info.val );

      PDEBUG( "Set LED num=%d, val=0x%lX\n", info.num, info.val );
      break;
    } /** the end of set LED **/


    /** clear LED. **/
  case P2IOPORT_IOC_CLR_LED:
    {
      int num = 0;

      /* get value. */
      if ( get_user(num, (unsigned int __user *)arg) ) {
	PERROR( "get_user failed at P2IOPORT_IOC_CLR_LED!\n" );
	return (-EFAULT);
      }

      /* clear LED. */
      retval = p2ioport_clr_led( num );

      PDEBUG( "Clear LED num=%d\n", num );
      break;
    } /** the end of clear LED **/


    /** toggle LED. **/
  case P2IOPORT_IOC_TOGGLE_LED:
    {
      struct p2ioport_val_s info;

      /* get and init values. */
      if ( copy_from_user((void *)&info,
			  (void *)arg,
			  sizeof(struct p2ioport_val_s)) ) {
	PERROR( "copy_from_user failed at P2IOPORT_IOC_TOGGLE_LED!\n" );
	return (-EFAULT);
      }

      /* toggle LED. */
      retval = p2ioport_toggle_led( info.num, info.val );

      PDEBUG( "Toggle LED num=%d, val=0x%lX\n", info.num, info.val );
      break;
    } /** the end of toggle LED **/


    /** get rotary switch. **/
  case P2IOPORT_IOC_GET_ROTARYSW:
    {
      struct p2ioport_val_s info;

      /* get and init values. */
      if ( copy_from_user((void *)&info,
			  (void *)arg,
			  sizeof(struct p2ioport_val_s)) ) {
	PERROR( "copy_from_user failed at P2IOPORT_IOC_GET_ROTARYSW!\n" );
	return (-EFAULT);
      }
      info.val = 0;

      /* get rotary switch. */
      retval = p2ioport_get_rotarysw( info.num, &(info.val) );

      /* put values. */
      if ( copy_to_user((void *)arg,
			(void *)&info,
			sizeof(struct p2ioport_val_s)) ) {
	PERROR( "copy_to_user failed at P2IOPORT_IOC_GET_ROTARYSW!\n" );
	return (-EFAULT);
      }

      PDEBUG( "Get rotary switch num=%d, val=0x%lX\n", info.num, info.val );
      break;
    } /** the end of get rotary switch **/


    /** get version info. **/
  case P2IOPORT_IOC_GET_VERSION:
    {
      struct p2ioport_version_s verinfo;

      /* get and init values. */
      if ( copy_from_user((void *)&verinfo,
			  (void *)arg,
			  sizeof(struct p2ioport_version_s)) ) {
	PERROR( "copy_from_user failed at P2IOPORT_IOC_GET_VERSION!\n" );
	return (-EFAULT);
      }
      verinfo.ver = 0;

      /* get version info. */
      retval = p2ioport_get_version( verinfo.num, &(verinfo.ver) );

      /* put values. */
      if ( copy_to_user((void *)arg,
			(void *)&verinfo,
			sizeof(struct p2ioport_version_s)) ) {
	PERROR( "copy_to_user failed at P2IOPORT_IOC_GET_VERSION!\n" );
	return (-EFAULT);
      }

      PDEBUG( "Get version num=%d, ver=0x%llX\n", verinfo.num, verinfo.ver );
      break;
    } /** the end of get version info. **/


    /** get virtual port. **/
  case P2IOPORT_IOC_GET_VPORT:
      {
	struct p2ioport_val_s info;
	int val = 0;

	/* get and init values. */
	if ( copy_from_user((void *)&info,
			    (void *)arg,
			    sizeof(struct p2ioport_val_s)) ) {
	  PERROR( "copy_from_user failed at P2IOPORT_IOC_GET_VPORT!\n" );
	  return (-EFAULT);
	}

	/* get ports. */
	retval = p2ioport_get_vport( info.num, &val );
	info.val = val?1:0;

	/* put values. */
	if ( copy_to_user((void *)arg,
			  (void *)&info,
			  sizeof(struct p2ioport_val_s)) ) {
	  PERROR( "copy_to_user failed at P2IOPORT_IOC_GET_VPORT!\n" );
	  return (-EFAULT);
	}

	PDEBUG( "Get virtual port: port=0x%08X, val=0x%08lX\n", info.num, info.val );
	break;
      } /** the end of get vport **/


      /** set virtual port. **/
  case P2IOPORT_IOC_SET_VPORT:
      {
	struct p2ioport_val_s info;

	/* get and init values. */
	if ( copy_from_user((void *)&info,
			    (void *)arg,
			    sizeof(struct p2ioport_val_s)) ) {
	  PERROR( "copy_from_user failed at P2IOPORT_IOC_SET_VPORT!\n" );
	  return (-EFAULT);
	}

	/* set ports. */
	retval = p2ioport_set_vport( info.num, info.val?1:0 );

	PDEBUG( "Set virtual port: port=0x%08X, val=0x%lX\n", info.num, info.val );
	break;
      } /** the end of set vport **/


    /** test and lock virtual port. **/
  case P2IOPORT_IOC_LOCK_VPORT:
      {
          struct p2ioport_val_s info;
          int val;

          /* get and init values. */
          if ( copy_from_user((void *)&info,
                              (void *)arg,
                              sizeof(struct p2ioport_val_s)) ) {
              PERROR( "copy_from_user failed at P2IOPORT_IOC_LOCK_VPORT!\n" );
              return (-EFAULT);
          }

          /* lock ports. */
          retval = p2ioport_lock_vport( info.num, &val);
          if(retval) {
              PERROR( "copy_to_user failed at P2IOPORT_IOC_LOCK_VPORT!\n" );
              return retval;
          }
          info.val = val;

          /* put values. */
          if ( copy_to_user((void *)arg,
                            (void *)&info,
                            sizeof(struct p2ioport_val_s)) ) {
              PERROR( "copy_to_user failed at P2IOPORT_IOC_LOCK_VPORT!\n" );
              return (-EFAULT);
          }

          PDEBUG( "lock virtual port: port=0x%08X, %s\n", info.num, info.val?"BUSY":"SUCCESS" );
      } /** the end of lock vport **/
      break;

    /** unlock virtual port. **/
  case P2IOPORT_IOC_UNLOCK_VPORT:
      {
          struct p2ioport_val_s info;

          /* get and init values. */
          if ( copy_from_user((void *)&info,
                              (void *)arg,
                              sizeof(struct p2ioport_val_s)) ) {
              PERROR( "copy_from_user failed at P2IOPORT_IOC_UNLOCK_VPORT!\n" );
              return (-EFAULT);
          }

          /* unlock ports. */
          retval = p2ioport_unlock_vport( info.num);
          if(retval) {
              PERROR( "copy_to_user failed at P2IOPORT_IOC_UNLOCK_VPORT!\n" );
              return retval;
          }

          PDEBUG( "unlock virtual port: port=0x%08X\n", info.num);
      } /** the end of lock vport **/
      break;

    /** special commands **/
  default:
    {
      if ( NULL == p2ioport_info.ops->ioctl ) {
	PERROR( "Unknown ioctl command!(0x%X)\n", cmd );
	retval = -EINVAL;
      } else {
	retval = p2ioport_info.ops->ioctl( cmd, arg );
      }
    }
  } /* the end of switch */
  
  return (retval);
}


/***************************************************************************
 * p2ioport_open
 **************************************************************************/
static int p2ioport_open( struct inode *inode, struct file *filp )
{
  PDEBUG( "Open(%d:%d)\n", p2ioport_major, MINOR(inode->i_rdev) );
  return (0);
}


/***************************************************************************
 * p2ioport_release
 **************************************************************************/
static int p2ioport_release( struct inode *inode, struct file *filp )
{
  PDEBUG( "Close(%d:%d)\n", p2ioport_major, MINOR(inode->i_rdev) );
  return (0);
}


/***** file operations *****/
static struct file_operations p2ioport_fops = {
  .owner    = THIS_MODULE,
  .ioctl    = p2ioport_ioctl,   /* p2ioport_ioctl   */
  .open     = p2ioport_open,    /* p2ioport_open    */
  .release  = p2ioport_release, /* p2ioport_release */
  /* nothing more, fill with NULLs */
};


/***************************************************************************
 * p2ioport_read_proc
 **************************************************************************/
static int p2ioport_read_proc( char *buf, char **start, off_t offset,
			       int length, int *eof, void *data )
{
  int len = 0;
  int i = 0;
  struct p2ioport_info_s *info = &p2ioport_info;
  struct p2ioport_operations *ops = info->ops;

  /* print version number. */
  len += sprintf( buf+len, "P2PF I/O port driver ver. %s\n\n", P2IOPORT_DRV_VERSION );
  
  /* print board infomation. */
  len += sprintf( buf+len, " Board type:\t\t%s\n", info->name );

  /* print DIPSW. */
  len += sprintf( buf+len, " Max DIPSW num:\t\t%d\n", info->nr_dipsw );
  for ( i = 0; i < info->nr_dipsw; ++i ) {
    unsigned long dipsw = 0;
    if ( !p2ioport_get_dipsw(i, &dipsw) )
      len += sprintf( buf+len, "  * DIPSW#%d:\t\t0x%lX\n", i, dipsw );
  }

  /* print LED. */
  len += sprintf( buf+len, " Max LED num:\t\t%d\n", info->nr_led );
  for ( i = 0; i < info->nr_led; ++i ) {
    unsigned long led = 0;
    if ( !p2ioport_get_led(i, &led) )
      len += sprintf( buf+len, "  * LED#%d:\t\t0x%lX\n", i, led );
  }

  /* print RotarySW. */
  len += sprintf( buf+len, " Max Rotary sw num:\t%d\n", info->nr_rotarysw );
  for ( i = 0; i < info->nr_rotarysw; ++i ) {
    unsigned long rotary = 0;
    if ( !p2ioport_get_rotarysw(i, &rotary) )
      len += sprintf( buf+len, "  RotarySW#%d:\t0x%lX\n", i, rotary );
  }

  /* print device version number. */
  len += sprintf( buf+len, " Max device(FPGA) num:\t%d\n", info->nr_device );
  for ( i = 0; i < info->nr_device; ++i ) {
    unsigned long long version = 0;
    if ( !p2ioport_get_version(i, &version) );
    len += sprintf( buf+len, "  * Version#%d:\t\t0x%llX\n", i, version );
  }

  /* print support API. */
  len += sprintf( buf+len, " Support API:\n" );
  if ( ops->get_dipsw )     len += sprintf( buf+len, "  * get_dipsw\n" );
  if ( ops->get_led )       len += sprintf( buf+len, "  * get_led\n" );
  if ( ops->set_led )       len += sprintf( buf+len, "  * set_led\n" );
  if ( ops->clr_led )       len += sprintf( buf+len, "  * clr_led\n" );
  if ( ops->toggle_led )    len += sprintf( buf+len, "  * toggle_led\n" );
  if ( ops->heartbeat_led ) len += sprintf( buf+len, "  * heartbeat_led\n" );
  if ( ops->get_rotarysw )  len += sprintf( buf+len, "  * get_rotarysw\n" );
  if ( ops->get_version )   len += sprintf( buf+len, "  * get_version\n" );
  if ( ops->get_vport )     len += sprintf( buf+len, "  * get_vport\n" );
  if ( ops->set_vport )     len += sprintf( buf+len, "  * set_vport\n" );
  if ( ops->poll )          len += sprintf( buf+len, "  * poll\n" );
  if ( ops->notify_int )    len += sprintf( buf+len, "  * notify_int\n" );

  *eof = 1;
  return (len);
}


/***************************************************************************
 * p2ioport_init
 **************************************************************************/
int __init p2ioport_init( void )
{
  int	retval = 0; /* return value */
  dev_t devnum = 0; /* device number */
  struct proc_dir_entry *pentry = NULL; /* proc entry */

  /* init debug switch. */
  P2IOPORT_DEBUG = 0;

  /* print init Message. */
  PINFO( "P2PF I/O port driver ver. %s\n", P2IOPORT_DRV_VERSION );

  /* set parameters. */
  memset( &p2ioport_info, 0, sizeof(struct p2ioport_info_s) );
  if ( __p2ioport_init_info(&p2ioport_info) ) {
    PERROR( "Init p2ioport info is failed!!\n" );
    return (-ENODEV);
  }
  PDEBUG( "%s dipsw=%d led=%d rotarysw=%d device=%d\n",
	  p2ioport_info.name, p2ioport_info.nr_dipsw, p2ioport_info.nr_led,
	  p2ioport_info.nr_rotarysw, p2ioport_info.nr_device );

  /* register major, and accept a dynamic number. */
  if ( p2ioport_major ) {
    devnum = MKDEV(p2ioport_major, 0);
    retval = register_chrdev_region( devnum, 1, P2IOPORT_DEVNAME );
    if ( unlikely(retval < 0) ) {
      PERROR( "Unable to register %d for p2ioport!\n", p2ioport_major );
      return (retval);
    }
  } else {
    retval = alloc_chrdev_region( &devnum, 0, 1, P2IOPORT_DEVNAME );
    if ( unlikely(retval < 0) ) {
      PERROR( "alloc_chrdev_region failed(%d)!\n", retval );
      return (retval);
    }

    /* set dynamic major number. */
    p2ioport_major = MAJOR( devnum );
    PINFO( "Change device major number %d to %d!\n", P2IOPORT_MAJOR, p2ioport_major );
  }

  /* optional poll method */
  if(p2ioport_info.ops && p2ioport_info.ops->poll)
      p2ioport_fops.poll = p2ioport_info.ops->poll;

  /* init cdev structure. */
  cdev_init( &p2ioport_cdev, &p2ioport_fops );
  p2ioport_cdev.owner = THIS_MODULE;
  retval = cdev_add( &p2ioport_cdev, devnum, 1 );
  if ( unlikely(retval < 0) ) {
    PERROR( "cdev_add failed(%d)!\n", retval );
  }

  /* make proc entry. */
  pentry = create_proc_entry( P2IOPORT_PROCNAME, 0, 0 );
  if ( pentry ) {
    pentry->read_proc = p2ioport_read_proc;
  }

  return (0);
}


/***************************************************************************
 * p2ioport_cleanup
 **************************************************************************/
static void __exit p2ioport_cleanup( void )
{
  PINFO( "Clean up P2PF I/O port driver\n" ); /* Message */

  /* cleanup parameters. */
  __p2ioport_cleanup_info( &p2ioport_info );

  /* remove proc entry. */
  remove_proc_entry( P2IOPORT_PROCNAME, 0 );

  /* unregister the character device driver. */
  cdev_del( &p2ioport_cdev );
  unregister_chrdev_region( MKDEV(p2ioport_major, 0), 1 );

  /* clear debug switch. */
  P2IOPORT_DEBUG = 0;
}

#if defined(MODULE)
module_init(p2ioport_init);
module_exit(p2ioport_cleanup);
#else /* ! MODULE */
postcore_initcall(p2ioport_init);
#endif /* MODULE */


/** Export symbols **/
EXPORT_SYMBOL(p2ioport_get_dipsw);
EXPORT_SYMBOL(p2ioport_get_led);
EXPORT_SYMBOL(p2ioport_set_led);
EXPORT_SYMBOL(p2ioport_clr_led);
EXPORT_SYMBOL(p2ioport_toggle_led);
EXPORT_SYMBOL(p2ioport_get_rotarysw);
EXPORT_SYMBOL(p2ioport_get_version);
EXPORT_SYMBOL(p2ioport_get_vport);
EXPORT_SYMBOL(p2ioport_set_vport);
EXPORT_SYMBOL(p2ioport_get_operations);
EXPORT_SYMBOL(p2ioport_set_int_callback);


/******************** the end of the file "p2ioport.c" ********************/
