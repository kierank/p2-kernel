/*****************************************************************************
 * linux/drivers/p2pf/p2ioport/hmc80.c
 *
 *   AG-HMC80 I/O port and GPIO access low-level driver
 *   
 *     Copyright (C) 2009-2010 Panasonic corp.
 *     All Rights Reserved.
 *
 *****************************************************************************/

/*
 * Register Map: AG-HMC80 (GPIO)

 Offset  Definition
 ----------------------------------------------------------------------------
 0xC00   - GPIO direction reg. Indicates whether a signal is used as In/Out.
 0xC08   - GPIO data reg. 0 = DIPSW On LED Off, 1 = DIPSW Off LED On.


 GPIO function table

 [pinNo] : [I/O]: [init]   :  [symbol] : [function]
 -------------------------------------------------------
  GPIO[10]:  OUT :   1     : IIC2_SDA  : RST_SM331_N
  GPIO[11]:  OUT :   0     : IIC2_SCL  : LED2(D500)
  GPIO[15]:  OUT :   0     : TSEC2_COL : LED1(D200)
  GPIO[16]:  IN  :   -     : TSEC2_CRS : SDCARD_DET_DV
  GPIO[28]:  IN  :   -     : SPIMOSI   : DIP_SW[3]
  GPIO[29]:  IN  :   -     : SPIMISO   : DIP_SW[2]
  GPIO[30]:  IN  :   -     : SPICLK    : DIP_SW[1]
  GPIO[31]:  IN  :   -     : SPISEL    : CARD_DOOR_OPEN

 - CARD_DOOR_OPEN (input)
 - SDCARD_DET_DV (input)
 - RST_SM331_N (Output)
 - DIPSW
     bit0: unused
     bit[2,1]=(Off,Off): ROM boot
             =(Off,On ): NFS boot
             =(On ,Off): VUP boot
             =(On ,On ): u-boot prompt
     bit3: Output Log to console?
 - LED
     LED1: General perpose(for debug)
     LED2: General perpose(for debug)
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
#define BOARDTYPE_NAME "HMC80"
#define HMC80_VER "0.94"

/* the number of devices */
enum HMC80_IOPORT_CONST {
  HMC80_MAXNR_DIPSW    = 1, /* 3bit-DIPSW x 1 */
  HMC80_MAXNR_LED      = 1, /* 2bit-LED x 1 */
  HMC80_MAXNR_ROTARYSW = 0, /* NOT existent */
  HMC80_MAXNR_DEVICE   = 0, /* NOT existent, using GPIO */
};

/* register address */
enum HMC80_IOPORT_REG {
  HMC80_REG_DIPSW   = 0xC08,      /* GPIO DAT[28-30] */
  HMC80_DIPSW_MASK  = 0x0000000E, /* DIPSW mask[28-30] */
  HMC80_DIPSW_SHIFT = 1,          /* DIPSW bit shift[28-30] */
  HMC80_REG_LED     = 0xC08,      /* GPIO DAT[11,15] */
  HMC80_LED_MASK    = 0x00110000, /* LED mask[11,15] */
  HMC80_LED_SHIFT   = 16,         /* LED bit shift[15] */
  HMC80_REG_PORT    = 0xC08,      /* GPIO DAT[10] */
};

/* bit assign */
enum HMC80_IOPORT_BIT {
  HMC80_INT_CDO       = 0x00000001, /* CARD_DOOR_OPEN: GPIO[31] */
  HMC80_INT_SDD       = 0x00008000, /* SDCARD_DET_DV : GPIO[16] */
  HMC80_PORT_SM331    = 0x00200000, /* RST_SM331_N   : GPIO[10] */
  HMC80_HEARTBEAT_BIT = 0x01,       /* LED1          : GPIO[15] */
};

#if defined(CONFIG_P2IOPORT_MPC83XXGPIOINT)
/* GPIO mask */
extern unsigned int GPIO_IMR;
#endif /* CONFIG_P2IOPORT_MPC83XXGPIOINT */


