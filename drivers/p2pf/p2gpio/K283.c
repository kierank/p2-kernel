/*
 * linux/drivers/p2pf/p2gpio/K283.c
 *
 *   K283 I/O port and GPIO access low-level driver
 *   
 *     Copyright (C) 2011 Panasonic corp.
 *     All Rights Reserved.
 *
 */
/* $Id: K283.c 14405 2011-05-18 04:37:23Z Noguchi Isao $ */

#include <linux/kernel.h>
#include <linux/ioctl.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/p2gpio.h>

#include <linux/p2gpio_user.h>
#include <linux/p2gpio_K283.h>
#include "p2gpio.h"

#define BOARDTYPE_NAME "K283"
#define K283_VER "0.01"

/* the number of devices */
enum K283_IOPORT_CONST {
    K283_MAXNR_DIPSW_BITS = 4,
    K283_MAXNR_ROTSW_BITS = 16,
    K283_MAXNR_LED_BITS   = 4,
};

/* gpio port */
enum K283_GPIO_IOPORT {
    K283_GPIO_DIPSW_0_N = 23,
    K283_GPIO_DIPSW_1_N = 22,
    K283_GPIO_DIPSW_2_N = 21,
    K283_GPIO_DIPSW_3_N = 20,
    K283_GPIO_LED_0 = 16,
    K283_GPIO_LED_1 = 17,
    K283_GPIO_LED_2 = 18,
    K283_GPIO_LED_3 = 19,
    K283_GPIO_ZION_RDY_N = 24,
    K283_GPIO_SD_LED_N = 25,
/*     K283_GPIO_PQ2_DEBUG_N = 27, */

    K283_GPIO_VUP_MODE_N = 32,
    K283_GPIO_CAM_RST_N = 38,
    K283_GPIO_CAM_W_N = 39,

    K283_GPIO_USB_OCI_N = 40,
    K283_GPIO_VBUS_USBDEV_ON = 41,
    K283_GPIO_VBUS_USBHOST_ON = 42,
    K283_GPIO_PQ2_SPI_CS_N = 47,

    K283_GPIO_ROTSW_0 = 63,
    K283_GPIO_ROTSW_1 = 62,
    K283_GPIO_ROTSW_2 = 61,
    K283_GPIO_ROTSW_3 = 60,
    K283_GPIO_ROTSW_4 = 59,
    K283_GPIO_ROTSW_5 = 58,
    K283_GPIO_ROTSW_6 = 57,
    K283_GPIO_ROTSW_7 = 56,
    K283_GPIO_ROTSW_8 = 55,
    K283_GPIO_ROTSW_9 = 54,
    K283_GPIO_ROTSW_10 = 53,
    K283_GPIO_ROTSW_11 = 52,
    K283_GPIO_ROTSW_12 = 51,
    K283_GPIO_ROTSW_13 = 50,
    K283_GPIO_ROTSW_14 = 49,
    K283_GPIO_ROTSW_15 = 48,
};

