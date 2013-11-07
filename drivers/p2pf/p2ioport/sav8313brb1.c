/*****************************************************************************
 * linux/drivers/p2pf/p2ioport/sav8313brb1.c
 *
 *   SAV8313 BRB#1 I/O port and GPIO access low-level driver
 *   
 *     Copyright (C) 2008-2010 Panasonic corp.
 *     All Rights Reserved.
 *
 *****************************************************************************/

/*
 * Register Map: PLD (EPM570T144)

 StartAdr         Definition
 ------------------------------------------------------------------------
 0xe400_0000      - output reg LED_port, RST_N, etc... (Word access)
 
     --  15  14  13  12  11  10  09  08  07  06  05  04  03  02  01  00
     --  |--- open ---|  |   |   |   |   |--------- LED[7:0] ---------|  
     --                  |   |   |   |------------- LED_SD_N             
     --                  |   |   |----------------- RST_PCC_N            
     --                  |   |--------------------- RST_GBE_PHY_N        
     --                  |------------------------- EXT_USB_RST(1=reset)
 0xe480_0000      - input reg DIP_SW, CONF_DN, etc... (Word access)
 0xe4c0_0000      - input reg FPGAversionL(FPGA_01_REG & FPGA_00_REG)
 0xe4c2_0000      - input reg FPGAversionM(FPGA_03_REG & FPGA_02_REG)

 NOTICE: assigned LED and DIP_SW bits
  - LED
     bit0: Heart beat LED
     bit1: General perpose(ex. measurement at time and so on.)
  - DIP_SW
     bit0: Switching ROM/NFS boot
     bit1: Force version update
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
#define BOARDTYPE_NAME "SAV8313BRB1"
#define SAV8313BRB1_VER "0.92"

/* the number of devices */
enum SAV8313BRB1_IOPORT_CONST {
  SAV8313BRB1_MAXNR_DIPSW    = 1,
  SAV8313BRB1_MAXNR_LED      = 1,
  SAV8313BRB1_MAXNR_ROTARYSW = 0,
  SAV8313BRB1_MAXNR_DEVICE   = 1,
};

/* register address */
enum SAV8313BRB1_IOPORT_REG {
  SAV8313BRB1_REG_DIPSW  = 0xE4800000,
  SAV8313BRB1_DIPSW_MASK = 0x000000FF,
  SAV8313BRB1_REG_LED    = 0xE4000000,
  SAV8313BRB1_LED_MASK   = 0x0000FF00,
  SAV8313BRB1_REG_VERL   = 0xE4C00000,
  SAV8313BRB1_REG_VERM   = 0xE4C20000,
  SAV8313BRB1_VER_MASK   = 0x00000000FFFFFFFF,
};

/* bit assign */
enum SAV8313BRB1_IOPORT_BIT {
  SAV8313BRB1_HEARTBEAT_BIT = 0x01,
};


/** variables **/
static struct sav8313brb1_regs_s {
  unsigned char  *dipsw;
  unsigned short *led;
  unsigned short *ver_l;
  unsigned short *ver_m;
} regs;

static unsigned char SAV8313BRB1_DEBUG = 0; /* for DEBUG */


/** print messages **/
#define PINFO( fmt, args... )	printk( KERN_INFO "[p2ioport] " fmt, ## args)
#define PERROR( fmt, args... )	printk( KERN_ERR "[p2ioport](E)" fmt, ## args)
#define PDEBUG( fmt, args... )	do { if (SAV8313BRB1_DEBUG) printk( KERN_INFO "[p2ioport:sav8313brb1-l.%d] " fmt, __LINE__, ## args); } while(0)

/** prototype **/


/********************************* function *********************************/


/***************************************************************************
 * sav8313brb1_get_dipsw
 *   - 0=On, 1=Off (val: 0=Off, 1=On)
 **************************************************************************/
static int sav8313brb1_get_dipsw( int num, unsigned long *val )
{
  if ( unlikely(NULL == regs.dipsw) ) {
    PERROR( "ioremap failed 0x%08X\n", SAV8313BRB1_REG_DIPSW+1 );
    return (-ENOMEM);
  }

  *val = ~((unsigned long)ioread8( regs.dipsw )) & SAV8313BRB1_DIPSW_MASK;
  PDEBUG( "Get DIPSW num=%d, val=0x%lX val=0x%02X\n", num, *val, (unsigned char)*val );
  return (0);
}