/** variables **/
static struct hmc80_regs_s {
  unsigned long *dipsw;
  unsigned long *led;
  unsigned long *port;
} regs;

static unsigned char HMC80_DEBUG = 0;	/* for DEBUG */
static unsigned long HMC80_IMMRBAR = 0;	/* IMMR base address */
#if defined(CONFIG_P2IOPORT_MPC83XXGPIOINT)
static struct list_head *cb_list;
#endif /* CONFIG_P2IOPORT_MPC83XXGPIOINT */

/** print messages **/
#define PINFO( fmt, args... )	printk( KERN_INFO "[p2ioport] " fmt, ## args)
#define PERROR( fmt, args... )	printk( KERN_ERR "[p2ioport](E)" fmt, ## args)
#define PDEBUG( fmt, args... )	do { if (HMC80_DEBUG) printk( KERN_INFO "[p2ioport:hmc80-l.%d] " fmt, __LINE__, ## args); } while(0)

/** prototype **/
extern phys_addr_t get_immrbase(void); /* defined at arch/powerpc/sysdev/fsl_soc.c */

#if defined(CONFIG_P2IOPORT_MPC83XXGPIOINT)
extern void MPC83xxGpioCleanupModule(void);
#endif /* CONFIG_P2IOPORT_MPC83XXGPIOINT */


/********************************* function *********************************/


/* Covert user definition LED bits to GPIO bits. */
static inline unsigned long hmc80_led_usr2gpio( unsigned long val )
{
  unsigned long tmpval = 0;

  tmpval = (val & 0x02) << 3 | (val & 0x01); /* bit0=GPIO[15], bit1=GPIO[11] */
  PDEBUG( "Convert 0x%08lX -> 0x%08lX\n", val, tmpval );

  return (tmpval);
}


/* Convert GPIO bits to user definition LED bits. */
static inline unsigned long hmc80_led_gpio2usr( unsigned long val )
{
  unsigned long tmpval = 0;

  tmpval = (val & 0x10) >> 3 | (val & 0x01); /* bit0=GPIO[15], bit1=GPIO[11] */
  PDEBUG( "Convert 0x%08lX -> 0x%08lX\n", val, tmpval );

  return (tmpval);
}

#if defined(CONFIG_P2IOPORT_MPC83XXGPIOINT)
/* Execute int callback. */
static inline void hmc80_exec_callback( struct list_head *head )
{
  struct p2ioport_cbentry_s *walk = NULL;

  list_for_each_entry( walk, head, cbentry_list ) {
    walk->func( walk->data );
  }
}
#endif /* CONFIG_P2IOPORT_MPC83XXGPIOINT */


/***************************************************************************
 * hmc80_get_dipsw
 *   - 0=On, 1=Off (val: 0=Off, 1=On)
 **************************************************************************/
static int hmc80_get_dipsw( int num, unsigned long *val )
{
  unsigned long tmpval = 0;       /* GPIO DAT value */

  /* check mapping I/O registers. */
  if ( unlikely(NULL == regs.dipsw) ) {
    PERROR( "ioremap failed 0x%08X\n", HMC80_REG_DIPSW );
    return (-ENOMEM);
  }

  /* get DIPSW. */
  tmpval = ioread32be(regs.dipsw);
  *val = (~tmpval & HMC80_DIPSW_MASK) >> HMC80_DIPSW_SHIFT;

  PDEBUG( "Get DIPSW num=%d, tmpval=0x%08lX val=0x%08lX val=0x%02X\n", num, tmpval, *val, (unsigned char)*val );
  return (0);
}


/***************************************************************************
 * hmc80_get_led
 *   - 0=Off, 1=On (val: 0=Off, 1=On)
 **************************************************************************/
