/*****************************************************************************
 * linux/drivers/p2pf/p2ioport/mpc837xerdb.c
 *
 *   MPC837xERDB(black case) I/O port and GPIO access low-level driver
 *   
 *     Copyright (C) 2008-2009 Panasonic corp.
 *     All Rights Reserved.
 *
 *****************************************************************************/

/*
 * Register Map: MPC837xERDB (GPIO)

 Offset  Definition
 ----------------------------------------------------------------------------
 0xC00   - GPIO1 direction reg. Indicates whether a signal is used as In/Out.
 0xC08   - GPIO1 data reg. 0 = LED On, 1 = LED Off.

      --  15  14  13  12  11  10  09  08  07  06  05  04  03  02  01  00
      --                  |   |   |--- LED D3(Red)
      --                  |   |------- LED D4(Yellow)
      --                  |----------- LED D5(Green)
*/

/***** include header files & define macros *****/
/** header files **/
#include <linux/kernel.h>
#include <linux/string.h>
#include <asm/io.h>
#include <linux/ioctl.h>
#include <linux/errno.h>
#include <linux/p2ioport.h>

/***** definitions *****/
/** constants **/
#define BOARDTYPE_NAME "MPC837xERDB"
#define MPC837XERDB_VER "0.91"

/* the number of devices */
enum MPC837XERDB_IOPORT_CONST {
  MPC837XERDB_MAXNR_DIPSW    = 0,
  MPC837XERDB_MAXNR_LED      = 1,
  MPC837XERDB_MAXNR_ROTARYSW = 0,
  MPC837XERDB_MAXNR_DEVICE   = 0,
};

/* register address */
enum MPC837XERDB_IOPORT_REG {
  MPC837XERDB_REG_LED     = 0xC08,      /* GPIO1 DAT[0-31] */
  MPC837XERDB_REG_LED_DIR = 0xC00,      /* GPIO1 DIR[0-31] */
  MPC837XERDB_LED_MASK    = 0x00700000, /* GPIO1[9-11] */
  MPC837XERDB_LED_SHIFT   = 20,         /* GPIO1[9-11] */
};

/** variables **/
static struct mpc837xerdb_regs_s {
	unsigned long *led;
} regs;

static unsigned char MPC837XERDB_DEBUG = 0;
static unsigned long MPC837XERDB_IMMRBAR = 0;

/** print messages **/
#define PINFO( fmt, args... )	printk( KERN_INFO "[p2ioport] " fmt, ## args)
#define PERROR( fmt, args... )	printk( KERN_ERR "[p2ioport](E)" fmt, ## args)
#define PDEBUG( fmt, args... )	do { if (MPC837XERDB_DEBUG) printk( KERN_INFO "[p2ioport:mpc837xerdb-l.%d] " fmt, __LINE__, ## args); } while(0)

/** prototype **/
extern phys_addr_t get_immrbase(void);


/********************************* function *********************************/


/***************************************************************************
 * led2gpio
 *   - convert a bit alignment from API(0bit=D3...) to GPIO(0bit=D5...)
 **************************************************************************/
static inline unsigned long led2gpio( unsigned long val )
{
  return (unsigned long)( ((val&0x01L)<<2) | (val&0x02L) | ((val&0x04L)>>2) );
}


/***************************************************************************
 * mpc837xerdb_set_led
 *   - DWORD access
 *   - 0=On, 1=Off (val: 0=Off, 1=On)
 *   - val: 0bit=D3(Red), 1bit=D4(Yellow), 2bit=D5(Green)
 **************************************************************************/
static int mpc837xerdb_set_led( int num, unsigned long val )
{
  if ( unlikely(NULL == regs.led) ) {
    PERROR( "ioremap failed 0x%08X\n", MPC837XERDB_REG_LED );
    return (-ENOMEM);
  }

  /* set LED On/Off. */
  PDEBUG( "Set LED num=%d, val=0x%lX 0x%lX\n", num, val,
	  (~(led2gpio(val)<<MPC837XERDB_LED_SHIFT)|~MPC837XERDB_LED_MASK) );

  iowrite32be( (~(led2gpio(val)<<MPC837XERDB_LED_SHIFT)|~MPC837XERDB_LED_MASK), regs.led );

  return (0);
}


/***************************************************************************
 * mpc837xerdb_clr_led
 *   - DWORD access
 *   - 0=On, 1=Off (val: 0=Off, 1=On)
 **************************************************************************/
static int mpc837xerdb_clr_led( int num )
{
  if ( unlikely(NULL == regs.led) ) {
    PERROR( "ioremap failed 0x%08X\n", MPC837XERDB_REG_LED );
    return (-ENOMEM);
  }

  /* set Led Off. */
  iowrite32be( (*(regs.led)|MPC837XERDB_LED_MASK), regs.led );

  PDEBUG( "Clear LED num=%d\n", num );
  return (0);
}


