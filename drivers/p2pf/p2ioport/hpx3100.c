/*****************************************************************************
 * linux/drivers/p2pf/p2ioport/hpx3100.c
 *
 *   AJ-HPX3100 I/O port and GPIO access low-level driver
 *   
 *     Copyright (C) 2010 Panasonic corp.
 *     All Rights Reserved.
 *
 *****************************************************************************/

/*
 * Register Map: AJ-HPX3100 (GPIO & FPGA)

 Offset  Definition
 ----------------------------------------------------------------------------
 0x0000  - PQCNT_FPGA peripheral ctrl reg.

    --  07  06  05  04  03  02  01  00
    --  |   |   |   |   |           |- ZION_RDY_N(R)
    --  |   |   |   |   |------------- PC_MODE[0](R)
    --  |   |   |   |----------------- PC_MODE[1](R)
    --  |   |   |--------------------- PC_MODE[2](R)
    --  |   |------------------------- PC_MODE[3](R)
    --  |----------------------------- PC_MODE[4](R)

 0x0001  - PQCNT_FPGA version reg. bit0-1: PCB_VER[1:0](R)
 0x0002  - PQCNT_FPGA LED ctrl reg.

    --  07  06  05  04  03  02  01  00
    --  |               |- LED[3:0] -|
    --  |----------------- SD_LED

 0x0003  - PQCNT_FPGA version up ctrl reg.

    --  07  06  05  04  03  02  01  00
    --  |   |   |   |   |   |   |   |- SYSCON_RST(W)
    --  |   |   |   |   |   |   |----- SYSCON_W(W)
    --  |   |   |   |   |   |--------- CAM_RST(W)
    --  |   |   |   |   |------------- CAM_W(W)
    --  |   |   |   |----------------- CAM_EXT_W(W)
    --  |   |   |--------------------- LCD_RST(W)
    --  |   |------------------------- LCD_W(W)
    --  |----------------------------- VUP_MODE_N(W)

 0x0004  - PQCNT_FPGA JTAG ctrl reg.

    --  07  06  05  04  03  02  01  00
    --                  |   |   |   |- TCK(W)
    --                  |   |   |----- TMS(W)
    --                  |   |--------- TDI(W)
    --                  |------------- TDO(R)

 0x0005  - PQCNT_FPGA JTAG switch reg.

    --  07  06  05  04  03  02  01  00
    --              |   |   |   |   |- FM(W)
    --              |   |   |   |----- CHAR(W)
    --              |   |   |--------- PULSE(W)
    --              |   |------------- PXY_FPGA(W)
    --              |----------------- PXY_DSP(W)

 0x0006  - PQCNT_FPGA reset ctrl reg.

    --  07  06  05  04  03  02  01  00
    --              |               |- RICOH_RST(W)
    --              |----------------- PXY_DSP_TRST(W)

 0x0007  - PQCNT_FPGA USB ctrl reg.

    --  07  06  05  04  03  02  01  00
    --      |   |   |   |   |   |   |- RST_USBHUB_N(W)
    --      |   |   |   |   |   |----- USB_OE_N
    --      |   |   |   |   |--------- USB_HOST_N(W)
    --      |   |   |   |------------- USB_VBUS_ON_P[0](W)
    --      |   |   |----------------- USB_VBUS_ON_P[1](W)
    --      |   |--------------------- USB_OCI_N[0](R)
    --      |------------------------- USB_OCI_N[1](R)

 0x0008
 0x0009  - PQCNT_FPGA rear switch reg.
            0: -
            1: PQ_DEBUG
            2: CHAR_FPGA
            2: FMUC_FPGA
            4: PULSE_FPGA
            5: PROXY_FPGA
            6: PROXY_DSP
            7: -
            8: AERO(w/RESET+WP)
            9: CAMuCOM(w/RESET+WP)
            A: CAM Flash(w/RESET+WP)
            B: LCDuCOM(w/RESET+WP)
            C: AERO
            D: CAMuCOM
            E: LCDuCOM
            F: PowerQUICC

 Offset  Definition
 ----------------------------------------------------------------------------
 0xC00   - GPIO direction reg. Indicates whether a signal is used as In/Out.
 0xC08   - GPIO data reg. 0 = DIPSW On, 1 = DIPSW Off.

 GPIO function table

  [pinNo] : [I/O]: [init]  :  [symbol]   : [function]
 -----------------------------------------------------------------
  GPIO[10]:  IN  :   -     : IIC2_SDA    : nSTATUS_PQCNT
  GPIO[11]:  IN  :   -     : IIC2_SCL    : CONF_DONE_PQCNT
  GPIO[15]:  IN  :   -     : TSEC2_COL   : RSV_SYS_PQ1(PCB_PAT[1])
  GPIO[16]:  IN  :   -     : TSEC2_CRS   : RSV_SYS_PQ2(PCB_PAT[0])
  GPIO[24]:  OUT :   0     : TSEC2_RX_ER : nCONFIG_PQCNT
  GPIO[25]:  IN  :   -     : TSEC2_TX_CLK: PXY_DET_BOARD_N
  GPIO[28]:  IN  :   -     : SPIMOSI     : PQ_DIPSW[2]
  GPIO[29]:  IN  :   -     : SPIMISO     : PQ_DIPSW[1]
  GPIO[30]:  IN  :   -     : SPICLK      : PQ_DIPSW[0]
  GPIO[31]:  IN  :   -     : SPISEL      : PQ_DIPSW[3]


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
#define BOARDTYPE_NAME "HPX3100"
#define HPX3100_VER "1.00"

/* the number of devices */
enum HPX3100_IOPORT_CONST {
  HPX3100_MAXNR_DIPSW    = 1, /* 4bit-DIPSW x 1 (GPIO) */
  HPX3100_MAXNR_LED      = 1, /* 4bit-LED x 1 (PQCNT FPGA) */
  HPX3100_MAXNR_ROTARYSW = 1, /* PQCNT FPGA */
  HPX3100_MAXNR_DEVICE   = 1, /* PQCNT FPGA */
};