static const struct p2gpio_pmap K283_pmap[] = {

    /* SDCARD */
    P2GPIO_ENTRY_PMAP( SDLED,
                       K283_GPIO_SD_LED_N,
                       P2GPIO_PMAP_REVERSE ),
    /* SPI by GPIO */
    P2GPIO_ENTRY_PMAP(SPI_CS0,
                      K283_GPIO_PQ2_SPI_CS_N,
                      P2GPIO_PMAP_REVERSE ),

/*     /\* LOG_EVENT *\/ */
/*     P2GPIO_ENTRY_PMAP_INT( LOG_EVENT, */
/*                            K283_GPIO_PQ2_DEBUG_N, */
/*                            P2GPIO_PMAP_RONLY | P2GPIO_PMAP_REVERSE, */
/*                            POLLIN, NULL ), */

    /* USB */
    P2GPIO_ENTRY_PMAP( VBUS_USBHOST,
                       K283_GPIO_VBUS_USBHOST_ON, 0),
    P2GPIO_ENTRY_PMAP( VBUS_USBDEV,
                       K283_GPIO_VBUS_USBDEV_ON, 0),
    P2GPIO_ENTRY_PMAP (USB_OCI,
                       K283_GPIO_USB_OCI_N,
                       P2GPIO_PMAP_RONLY | P2GPIO_PMAP_REVERSE ),

    /* ZION */
    P2GPIO_ENTRY_PMAP( ZION_INIT_L,
                       K283_GPIO_ZION_RDY_N, P2GPIO_PMAP_RONLY ),

    /* DIPSW */
    P2GPIO_ENTRY_PMAP( DIPSW_0, /* DIPSW-0 */
                       K283_GPIO_DIPSW_0_N,
                       P2GPIO_PMAP_RONLY | P2GPIO_PMAP_REVERSE ),
    P2GPIO_ENTRY_PMAP( DIPSW_1, /* DIPSW-1 */
                       K283_GPIO_DIPSW_1_N,
                       P2GPIO_PMAP_RONLY | P2GPIO_PMAP_REVERSE ),
    P2GPIO_ENTRY_PMAP( DIPSW_2, /* DIPSW-2 */
                       K283_GPIO_DIPSW_2_N,
                       P2GPIO_PMAP_RONLY | P2GPIO_PMAP_REVERSE ),
    P2GPIO_ENTRY_PMAP( DIPSW_3, /* DIPSW-3 */
                       K283_GPIO_DIPSW_3_N,
                       P2GPIO_PMAP_RONLY | P2GPIO_PMAP_REVERSE ),

    /* CAMSYS */
    P2GPIO_ENTRY_PMAP(CAMSYS_RST,
                      K283_GPIO_CAM_RST_N,
                      P2GPIO_PMAP_REVERSE ),
    P2GPIO_ENTRY_PMAP(CAMSYS_W,
                      K283_GPIO_CAM_W_N,
                      P2GPIO_PMAP_REVERSE ),
    P2GPIO_ENTRY_PMAP(VUP_MODE,
                      K283_GPIO_VUP_MODE_N,
                      P2GPIO_PMAP_REVERSE ),

    /* ROTSW */
    P2GPIO_ENTRY_PMAP( ROTSW_15, /* ROTSW-15 */
                       K283_GPIO_ROTSW_15, P2GPIO_PMAP_RONLY ),
    P2GPIO_ENTRY_PMAP( ROTSW_14, /* ROTSW-14 */
                       K283_GPIO_ROTSW_14, P2GPIO_PMAP_RONLY ),
    P2GPIO_ENTRY_PMAP( ROTSW_13, /* ROTSW-13 */
                       K283_GPIO_ROTSW_13, P2GPIO_PMAP_RONLY ),
    P2GPIO_ENTRY_PMAP( ROTSW_12, /* ROTSW-12 */
                       K283_GPIO_ROTSW_12, P2GPIO_PMAP_RONLY ),
    P2GPIO_ENTRY_PMAP( ROTSW_11, /* ROTSW-11 */
                       K283_GPIO_ROTSW_11, P2GPIO_PMAP_RONLY ),
    P2GPIO_ENTRY_PMAP( ROTSW_10, /* ROTSW-10 */
                       K283_GPIO_ROTSW_10, P2GPIO_PMAP_RONLY ),
    P2GPIO_ENTRY_PMAP( ROTSW_9, /* ROTSW-9 */
                       K283_GPIO_ROTSW_9, P2GPIO_PMAP_RONLY ),
    P2GPIO_ENTRY_PMAP( ROTSW_8, /* ROTSW-8 */
                       K283_GPIO_ROTSW_8, P2GPIO_PMAP_RONLY ),
    P2GPIO_ENTRY_PMAP( ROTSW_7, /* ROTSW-7 */
                       K283_GPIO_ROTSW_7, P2GPIO_PMAP_RONLY ),
    P2GPIO_ENTRY_PMAP( ROTSW_6, /* ROTSW-6 */
                       K283_GPIO_ROTSW_6, P2GPIO_PMAP_RONLY ),
    P2GPIO_ENTRY_PMAP( ROTSW_5, /* ROTSW-5 */
                       K283_GPIO_ROTSW_5, P2GPIO_PMAP_RONLY ),
    P2GPIO_ENTRY_PMAP( ROTSW_4, /* ROTSW-4 */
                       K283_GPIO_ROTSW_4, P2GPIO_PMAP_RONLY ),
    P2GPIO_ENTRY_PMAP( ROTSW_3, /* ROTSW-3 */
                       K283_GPIO_ROTSW_3, P2GPIO_PMAP_RONLY ),
    P2GPIO_ENTRY_PMAP( ROTSW_2, /* ROTSW-2 */
                       K283_GPIO_ROTSW_2, P2GPIO_PMAP_RONLY ),
    P2GPIO_ENTRY_PMAP( ROTSW_1, /* ROTSW-1 */
                       K283_GPIO_ROTSW_1, P2GPIO_PMAP_RONLY ),
    P2GPIO_ENTRY_PMAP( ROTSW_0, /* ROTSW-0 */
                       K283_GPIO_ROTSW_0, P2GPIO_PMAP_RONLY ),

    /* LED(for debug) */
    P2GPIO_ENTRY_PMAP( LED_0, /* LED-0 */
                       K283_GPIO_LED_0,
                       0 ),
    P2GPIO_ENTRY_PMAP( LED_1, /* LED-1 */
                       K283_GPIO_LED_1,
                       0 ),
    P2GPIO_ENTRY_PMAP( LED_2, /* LED-2 */
                       K283_GPIO_LED_2,
                       0 ),
    P2GPIO_ENTRY_PMAP( LED_3, /* LED-3 */
                       K283_GPIO_LED_3,
                       0 ),

};


static int K283_ioctl( struct p2gpio_dev *dev, unsigned int cmd, unsigned long arg )
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
static struct p2gpio_operations K283_ops = {
    .ioctl        = K283_ioctl, /* Ioctl(special commands) */
};

/*
 * initialize and get device information
 */
int __p2gpio_init_info( struct p2gpio_dev_info *info )
{
    if(unlikely(!info))
        return -EINVAL;

    info->nr_dipsw  = K283_MAXNR_DIPSW_BITS;
    info->nr_rotsw  = K283_MAXNR_ROTSW_BITS;
    info->nr_led    = K283_MAXNR_LED_BITS;
    info->ops       = &K283_ops;
    info->pmap      = K283_pmap;
    info->nr_pmap   = sizeof(K283_pmap)/sizeof(struct p2gpio_pmap);
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


