/*****************************************************************************
 * linux/drivers/p2pf/p2ioport/3dp1.c
 *
 *   AG-3DP1 I/O port and GPIO access low-level driver
 *   
 *     Copyright (C) 2009-2010 Panasonic corp.
 *     All Rights Reserved.
 *
 *****************************************************************************/

/*
 * Register Map: AG-3DP1 (GPIO)

 Offset  Definition
 ----------------------------------------------------------------------------
 0x0000  - PQCNT_FPGA peripheral ctrl reg.

    --  07  06  05  04  03  02  01  00
    --  |
    --  |----------------------------- VUP_MODE(W)

 0x0001  - PQCNT_FPGA version up ctrl reg.

    --  07  06  05  04  03  02  01  00
    --  |   |   |   |   |   |   |   |- ARIA_L_RST(W)
    --  |   |   |   |   |   |   |----- ARIA_R_W(W)
    --  |   |   |   |   |   |--------- ARIA_R_RST(W)
    --  |   |   |   |   |------------- ARIA_R_W(W)
    --  |   |   |   |----------------- CAM_L_RST(W)
    --  |   |   |--------------------- CAM_L_W(W)
    --  |   |------------------------- CAM_R_RST(W)
    --  |----------------------------- CAM_R_W(W)

 0x0002  - PQCNT_FPGA JTAG ctrl reg.

    --  07  06  05  04  03  02  01  00
    --                  |   |   |   |- TCK(W)
    --                  |   |   |----- TMS(W)
    --                  |   |--------- TDI(W)
    --                  |------------- TDO(R)

 0x0003  - PQCNT_FPGA JTAG switch reg.

 0x0004  - PQCNT_FPGA Rear info reg.

 0x0005  - PQCNT_FPGA VUP route select reg.

           AERO_L   0x05
           AERO_R   0x06
           AVIO     0x07
           CAMU_L   0x09
           CAMU_R   0x0A
           CAMIO    0x0B
           CAMF_L   0x0C
           CAMF_R   0x0D

 Offset  Definition
 ----------------------------------------------------------------------------
 0xC00   - GPIO direction reg. Indicates whether a signal is used as In/Out.
 0xC08   - GPIO data reg. 0 = DIPSW On, 1 = DIPSW Off.

 GPIO function table

  [pinNo] : [I/O]: [init]  :  [Symbol]    : [function]
 -----------------------------------------------------------------
  GPIO[ 0]:  IN  :   -     : DMA_DREQ_B1  : DIPSW[4]
  GPIO[ 1]:  IN  :   -     : DMA_DACK_B1  : DIPSW[3]
  GPIO[ 2]:  IN  :   -     : DMA_DONE1    : DIPSW[2]
  GPIO[ 3]:  IN  :   -     : GPIO[3]      : DIPSW[1]
  GPIO[ 4]:  OUT :   0     : GPIO[4]      : LED[0]
  GPIO[ 5]:  OUT :   0     : GPIO[5]      : LED[1]
  GPIO[ 6]:  OUT :   0     : GPIO[6]      : LED[2]
  GPIO[ 7]:  OUT :   0     : GPIO[7]      : LED[3]
  GPIO[10]:  IN  :   -     : USBDR_PCTL0  : PCB_VER[1]
  GPIO[11]:  IN  :   -     : USBDR_PCTL1  : PCB_VER[0]
  GPIO[12]:  IN  :   -     : DMA_DREQ_B0  : 3D_MODE
  GPIO[13]:  IN  :   -     : DMA_DACK_B0  : PQ_MODE
  GPIO[14]:  IN  :   -     : DMA_DONE0    : CONF_DONE_PQCNT
  GPIO[15]:  IN  :   -     : SPI_MOSI     : ZION_RDY_N
  GPIO[16]:  OUT :   1     : SPI_MISO     : SD_LED_N
  GPIO[17]:  OUT :   0     : SPISEL       : RST_RICOH_N
  GPIO[18]:  OUT :   0     : TDM_RCK      : RST_PQ_R_N
  GPIO[19]:  OUT :   0     : TDM_RFS      : PQ_L_INFO(send)
  GPIO[20]:  IN  :   -     : TDM_RD       : PQ_R_INFO(receive)
  GPIO[21]:  OUT :   0     : TDM_TCK      : VBUS_USB_OFF_N
  GPIO[22]:  OUT :   0     : TDM_TFS      : nCONFIG_PQCNT
  GPIO[23]:  IN  :   -     : TDM_TD       : nSTATUS_PQCNT
  GPIO[26]:  IN  :   -     : TSEC2_COL    : RESERVED[1]
  GPIO[27]:  IN  :   -     : TSEC2_CRS    : RESERVED[0]

 - DIPSW
     bit0: Switching ROM/NFS boot
     bit1: Force version update
     bit2: Output Log to console
 - LED
     bit0: Heartbeat
     bit1: General perpose(for debug)
     bit2: General perpose(for debug)
     bit3: General perpose(for debug)

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
#define BOARDTYPE_NAME "AG3DP1"
#define AG3DP1_VER "0.96"

/* the number of devices */
enum AG3DP1_IOPORT_CONST {
  AG3DP1_MAXNR_DIPSW    = 1, /* 4bit-DIPSW x 1(GPIO) */
  AG3DP1_MAXNR_LED      = 1, /* 4bit-LED x 1(GPIO) */
  AG3DP1_MAXNR_ROTARYSW = 1, /* PQCNT FPGA */
  AG3DP1_MAXNR_DEVICE   = 1, /* PQCNT FPGA */
};