/* register address */
enum HPX3100_IOPORT_REG {
  HPX3100_REG_DIPSW     = 0xC08,              /* GPIO DAT[31:28] */
  HPX3100_DIPSW_MASK    = 0x0000000F,         /* DIPSW mask[31:28] */
  HPX3100_DIPSW_SHIFT   = 0,                  /* DIPSW bit shift[31:28] */
  HPX3100_REG_PXYDET    = 0xC08,              /* GPIO DAT[25] */
  HPX3100_PXYDET_MASK   = 0x00000040,         /* PXY_DET_BOARD_N mask[25] */
  HPX3100_PXYDET_SHIFT  = 6,                  /* PXY_DET bit shift[25] */
  HPX3100_REG_LED       = 0xE8000002,         /* FPGA LED reg */
  HPX3100_LED_MASK      = 0x0F,               /* FPGA LED mask[3:0]*/
  HPX3100_LED_SHIFT     = 0,                  /* FPGA LED bit shift[3:0]*/
  HPX3100_REG_ROTARYSW  = 0xE8000008,         /* FPGA rear sw (16bit) */
  HPX3100_ROTARYSW_MASK = 0x0000FFFF,         /* FPGA rear sw mask(16bit) */
  HPX3100_REG_VER       = 0xE8000001,         /* FPGA version reg */
  HPX3100_VER_MASK      = 0x0000000000000003, /* FPGA version mask(64bit) */
  HPX3100_REG_PRPHRL    = 0xE8000000,         /* FPGA peripheral ctrl reg */
  HPX3100_REG_VUP       = 0xE8000003,         /* FPGA version up ctrl reg */
  HPX3100_REG_JTAGCTRL  = 0xE8000004,         /* FPGA JTAG ctrl reg */
  HPX3100_REG_JTAGSW    = 0xE8000005,         /* FPGA JTAG sw reg */
  HPX3100_REG_RSTCTRL   = 0xE8000006,         /* FPGA reset ctrl reg */
  HPX3100_REG_USBCTRL   = 0xE8000007,         /* FPGA USB ctrl reg */
};

/* bit assign */
enum HPX3100_IOPORT_BIT {
  HPX3100_HEARTBEAT_BIT = 0x01,      /* FPGA LED bit0 */
};


/** variables **/
static struct hpx3100_regs_s {
  unsigned long  *dipsw;
  unsigned long  *pxydet;
  unsigned char  *led;
  unsigned short *rotarysw;
  unsigned char  *ver;
/*   unsigned char  *peripheral; */
  unsigned char  *vup;
  unsigned char  *jtagctrl;
  unsigned char  *jtagsw;
  unsigned char  *rstctrl;
  unsigned char  *usbctrl;
} regs;

