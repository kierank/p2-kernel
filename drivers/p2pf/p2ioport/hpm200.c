/*****************************************************************************
 * linux/drivers/p2pf/p2ioport/hpm200.c
 *
 *   AJ-HPM200 I/O port and GPIO access low-level driver
 *   
 *     Copyright (C) 2008-2009 Panasonic corp.
 *     All Rights Reserved.
 *
 *****************************************************************************/
/* $Id: hpm200.c 5088 2010-02-09 02:49:16Z Sawada Koji $ */

/*
 * Register Map: AJ-HPM200 (GPIO)

 Offset  Definition
 ----------------------------------------------------------------------------
 0xC00   - GPIO1 direction reg. Indicates whether a signal is used as In/Out.
 0xC08   - GPIO1 data reg. 0 = DIPSW On, 1 = DIPSW Off.

      --  15  14  13  12  11  10  09  08  07  06  05  04  03  02  01  00
      --                                                  |-- DIPSW --|

 0xD00   - GPIO2 direction reg. Indicates whether a signal is used as In/Out.
 0xD08   - GPIO2 data reg. 0 = LED Off, 1 = LED On.

      --  31  30  29  28  27  26  25  24  23  22  21  20  19  18  17  16 --
      --                                          |--- LED ---|
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
#define BOARDTYPE_NAME "HPM200"
#define HPM200_VER "1.02"

/* the number of devices */
enum HPM200_IOPORT_CONST {
  HPM200_MAXNR_DIPSW    = 1, /* 4bit-DIPSW x 1 */
  HPM200_MAXNR_LED      = 1, /* 4bit-LED x 1 */
  HPM200_MAXNR_ROTARYSW = 0, /* NOT existent */
  HPM200_MAXNR_DEVICE   = 0, /* NOT existent, using GPIO */
};

/* register address, bit mask and bit shift values */
enum HPM200_IOPORT_REG {
  HPM200_REG_DIPSW     = 0xC08,      /* GPIO1 DAT[0-3] */
  HPM200_REG_DIPSW_DIR = 0xC00,      /* GPIO1 DIR[0-31] */
  HPM200_DIPSW_MASK    = 0xF0000000, /* DIPSW mask[0-3] */
  HPM200_DIPSW_SHIFT   = 28,         /* DIPSW bit shift[0-3] */
  HPM200_REG_LED       = 0xD08,      /* GPIO2 DAT[18-21] */
  HPM200_REG_LED_DIR   = 0xD00,      /* GPIO2 DIR[0-31] */
  HPM200_LED_MASK      = 0x00003C00, /* LED mask[18-21] */
  HPM200_LED_SHIFT     = 10,         /* LED bit shift[18-21] */
};

/** variables **/
static struct hpm200_regs_s {
  unsigned long *dipsw;
  unsigned long *led;
} regs;

static unsigned char HPM200_DEBUG = 0;   /* for DEBUG */
static unsigned long HPM200_IMMRBAR = 0; /* IMMR base address */

/** print messages **/
#define PINFO( fmt, args... )	printk( KERN_INFO "[p2ioport] " fmt, ## args)
#define PERROR( fmt, args... )	printk( KERN_ERR "[p2ioport](E)" fmt, ## args)
#define PDEBUG( fmt, args... )	do { if (HPM200_DEBUG) printk( KERN_INFO "[p2ioport:hpm200-l.%d] " fmt, __LINE__, ## args); } while(0)

/** prototype **/
extern phys_addr_t get_immrbase(void); /* defined at arch/powerpc/sysdev/fsl_soc.c */


/********************************* function *********************************/


/***************************************************************************
 * hpm200_get_dipsw
 *   - 0=On, 1=Off! (val: 0=Off, 1=On)
 *   - don't need to convert endian
 **************************************************************************/
static int hpm200_get_dipsw( int num, unsigned long *val )
{
  unsigned long tmpval = 0;       /* GPIO1 DAT value */

  /* check mapping I/O registers. */
  if ( unlikely(NULL == regs.dipsw) ) {
    PERROR( "ioremap failed 0x%08X\n", HPM200_REG_DIPSW );
    return (-ENOMEM);
  }

  /* get DIPSW. */
  tmpval = ioread32be(regs.dipsw);
  *val = ( ~tmpval & HPM200_DIPSW_MASK ) >> HPM200_DIPSW_SHIFT;

  PDEBUG( "Get DIPSW num=%d, tmpval=0x%lX val=0x%lX val=0x%02X\n", num, tmpval, *val, (unsigned char)*val );
  return (0);
}