/* register address */
enum AG3DP1_IOPORT_REG {
  AG3DP1_REG_DIPSW       = 0xC08,      /* GPIO DAT[28-31] */
  AG3DP1_DIPSW_MASK      = 0xF0000000, /* DIPSW mask[28-31] */
  AG3DP1_DIPSW_SHIFT     = 28,         /* DIPSW bit shift[28-31] */
  AG3DP1_REG_LED         = 0xC08,      /* GPIO DAT[24-27] */
  AG3DP1_LED_MASK        = 0x0F000000, /* LED mask[24-27] */
  AG3DP1_LED_SHIFT       = 24,         /* LED bit shift[24-27] */
  AG3DP1_REG_PQ_MODE     = 0xC08,      /* GPIO DAT[18] */
  AG3DP1_PQ_MODE_MASK    = 0x00040000, /* PQ_MODE mask[18] */
  AG3DP1_PQ_MODE_SHIFT   = 18,         /* PQ_MODE bit shift[18] */
  AG3DP1_REG_USB_VBUS    = 0xC08,      /* GPIO DAT[10] */
  AG3DP1_USB_VBUS_MASK   = 0x00000400, /* USB_VBUS_OFF_N mask[10] */
  AG3DP1_USB_VBUS_SHIFT  = 10,         /* USB_VBUS_OFF_N bit shift[10] */
  AG3DP1_REG_PQ_INFO     = 0xC08,      /* GPIO DAT[11-12] */
  AG3DP1_PQ_INFO_SND_MASK = 0x00001000,/* PQ_INFO_SND(send) mask[12] */
  AG3DP1_PQ_INFO_SND_SHIFT = 12,       /* PQ_INFO_SND bit shift[12] */
  AG3DP1_PQ_INFO_RCV_MASK = 0x00000800,/* PQ_INFO_RCV(receive) mask[11] */
  AG3DP1_PQ_INFO_RCV_SHIFT = 11,       /* PQ_INFO_RCV bit shift[11] */
  AG3DP1_REG_VUP         = 0xE8000000, /* version up ctrl reg */
  AG3DP1_REG_UART_SELECT = 0xE8000001, /* FPGA UART select reg */
  AG3DP1_REG_JTAGCTRL    = 0xE8000002, /* FPGA JTAG ctrl reg */
  AG3DP1_REG_JTAG_SEL    = 0xE8000003, /* JTAG ROUTE select reg (Unused) */
  AG3DP1_REG_ROTARYSW    = 0xE8000004, /* Rear sw (8bit) */
  AG3DP1_REG_ROUTE_SEL   = 0xE8000005, /* VUP ROUTE select reg */
  AG3DP1_ROTARYSW_MASK   = 0x0000001F, /* Rear sw mask(5bit) */
};

/* bit assign */
enum AG3DP1_IOPORT_BIT {
  AG3DP1_HEARTBEAT_BIT = 0x08,         /* LED bit0 */
};

