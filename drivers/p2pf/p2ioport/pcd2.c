/*****************************************************************************
 * linux/drivers/p2pf/p2ioport/pcd2.c
 *
 *   AJ-PCD2 I/O port and GPIO access low-level driver
 *   
 *     Copyright (C) 2009-2010 Panasonic corp.
 *     All Rights Reserved.
 *
 *****************************************************************************/

/*
 * Register Map: AJ-PCD2 (GPIO)

 Offset  Definition
 ----------------------------------------------------------------------------
 0xC00   - GPIO direction reg. Indicates whether a signal is used as In/Out.
 0xC08   - GPIO data reg. 0 = DIPSW On? LED On?, 1 = DIPSW Off? LED Off?.

      --  31  30  29  28  27  26  25  24  23  22  21  20  19  18  17  16
      --  |-- DIPSW --|
          
      --  15  14  13  12  11  10  09  08  07  06  05  04  03  02  01  00
      --                  |   |-- LED2
      --                  |------ LED1
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
#define BOARDTYPE_NAME "PCD2"
#define PCD2_VER "0.93"

/* the number of devices */
enum PCD2_IOPORT_CONST {
  PCD2_MAXNR_DIPSW    = 1, /* 4bit-DIPSW x 1 */
  PCD2_MAXNR_LED      = 1, /* 2bit-LED x 1 */
  PCD2_MAXNR_ROTARYSW = 0, /* NOT existent */
  PCD2_MAXNR_DEVICE   = 0, /* NOT existent, using GPIO */
};

/* register address */
enum PCD2_IOPORT_REG {
  PCD2_REG_DIPSW   = 0xC08,      /* GPIO DAT[28-31] */
  PCD2_DIPSW_MASK  = 0x0000000F, /* DIPSW mask[28-31] */
  PCD2_DIPSW_SHIFT = 0,          /* DIPSW bit shift[28-31] */
  PCD2_REG_LED     = 0xC08,      /* GPIO DAT[10-11] */
  PCD2_LED_MASK    = 0x00300000, /* LED mask[10-11] */
  PCD2_LED_SHIFT   = 20,         /* LED bit shift[10-11] */
};

/** variables **/
static struct pcd2_regs_s {
  unsigned long *dipsw;
  unsigned long *led;
} regs;

static unsigned char PCD2_DEBUG = 0;	/* for DEBUG */
static unsigned long PCD2_IMMRBAR = 0;	/* IMMR base address */

/** print messages **/
#define PINFO( fmt, args... )	printk( KERN_INFO "[p2ioport] " fmt, ## args)
#define PERROR( fmt, args... )	printk( KERN_ERR "[p2ioport](E)" fmt, ## args)
#define PDEBUG( fmt, args... )	do { if (PCD2_DEBUG) printk( KERN_INFO "[p2ioport:pcd2-l.%d] " fmt, __LINE__, ## args); } while(0)

/** prototype **/
extern phys_addr_t get_immrbase(void); /* defined at arch/powerpc/sysdev/fsl_soc.c */

/********************************* function *********************************/


/***************************************************************************
 * pcd2_get_dipsw
 *   - 0=On?, 1=Off? (val: 0=Off, 1=On)
 **************************************************************************/
static int pcd2_get_dipsw( int num, unsigned long *val )
{
  unsigned long tmpval = 0;       /* GPIO DAT value */

  /* check mapping I/O registers. */
  if ( unlikely(NULL == regs.dipsw) ) {
    PERROR( "ioremap failed 0x%08X\n", PCD2_REG_DIPSW );
    return (-ENOMEM);
  }

  /* get DIPSW. */
  tmpval = ioread32be(regs.dipsw);
  *val = ( tmpval & PCD2_DIPSW_MASK ) >> PCD2_DIPSW_SHIFT;

  PDEBUG( "Get DIPSW num=%d, tmpval=0x%lX val=0x%lX val=0x%02X\n", num, tmpval, *val, (unsigned char)*val );
  return (0);
}


/***************************************************************************
 * pcd2_set_led
 *   - 0=On?, 1=Off? (val: 0=Off, 1=On)
 **************************************************************************/
/* static int pcd2_set_led( int num, unsigned long val ) */
int pcd2_set_led( int num, unsigned long val )
{
  unsigned long data;

  /* check mapping I/O registers. */
  if ( unlikely(NULL == regs.led) ) {
    PERROR( "ioremap failed 0x%08X\n", PCD2_REG_LED );
    return (-ENOMEM);
  }

  /* set LED On/Off. */  
  data = ioread32be( regs.led );
  data = ( (val << PCD2_LED_SHIFT) & PCD2_LED_MASK ) | (data & ~PCD2_LED_MASK);
  iowrite32be( data, regs.led );

  PDEBUG( "Set LED num=%d, val=0x%lX 0x%lX\n", num, val, data );
  return (0);
}


/***************************************************************************
 * pcd2_clr_led
 *   - 0=On?, 1=Off? (val: 0=Off, 1=On)
 **************************************************************************/
