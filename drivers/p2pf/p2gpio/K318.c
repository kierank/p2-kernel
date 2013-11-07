/*
 * linux/drivers/p2pf/p2gpio/K318.c
 *
 *   K318 I/O port and GPIO access low-level driver
 *   
 *     Copyright (C) 2011 Panasonic corp.
 *     All Rights Reserved.
 *
 */
/* $Id: K318.c 21241 2012-05-08 06:41:52Z Yoshioka Masaki $ */

#include <linux/kernel.h>
#include <linux/ioctl.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/p2gpio.h>

#include <linux/p2gpio_user.h>
#include <linux/p2gpio_K318.h>
#include "p2gpio.h"

#define BOARDTYPE_NAME "K318"
#define K318_VER "0.01"

/* the number of devices */
enum K318_IOPORT_CONST {
    K318_MAXNR_DIPSW_BITS = 4,
    K318_MAXNR_ROTSW_BITS = 16,
    K318_MAXNR_LED_BITS   = 4,
};

/* gpio port */
enum K318_GPIO_IOPORT {
    K318_GPIO_DIPSW_0_N = 31,
    K318_GPIO_DIPSW_1_N = 16,

    K318_GPIO_VUP_MODE_N = 32,
    K318_GPIO_TV_SYSTEM = 33,
    K318_GPIO_CAM_RST_N = 38,
    K318_GPIO_CAM_W_N = 39,

    K318_GPIO_VUP_STS_BLINK = 40,
    K318_GPIO_VUP_STS_ERROR = 41,
    K318_GPIO_VUP_STS_FINISH = 42,
    K318_GPIO_VUP_STS_BIGIN = 43,
    K318_GPIO_PQ2_SPI_CS_N = 47,

    K318_GPIO_ROTSW_0 = 63,
    K318_GPIO_ROTSW_1 = 62,
    K318_GPIO_ROTSW_2 = 61,
    K318_GPIO_ROTSW_3 = 60,
    K318_GPIO_ROTSW_4 = 59,
    K318_GPIO_ROTSW_5 = 58,
    K318_GPIO_ROTSW_6 = 57,
    K318_GPIO_ROTSW_7 = 56,
    K318_GPIO_ROTSW_8 = 55,
    K318_GPIO_ROTSW_9 = 54,
    K318_GPIO_ROTSW_10 = 53,
    K318_GPIO_ROTSW_11 = 52,
    K318_GPIO_ROTSW_12 = 51,
    K318_GPIO_ROTSW_13 = 50,
    K318_GPIO_ROTSW_14 = 49,
    K318_GPIO_ROTSW_15 = 48,

    K318_GPIO_PC_MODE_2 = 65,
    K318_GPIO_PC_MODE_1 = 66,
    K318_GPIO_PC_MODE_0 = 67,
    K318_GPIO_PCB_VER_1 = 69,
    K318_GPIO_PCB_VER_0 = 70,
    K318_GPIO_ZION_RDY_N = 71,

    K318_GPIO_PXY_DSP_TGTRST = 75,
    K318_GPIO_RST_RICOH_N = 79,

    K318_GPIO_PXY_DETECT_N = 81,
    K318_GPIO_EVF_DETECT_N = 82,
    K318_GPIO_SDI_DETECT_N = 83,
    K318_GPIO_PXY_DSP = 84,
    K318_GPIO_PXY_FPGA = 85,
    K318_GPIO_EVF_SPI = 86,
    K318_GPIO_SDI_SPI = 87,

    K318_GPIO_TDO = 92,
    K318_GPIO_TDI = 93,
    K318_GPIO_TMS = 94,
    K318_GPIO_TCK = 95,

    K318_GPIO_USB2_OCI_N = 97,
    K318_GPIO_USB_OCI_N = 98,
    K318_GPIO_VBUS2_USBHOST_ON = 99,
    K318_GPIO_VBUS_USBHOST_ON = 100,
    K318_GPIO_USB_HOST_N = 101,
    K318_GPIO_USB_OE_N = 102,
    K318_GPIO_USB_HUB_N = 103,

    K318_GPIO_SD_LED_N = 104,
    K318_GPIO_DIPSW_3_N = 106,
    K318_GPIO_DIPSW_2_N = 107,
    K318_GPIO_LED_3 = 108,
    K318_GPIO_LED_2 = 109,
    K318_GPIO_LED_1 = 110,
    K318_GPIO_LED_0 = 111,

};