/** variables **/
static struct ag3dp1_regs_s {
  unsigned long  *dipsw;
  unsigned long  *led;
  unsigned long  *pqmode;
  unsigned long  *usbvbus;
  unsigned long  *pqinfo;
  unsigned char  *vup;
  unsigned char  *uartselect;
  unsigned char  *jtagctrl;
  unsigned char  *routesel;
  unsigned char  *rotarysw;
} regs;

static unsigned char AG3DP1_DEBUG = 0;   /* for DEBUG */
static unsigned long AG3DP1_IMMRBAR = 0; /* IMMR base address */

/** print messages **/
#define PINFO( fmt, args... )     printk( KERN_INFO "[p2ioport] " fmt, ## args)
#define PERROR( fmt, args... )    printk( KERN_ERR "[p2ioport](E)" fmt, ## args)
#define PDEBUG( fmt, args... )    do { if (AG3DP1_DEBUG) printk( KERN_INFO "[p2ioport:3dp1-l.%d] " fmt, __LINE__, ## args); } while(0)

/** prototype **/
extern phys_addr_t get_immrbase(void); /* defined at arch/powerpc/sysdev/fsl_soc.c */

/********************************* function *********************************/


/***************************************************************************
 * ag3dp1_get_dipsw
 *   - 0=On?, 1=Off? (val: 0=Off, 1=On)
 **************************************************************************/
static int ag3dp1_get_dipsw( int num, unsigned long *val )
{
  unsigned long tmpval = 0;       /* GPIO DAT value */

  /* check mapping I/O registers. */
  if ( unlikely(NULL == regs.dipsw) ) {
    PERROR( "ioremap failed 0x%08X\n", AG3DP1_REG_DIPSW );
    return (-ENOMEM);
  }

  /* get DIPSW. */
  tmpval = ioread32be(regs.dipsw);
  *val = (~tmpval & AG3DP1_DIPSW_MASK) >> AG3DP1_DIPSW_SHIFT;

  PDEBUG( "Get DIPSW num=%d, tmpval=0x%lX val=0x%lX val=0x%02X\n", num, tmpval, *val, (unsigned char)*val );
  return (0);
}

/***************************************************************************
 * ag3dp1_get_led
 *   - FPGA
 *   - 0=Off, 1=On (val: 0=Off, 1=On)
 **************************************************************************/
static int ag3dp1_get_led( int num, unsigned long *val )
{
  unsigned long tmpval = 0;

  /* check mapping I/O registers. */
  if ( unlikely(NULL == regs.led) ) {
    PERROR( "ioremap failed 0x%08X\n", AG3DP1_REG_LED );
    return (-ENOMEM);
  }

  /* get LED setting. */
  tmpval = (unsigned long)ioread8( regs.led );
  *val = tmpval & AG3DP1_LED_MASK;

  PDEBUG( "Get LED num=%d, val=0x%08lX tmpval=0x%08lX\n", num, *val, tmpval );
  return (0);
}

/***************************************************************************
 * ag3dp1_set_led
 *   - 0=On?, 1=Off? (val: 0=Off, 1=On)
 **************************************************************************/
/* static int ag3dp1_set_led( int num, unsigned long val ) */
int ag3dp1_set_led( int num, unsigned long val )
{
  unsigned long data;

  /* check mapping I/O registers. */
  if ( unlikely(NULL == regs.led) ) {
    PERROR( "ioremap failed 0x%08X\n", AG3DP1_REG_LED );
    return (-ENOMEM);
  }

  /* set LED On/Off. */  
  data = ioread32be( regs.led );
  data = ( (val << AG3DP1_LED_SHIFT) & AG3DP1_LED_MASK ) | (data & ~AG3DP1_LED_MASK);
  iowrite32be( data, regs.led );

  PDEBUG( "Set LED num=%d, val=0x%lX 0x%lX\n", num, val, data );
  return (0);
}


/***************************************************************************
 * ag3dp1_clr_led
 *   - 0=On?, 1=Off? (val: 0=Off, 1=On)
 **************************************************************************/
static int ag3dp1_clr_led( int num )
{
  unsigned long data;

  /* check mapping I/O registers. */
  if ( unlikely(NULL == regs.led) ) {
    PERROR( "ioremap failed 0x%08X\n", AG3DP1_REG_LED );
    return (-ENOMEM);
  }

  /* set Led Off. */
  data = ioread32be( regs.led );
  iowrite32be( (data & ~AG3DP1_LED_MASK), regs.led );

  PDEBUG( "Clear LED num=%d\n", num );
  return (0);
}