static unsigned char HPX3100_DEBUG = 0;	/* for DEBUG */
static unsigned long HPX3100_IMMRBAR = 0; /* IMMR base address */

/** print messages **/
#define PINFO( fmt, args... )	printk( KERN_INFO "[p2ioport] " fmt, ## args)
#define PERROR( fmt, args... )	printk( KERN_ERR "[p2ioport](E)" fmt, ## args)
#define PDEBUG( fmt, args... )	do { if (HPX3100_DEBUG) printk( KERN_INFO "[p2ioport:hpx3100-l.%d] " fmt, __LINE__, ## args); } while(0)

/** prototype **/
extern phys_addr_t get_immrbase(void); /* defined at arch/powerpc/sysdev/fsl_soc.c */


/********************************* function *********************************/


/***************************************************************************
 * hpx3100_get_dipsw
 *   - GPIO
 *   - 0=On, 1=Off (val: 0=Off, 1=On)
 **************************************************************************/
static int hpx3100_get_dipsw( int num, unsigned long *val )
{
  unsigned long tmpval = 0;       /* GPIO DAT value */

  /* check mapping I/O registers. */
  if ( unlikely(NULL == regs.dipsw) ) {
    PERROR( "ioremap failed 0x%08X\n", HPX3100_REG_DIPSW );
    return (-ENOMEM);
  }

  /* get DIPSW. */
  tmpval = ioread32be(regs.dipsw);
  *val = (~tmpval & HPX3100_DIPSW_MASK) >> HPX3100_DIPSW_SHIFT;

  PDEBUG( "Get DIPSW num=%d, tmpval=0x%08lX val=0x%08lX val=0x%02X\n", num, tmpval, *val, (unsigned char)*val );
  return (0);
}


/***************************************************************************
 * hpx3100_get_led
 *   - FPGA
 *   - 0=Off, 1=On (val: 0=Off, 1=On)
 **************************************************************************/
static int hpx3100_get_led( int num, unsigned long *val )
{
  unsigned long tmpval = 0;

  /* check mapping I/O registers. */
  if ( unlikely(NULL == regs.led) ) {
    PERROR( "ioremap failed 0x%08X\n", HPX3100_REG_LED );
    return (-ENOMEM);
  }

  /* get LED setting. */
  tmpval = (unsigned long)ioread8( regs.led );
  *val = tmpval & HPX3100_LED_MASK;

  PDEBUG( "Get LED num=%d, val=0x%08lX tmpval=0x%08lX\n", num, *val, tmpval );
  return (0);
}


/***************************************************************************
 * hpx3100_set_led
 *   - FPGA
 *   - 0=Off, 1=On (val: 0=Off, 1=On)
 **************************************************************************/
static int hpx3100_set_led( int num, unsigned long val )
{
  unsigned char data = 0;
  unsigned char tmpdata = 0;

  /* check mapping I/O registers. */
  if ( unlikely(NULL == regs.led) ) {
    PERROR( "ioremap failed 0x%08X\n", HPX3100_REG_LED );
    return (-ENOMEM);
  }

  /* set LED On/Off. */
  tmpdata = ioread8( regs.led );
  data = (tmpdata & ~HPX3100_LED_MASK) | ((unsigned char)val & HPX3100_LED_MASK);
  iowrite8( data, regs.led );

  PDEBUG( "Set LED num=%d, val=0x%08lX tmpdata=0x%02X data=0x%02X\n", num, val, tmpdata, data );
  return (0);
}


/***************************************************************************
 * hpx3100_clr_led
 *   - FPGA
 *   - 0=Off, 1=On (val: 0=Off, 1=On)
 **************************************************************************/
static int hpx3100_clr_led( int num )
{
  unsigned char data = 0;

  /* check mapping I/O registers. */
  if ( unlikely(NULL == regs.led) ) {
    PERROR( "ioremap failed 0x%08X\n", HPX3100_REG_LED );
    return (-ENOMEM);
  }

  /* set LED Off. */
  data = ioread8( regs.led );
  iowrite8( (data & ~HPX3100_LED_MASK), regs.led );

  PDEBUG( "Clear LED num=%d data=0x%02X\n", num, data );
  return (0);
}