/***************************************************************************
 * hpm200_get_led
 *   - 0=Off, 1=On (val: 0=Off, 1=On)
 *   - don't need to convert endian
 **************************************************************************/
static int hpm200_get_led( int num, unsigned long *val )
{
  /* check mapping I/O registers. */
  if ( unlikely(NULL == regs.led) ) {
    PERROR( "ioremap failed 0x%08X\n", HPM200_REG_LED );
    return (-ENOMEM);
  }

  /* get LED setting */
  *val = (ioread32be(regs.led) & HPM200_LED_MASK) >> HPM200_LED_SHIFT;

  PDEBUG( "Get LED num=%d, val=0x%lX\n", num, *val);
  return (0);
}


/***************************************************************************
 * hpm200_set_led
 *   - DWORD access
 *   - 0=Off, 1=On (val: 0=Off, 1=On)
 *   - don't need to convert endian
 **************************************************************************/
static int hpm200_set_led( int num, unsigned long val )
{
  unsigned long data;

  /* check mapping I/O registers. */
  if ( unlikely(NULL == regs.led) ) {
    PERROR( "ioremap failed 0x%08X\n", HPM200_REG_LED );
    return (-ENOMEM);
  }

  /* set LED On/Off. */
  data = ioread32be(regs.led);
  data = ((val<<HPM200_LED_SHIFT) & HPM200_LED_MASK) | (data & ~HPM200_LED_MASK);
  PDEBUG( "Set LED num=%d, val=0x%lX 0x%lX\n", num, val, data);

  iowrite32be( data, regs.led );

  return (0);
}


/***************************************************************************
 * hpm200_clr_led
 *   - DWORD access
 *   - 0=Off, 1=On (val: 0=Off, 1=On)
 **************************************************************************/
static int hpm200_clr_led( int num )
{
  unsigned long data;

  /* check mapping I/O registers. */
  if ( unlikely(NULL == regs.led) ) {
    PERROR( "ioremap failed 0x%08X\n", HPM200_REG_LED );
    return (-ENOMEM);
  }

  /* set Led Off. */
  data = ioread32be(regs.led);
  iowrite32be( (data&~HPM200_LED_MASK), regs.led );

  PDEBUG( "Clear LED num=%d\n", num );
  return (0);
}


/***************************************************************************
 * hpm200_toggle_led
 *   - DWORD access
 *   - 0=Off, 1=On (val: 0=Off, 1=On)
 **************************************************************************/
static int hpm200_toggle_led( int num, unsigned long val )
{
  unsigned long old = 0;        /* GPIO2 DAT old data */

  /* check mapping I/O registers. */
  if ( unlikely(NULL == regs.led) ) {
    PERROR( "ioremap failed 0x%08X\n", HPM200_REG_LED );
    return (-ENOMEM);
  }

  /* get LED. */
  old = ioread32be( regs.led );
  PDEBUG( "old=0x%08lX\n", old );

  /* toggle LED On/Off. */
  old ^= (val<<HPM200_LED_SHIFT) & HPM200_LED_MASK;
  PDEBUG( "Toggle LED num=%d, val=0x%lX old=0x%lX\n", num, val,old);

  iowrite32be( old, regs.led );

  return (0);
}


/***************************************************************************
 * hpm200_get_vport
 * port : virtual port number
 *   ZION_INIT_L -> if zero then ZION is initialized.
 * p_val : pointer of value
 **************************************************************************/
static int hpm200_get_vport( int port, int *p_val )
{
    int retval=0;
    int val=0;
    unsigned long gpio_1 = get_immrbase() + HPM200_REG_DIPSW;
    unsigned long gpio_2 = get_immrbase() + HPM200_REG_LED;
    volatile unsigned long *reg_1 = NULL;
    volatile unsigned long *reg_2 = NULL;

    reg_1 = (volatile unsigned long *)ioremap(gpio_1,4);
    if (unlikely(NULL==reg_1)){
        PERROR("ioremap failed 0x%08lX\n", gpio_1);
        retval=-ENOMEM;
        goto fail;
    }

    reg_2 = (volatile unsigned long *)ioremap(gpio_2,4);
    if (unlikely(NULL==reg_2)){
        PERROR("ioremap failed 0x%08lX\n", gpio_2);
        retval=-ENOMEM;
        goto fail;
    }

    switch(port) {

        /* ZION_INIT_L */
    case P2IOPORT_VPORT_ZION_INIT_L:
        val = (ioread32be((void*)reg_1)&(1<<27))?1:0;
        break;

    default:                    /* check unsupported port */
        PERROR("unsupported ports 0x%08X\n", port);
        retval=-EINVAL;
        
    }

 fail:	
    /* complete */
    if(NULL!=reg_1) iounmap(reg_1);
    if(NULL!=reg_2) iounmap(reg_2);
    PDEBUG( "port=0x%08X, val=%d\n", port, val);
    *p_val = val;
    return retval;
}