static const struct p2gpio_pmap K318_pmap[] = {

    /* SDCARD */
    P2GPIO_ENTRY_PMAP( SDLED,
                       K318_GPIO_SD_LED_N,
                       P2GPIO_PMAP_REVERSE ),
    /* SPI by GPIO */
    P2GPIO_ENTRY_PMAP(SPI_CS0,
                      K318_GPIO_PQ2_SPI_CS_N,
                      P2GPIO_PMAP_REVERSE ),

/*     /\* LOG_EVENT *\/ */
/*     P2GPIO_ENTRY_PMAP_INT( LOG_EVENT, */
/*                            K318_GPIO_PQ2_DEBUG_N, */
/*                            P2GPIO_PMAP_RONLY | P2GPIO_PMAP_REVERSE, */
/*                            POLLIN, NULL ), */

    /* USB */
    P2GPIO_ENTRY_PMAP( VBUS_USBHOST,
                       K318_GPIO_VBUS_USBHOST_ON, 0),
    P2GPIO_ENTRY_PMAP (USB_OCI,
                       K318_GPIO_USB_OCI_N,
                       P2GPIO_PMAP_RONLY | P2GPIO_PMAP_REVERSE ),

    P2GPIO_ENTRY_PMAP( VBUS_USBHOST2,
                       K318_GPIO_VBUS2_USBHOST_ON, 0),
    P2GPIO_ENTRY_PMAP (USB_OCI2,
                       K318_GPIO_USB2_OCI_N,
                       P2GPIO_PMAP_RONLY | P2GPIO_PMAP_REVERSE ),
    P2GPIO_ENTRY_PMAP (USB_HOST,
                       K318_GPIO_USB_HOST_N,
                       P2GPIO_PMAP_REVERSE ),
    P2GPIO_ENTRY_PMAP (USB_OE,
                       K318_GPIO_USB_OE_N,
                       P2GPIO_PMAP_REVERSE ),
    P2GPIO_ENTRY_PMAP (USB_HUB,
                       K318_GPIO_USB_HUB_N,
                       P2GPIO_PMAP_REVERSE ),

    /* ZION */
    P2GPIO_ENTRY_PMAP( ZION_INIT_L,
                       K318_GPIO_ZION_RDY_N, P2GPIO_PMAP_RONLY ),

    /* DIPSW */
    P2GPIO_ENTRY_PMAP( DIPSW_0, /* DIPSW-0 */
                       K318_GPIO_DIPSW_0_N,
                       P2GPIO_PMAP_RONLY | P2GPIO_PMAP_REVERSE ),
    P2GPIO_ENTRY_PMAP( DIPSW_1, /* DIPSW-1 */
                       K318_GPIO_DIPSW_1_N,
                       P2GPIO_PMAP_RONLY | P2GPIO_PMAP_REVERSE ),
    P2GPIO_ENTRY_PMAP( DIPSW_2, /* DIPSW-2 */
                       K318_GPIO_DIPSW_2_N,
                       P2GPIO_PMAP_RONLY | P2GPIO_PMAP_REVERSE ),
    P2GPIO_ENTRY_PMAP( DIPSW_3, /* DIPSW-3 */
                       K318_GPIO_DIPSW_3_N,
                       P2GPIO_PMAP_RONLY | P2GPIO_PMAP_REVERSE ),

    /* CAMSYS */
    P2GPIO_ENTRY_PMAP(CAMSYS_RST,
                      K318_GPIO_CAM_RST_N,
                      P2GPIO_PMAP_REVERSE ),
    P2GPIO_ENTRY_PMAP(CAMSYS_W,
                      K318_GPIO_CAM_W_N,
                      P2GPIO_PMAP_REVERSE ),
    P2GPIO_ENTRY_PMAP(VUP_MODE,
                      K318_GPIO_VUP_MODE_N,
                      P2GPIO_PMAP_REVERSE ),

    /* ROTSW */
    P2GPIO_ENTRY_PMAP( ROTSW_15, /* ROTSW-15 */
                       K318_GPIO_ROTSW_15, P2GPIO_PMAP_RONLY ),
    P2GPIO_ENTRY_PMAP( ROTSW_14, /* ROTSW-14 */
                       K318_GPIO_ROTSW_14, P2GPIO_PMAP_RONLY ),
    P2GPIO_ENTRY_PMAP( ROTSW_13, /* ROTSW-13 */
                       K318_GPIO_ROTSW_13, P2GPIO_PMAP_RONLY ),
    P2GPIO_ENTRY_PMAP( ROTSW_12, /* ROTSW-12 */
                       K318_GPIO_ROTSW_12, P2GPIO_PMAP_RONLY ),
    P2GPIO_ENTRY_PMAP( ROTSW_11, /* ROTSW-11 */
                       K318_GPIO_ROTSW_11, P2GPIO_PMAP_RONLY ),
    P2GPIO_ENTRY_PMAP( ROTSW_10, /* ROTSW-10 */
                       K318_GPIO_ROTSW_10, P2GPIO_PMAP_RONLY ),
    P2GPIO_ENTRY_PMAP( ROTSW_9, /* ROTSW-9 */
                       K318_GPIO_ROTSW_9, P2GPIO_PMAP_RONLY ),
    P2GPIO_ENTRY_PMAP( ROTSW_8, /* ROTSW-8 */
                       K318_GPIO_ROTSW_8, P2GPIO_PMAP_RONLY ),
    P2GPIO_ENTRY_PMAP( ROTSW_7, /* ROTSW-7 */
                       K318_GPIO_ROTSW_7, P2GPIO_PMAP_RONLY ),
    P2GPIO_ENTRY_PMAP( ROTSW_6, /* ROTSW-6 */
                       K318_GPIO_ROTSW_6, P2GPIO_PMAP_RONLY ),
    P2GPIO_ENTRY_PMAP( ROTSW_5, /* ROTSW-5 */
                       K318_GPIO_ROTSW_5, P2GPIO_PMAP_RONLY ),
    P2GPIO_ENTRY_PMAP( ROTSW_4, /* ROTSW-4 */
                       K318_GPIO_ROTSW_4, P2GPIO_PMAP_RONLY ),
    P2GPIO_ENTRY_PMAP( ROTSW_3, /* ROTSW-3 */
                       K318_GPIO_ROTSW_3, P2GPIO_PMAP_RONLY ),
    P2GPIO_ENTRY_PMAP( ROTSW_2, /* ROTSW-2 */
                       K318_GPIO_ROTSW_2, P2GPIO_PMAP_RONLY ),
    P2GPIO_ENTRY_PMAP( ROTSW_1, /* ROTSW-1 */
                       K318_GPIO_ROTSW_1, P2GPIO_PMAP_RONLY ),
    P2GPIO_ENTRY_PMAP( ROTSW_0, /* ROTSW-0 */
                       K318_GPIO_ROTSW_0, P2GPIO_PMAP_RONLY ),

    /* LED(for debug) */
    P2GPIO_ENTRY_PMAP( LED_0, /* LED-0 */
                       K318_GPIO_LED_0,
                       0 ),
    P2GPIO_ENTRY_PMAP( LED_1, /* LED-1 */
                       K318_GPIO_LED_1,
                       0 ),
    P2GPIO_ENTRY_PMAP( LED_2, /* LED-2 */
                       K318_GPIO_LED_2,
                       0 ),
    P2GPIO_ENTRY_PMAP( LED_3, /* LED-3 */
                       K318_GPIO_LED_3,
                       0 ),

    /* PC_MODE */
    P2GPIO_ENTRY_PMAP( PC_MODE0, /* PC_MODE0 */
                       K318_GPIO_PC_MODE_0, P2GPIO_PMAP_RONLY ),
    P2GPIO_ENTRY_PMAP( PC_MODE1, /* PC_MODE1 */
                       K318_GPIO_PC_MODE_1, P2GPIO_PMAP_RONLY ),
    P2GPIO_ENTRY_PMAP( PC_MODE2, /* PC_MODE2 */
                       K318_GPIO_PC_MODE_2, P2GPIO_PMAP_RONLY ),

    /* PCB_VER */
    P2GPIO_ENTRY_PMAP( PCB_VER0, /* PCB_VER0 */
                       K318_GPIO_PCB_VER_0, P2GPIO_PMAP_RONLY ),
    P2GPIO_ENTRY_PMAP( PCB_VER1, /* PCB_VER1 */
                       K318_GPIO_PCB_VER_1, P2GPIO_PMAP_RONLY ),

    /* PROXY */
    P2GPIO_ENTRY_PMAP( PROXY_DETECTION, /* PROXY_DETECTION */
                       K318_GPIO_PXY_DETECT_N,
                       P2GPIO_PMAP_RONLY | P2GPIO_PMAP_REVERSE ),
    P2GPIO_ENTRY_PMAP( PROXY_TGT_RST,
                       K318_GPIO_PXY_DSP_TGTRST, 0 ),
    P2GPIO_ENTRY_PMAP( PROXY_DSP,
                       K318_GPIO_PXY_DSP, 0 ),
    P2GPIO_ENTRY_PMAP( PROXY_FPGA,
                       K318_GPIO_PXY_FPGA, 0 ),

    /* EVF */
    P2GPIO_ENTRY_PMAP( EVF_DETECTION, /* EVF_DETECTION */
                       K318_GPIO_EVF_DETECT_N,
                       P2GPIO_PMAP_RONLY | P2GPIO_PMAP_REVERSE ),
    P2GPIO_ENTRY_PMAP( EVF_SPI,
                       K318_GPIO_EVF_SPI, 0 ),

    /* SDI_IN */
    P2GPIO_ENTRY_PMAP( SDI_DETECTION, /* SDI_DETECTION */
                       K318_GPIO_SDI_DETECT_N,
                       P2GPIO_PMAP_RONLY | P2GPIO_PMAP_REVERSE ),
    P2GPIO_ENTRY_PMAP( SDI_SPI,
                       K318_GPIO_SDI_SPI, 0 ),

    /* JTAG */
    P2GPIO_ENTRY_PMAP( JTAG_TDO,
                       K318_GPIO_TDO, P2GPIO_PMAP_RONLY ),
    P2GPIO_ENTRY_PMAP( JTAG_TDI,
                       K318_GPIO_TDI, 0 ),
    P2GPIO_ENTRY_PMAP( JTAG_TMS,
                       K318_GPIO_TMS, 0 ),
    P2GPIO_ENTRY_PMAP( JTAG_TCK,
                       K318_GPIO_TCK, 0 ),

    /* RICOH */
    P2GPIO_ENTRY_PMAP( RST_RICOH,
                       K318_GPIO_RST_RICOH_N,
                       P2GPIO_PMAP_REVERSE ),

    /* VUP_STATUS */
    P2GPIO_ENTRY_PMAP( VUP_STS_BLINK,
                       K318_GPIO_VUP_STS_BLINK, 0 ),
    P2GPIO_ENTRY_PMAP( VUP_STS_ERROR,
                       K318_GPIO_VUP_STS_ERROR, 0 ),
    P2GPIO_ENTRY_PMAP( VUP_STS_FINISH,
                       K318_GPIO_VUP_STS_FINISH, 0 ),
    P2GPIO_ENTRY_PMAP( VUP_STS_BIGIN,
                       K318_GPIO_VUP_STS_BIGIN, 0 ),

};