/***************************************************************************
 * hpx3100_toggle_led
 *   - FPGA
 *   - 0=Off, 1=On (val: 0=Off, 1=On)
 **************************************************************************/
static int hpx3100_toggle_led( int num, unsigned long val )
{
  unsigned char old = 0;    /* old data */

  /* check mapping I/O registers. */
  if ( unlikely(NULL == regs.led) ) {
    PERROR( "ioremap failed 0x%08X\n", HPX3100_REG_LED );
    return (-ENOMEM);
  }

  /* get LED. */
  old = ioread8( regs.led );
  PDEBUG( "old=0x%02X\n", old );

  /* toggle LED On/Off. */
  old ^= (unsigned char)val & HPX3100_LED_MASK;
  iowrite8( old, regs.led );

  PDEBUG( "Toggle LED num=%d, val=0x%08lX old=0x%02X\n", num, val, old );
  return (0);
}


/***************************************************************************
 * hpx3100_get_rotarysw
 *   - FPGA
 *   - val: 0x0 - 0xF
 **************************************************************************/
static int hpx3100_get_rotarysw( int num, unsigned long *val )
{
  unsigned long tmpval1 = 0;       /* FPGA reg value */
  unsigned long tmpval2 = 0;      /* output of find first bit */

  /* check mapping I/O registers. */
  if ( unlikely(NULL == regs.rotarysw) ) {
    PERROR( "ioremap failed 0x%08X\n", HPX3100_REG_ROTARYSW );
    return (-ENOMEM);
  }

  /* get ROTARYSW. */
  tmpval1 = (unsigned long)(ioread16(regs.rotarysw)) & HPX3100_ROTARYSW_MASK;

  /* convert to bit position. */
  tmpval2 = ffs( tmpval1 ); /* defined at arch/powerpc/include/asm/bitops.h */
  *val = tmpval2 ? tmpval2-1 : tmpval2;

  PDEBUG( "Get RotarySW num=%d, tmpval1=0x%08lX tmpval2=0x%08lX val=0x%08lX\n", num, tmpval1, tmpval2, *val );
  return (0);
}


/***************************************************************************
 * hpx3100_get_vport
 *   - port: port name 
 *   - val : 0=stop/off/disable, 1=start/on/enable
 **************************************************************************/