static int hmc80_get_led( int num, unsigned long *val )
{
  unsigned long tmpval = 0;

  /* check mapping I/O registers. */
  if ( unlikely(NULL == regs.led) ) {
    PERROR( "ioremap failed 0x%08X\n", HMC80_REG_LED );
    return (-ENOMEM);
  }

  /* get LED setting. */
  tmpval = ioread32be( regs.led );
  *val = hmc80_led_gpio2usr( (tmpval & HMC80_LED_MASK) >> HMC80_LED_SHIFT );

  PDEBUG( "Get LED num=%d, val=0x%08lX 0x%08lX\n", num, *val, tmpval );
  return (0);
}


/***************************************************************************
 * hmc80_set_led
 *   - 0=Off, 1=On (val: 0=Off, 1=On)
 **************************************************************************/
static int hmc80_set_led( int num, unsigned long val )
{
  unsigned long data = 0;
  unsigned long tmpval = 0;

  /* check mapping I/O registers. */
  if ( unlikely(NULL == regs.led) ) {
    PERROR( "ioremap failed 0x%08X\n", HMC80_REG_LED );
    return (-ENOMEM);
  }

  /* set LED On/Off. */
  tmpval = hmc80_led_usr2gpio( val );
  data = ioread32be( regs.led );
  data = ( (tmpval << HMC80_LED_SHIFT) & HMC80_LED_MASK ) | (data & ~HMC80_LED_MASK);
  iowrite32be( data, regs.led );

  PDEBUG( "Set LED num=%d, val=0x%08lX 0x%08lX 0x%08lX\n", num, val, tmpval, data );
  return (0);
}


/***************************************************************************
 * hmc80_clr_led
 *   - 0=Off, 1=On (val: 0=Off, 1=On)
 **************************************************************************/
static int hmc80_clr_led( int num )
{
  unsigned long data = 0;

  /* check mapping I/O registers. */
  if ( unlikely(NULL == regs.led) ) {
    PERROR( "ioremap failed 0x%08X\n", HMC80_REG_LED );
    return (-ENOMEM);
  }

  /* set LED Off. */
  data = ioread32be( regs.led );
  iowrite32be( (data & ~HMC80_LED_MASK), regs.led );

  PDEBUG( "Clear LED num=%d\n", num );
  return (0);
}


/***************************************************************************
 * hmc80_toggle_led
 *   - 0=Off, 1=On (val: 0=Off, 1=On)
 **************************************************************************/
static int hmc80_toggle_led( int num, unsigned long val )
{
  unsigned long old = 0;    /* GPIO DAT old data */
  unsigned long tmpval = 0;

  /* check mapping I/O registers. */
  if ( unlikely(NULL == regs.led) ) {
    PERROR( "ioremap failed 0x%08X\n", HMC80_REG_LED );
    return (-ENOMEM);
  }

  /* get LED. */
  old = ioread32be( regs.led );
  PDEBUG( "old=0x%08lX\n", old );

  /* change input val. */
  tmpval = hmc80_led_usr2gpio( val );

  /* toggle LED On/Off. */
  old ^= (tmpval << HMC80_LED_SHIFT) & HMC80_LED_MASK;
  iowrite32be( old, regs.led );

  PDEBUG( "Toggle LED num=%d, val=0x%08lX tmpval=0x%08lX old=0x%08lX\n", num, val, tmpval, old);
  return (0);
}


/***************************************************************************
 * hmc80_get_vport
 *   - port: port name 
 *      RST_SM331_N -> P2IOPORT_PORT_SM331_RST_N(=0x01)
 *   - val:
 *      RST_SM331_N -> 0=No-Reset, 1=Reset
 **************************************************************************/
static int hmc80_get_vport( int port, int *val )
{
  unsigned long data = 0;

  /* check mapping I/O registers. */
  if ( unlikely(NULL == regs.port) ) {
    PERROR( "ioremap failed 0x%08X\n", HMC80_REG_PORT );
    return (-ENOMEM);
  }

  /* get RST_SM331_N. */
  if ( port & P2IOPORT_PORT_SM331_RST_N ) {
    data = ioread32be( regs.port );

    if ( data & HMC80_PORT_SM331 ) {
      *val = 0; /* No-Reset */
    } else {
      *val = 1; /* Reset */
    }
    PDEBUG( "get VPORT=0x%08X val=0x%X <- 0x%08lX\n", port, *val, data );
  }

  return (0);
}