/***************************************************************************
 * ag3dp1_toggle_led
 *   - 0=On?, 1=Off? (val: 0=Off, 1=On)
 **************************************************************************/
static int ag3dp1_toggle_led( int num, unsigned long val )
{
  unsigned long old = 0;        /* GPIO DAT old data */

  /* check mapping I/O registers. */
  if ( unlikely(NULL == regs.led) ) {
    PERROR( "ioremap failed 0x%08X\n", AG3DP1_REG_LED );
    return (-ENOMEM);
  }

  /* get LED. */
  old = ioread32be( regs.led );
  PDEBUG( "old=0x%08lX\n", old );

  /* toggle LED On/Off. */
  old ^= (val << AG3DP1_LED_SHIFT) & AG3DP1_LED_MASK;
  iowrite32be( old, regs.led );

  PDEBUG( "Toggle LED num=%d, val=0x%lX old=0x%lX\n", num, val, old);
  return (0);
}

/***************************************************************************
 * ag3dp1_get_rotarysw
 *   - FPGA
 *   - val: 0x0 - 0xF
 **************************************************************************/
static int ag3dp1_get_rotarysw( int num, unsigned long *val )
{
  unsigned long data;

  /* check mapping I/O registers. */
  if ( unlikely(NULL == regs.rotarysw) ) {
    PERROR( "ioremap failed 0x%08X\n", AG3DP1_REG_ROTARYSW );
    return (-ENOMEM);
  }

  /* get ROTARYSW. */
  data = (unsigned long)(ioread8(regs.rotarysw)) & AG3DP1_ROTARYSW_MASK;

  switch(data){
    case 0x00:  /* Normal mode */
      *val = 0x00;
      break;
    case 0x13:  /* PQ_L(debug mode) */
    case 0x14:  /* PQ_R(debug mode) */
    case 0x0F:  /* PQ(getLog mode) */
      *val = 0x01;
      break;
    case 0x03:  /* PQ_L(console mode) */
    case 0x04:  /* PQ_R(console mode) */
      *val = 0x02;
      break;
    case 0x01:  /* EVR_L(R) */
      *val = 0x03;
      break;
    case 0x02:  /* EVR_R */
      *val = 0x04;
      break;
    case 0x05:  /* AERO_L */
    case 0x15:  /* AERO_L(dipsw:ON) */
      *val = 0x05;
      break;
    case 0x06:  /* AERO_R */
    case 0x16:  /* AERO_R(dipsw:ON) */
      *val = 0x06;
      break;
    case 0x07:  /* AVIO */
    case 0x17:  /* AVIO(dipsw:ON) */
      *val = 0x07;
      break;
    case 0x09:  /* CAM_micom_L */
    case 0x19:  /* CAM_micom_L(dipsw:ON) */
      *val = 0x09;
      break;
    case 0x0A:  /* CAM_micom_R */
    case 0x1A:  /* CAM_micom_R(dipsw:ON) */
      *val = 0x0A;
      break;
    default:    /* Other mode */
      *val = 0xFF;
  }

  PDEBUG( "Get RotarySW num=%d, val=0x%08lX\n", num, *val );
  return (0);
}

/***************************************************************************
 * ag3dp1_get_vport
 *   - port: port name 
 *   - val : 0=stop/off/disable, 1=start/on/enable
 **************************************************************************/
