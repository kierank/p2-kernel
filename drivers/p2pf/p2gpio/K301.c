/*
 * linux/drivers/p2pf/p2gpio/K301.c
 *
 *   K301 I/O port and GPIO access low-level driver
 *   
 *     Copyright (C) 2010 Panasonic corp.
 *     All Rights Reserved.
 *
 */
/* $Id: K301.c 10442 2010-11-16 05:33:35Z Noguchi Isao $ */

#include <linux/kernel.h>
#include <linux/ioctl.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/p2gpio.h>

#include <linux/p2gpio_user.h>
#include <linux/p2gpio_K301.h>
#include "p2gpio.h"

#define BOARDTYPE_NAME "K301"
#define K301_VER "0.01"

/* the number of devices */
enum K301_IOPORT_CONST {
    K301_MAXNR_DIPSW_BITS = 4,
    K301_MAXNR_LED_BITS   = 4,
};

/* gpio port */
enum K301_GPIO_IOPORT {
    K301_GPIO_DIPSW_0_N = 3,
    K301_GPIO_DIPSW_1_N = 2,
    K301_GPIO_DIPSW_2_N = 1,
    K301_GPIO_DIPSW_3_N = 0,
    K301_GPIO_LED_0 = 4,
    K301_GPIO_LED_1 = 5,
    K301_GPIO_LED_2 = 6,
    K301_GPIO_LED_3 = 7,
    K301_GPIO_SD_LED_N = 16,
    K301_GPIO_VBUS_USB_OFF_N = 21,
    K301_GPIO_ZION_RDY_N = 15,
};

static const struct p2gpio_pmap K301_pmap[] = {

    /* SDCARD */
    P2GPIO_ENTRY_PMAP( SDLED,
                       K301_GPIO_SD_LED_N,
                       P2GPIO_PMAP_REVERSE ),

    /* USB */
    P2GPIO_ENTRY_PMAP( VBUS_USB_OFF,
                       K301_GPIO_VBUS_USB_OFF_N,
                       P2GPIO_PMAP_REVERSE ),

    /* ZION */
    P2GPIO_ENTRY_PMAP( ZION_INIT_L,
                       K301_GPIO_ZION_RDY_N,
                       P2GPIO_PMAP_RONLY ),

    /* DIPSW */
    P2GPIO_ENTRY_PMAP( DIPSW_0, /* DIPSW-0 */
                       K301_GPIO_DIPSW_0_N,
                       P2GPIO_PMAP_RONLY | P2GPIO_PMAP_REVERSE ),
    P2GPIO_ENTRY_PMAP( DIPSW_1, /* DIPSW-1 */
                       K301_GPIO_DIPSW_1_N,
                       P2GPIO_PMAP_RONLY | P2GPIO_PMAP_REVERSE ),
    P2GPIO_ENTRY_PMAP( DIPSW_2, /* DIPSW-2 */
                       K301_GPIO_DIPSW_2_N,
                       P2GPIO_PMAP_RONLY | P2GPIO_PMAP_REVERSE ),
    P2GPIO_ENTRY_PMAP( DIPSW_3, /* DIPSW-3 */
                       K301_GPIO_DIPSW_3_N,
                       P2GPIO_PMAP_RONLY | P2GPIO_PMAP_REVERSE ),

    /* LED(for debug) */
    P2GPIO_ENTRY_PMAP( LED_0, /* LED-0 */
                       K301_GPIO_LED_0,
                       0 ),
    P2GPIO_ENTRY_PMAP( LED_1, /* LED-1 */
                       K301_GPIO_LED_1,
                       0 ),
    P2GPIO_ENTRY_PMAP( LED_2, /* LED-2 */
                       K301_GPIO_LED_2,
                       0 ),
    P2GPIO_ENTRY_PMAP( LED_3, /* LED-3 */
                       K301_GPIO_LED_3,
                       0 ),
};


static int K301_ioctl( struct p2gpio_dev *dev, unsigned int cmd, unsigned long arg )
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
static struct p2gpio_operations K301_ops = {
    .ioctl        = K301_ioctl, /* Ioctl(special commands) */
};

/*
 * initialize and get device information
 */
int __p2gpio_init_info( struct p2gpio_dev_info *info )
{
    if(unlikely(!info))
        return -EINVAL;

    info->nr_dipsw  = K301_MAXNR_DIPSW_BITS;
    info->nr_led    = K301_MAXNR_LED_BITS;
    info->ops       = &K301_ops;
    info->pmap      = K301_pmap;
    info->nr_pmap   = sizeof(K301_pmap)/sizeof(struct p2gpio_pmap);
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