static int hpx3100_get_vport( int port, int *val )
{
  unsigned char data = 0;
  unsigned char bitshift = 0;

  switch ( port&0xF0 ) {

  /* VUP ctrl */
  case 0x30:
    {
      PDEBUG( "VUP ctrl\n" );

      /* check mapping I/O registers. */
      if ( unlikely(NULL == regs.vup) ) {
	PERROR( "ioremap failed 0x%08X\n", HPX3100_REG_VUP );
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

  /* JTAG sw ctrl */
  case 0x50:
    {
      PDEBUG( "JTAG sw ctrl\n" );

      /* check mapping I/O registers. */
      if ( unlikely(NULL == regs.jtagsw) ) {
	PERROR( "ioremap failed 0x%08X\n", HPX3100_REG_JTAGSW );
	return (-ENOMEM);
      }
      data = ioread8( regs.jtagsw );
      bitshift = port & 0x00000007;

      if ( data & (1<<bitshift) ) {
	*val = 1; /* buffer enable */
      } else {
	*val = 0; /* buffer disable */
      }

      break;
    }

  /* RESET ctrl */
  case 0x60:
    {
      PDEBUG( "RESET ctrl\n" );

      /* check mapping I/O registers. */
      if ( unlikely(NULL == regs.rstctrl) ) {
	PERROR( "ioremap failed 0x%08X\n", HPX3100_REG_RSTCTRL );
	return (-ENOMEM);
      }
      data = ioread8( regs.rstctrl );
      bitshift = port & 0x00000007;

      if ( data & (1<<bitshift) ) {
	*val = 0; /* Off */
      } else {
	*val = 1; /* On */
      }

      break;
    }

  /* USB ctrl */
  case 0x70:
    {
      PDEBUG( "USB ctrl\n" );

      /* check mapping I/O registers. */
      if ( unlikely(NULL == regs.usbctrl) ) {
	PERROR( "ioremap failed 0x%08X\n", HPX3100_REG_USBCTRL );
	return (-ENOMEM);
      }
      data = ioread8( regs.usbctrl );
      bitshift = port & 0x00000003;

      if ( data & (1<<bitshift) ) {
	*val = 0; /* Off */
      } else {
	*val = 1; /* On */
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
 * hpx3100_set_vport
 *   - port: port name 
 *   - val : 0=stop/off/disable, 1=start/on/enable
 **************************************************************************/
static int hpx3100_set_vport( int port, int val )
{
  unsigned char bitshift = 0;
  unsigned char data = 0;

  switch ( port&0xF0 ) {

  /* VUP ctrl */
  case 0x30:
    {
      PDEBUG( "VUP ctrl\n" );

      /* check mapping I/O registers. */
      if ( unlikely(NULL == regs.vup) ) {
	PERROR( "ioremap failed 0x%08X\n", HPX3100_REG_VUP );
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

  /* JTAG sw ctrl */
  case 0x50:
    {
      PDEBUG( "JTAG sw ctrl\n" );

      /* check mapping I/O registers. */
      if ( unlikely(NULL == regs.jtagsw) ) {
	PERROR( "ioremap failed 0x%08X\n", HPX3100_REG_JTAGSW );
	return (-ENOMEM);
      }

      bitshift = port & 0x00000007;
      data = ioread8( regs.jtagsw );

      if ( val ) {
	data |= 1<<bitshift; /* buffer enable */
      } else {
	data &= ~(1<<bitshift); /* buffer disable */
      }

      iowrite8( data, regs.jtagsw );
      break;
    }

  /* RESET ctrl */
  case 0x60:
    {
      PDEBUG( "RESET ctrl\n" );

      /* check mapping I/O registers. */
      if ( unlikely(NULL == regs.rstctrl) ) {
	PERROR( "ioremap failed 0x%08X\n", HPX3100_REG_RSTCTRL );
	return (-ENOMEM);
      }

      bitshift = port & 0x0000007;
      data = ioread8( regs.rstctrl );

      if ( val ) {
	data &= ~(1<<bitshift); /* On */
      } else {
	data |= 1<<bitshift; /* Off */
      }

      iowrite8( data, regs.rstctrl );
      break;
    }

  /* USB ctrl */
  case 0x70:
    {
      PDEBUG( "USB ctrl\n" );

      /* check mapping I/O registers. */
      if ( unlikely(NULL == regs.usbctrl) ) {
	PERROR( "ioremap failed 0x%08X\n", HPX3100_REG_USBCTRL );
	return (-ENOMEM);
      }

      bitshift = port & 0x00000003;
      data = ioread8( regs.usbctrl );

      if ( val ) {
	data &= ~(1<<bitshift); /* On */
      } else {
	data |= 1<<bitshift; /* Off */
      }

      iowrite8( data, regs.usbctrl );
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
 * hpx3100_heartbeat_led
 **************************************************************************/
static inline int hpx3100_heartbeat_led( int num )
{
  return hpx3100_toggle_led( num, HPX3100_HEARTBEAT_BIT );
}


/***************************************************************************
 * hpx3100_get_version
 *   - FPGA
 *   - 2bit
 **************************************************************************/
static int hpx3100_get_version( int num, unsigned long long *val )
{
  /* check mapping I/O registers. */
  if ( unlikely(NULL == regs.ver) ) {
    PERROR( "ioremap failed 0x%08X\n", HPX3100_REG_VER );
    return (-ENOMEM);
  }

  /* get PCB_VER. */
  *val = ((unsigned long long)ioread8(regs.ver)) & HPX3100_VER_MASK;

  PDEBUG( "Get version num=%d val=0x%llX ver=%X\n", num, *val, ioread8(regs.ver) );
  return (0);
}


/***************************************************************************
 * hpx3100_ioc_jtag_ctrl
 *   - special command
 *   - Read 4bit, Write 3bit
 **************************************************************************/
static int hpx3100_ioc_jtag_ctrl( int dir, unsigned char *val )
{
  /* check mapping I/O registers. */
  if ( unlikely(NULL == regs.jtagctrl) ) {
    PERROR( "ioremap failed 0x%08X\n", HPX3100_REG_JTAGCTRL );
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
 * hpx3100_ioc_usb_vbus
 *   - special command
 *   - 2bit
 **************************************************************************/
static int hpx3100_ioc_usb_vbus( int dir, unsigned char *val )
{
  /* check mapping I/O registers. */
  if ( unlikely(NULL == regs.usbctrl) ) {
    PERROR( "ioremap failed 0x%08X\n", HPX3100_REG_USBCTRL );
    return (-ENOMEM);
  }
 
  if ( 1 == dir ) {
    /* Write reg. */
    unsigned char old = ioread8( regs.usbctrl );
    unsigned char new = 0;

    new = (old & 0x67) | ((*val & 0x03) << 3);
    iowrite8( new, regs.usbctrl );

    PDEBUG( "Set USB_VBUS_ON val=0x%02X new=0x%02X old=0x%02X\n", *val, new, old );
  } else {
    /* Read reg. */
    unsigned char tmpval = ioread8( regs.usbctrl );
    *val = (tmpval >> 3 ) & 0x03;

    PDEBUG( "Get USB_VBUS_ON val=0x%02X old=0x%02X\n", *val, tmpval );
  }

  return (0);
}


/***************************************************************************
 * hpx3100_ioc_usb_oci
 *   - special command
 *   - 2bit, read only
 **************************************************************************/
static int hpx3100_ioc_usb_oci( unsigned char *val )
{
  unsigned char tmpval = 0;

  /* check mapping I/O registers. */
  if ( unlikely(NULL == regs.usbctrl) ) {
    PERROR( "ioremap failed 0x%08X\n", HPX3100_REG_USBCTRL );
    return (-ENOMEM);
  }

  /* Read reg. */
  tmpval = ioread8( regs.usbctrl );
  *val = (tmpval >> 5) & 0x03;

  PDEBUG( "Get USB_OCI_N val=0x%02X tmpval=0x%02X\n", *val, tmpval );
  return (0);
}


/***************************************************************************
 * hpx3100_ioc_pxy_detection
 *   - special command
 *   - 1bit, read only
 **************************************************************************/
static int hpx3100_ioc_pxy_detection( unsigned char *val )
{
  unsigned long tmpval = 0;

  /* check mapping I/O registers. */
  if ( unlikely(NULL == regs.pxydet) ) {
    PERROR( "ioremap failed 0x%08X\n", HPX3100_REG_PXYDET );
    return (-ENOMEM);
  }

  /* Read reg. */
  tmpval = ioread32be(regs.pxydet);
  *val = (unsigned char)((~tmpval & HPX3100_PXYDET_MASK) >> HPX3100_PXYDET_SHIFT);

  PDEBUG( "Get PXY_DET_BOARD_N val=0x%02X tmpval=0x%08lX\n", *val, tmpval );
  return (0);
}


/***************************************************************************
 * ioctl (special commands)
 **************************************************************************/
static int hpx3100_ioctl( unsigned int cmd, unsigned long arg )
{
  int retval = 0; /* return value */

  /*** main routine ***/
  switch ( cmd ) {

  case P2IOPORT_IOC_HPX3100_JTAGCTRL:
    {
      struct p2ioport_rwval_s info;

      /* get values. */
      if ( copy_from_user((void *)&info,
			  (void *)arg,
			  sizeof(struct p2ioport_rwval_s)) ) {
	PERROR( "copy_from_user failed at P2IOPORT_IOC_HPX3100_JTAGCTRL!\n" );
	return (-EFAULT);
      }

      /* set or get JTAG ctrl. */
      retval = hpx3100_ioc_jtag_ctrl( info.dir, &(info.val) );

      /* put values. */
      if ( copy_to_user((void *)arg,
			(void *)&info,
			sizeof(struct p2ioport_rwval_s)) ) {
	PERROR( "copy_to_user failed at P2IOPORT_IOC_GET_DIPSW!\n" );
	return (-EFAULT);
      }

      PDEBUG( "JTAG ctrl dir=%d, val=0x%02X\n", info.dir, info.val );
      break;
    }

  case P2IOPORT_IOC_HPX3100_USB_VBUS_ON:
    {
      struct p2ioport_rwval_s info;

      /* get values. */
      if ( copy_from_user((void *)&info,
			  (void *)arg,
			  sizeof(struct p2ioport_rwval_s)) ) {
	PERROR( "copy_from_user failed at P2IOPORT_IOC_HPX3100_USB_VBUS_ON!\n" );
	return (-EFAULT);
      }

      /* set or get USB_VBUS_ON reg. */
      retval = hpx3100_ioc_usb_vbus( info.dir, &(info.val) );

      /* put values. */
      if ( copy_to_user((void *)arg,
			(void *)&info,
			sizeof(struct p2ioport_rwval_s)) ) {
	PERROR( "copy_to_user failed at P2IOPORT_IOC_GET_DIPSW!\n" );
	return (-EFAULT);
      }

      PDEBUG( "USB_VBUS_ON dir=%d, val=0x%02X\n", info.dir, info.val );
      break;
    }

  case P2IOPORT_IOC_HPX3100_USB_OCI:
    {
      unsigned char val = 0;

      /* get USB_OCI_N reg. */
      retval = hpx3100_ioc_usb_oci( &val );

      /* put value. */
      if ( put_user(val, (unsigned char __user *)arg) ) {
	PERROR( "put_user failed at P2IOPORT_IOC_HPX3100_USB_OCI!\n" );
	return (-EFAULT);
      }

      PDEBUG( "USB_OCI_N val=0x%02X\n", val );
      break;
    }

  case P2IOPORT_IOC_HPX3100_PROXY_OPT_DETECTION:
    {
      unsigned char val = 0;

      /* get PXY_DET_BOARD_N reg. */
      retval = hpx3100_ioc_pxy_detection( &val );

      /* put value. */
      if ( put_user(val, (unsigned char __user *)arg) ) {
        PERROR( "put_user failed at P2IOPORT_IOC_HPX3100_PROXY_OPT_DETECTION!\n" );
        return (-EFAULT);
      }

      PDEBUG( "PXY_DET_BOARD_N val=0x%02X\n", val );
      break;
    }

  case P2IOPORT_IOC_HPX3100_TOGGLE_DEBUGMODE:
    {
      HPX3100_DEBUG ^= 1;
      PINFO( "%s mode\n", HPX3100_DEBUG?"Debug":"Non-Debug" );
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
struct p2ioport_operations hpx3100_ops = {
  .get_dipsw      = hpx3100_get_dipsw,     /* Get DIPSW */
  .get_led        = hpx3100_get_led,       /* Get LED */
  .set_led        = hpx3100_set_led,       /* Set LED */
  .clr_led        = hpx3100_clr_led,       /* Clear LED */
  .toggle_led     = hpx3100_toggle_led,    /* Toggle LED */
  .get_rotarysw   = hpx3100_get_rotarysw,  /* Get RotarySW */
  .get_version    = hpx3100_get_version,   /* Get version */
  .get_vport      = hpx3100_get_vport,     /* Set VPORT */
  .set_vport      = hpx3100_set_vport,     /* Set VPORT */
  .heartbeat_led  = hpx3100_heartbeat_led, /* for Heartbeat LED(kernel only) */
  .ioctl          = hpx3100_ioctl,         /* Ioctl(special commands) */
};


/***************************************************************************
 * hpx3100_init
 **************************************************************************/
static inline int hpx3100_init( struct p2ioport_info_s *info )
{
  /* print init message. */
  PINFO( " board type: %s(ver. %s)\n", BOARDTYPE_NAME, HPX3100_VER );

  /* init debug switch. */
  HPX3100_DEBUG = 0;

  /* set info. */
  info->nr_dipsw    = HPX3100_MAXNR_DIPSW;
  info->nr_led      = HPX3100_MAXNR_LED;
  info->nr_rotarysw = HPX3100_MAXNR_ROTARYSW;
  info->nr_device   = HPX3100_MAXNR_DEVICE;
  info->ops         = &hpx3100_ops;
  strcpy( info->name, BOARDTYPE_NAME );

  /* set IMMR base address. */
  HPX3100_IMMRBAR = (unsigned long)get_immrbase();
  PDEBUG( "IMMRBAR: 0x%08lX\n", HPX3100_IMMRBAR );

  /* init regs. */
  memset( &regs, 0, sizeof(struct hpx3100_regs_s) );

  /*  GPIO */
  regs.dipsw = ioremap_nocache( (HPX3100_REG_DIPSW+HPX3100_IMMRBAR), 4 );
  if ( unlikely(NULL == regs.dipsw) ) goto FAIL;

  regs.pxydet = regs.dipsw;

  /*  PQCNT FPGA */
  regs.led        = ioremap_nocache( HPX3100_REG_LED,      1 );
  regs.rotarysw   = ioremap_nocache( HPX3100_REG_ROTARYSW, 2 );
  regs.ver        = ioremap_nocache( HPX3100_REG_VER,      1 );
/*   regs.peripheral = ioremap_nocache( HPX3100_REG_PRPHRL,   1 ); */
  regs.vup        = ioremap_nocache( HPX3100_REG_VUP,      1 );
  regs.jtagctrl   = ioremap_nocache( HPX3100_REG_JTAGCTRL, 1 );
  regs.jtagsw     = ioremap_nocache( HPX3100_REG_JTAGSW,   1 );
  regs.rstctrl    = ioremap_nocache( HPX3100_REG_RSTCTRL,  1 );
  regs.usbctrl    = ioremap_nocache( HPX3100_REG_USBCTRL,  1 );
  if ( unlikely(   NULL == regs.led
		|| NULL == regs.rotarysw
		|| NULL == regs.ver
/* 		|| NULL == regs.peripheral */
		|| NULL == regs.vup
		|| NULL == regs.jtagctrl
		|| NULL == regs.jtagsw
 		|| NULL == regs.rstctrl
		|| NULL == regs.usbctrl) ) goto FAIL;

  /* success */
  return (0);

  /* failed */
 FAIL:
  PERROR("ioremap failed!\n");
  if (regs.dipsw)      iounmap(regs.dipsw);
  if (regs.led)        iounmap(regs.led);
  if (regs.rotarysw)   iounmap(regs.rotarysw);
  if (regs.ver)        iounmap(regs.ver);
/*   if (regs.peripheral) iounmap(regs.peripheral); */
  if (regs.vup)        iounmap(regs.vup);
  if (regs.jtagctrl)   iounmap(regs.jtagctrl);
  if (regs.jtagsw)     iounmap(regs.jtagsw);
  if (regs.rstctrl)    iounmap(regs.rstctrl);
  if (regs.usbctrl)    iounmap(regs.usbctrl);

  return (-ENOMEM);
}


/***************************************************************************
 * hpx3100_cleanup
 **************************************************************************/
static inline void hpx3100_cleanup( void )
{
  PINFO( "Clean up %s\n", BOARDTYPE_NAME ); /* Message */

  /* clear debug switch. */
  HPX3100_DEBUG = 0;

  /* clear regs. */
  if (regs.dipsw)      iounmap(regs.dipsw);
  if (regs.led)        iounmap(regs.led);
  if (regs.rotarysw)   iounmap(regs.rotarysw);
  if (regs.ver)        iounmap(regs.ver);
/*   if (regs.peripheral) iounmap(regs.peripheral); */
  if (regs.vup)        iounmap(regs.vup);
  if (regs.jtagctrl)   iounmap(regs.jtagctrl);
  if (regs.jtagsw)     iounmap(regs.jtagsw);
  if (regs.rstctrl)    iounmap(regs.rstctrl);
  if (regs.usbctrl)    iounmap(regs.usbctrl);

  memset( &regs, 0, sizeof(struct hpx3100_regs_s) );
}


/** export getting operations function. **/
inline void __p2ioport_get_ops( struct p2ioport_operations *ops )
{
  ops->get_dipsw     = hpx3100_get_dipsw;
  ops->get_led       = hpx3100_get_led;
  ops->set_led       = hpx3100_set_led;
  ops->clr_led       = hpx3100_clr_led;
  ops->toggle_led    = hpx3100_toggle_led;
  ops->get_rotarysw  = hpx3100_get_rotarysw;
  ops->get_version   = hpx3100_get_version;
  ops->get_vport     = hpx3100_get_vport;
  ops->set_vport     = hpx3100_set_vport;
  ops->heartbeat_led = hpx3100_heartbeat_led;
}


/** export init/exit function. **/
inline int __p2ioport_init_info( struct p2ioport_info_s *info )
{
  return hpx3100_init( info );
}


inline void __p2ioport_cleanup_info( struct p2ioport_info_s *info )
{
  memset( info, 0, sizeof(struct p2ioport_info_s) );
  hpx3100_cleanup();
}


/******************** the end of the file "hpx3100.c" ********************/