static int ag3dp1_get_vport( int port, int *val )
{
  unsigned char data = 0;
  unsigned char bitshift = 0;
  unsigned long data32 = 0;

  switch ( port&0xF0 ) {

  /* VUP ctrl */
  case 0x30:
    {
      PDEBUG( "VUP ctrl\n" );

      /* check mapping I/O registers. */
      if ( unlikely(NULL == regs.vup) ) {
        PERROR( "ioremap failed 0x%08X\n", AG3DP1_REG_VUP );
        return (-ENOMEM);
      }
      data = ioread8( regs.vup );
      bitshift = port & 0x00000007;

      if ( data & (1<<bitshift) ) {
        *val = 0; /* Normal */
      } else {
        *val = 1; /* Reset or VUP */
      }
    
      break;
    }

  /* UART select ctrl */
  case 0x50:
    {
      PDEBUG( "UART select ctrl\n" );

      /* check mapping I/O registers. */
      if ( unlikely(NULL == regs.uartselect) ) {
        PERROR( "ioremap failed 0x%08X\n", AG3DP1_REG_UART_SELECT );
        return (-ENOMEM);
      }
      data = ioread8( regs.uartselect );
      bitshift = port & 0x00000007;

      if ( data & (1<<bitshift) ) {
        *val = 1; /* buffer enable */
      } else {
        *val = 0; /* buffer disable */
      }

      break;
    }

  /* Route select ctrl */
  case 0x70:
    {
      PDEBUG( "Route select ctrl\n" );

      /* check mapping I/O registers. */
      if ( unlikely(NULL == regs.routesel) ) {
        PERROR( "ioremap failed 0x%08X\n", AG3DP1_REG_ROUTE_SEL );
        return (-ENOMEM);
      }
      data = ioread8( regs.routesel );
      bitshift = port & 0x0000000F;

      if ( bitshift == (data & 0x0000000F) ) {
        *val = 1; /* route enable */
      } else {
        *val = 0; /* route disable */
      }

      break;
    }

  /* USB VBUS control */
  case 0x90:
    {
      PDEBUG( "USB VBUS control\n" );

      /* check mapping I/O registers. */
      if ( unlikely(NULL == regs.usbvbus) ) {
        PERROR( "ioremap failed 0x%08X\n", AG3DP1_REG_USB_VBUS );
        return (-ENOMEM);
      }

      /* get USB_VBUS */
      data32 = ioread32be(regs.usbvbus);

      if ( data32 & AG3DP1_USB_VBUS_MASK ) {
        *val = 1; /* USB VBUS on */
      } else {
        *val = 0; /* USB VBUS off */
      }

      break;
    }

  /* PQ INFO reg */
  case 0xA0:
    {
      PDEBUG( "PQ INFO reg\n" );

      /* check mapping I/O registers. */
      if ( unlikely(NULL == regs.pqinfo) ) {
        PERROR( "ioremap failed 0x%08X\n", AG3DP1_REG_PQ_INFO );
        return (-ENOMEM);
      }

      /* get PQ INFO */
      data32 = ioread32be(regs.pqinfo);

      if ( (port & 0x01) ) {	/* If access to revieve port */
        PDEBUG( "Access to PQ INFO_RCV port\n" );
        if ( data32 & AG3DP1_PQ_INFO_RCV_MASK ) {
          *val = 1; /* PQ INFO on */
        } else {
          *val = 0; /* PQ INFO off */
        }
      } else {
        PDEBUG( "Access to PQ INFO_SND post\n" );
        if ( data32 & AG3DP1_PQ_INFO_SND_MASK ) {
          *val = 1; /* PQ INFO on */
        } else {
          *val = 0; /* PQ INFO off */
        }
      }
      break;
    }

  default:
    {
      PERROR( "Unknown VPORT number: 0x%08X\n", port );
      break;
    }
  } /* the end of switch */

  PDEBUG( "get VPORT=0x%08X val=0x%X <- 0x%02X bitshift=%d\n", port, *val, data, bitshift );
  
  return (0);
}


/***************************************************************************
 * ag3dp1_set_vport
 *   - port: port name 
 *   - val : 0=stop/off/disable, 1=start/on/enable
 **************************************************************************/