/***************************************************************************
 * mpc837xerdb_toggle_led
 *   - DWORD access
 *   - 0=On, 1=Off (val: 0=Off, 1=On)
 *   - val: 0bit=D3(Red), 1bit=D4(Yellow), 2bit=D5(Green)
 **************************************************************************/
static int mpc837xerdb_toggle_led( int num, unsigned long val )
{
  unsigned long old = 0;

  if ( unlikely(NULL == regs.led) ) {
    PERROR( "ioremap failed 0x%08X\n", MPC837XERDB_REG_LED );
    return (-ENOMEM);
  }

  /* get LED. */
  old = ioread32be( regs.led );
  PDEBUG( "old=0x%08lX\n", old );
  PINFO( "FIXME: Cannot read D3(=GPIO1[9])!\n" );

  /* set LED On/Off. */
  old ^= (led2gpio(val)<<MPC837XERDB_LED_SHIFT) & MPC837XERDB_LED_MASK;
  PDEBUG( "Toggle LED num=%d, val=0x%lX old=0x%08lX\n", num, val,
	  (old|~MPC837XERDB_LED_MASK) );

  iowrite32be( (old|~MPC837XERDB_LED_MASK), regs.led );

  return (0);
}


/***************************************************************************
 * ioctl (special commands)
 **************************************************************************/
static int mpc837xerdb_ioctl( unsigned int cmd, unsigned long arg )
{
  int retval = 0;	/* return value */

  /*** main routine ***/
  switch ( cmd ) {

  case P2IOPORT_IOC_MPC837XERDB_TOGGLE_DEBUGMODE:
    {
      MPC837XERDB_DEBUG ^= 1;
      PINFO( "%s mode\n", MPC837XERDB_DEBUG?"Debug":"Non-Debug" );
      break;
    }

  default:
    {
      PERROR( "Unknown ioctl command!(0x%X)\n", cmd );
      retval = -EINVAL;
    }
  } /* the end of switch */

  return (retval);
}


/*** set p2ioport operations ***/
struct p2ioport_operations mpc837xerdb_ops = {
  .set_led     = mpc837xerdb_set_led,     /* Set LED */
  .clr_led     = mpc837xerdb_clr_led,     /* Clear LED */
  .toggle_led  = mpc837xerdb_toggle_led,  /* Toggle LED */
  .ioctl       = mpc837xerdb_ioctl,       /* Ioctl(special commands) */
};


/***************************************************************************
 * mpc837xerdb_init
 **************************************************************************/
static inline int mpc837xerdb_init( struct p2ioport_info_s *info )
{
  /* print init Message. */
  PINFO( " board type: %s(ver. %s)\n", BOARDTYPE_NAME, MPC837XERDB_VER );

  /* init debug switch. */
  MPC837XERDB_DEBUG = 0;

  /* set info. */
  info->nr_dipsw    = MPC837XERDB_MAXNR_DIPSW;
  info->nr_led      = MPC837XERDB_MAXNR_LED;
  info->nr_rotarysw = MPC837XERDB_MAXNR_ROTARYSW;
  info->nr_device   = MPC837XERDB_MAXNR_DEVICE;
  info->ops         = &mpc837xerdb_ops;
  strcpy( info->name, BOARDTYPE_NAME );

  /* get IMMR base address. */
  MPC837XERDB_IMMRBAR = (unsigned long)get_immrbase();
  PDEBUG( "IMMRBAR: 0x%08lX\n", MPC837XERDB_IMMRBAR );

  /* init regs. */
  regs.led   = ioremap( (MPC837XERDB_REG_LED+MPC837XERDB_IMMRBAR), 4 );
  if ( unlikely(NULL == regs.led) ) goto EXIT;

  /* success. */
  return (0);

 EXIT:
  PERROR("ioremap failed!\n");
  return (-ENOMEM);
}


/***************************************************************************
 * mpc837xerdb_cleanup
 **************************************************************************/
static inline void mpc837xerdb_cleanup( void )
{
  PINFO( "Clean up %s\n", BOARDTYPE_NAME ); /* Message */

  /* clear debug switch. */
  MPC837XERDB_DEBUG = 0;

  /* clear regs. */
  if (regs.led)   iounmap(regs.led);

  memset( &regs, 0, sizeof(struct mpc837xerdb_regs_s) );
}


/** export getting operations function. **/
inline void __p2ioport_get_ops( struct p2ioport_operations *ops )
{
  ops->set_led    = mpc837xerdb_set_led;
  ops->clr_led    = mpc837xerdb_clr_led;
  ops->toggle_led = mpc837xerdb_toggle_led;
}


/** export init/exit function. **/
inline int __p2ioport_init_info( struct p2ioport_info_s *info )
{
  return mpc837xerdb_init( info );
}


inline void __p2ioport_cleanup_info( struct p2ioport_info_s *info )
{
  memset( info, 0, sizeof(struct p2ioport_info_s) );
  mpc837xerdb_cleanup();
}


/******************** the end of the file "mpc837xerdb.c" ********************/