/***************************************************************************
 * sav8313brb1_get_led
 *   - word access!
 *   - 0=On, 1=Off! (val: 0=Off, 1=On)
 **************************************************************************/
static int sav8313brb1_get_led( int num, unsigned long *val )
{
  unsigned short tmpval = 0;

  if ( unlikely(NULL == regs.led) ) {
    PERROR( "ioremap failed 0x%08X\n", SAV8313BRB1_REG_LED );
    return (-ENOMEM);
  }

  tmpval = ioread16be( regs.led );
  *val = (~tmpval) & 0x000000FF;

  PDEBUG( "Get LED num=%d, val=0x%lX 0x%X\n", num, *val, tmpval );
  return (0);
}


/***************************************************************************
 * sav8313brb1_set_led
 *   - word access!
 *   - 0=On, 1=Off! (val: 0=Off, 1=On)
 **************************************************************************/
/* static int sav8313brb1_set_led( int num, unsigned long val ) */
int sav8313brb1_set_led( int num, unsigned long val )
{
  if ( unlikely(NULL == regs.led) ) {
    PERROR( "ioremap failed 0x%08X\n", SAV8313BRB1_REG_LED );
    return (-ENOMEM);
  }

  PDEBUG( "Set LED num=%d, val=0x%lX 0x%X\n", num, val, (unsigned short)(~val|SAV8313BRB1_LED_MASK) );
  iowrite16be( (unsigned short)(~val|SAV8313BRB1_LED_MASK), regs.led );
  return (0);
}


/***************************************************************************
 * sav8313brb1_clr_led
 *   - word access!
 *   - 0=On, 1=Off! (val: 0=Off, 1=On)
 **************************************************************************/
static int sav8313brb1_clr_led( int num )
{
  if ( unlikely(NULL == regs.led) ) {
    PERROR( "ioremap failed 0x%08X\n", SAV8313BRB1_REG_LED );
    return (-ENOMEM);
  }

  iowrite16be( (*(regs.led)|0xFFFF), regs.led );
  PDEBUG( "Clear LED num=%d\n", num );
  return (0);
}


/***************************************************************************
 * sav8313brb1_toggle_led
 *   - word access!
 *   - 0=On, 1=Off! (val: 0=Off, 1=On)
 **************************************************************************/
static int sav8313brb1_toggle_led( int num, unsigned long val )
{
  unsigned short old = 0;

  if ( unlikely(NULL == regs.led) ) {
    PERROR( "ioremap failed 0x%08X\n", SAV8313BRB1_REG_LED );
    return (-ENOMEM);
  }

  old = ioread16be( regs.led );
  old ^= (unsigned short)(val&0x0000FFFF);
  PDEBUG( "Toggle LED num=%d, val=0x%lX old=0x%X\n", num, val, (old|SAV8313BRB1_LED_MASK) );
  iowrite16be( (old|SAV8313BRB1_LED_MASK), regs.led );
  return (0);
}


/***************************************************************************
 * sav8313brb1_heartbeat_led
 *   - word access!
 *   - 0=On, 1=Off! (val: 0=Off, 1=On)
 **************************************************************************/
static inline int sav8313brb1_heartbeat_led( int num )
{
  return sav8313brb1_toggle_led( num, SAV8313BRB1_HEARTBEAT_BIT );
}


/***************************************************************************
 * sav8313brb1_get_version
 *   - word x 2
 **************************************************************************/
static int sav8313brb1_get_version( int num, unsigned long long *val )
{
  if ( unlikely(NULL == regs.ver_l || NULL == regs.ver_m) ) {
    PERROR( "ioremap failed 0x%08X or 0x%08X\n",
	    SAV8313BRB1_REG_VERL, SAV8313BRB1_REG_VERM );
    return (-ENOMEM);
  }

  *val = ((ioread16be(regs.ver_m) << 16) | ioread16be(regs.ver_l)) & SAV8313BRB1_VER_MASK;
  PDEBUG( "Get version num=%d val=0x%llX ver_m=%X ver_l=%X\n", num, *val,
	  ioread16be(regs.ver_m), ioread16be(regs.ver_l) );
  return (0);
}


/***************************************************************************
 * ioctl (special commands)
 **************************************************************************/