static int K318_ioctl( struct p2gpio_dev *dev, unsigned int cmd, unsigned long arg )
{
    int retval = 0;	/* return value */

    switch ( cmd ) {
        
    default:
        retval = -ENOTTY;


    } /* the end of switch */

/*  failed: */
    return retval;
}


/*
 * p2gpio operations 
 */
static struct p2gpio_operations K318_ops = {
    .ioctl        = K318_ioctl, /* Ioctl(special commands) */
};

/*
 * initialize and get device information
 */
int __p2gpio_init_info( struct p2gpio_dev_info *info )
{
    if(unlikely(!info))
        return -EINVAL;

    info->nr_dipsw  = K318_MAXNR_DIPSW_BITS;
    info->nr_rotsw  = K318_MAXNR_ROTSW_BITS;
    info->nr_led    = K318_MAXNR_LED_BITS;
    info->ops       = &K318_ops;
    info->pmap      = K318_pmap;
    info->nr_pmap   = sizeof(K318_pmap)/sizeof(struct p2gpio_pmap);
    strncpy( info->name, BOARDTYPE_NAME, sizeof(info->name) );

    return 0;
}

/*
 * cleanup
 */
void __p2gpio_cleanup_info( struct p2gpio_dev_info *info )
{
    /* nothing to do */
}