/***************************************************************************
 * ioctl (special commands)
 **************************************************************************/
static int hpm200_ioctl( unsigned int cmd, unsigned long arg )
{
  int retval = 0;	/* return value */

  /*** main routine ***/
  switch ( cmd ) {

  case P2IOPORT_IOC_HPM200_TOGGLE_DEBUGMODE:
    {
      HPM200_DEBUG ^= 1;
      PINFO( "%s mode\n", HPM200_DEBUG?"Debug":"Non-Debug" );
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
struct p2ioport_operations hpm200_ops = {
  .get_dipsw   = hpm200_get_dipsw,   /* Get DIPSW */
  .get_led     = hpm200_get_led,     /* Get LED */
  .set_led     = hpm200_set_led,     /* Set LED */
  .clr_led     = hpm200_clr_led,     /* Clear LED */
  .toggle_led  = hpm200_toggle_led,  /* Toggle LED */
  .get_vport   = hpm200_get_vport,   /* Get virtual ports status */
  .ioctl       = hpm200_ioctl,       /* Ioctl(special commands) */
};


/***************************************************************************
 * hpm200_init
 **************************************************************************/
static inline int hpm200_init( struct p2ioport_info_s *info )
{
  /* print init Message. */
  PINFO( " board type: %s(ver. %s)\n", BOARDTYPE_NAME, HPM200_VER );

  /* init debug switch. */
  HPM200_DEBUG = 0;

  /* set info. */
  info->nr_dipsw    = HPM200_MAXNR_DIPSW;
  info->nr_led      = HPM200_MAXNR_LED;
  info->nr_rotarysw = HPM200_MAXNR_ROTARYSW;
  info->nr_device   = HPM200_MAXNR_DEVICE;
  info->ops         = &hpm200_ops;
  strcpy( info->name, BOARDTYPE_NAME );

  /* set IMMR base address. */
  HPM200_IMMRBAR = (unsigned long)get_immrbase();
  PDEBUG( "IMMRBAR: 0x%08lX\n", HPM200_IMMRBAR );

  /* init regs. */
  regs.dipsw = ioremap( (HPM200_REG_DIPSW+HPM200_IMMRBAR), 4 );
  if ( unlikely(NULL == regs.dipsw) ) goto EXIT;

  regs.led   = ioremap( (HPM200_REG_LED+HPM200_IMMRBAR)  , 4 );
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
 * hpm200_cleanup
 **************************************************************************/
static inline void hpm200_cleanup( void )
{
  PINFO( "Clean up %s\n", BOARDTYPE_NAME ); /* Message */

  /* clear debug switch. */
  HPM200_DEBUG = 0;

  /* clear regs. */
  if (regs.dipsw) iounmap(regs.dipsw);
  if (regs.led)   iounmap(regs.led);

  memset( &regs, 0, sizeof(struct hpm200_regs_s) );
}


/** export getting operations function. **/
inline void __p2ioport_get_ops( struct p2ioport_operations *ops )
{
  ops->get_dipsw  = hpm200_get_dipsw;
  ops->get_led    = hpm200_get_led;
  ops->set_led    = hpm200_set_led;
  ops->clr_led    = hpm200_clr_led;
  ops->toggle_led = hpm200_toggle_led;
  ops->get_vport  = hpm200_get_vport; /* Get virtual ports status */
}


/** export init/exit function. **/
/*   CAUTION: Never change function name! */
inline int __p2ioport_init_info( struct p2ioport_info_s *info )
{
  return hpm200_init( info );
}


inline void __p2ioport_cleanup_info( struct p2ioport_info_s *info )
{
  memset( info, 0, sizeof(struct p2ioport_info_s) );
  hpm200_cleanup();
}


/******************** the end of the file "hpm200.c" ********************/
