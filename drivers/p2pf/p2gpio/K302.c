/*
 * linux/drivers/p2pf/p2gpio/K302.c
 *
 *   K302 I/O port and GPIO access low-level driver
 *   
 *     Copyright (C) 2010 Panasonic corp.
 *     All Rights Reserved.
 *
 */
/* $Id: K302.c 11456 2010-12-28 03:06:29Z Noguchi Isao $ */

#include <linux/kernel.h>
#include <linux/ioctl.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/p2gpio.h>

#include <linux/p2gpio_user.h>
#include <linux/p2gpio_K302.h>
#include "p2gpio.h"

#define BOARDTYPE_NAME "K302"
#define K302_VER "0.01"

/* the number of devices */
enum K302_IOPORT_CONST {
    K302_MAXNR_DIPSW_BITS = 4,
    K302_MAXNR_LED_BITS   = 4,
};

/* gpio port */
enum K302_GPIO_IOPORT {
    K302_GPIO_DIPSW_0_N = 3,
    K302_GPIO_DIPSW_1_N = 2,
    K302_GPIO_DIPSW_2_N = 1,
    K302_GPIO_DIPSW_3_N = 0,
    K302_GPIO_LED_0 = 4,
    K302_GPIO_LED_1 = 5,
    K302_GPIO_LED_2 = 6,
    K302_GPIO_LED_3 = 7,
    K302_GPIO_SD_LED_N = 17,
    K302_GPIO_VBUS_3D_OFF_N = 8,
    K302_GPIO_VBUS_KBD_OFF_N = 10,
    K302_GPIO_VBUS_USB3_OFF_N = 18,
    K302_GPIO_VBUS_USBDEV_OFF_N = 11,
    K302_GPIO_REF5V_OCI_N = 19,
    K302_GPIO_ZION_RDY_N = 16,
    K302_GPIO_USB3_SPI_CS_N = 4,
    K302_GPIO_USB3_SPI_CLK = 6,
    K302_GPIO_USB3_SPI_DOUT = 5,
    K302_GPIO_USB3_SPI_DIN = 9,
    K302_GPIO_USB3_SPI_SEL = 21,
    K302_GPIO_PCIE_SSC_ON = 15,
};

static const struct p2gpio_pmap K302_pmap[] = {

    /* SPI by GPIO */
    P2GPIO_ENTRY_PMAP(SPI_CLK,
                      K302_GPIO_USB3_SPI_CLK,
                      0 ),
    P2GPIO_ENTRY_PMAP(SPI_DOUT,
                      K302_GPIO_USB3_SPI_DOUT,
                      0 ),
    P2GPIO_ENTRY_PMAP(SPI_DIN,
                      K302_GPIO_USB3_SPI_DIN,
                      P2GPIO_PMAP_RONLY ),
    P2GPIO_ENTRY_PMAP(SPI_CS0,
                      K302_GPIO_USB3_SPI_CS_N,
                      P2GPIO_PMAP_REVERSE ),
    P2GPIO_ENTRY_PMAP(USB3_SPI_SEL,
                      K302_GPIO_USB3_SPI_SEL,
                      0),

    /* SDCARD */
    P2GPIO_ENTRY_PMAP( SDLED,
                       K302_GPIO_SD_LED_N,
                       P2GPIO_PMAP_REVERSE ),
    /* USB */
    P2GPIO_ENTRY_PMAP( VBUS_3D_OFF,
                       K302_GPIO_VBUS_3D_OFF_N,
                       P2GPIO_PMAP_REVERSE ),
    P2GPIO_ENTRY_PMAP( VBUS_KBD_OFF,
                       K302_GPIO_VBUS_KBD_OFF_N,
                       P2GPIO_PMAP_REVERSE ),
    P2GPIO_ENTRY_PMAP( VBUS_USB3_OFF,
                       K302_GPIO_VBUS_USB3_OFF_N,
                       P2GPIO_PMAP_REVERSE ),
    P2GPIO_ENTRY_PMAP( VBUS_USBDEV_OFF,
                       K302_GPIO_VBUS_USBDEV_OFF_N,
                       P2GPIO_PMAP_REVERSE ),
    P2GPIO_ENTRY_PMAP_INT( REF5V_OCI,
                           K302_GPIO_REF5V_OCI_N,
                           P2GPIO_PMAP_REVERSE,
                           POLLIN, NULL ),

    /* ZION */
    P2GPIO_ENTRY_PMAP( ZION_INIT_L,
                       K302_GPIO_ZION_RDY_N,
                       P2GPIO_PMAP_RONLY ),

    /* PCIE */
    P2GPIO_ENTRY_PMAP( PCIE_SSC_ON,
                       K302_GPIO_PCIE_SSC_ON,
                       0),

    /* DIPSW */
    P2GPIO_ENTRY_PMAP( DIPSW_0, /* DIPSW-0 */
                       K302_GPIO_DIPSW_0_N,
                       P2GPIO_PMAP_RONLY | P2GPIO_PMAP_REVERSE ),
    P2GPIO_ENTRY_PMAP( DIPSW_1, /* DIPSW-1 */
                       K302_GPIO_DIPSW_1_N,
                       P2GPIO_PMAP_RONLY | P2GPIO_PMAP_REVERSE ),
    P2GPIO_ENTRY_PMAP( DIPSW_2, /* DIPSW-2 */
                       K302_GPIO_DIPSW_2_N,
                       P2GPIO_PMAP_RONLY | P2GPIO_PMAP_REVERSE ),
    P2GPIO_ENTRY_PMAP( DIPSW_3, /* DIPSW-3 */
                       K302_GPIO_DIPSW_3_N,
                       P2GPIO_PMAP_RONLY | P2GPIO_PMAP_REVERSE ),

    /* LED(for debug) */
    P2GPIO_ENTRY_PMAP( LED_0, /* LED-0 */
                       K302_GPIO_LED_0,
                       0 ),
    P2GPIO_ENTRY_PMAP( LED_1, /* LED-1 */
                       K302_GPIO_LED_1,
                       0 ),
    P2GPIO_ENTRY_PMAP( LED_2, /* LED-2 */
                       K302_GPIO_LED_2,
                       0 ),
    P2GPIO_ENTRY_PMAP( LED_3, /* LED-3 */
                       K302_GPIO_LED_3,
                       0 ),
};


static int K302_ioctl( struct p2gpio_dev *dev, unsigned int cmd, unsigned long arg )
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
static struct p2gpio_operations K302_ops = {
    .ioctl        = K302_ioctl, /* Ioctl(special commands) */
};

/*
 * initialize and get device information
 */
int __p2gpio_init_info( struct p2gpio_dev_info *info )
{
    if(unlikely(!info))
        return -EINVAL;

    info->nr_dipsw  = K302_MAXNR_DIPSW_BITS;
    info->nr_led    = K302_MAXNR_LED_BITS;
    info->ops       = &K302_ops;
    info->pmap      = K302_pmap;
    info->nr_pmap   = sizeof(K302_pmap)/sizeof(struct p2gpio_pmap);
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