/***************************************************************************
 * hmc80_set_vport
 *   - port: port name 
 *      RST_SM331_N -> P2IOPORT_PORT_SM331_RST_N(=0x01)
 *   - val: 
 *      RST_SM331_N -> 0=No-Reset, 1=Reset
 **************************************************************************/
static int hmc80_set_vport( int port, int val )
{
  unsigned long data = 0;

  /* check mapping I/O registers. */
  if ( unlikely(NULL == regs.port) ) {
    PERROR( "ioremap failed 0x%08X\n", HMC80_REG_PORT );
    return (-ENOMEM);
  }

  /* set RST_SM331_N. */
  if ( port & P2IOPORT_PORT_SM331_RST_N ) {
    data = ioread32be( regs.port );

    if ( val ) { /* Reset */
      data &= ~HMC80_PORT_SM331;
    } else { /* No-Reset */
      data |= HMC80_PORT_SM331;
    }

    PDEBUG( "Set VPORT=0x%08X val=0x%X ->0x%08lX\n", port, val, data );
    iowrite32be( data, regs.port );
  }

  return (0);
}


/***************************************************************************
 * hmc80_heartbeat_led
 **************************************************************************/
static inline int hmc80_heartbeat_led( int num )
{
  return hmc80_toggle_led( num, HMC80_HEARTBEAT_BIT );
}


#if defined(CONFIG_P2IOPORT_MPC83XXGPIOINT)
/***************************************************************************
 * hmc80_notify_int
 *   - unsigned long bits: GPIO status register bits
 **************************************************************************/
static int hmc80_notify_int( unsigned long bits )
{
  /* GPIO status register bits to callback number */
  /*  NOTICE: hard cording!! */
  /*   - #0 : GPIO[31]=CDO [in] */
  /*   - #1 : GPIO[16]=SDD [in] */

  PDEBUG( "bits=0x%08lX\n", bits );

  if ( bits & HMC80_INT_CDO ) {
    hmc80_exec_callback( &(cb_list[0]) );
  }

  if ( bits & HMC80_INT_SDD ) {
    hmc80_exec_callback( &(cb_list[1]) );
  }

  return (0);
}
#endif /* CONFIG_P2IOPORT_MPC83XXGPIOINT */


/***************************************************************************
 * ioctl (special commands)
 **************************************************************************/