static int ag3dp1_set_vport( int port, int val )
{
  unsigned char bitshift = 0;
  unsigned char data = 0;
  unsigned long data32 = 0;

  switch ( port&0xF0 ) {

  /* VUP ctrl */
  case 0x30:
    {
      PDEBUG( "VUP ctrl\n" );

      /* check mapping I/O registers. */
      if ( unlikely(NULL == regs.vup) ) {
        PERROR( "ioremap failed 0x%08X\n", AG3DP1_REG_VUP );
        return (-ENOMEM);
      }

      bitshift = port & 0x00000007;
      data = ioread8( regs.vup );

      if ( val ) {
        data &= ~(1<<bitshift); /* Reset or VUP */
      } else {
        data |= 1<<bitshift; /* Normal */
      }
    
      iowrite8( data, regs.vup );
      break;
    }

  /* UART select ctrl */
  case 0x50:
    {
      PDEBUG( "UART select ctrl\n" );

      /* check mapping I/O registers. */
      if ( unlikely(NULL == regs.uartselect) ) {
        PERROR( "ioremap failed 0x%08X\n", AG3DP1_REG_UART_SELECT );
        return (-ENOMEM);
      }

      bitshift = port & 0x00000007;
      data = ioread8( regs.uartselect );

      if ( val ) {
        data |= 1<<bitshift; /* buffer enable */
      } else {
        data &= ~(1<<bitshift); /* buffer disable */
      }

      iowrite8( data, regs.uartselect );
      break;
    }

  /* Route select ctrl */
  case 0x70:
    {
      PDEBUG( "Route select ctrl\n" );

      /* check mapping I/O registers. */
      if ( unlikely(NULL == regs.routesel) ) {
        PERROR( "ioremap failed 0x%08X\n", AG3DP1_REG_ROUTE_SEL );
        return (-ENOMEM);
      }

      if ( val ) {
        data = (unsigned char)(port & 0x0000000F);
      } else {
        data = 0;
      }

      iowrite8( data, regs.routesel );
      break;
    }

  /* USB VBUS control */
  case 0x90:
    {
      PDEBUG( "USB VBUS control\n" );

      /* check mapping I/O registers. */
      if ( unlikely(NULL == regs.usbvbus) ) {
        PERROR( "ioremap failed 0x%08X\n", AG3DP1_REG_USB_VBUS );
        return (-ENOMEM);
      }

      /* get USB_VBUS */
      data32 = ioread32be(regs.usbvbus);

      /* set USB_VBUS */
      if ( val ) {
        data32 |= AG3DP1_USB_VBUS_MASK;		/* ON */
      } else {
        data32 &= ~AG3DP1_USB_VBUS_MASK;	/* OFF */
      }

      /* set USB_VBUS */
      iowrite32be( data32, regs.usbvbus );
      break;
    }

  /* PQ INFO reg */
  case 0xA0:
    {
      PDEBUG( "PQ INFO reg\n" );

      /* check mapping I/O registers. */
      if ( unlikely(NULL == regs.pqinfo) ) {
        PERROR( "ioremap failed 0x%08X\n", AG3DP1_REG_PQ_INFO );
        return (-ENOMEM);
      }

      /* get PQ INFO */
      data32 = ioread32be(regs.pqinfo);

      if ( (port & 0x01) ) {	/* If access to revieve port */
        PERROR( "can not access to recieve reg.\n");
        return (-ENOMEM);
      } else {
        if ( val ) {
          data32 |= AG3DP1_PQ_INFO_SND_MASK;	/* ON */
        } else {
          data32 &= ~AG3DP1_PQ_INFO_SND_MASK;	/* OFF */
        }
      }

      /* set PQ INFO */
      iowrite32be( data32, regs.pqinfo );
      break;
    }

  default:
    {
      PERROR( "Unknown VPORT number: 0x%08X\n", port );
      break;
    }
  } /* the end of switch */

  PDEBUG( "set VPORT=0x%08X val=0x%X -> 0x%02X bitshift=%d\n", port, val, data, bitshift );
  return (0);
}

/***************************************************************************
 * ag3dp1_heartbeat_led
 **************************************************************************/
static inline int ag3dp1_heartbeat_led( int num )
{
  return ag3dp1_toggle_led( num, AG3DP1_HEARTBEAT_BIT );
}

/***************************************************************************
 * ag3dp1_ioc_jtag_ctrl
 *   - special command
 *   - Read 4bit, Write 3bit
 **************************************************************************/
static int ag3dp1_ioc_jtag_ctrl( int dir, unsigned char *val )
{
  /* check mapping I/O registers. */
  if ( unlikely(NULL == regs.jtagctrl) ) {
    PERROR( "ioremap failed 0x%08X\n", AG3DP1_REG_JTAGCTRL );
    return (-ENOMEM);
  }

  if ( 1 == dir ) {
    /* Write reg. */
    PDEBUG( "Set JTAG ctrl val=0x%02X\n", *val & 0x07 );

    iowrite8( (*val & 0x07), regs.jtagctrl );
  } else {
    /* Read reg. */
    *val = ioread8( regs.jtagctrl ) & 0x0F;

    PDEBUG( "Get JTAG ctrl val=0x%02X\n", *val );
  }

  return (0);
}