static int sav8313brb1_ioctl( unsigned int cmd, unsigned long arg )
{
  int retval = 0;	/* return value */

  /*** main routine ***/
  switch ( cmd ) {

  case P2IOPORT_IOC_SAV8313BRB1_TOGGLE_DEBUGMODE:
    {
      SAV8313BRB1_DEBUG ^= 1;
      PINFO( "%s mode\n", SAV8313BRB1_DEBUG?"Debug":"Non-Debug" );
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
struct p2ioport_operations sav8313brb1_ops = {
  .get_dipsw      = sav8313brb1_get_dipsw,     /* Get DIPSW */
  .get_led        = sav8313brb1_get_led,       /* Get LED */
  .set_led        = sav8313brb1_set_led,       /* Set LED */
  .clr_led        = sav8313brb1_clr_led,       /* Clear LED */
  .toggle_led     = sav8313brb1_toggle_led,    /* Toggle LED */
  .heartbeat_led  = sav8313brb1_heartbeat_led, /* for Heartbeat LED(kernel only) */
  .get_version    = sav8313brb1_get_version,   /* Get version */
  .ioctl          = sav8313brb1_ioctl,         /* Ioctl(special commands) */
};


/***************************************************************************
 * sav8313brb1_init
 **************************************************************************/
static inline int sav8313brb1_init( struct p2ioport_info_s *info )
{
  /* print init Message. */
  PINFO( " board type: %s(ver. %s)\n", BOARDTYPE_NAME, SAV8313BRB1_VER );

  /* init debug switch. */
  SAV8313BRB1_DEBUG = 0;

  /* set info. */
  info->nr_dipsw    = SAV8313BRB1_MAXNR_DIPSW;
  info->nr_led      = SAV8313BRB1_MAXNR_LED;
  info->nr_rotarysw = SAV8313BRB1_MAXNR_ROTARYSW;
  info->nr_device   = SAV8313BRB1_MAXNR_DEVICE;
  info->ops         = &sav8313brb1_ops;
  strcpy( info->name, BOARDTYPE_NAME );

  /* init regs. */
  regs.dipsw = ioremap( SAV8313BRB1_REG_DIPSW+1, 1 ); /* Big Endian */
  if ( unlikely(NULL == regs.dipsw) ) goto EXIT;

  regs.led   = ioremap( SAV8313BRB1_REG_LED    , 2 );
  if ( unlikely(NULL == regs.led) ) goto FAIL_LED;

  regs.ver_l = ioremap( SAV8313BRB1_REG_VERL   , 2 ); /* Big Endian */
  if ( unlikely(NULL == regs.ver_l) ) goto FAIL_VERL;

  regs.ver_m = ioremap( SAV8313BRB1_REG_VERM   , 2 ); /* Big Endian */
  if ( unlikely(NULL == regs.ver_m) ) goto FAIL_VERM;

  /* success. */
  return (0);

 FAIL_VERM:
  iounmap( regs.ver_l );
  regs.ver_l = NULL;

 FAIL_VERL:
  iounmap( regs.led );
  regs.led = NULL;

 FAIL_LED:
  iounmap( regs.dipsw );
  regs.dipsw = NULL;

 EXIT:
  PERROR("ioremap failed!\n");
  return (-ENOMEM);
}


/***************************************************************************
 * sav8313brb1_cleanup
 **************************************************************************/
static inline void sav8313brb1_cleanup( void )
{
  PINFO( "Clean up %s\n", BOARDTYPE_NAME ); /* Message */

  /* clear debug switch. */
  SAV8313BRB1_DEBUG = 0;

  /* clear regs. */
  if (regs.dipsw) iounmap(regs.dipsw);
  if (regs.led)   iounmap(regs.led);
  if (regs.ver_l) iounmap(regs.ver_l);
  if (regs.ver_m) iounmap(regs.ver_m);

  memset( &regs, 0, sizeof(struct sav8313brb1_regs_s) );
}


/** export getting operations function. **/
inline void __p2ioport_get_ops( struct p2ioport_operations *ops )
{
  ops->get_dipsw     = sav8313brb1_get_dipsw;
  ops->get_led       = sav8313brb1_get_led;
  ops->set_led       = sav8313brb1_set_led;
  ops->clr_led       = sav8313brb1_clr_led;
  ops->toggle_led    = sav8313brb1_toggle_led;
  ops->heartbeat_led = sav8313brb1_heartbeat_led;
}


/** export init/exit function. **/
inline int __p2ioport_init_info( struct p2ioport_info_s *info )
{
  return sav8313brb1_init( info );
}


inline void __p2ioport_cleanup_info( struct p2ioport_info_s *info )
{
  memset( info, 0, sizeof(struct p2ioport_info_s) );
  sav8313brb1_cleanup();
}


/******************** the end of the file "sav8313brb1.c" ********************/