static int pcd2_clr_led( int num )
{
  unsigned long data;

  /* check mapping I/O registers. */
  if ( unlikely(NULL == regs.led) ) {
    PERROR( "ioremap failed 0x%08X\n", PCD2_REG_LED );
    return (-ENOMEM);
  }

  /* set Led Off. */
  data = ioread32be( regs.led );
  iowrite32be( (data & ~PCD2_LED_MASK), regs.led );

  PDEBUG( "Clear LED num=%d\n", num );
  return (0);
}


/***************************************************************************
 * pcd2_toggle_led
 *   - 0=On?, 1=Off? (val: 0=Off, 1=On)
 **************************************************************************/
static int pcd2_toggle_led( int num, unsigned long val )
{
  unsigned long old = 0;        /* GPIO DAT old data */

  /* check mapping I/O registers. */
  if ( unlikely(NULL == regs.led) ) {
    PERROR( "ioremap failed 0x%08X\n", PCD2_REG_LED );
    return (-ENOMEM);
  }

  /* get LED. */
  old = ioread32be( regs.led );
  PDEBUG( "old=0x%08lX\n", old );

  /* toggle LED On/Off. */
  old ^= (val << PCD2_LED_SHIFT) & PCD2_LED_MASK;
  iowrite32be( old, regs.led );

  PDEBUG( "Toggle LED num=%d, val=0x%lX old=0x%lX\n", num, val, old);
  return (0);
}


/***************************************************************************
 * ioctl (special commands)
 **************************************************************************/
static int pcd2_ioctl( unsigned int cmd, unsigned long arg )
{
  int retval = 0;	/* return value */

  /*** main routine ***/
  switch ( cmd ) {

  case P2IOPORT_IOC_PCD2_TOGGLE_DEBUGMODE:
    {
      PCD2_DEBUG ^= 1;
      PINFO( "%s mode\n", PCD2_DEBUG?"Debug":"Non-Debug" );
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
struct p2ioport_operations pcd2_ops = {
  .get_dipsw   = pcd2_get_dipsw,   /* Get DIPSW */
  .set_led     = pcd2_set_led,     /* Set LED */
  .clr_led     = pcd2_clr_led,     /* Clear LED */
  .toggle_led  = pcd2_toggle_led,  /* Toggle LED */
  .ioctl       = pcd2_ioctl,       /* Ioctl(special commands) */
};


/***************************************************************************
 * pcd2_init
 **************************************************************************/
static inline int pcd2_init( struct p2ioport_info_s *info )
{
  /* print init Message. */
  PINFO( " board type: %s(ver. %s)\n", BOARDTYPE_NAME, PCD2_VER );

  /* init debug switch. */
  PCD2_DEBUG = 0;

  /* set info. */
  info->nr_dipsw    = PCD2_MAXNR_DIPSW;
  info->nr_led      = PCD2_MAXNR_LED;
  info->nr_rotarysw = PCD2_MAXNR_ROTARYSW;
  info->nr_device   = PCD2_MAXNR_DEVICE;
  info->ops         = &pcd2_ops;
  strcpy( info->name, BOARDTYPE_NAME );

  /* set IMMR base address. */
  PCD2_IMMRBAR = (unsigned long)get_immrbase();
  PDEBUG( "IMMRBAR: 0x%08lX\n", PCD2_IMMRBAR );

  /* init regs. */
  regs.dipsw = ioremap( (PCD2_REG_DIPSW+PCD2_IMMRBAR), 4 );
  if ( unlikely(NULL == regs.dipsw) ) goto EXIT;

  regs.led   = ioremap( (PCD2_REG_LED+PCD2_IMMRBAR)  , 4 );
  if ( unlikely(NULL == regs.led) ) goto FAIL_LED;

  /* success. */
  return (0);

 FAIL_LED:
  iounmap( regs.dipsw );
  regs.dipsw = NULL;

 EXIT:
  PERROR("ioremap failed!\n");
  return (-ENOMEM);
}


/***************************************************************************
 * pcd2_cleanup
 **************************************************************************/
static inline void pcd2_cleanup( void )
{
  PINFO( "Clean up %s\n", BOARDTYPE_NAME ); /* Message */

  /* clear debug switch. */
  PCD2_DEBUG = 0;

  /* clear regs. */
  if (regs.dipsw) iounmap(regs.dipsw);
  if (regs.led)   iounmap(regs.led);

  memset( &regs, 0, sizeof(struct pcd2_regs_s) );
}


/** export getting operations function. **/
inline void __p2ioport_get_ops( struct p2ioport_operations *ops )
{
  ops->get_dipsw  = pcd2_get_dipsw;
  ops->set_led    = pcd2_set_led;
  ops->clr_led    = pcd2_clr_led;
  ops->toggle_led = pcd2_toggle_led;
}


/** export init/exit function. **/
inline int __p2ioport_init_info( struct p2ioport_info_s *info )
{
  return pcd2_init( info );
}


inline void __p2ioport_cleanup_info( struct p2ioport_info_s *info )
{
  memset( info, 0, sizeof(struct p2ioport_info_s) );
  pcd2_cleanup();
}


/******************** the end of the file "pcd2.c" ********************/