/***************************************************************************
 * ag3dp1_ioc_pq_mode_detection
 *   - special command
 *   - 1bit, read only
 **************************************************************************/
static int ag3dp1_ioc_pq_mode_detection( unsigned char *val )
{
  unsigned long tmpval = 0;

  /* check mapping I/O registers. */
  if ( unlikely(NULL == regs.pqmode) ) {
    PERROR( "ioremap failed 0x%08X\n", AG3DP1_REG_PQ_MODE );
    return (-ENOMEM);
  }

  /* Read reg. */
  tmpval = ioread32be(regs.pqmode);
  *val = (unsigned char)((tmpval & AG3DP1_PQ_MODE_MASK) >> AG3DP1_PQ_MODE_SHIFT);

  PDEBUG( "Get PQ_MODE val=0x%02X tmpval=0x%08lX\n", *val, tmpval );
  return (0);
}

/***************************************************************************
 * ioctl (special commands)
 **************************************************************************/
static int ag3dp1_ioctl( unsigned int cmd, unsigned long arg )
{
  int retval = 0;        /* return value */

  /*** main routine ***/
  switch ( cmd ) {

  case P2IOPORT_IOC_3DP1_JTAGCTRL:
    {
      struct p2ioport_rwval_s info;

      /* get values. */
      if ( copy_from_user((void *)&info,
                          (void *)arg,
                          sizeof(struct p2ioport_rwval_s)) ) {
        PERROR( "copy_from_user failed at P2IOPORT_IOC_3DP1_JTAGCTRL!\n" );
        return (-EFAULT);
      }

      /* set or get JTAG ctrl. */
      retval = ag3dp1_ioc_jtag_ctrl( info.dir, &(info.val) );

      /* put values. */
      if ( copy_to_user((void *)arg,
                        (void *)&info,
                        sizeof(struct p2ioport_rwval_s)) ) {
        PERROR( "copy_to_user failed at P2IOPORT_IOC_3DP1_JTAGCTRL!\n" );
        return (-EFAULT);
      }

      PDEBUG( "JTAG ctrl dir=%d, val=0x%02X\n", info.dir, info.val );
      break;
    }

  case P2IOPORT_IOC_3DP1_PQ_MODE_DET:
    {
      unsigned char val = 0;

      /* get GPIO[13] PQ_MODE */
      retval = ag3dp1_ioc_pq_mode_detection( &val );

      /* put value. */
      if ( put_user(val, (unsigned char __user *)arg) ) {
        PERROR( "put_user failed at P2IOPORT_IOC_3DP1_PQ_MODE_DETECTION!\n" );
        return (-EFAULT);
      }

      PDEBUG( "PQ_MODE detection val=0x%02X\n", val );
      break;
    }

  case P2IOPORT_IOC_3DP1_TOGGLE_DEBUGMODE:
    {
      AG3DP1_DEBUG ^= 1;
      PINFO( "%s mode\n", AG3DP1_DEBUG?"Debug":"Non-Debug" );
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
struct p2ioport_operations ag3dp1_ops = {
  .get_dipsw     = ag3dp1_get_dipsw,     /* Get DIPSW */
  .get_led       = ag3dp1_get_led,       /* Get LED */
  .set_led       = ag3dp1_set_led,       /* Set LED */
  .clr_led       = ag3dp1_clr_led,       /* Clear LED */
  .toggle_led    = ag3dp1_toggle_led,    /* Toggle LED */
  .get_rotarysw  = ag3dp1_get_rotarysw,  /* Get RotarySW */
  .get_vport     = ag3dp1_get_vport,     /* Set VPORT */
  .set_vport     = ag3dp1_set_vport,     /* Set VPORT */
  .heartbeat_led = ag3dp1_heartbeat_led, /* for Heartbeat LED(kernel only) */
  .ioctl         = ag3dp1_ioctl,         /* Ioctl(special commands) */
};


/***************************************************************************
 * ag3dp1_init
 **************************************************************************/
static inline int ag3dp1_init( struct p2ioport_info_s *info )
{
  /* print init Message. */
  PINFO( " board type: %s(ver. %s)\n", BOARDTYPE_NAME, AG3DP1_VER );

  /* init debug switch. */
  AG3DP1_DEBUG = 0;

  /* set info. */
  info->nr_dipsw    = AG3DP1_MAXNR_DIPSW;
  info->nr_led      = AG3DP1_MAXNR_LED;
  info->nr_rotarysw = AG3DP1_MAXNR_ROTARYSW;
  info->nr_device   = AG3DP1_MAXNR_DEVICE;
  info->ops         = &ag3dp1_ops;
  strcpy( info->name, BOARDTYPE_NAME );

  /* set IMMR base address. */
  AG3DP1_IMMRBAR = (unsigned long)get_immrbase();
  PDEBUG( "IMMRBAR: 0x%08lX\n", AG3DP1_IMMRBAR );

  /* init regs. */
  regs.dipsw = ioremap( (AG3DP1_REG_DIPSW+AG3DP1_IMMRBAR), 4 );
  if ( unlikely(NULL == regs.dipsw) ) goto EXIT;

  regs.led   = ioremap( (AG3DP1_REG_LED+AG3DP1_IMMRBAR)  , 4 );
  if ( unlikely(NULL == regs.led) ) goto FAIL_LED;

  regs.pqmode = regs.dipsw;
  regs.usbvbus = regs.dipsw;
  regs.pqinfo = regs.dipsw;

  /*  PQCNT FPGA */
  regs.vup        = ioremap_nocache( AG3DP1_REG_VUP,      1 );
  regs.uartselect = ioremap_nocache( AG3DP1_REG_UART_SELECT,   1 );
  regs.jtagctrl   = ioremap_nocache( AG3DP1_REG_JTAGCTRL, 1 );
  regs.routesel   = ioremap_nocache( AG3DP1_REG_ROUTE_SEL,   1 );
  regs.rotarysw   = ioremap_nocache( AG3DP1_REG_ROTARYSW, 1 );
  if ( unlikely(   NULL == regs.vup
                                || NULL == regs.uartselect
                                || NULL == regs.jtagctrl
                                || NULL == regs.routesel
                                || NULL == regs.rotarysw) ) goto FAIL;

  /* success. */
  return (0);

 FAIL:
  if (regs.vup)        iounmap(regs.vup);
  if (regs.uartselect) iounmap(regs.uartselect);
  if (regs.jtagctrl)   iounmap(regs.jtagctrl);
  if (regs.routesel)   iounmap(regs.routesel);
  if (regs.rotarysw)   iounmap(regs.rotarysw);

 FAIL_LED:
  iounmap( regs.dipsw );
  regs.dipsw = NULL;

 EXIT:
  PERROR("ioremap failed!\n");
  return (-ENOMEM);
}


/***************************************************************************
 * ag3dp1_cleanup
 **************************************************************************/
static inline void ag3dp1_cleanup( void )
{
  PINFO( "Clean up %s\n", BOARDTYPE_NAME ); /* Message */

  /* clear debug switch. */
  AG3DP1_DEBUG = 0;

  /* clear regs. */
  if (regs.dipsw)      iounmap(regs.dipsw);
  if (regs.led)        iounmap(regs.led);
  if (regs.vup)        iounmap(regs.vup);
  if (regs.uartselect) iounmap(regs.uartselect);
  if (regs.jtagctrl)   iounmap(regs.jtagctrl);
  if (regs.routesel)   iounmap(regs.routesel);
  if (regs.rotarysw)   iounmap(regs.rotarysw);

  memset( &regs, 0, sizeof(struct ag3dp1_regs_s) );
}


/** export getting operations function. **/
inline void __p2ioport_get_ops( struct p2ioport_operations *ops )
{
  ops->get_dipsw     = ag3dp1_get_dipsw;
  ops->set_led       = ag3dp1_set_led;
  ops->clr_led       = ag3dp1_clr_led;
  ops->toggle_led    = ag3dp1_toggle_led;
  ops->get_rotarysw  = ag3dp1_get_rotarysw;
  ops->get_vport     = ag3dp1_get_vport;
  ops->set_vport     = ag3dp1_set_vport;
  ops->heartbeat_led = ag3dp1_heartbeat_led;
}


/** export init/exit function. **/
inline int __p2ioport_init_info( struct p2ioport_info_s *info )
{
  return ag3dp1_init( info );
}


inline void __p2ioport_cleanup_info( struct p2ioport_info_s *info )
{
  memset( info, 0, sizeof(struct p2ioport_info_s) );
  ag3dp1_cleanup();
}


/******************** the end of the file "3dp1.c" ********************/