static int hmc80_ioctl( unsigned int cmd, unsigned long arg )
{
  int retval = 0; /* return value */

  /*** main routine ***/
  switch ( cmd ) {

  case P2IOPORT_IOC_HMC80_TOGGLE_DEBUGMODE:
    {
      HMC80_DEBUG ^= 1;
      PINFO( "%s mode\n", HMC80_DEBUG?"Debug":"Non-Debug" );
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
struct p2ioport_operations hmc80_ops = {
  .get_dipsw      = hmc80_get_dipsw,     /* Get DIPSW */
  .get_led        = hmc80_get_led,       /* Get LED */
  .set_led        = hmc80_set_led,       /* Set LED */
  .clr_led        = hmc80_clr_led,       /* Clear LED */
  .toggle_led     = hmc80_toggle_led,    /* Toggle LED */
  .get_vport      = hmc80_get_vport,     /* Get VPORT */
  .set_vport      = hmc80_set_vport,     /* Set VPORT */
  .heartbeat_led  = hmc80_heartbeat_led, /* for Heartbeat LED(kernel only) */
  .ioctl          = hmc80_ioctl,         /* Ioctl(special commands) */
};


/***************************************************************************
 * hmc80_init
 **************************************************************************/
static inline int hmc80_init( struct p2ioport_info_s *info )
{
#if defined(CONFIG_P2IOPORT_MPC83XXGPIOINT)
  int i = 0;
#endif /* CONFIG_P2IOPORT_MPC83XXGPIOINT */

  /* print init Message. */
  PINFO( " board type: %s(ver. %s)\n", BOARDTYPE_NAME, HMC80_VER );

  /* init debug switch. */
  HMC80_DEBUG = 0;

  /* set info. */
  info->nr_dipsw    = HMC80_MAXNR_DIPSW;
  info->nr_led      = HMC80_MAXNR_LED;
  info->nr_rotarysw = HMC80_MAXNR_ROTARYSW;
  info->nr_device   = HMC80_MAXNR_DEVICE;
  info->ops         = &hmc80_ops;
  strcpy( info->name, BOARDTYPE_NAME );

#if defined(CONFIG_P2IOPORT_MPC83XXGPIOINT)
  for ( i = 0; i < 32; i++ ) {
    INIT_LIST_HEAD( &(info->cb_list[i]) );
  }

  /* get cb list. */
  cb_list = info->cb_list;

  /* init MPC83xx GPIO interrupt driver. */
  /* NOTICE: driver init function is called by module_init(). */

  GPIO_IMR = HMC80_INT_CDO; /* CARD_DOOR_OPEN only */
  //  GPIO_IMR = HMC80_INT_SDD;  /* SDCARD_DET_DV only */
  //  GPIO_IMR = HMC80_INT_CDO | HMC80_INT_SDD;
#endif /* CONFIG_P2IOPORT_MPC83XXGPIOINT */

  /* set IMMR base address. */
  HMC80_IMMRBAR = (unsigned long)get_immrbase();
  PDEBUG( "IMMRBAR: 0x%08lX\n", HMC80_IMMRBAR );

  /* init regs. */
  regs.dipsw = ioremap_nocache( (HMC80_REG_DIPSW+HMC80_IMMRBAR), 4 );
  if ( unlikely(NULL == regs.dipsw) ) goto EXIT;

  regs.led = regs.dipsw;
  regs.port = regs.dipsw;

  /* success */
  return (0);

  /* failed */
 EXIT:
  PERROR("ioremap failed!\n");
  return (-ENOMEM);
}


/***************************************************************************
 * hmc80_cleanup
 **************************************************************************/
static inline void hmc80_cleanup( void )
{
  PINFO( "Clean up %s\n", BOARDTYPE_NAME ); /* Message */

  /* clear debug switch. */
  HMC80_DEBUG = 0;

  /* clear regs. */
  if (regs.dipsw) iounmap(regs.dipsw);
  memset( &regs, 0, sizeof(struct hmc80_regs_s) );

#if defined(CONFIG_P2IOPORT_MPC83XXGPIOINT)
  /* cleanup MPC83xx GPIO interrupt driver. */
  MPC83xxGpioCleanupModule();
#endif /* CONFIG_P2IOPORT_MPC83XXGPIOINT */
}


/** export getting operations function. **/
inline void __p2ioport_get_ops( struct p2ioport_operations *ops )
{
  ops->get_dipsw     = hmc80_get_dipsw;
  ops->get_led       = hmc80_get_led;
  ops->set_led       = hmc80_set_led;
  ops->clr_led       = hmc80_clr_led;
  ops->toggle_led    = hmc80_toggle_led;
  ops->get_vport     = hmc80_get_vport;
  ops->set_vport     = hmc80_set_vport;
  ops->heartbeat_led = hmc80_heartbeat_led;
#if defined(CONFIG_P2IOPORT_MPC83XXGPIOINT)
  ops->notify_int    = hmc80_notify_int;
#endif /* CONFIG_P2IOPORT_MPC83XXGPIOINT */
}


/** export init/exit function. **/
inline int __p2ioport_init_info( struct p2ioport_info_s *info )
{
  return hmc80_init( info );
}


inline void __p2ioport_cleanup_info( struct p2ioport_info_s *info )
{
  memset( info, 0, sizeof(struct p2ioport_info_s) );
  hmc80_cleanup();
}


/******************** the end of the file "hmc80.c" ********************/
