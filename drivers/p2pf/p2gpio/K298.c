/*
 * linux/drivers/p2pf/p2gpio/K298.c
 *
 *   K298 I/O port and GPIO access low-level driver
 *   
 *     Copyright (C) 2009-2010 Panasonic corp.
 *     All Rights Reserved.
 *
 */
/* $Id: K298.c 10442 2010-11-16 05:33:35Z Noguchi Isao $ */

#include <linux/kernel.h>
#include <linux/ioctl.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/p2gpio.h>

#include <linux/p2gpio_user.h>
#include <linux/p2gpio_K298.h>
#include "p2gpio.h"

#define BOARDTYPE_NAME "K298"
#define K298_VER "0.01"

/* the number of devices */
enum K298_IOPORT_CONST {
    K298_MAXNR_DIPSW_BITS = 4,
    K298_MAXNR_LED_BITS   = 4,
};

/* gpio port */
enum K298_GPIO_IOPORT {
    K298_GPIO_DIPSW_0  = 3,
    K298_GPIO_DIPSW_1  = 2,
    K298_GPIO_DIPSW_2  = 1,
    K298_GPIO_DIPSW_3  = 0,
    K298_GPIO_LED_0_N  = 27,
    K298_GPIO_LED_1_N  = 26,
    K298_GPIO_LED_2_N  = 36,
    K298_GPIO_LED_3_N  = 35,
    K298_GPIO_TESTP_0 = 47,
    K298_GPIO_TESTP_1 = 46,
    K298_GPIO_TESTP_2 = 45,
    K298_GPIO_TESTP_3 = 44,
    K298_GPIO_TESTP_4 = 43,
    K298_GPIO_TESTP_5 = 42,
    K298_GPIO_TESTP_6 = 41,
    K298_GPIO_TESTP_7 = 40,
    K298_GPIO_TESTP_8 = 39,
    K298_GPIO_PCB_VER1 = 4,
    K298_GPIO_PCB_VER0 = 5,
    K298_GPIO_PWR_OFF_P = 6,
    K298_GPIO_RST_RICOH_N = 22,
    K298_GPIO_CARD_DET_P = 23,
    K298_GPIO_SSD_DET_P = 21,
    K298_GPIO_SSD_HOST_P = 18,
    K298_GPIO_SSD_POFF_N = 20,
    K298_GPIO_SSD_OCI_N  = 32,
    K298_GPIO_SSD_LED_SEL = 34,
    K298_GPIO_USB_HOST_N = 10,
    K298_GPIO_USB_POFF_N = 11,
    K298_GPIO_USB_OCI_N  = 33,
    K298_GPIO_BATTERY_N = 37,
};

static const struct p2gpio_pmap K298_pmap[] = {

    /* POWER-off */
    P2GPIO_ENTRY_PMAP( POWER_OFF, 
                       K298_GPIO_PWR_OFF_P,
                       0 ),

    /* battery or AC-adaptor */
    P2GPIO_ENTRY_PMAP ( BATTERY,
                        K298_GPIO_BATTERY_N,
                        P2GPIO_PMAP_RONLY | P2GPIO_PMAP_REVERSE ),


    /*
     * P2CARD
     */

    /* Reset RICOH */
    P2GPIO_ENTRY_PMAP ( RST_RICOH,
                        K298_GPIO_RST_RICOH_N,
                        P2GPIO_PMAP_REVERSE ),

    /* detecting signal for P2CARD  */
    P2GPIO_ENTRY_PMAP_INT ( P2CARD_DETECT,
                            K298_GPIO_CARD_DET_P,
                            P2GPIO_PMAP_RONLY,
                            POLLIN, NULL ),

    /*
     * SSD
     */

    /* detecting SSD */
    P2GPIO_ENTRY_PMAP_INT ( SSD_DETECT,
                            K298_GPIO_SSD_DET_P,
                            P2GPIO_PMAP_RONLY,
                            POLLOUT, NULL ),

    P2GPIO_ENTRY_PMAP ( SSD_SEL_HOST,
                        K298_GPIO_SSD_HOST_P,
                        0 ),

    P2GPIO_ENTRY_PMAP ( SSD_PON,
                        K298_GPIO_SSD_POFF_N,
                        0 ),

    P2GPIO_ENTRY_PMAP ( SSD_OCI,
                        K298_GPIO_SSD_OCI_N,
                        P2GPIO_PMAP_RONLY | P2GPIO_PMAP_REVERSE ),

    P2GPIO_ENTRY_PMAP ( SSD_LED_SEL,
                        K298_GPIO_SSD_LED_SEL,
                        0 ),


    /*
     * USB
     */

    P2GPIO_ENTRY_PMAP ( USB_SEL_HOST,
                        K298_GPIO_USB_HOST_N,
                        P2GPIO_PMAP_REVERSE ),

    P2GPIO_ENTRY_PMAP ( USB_PON,
                        K298_GPIO_USB_POFF_N,
                        0 ),

    P2GPIO_ENTRY_PMAP ( USB_OCI,
                        K298_GPIO_USB_OCI_N,
                        P2GPIO_PMAP_RONLY | P2GPIO_PMAP_REVERSE ),

    /*
     * DIPSW
     */

    /* DIPSW-0 */
    P2GPIO_ENTRY_PMAP ( DIPSW_0,
                        K298_GPIO_DIPSW_0,
                        P2GPIO_PMAP_RONLY | P2GPIO_PMAP_REVERSE ),

    /* DIPSW-1 */
    P2GPIO_ENTRY_PMAP ( DIPSW_1,
                        K298_GPIO_DIPSW_1,
                        P2GPIO_PMAP_RONLY | P2GPIO_PMAP_REVERSE ),

    /* DIPSW-2 */
    P2GPIO_ENTRY_PMAP ( DIPSW_2,
                        K298_GPIO_DIPSW_2,
                        P2GPIO_PMAP_RONLY | P2GPIO_PMAP_REVERSE ),

    /* DIPSW-3 */
    P2GPIO_ENTRY_PMAP ( DIPSW_3,
                        K298_GPIO_DIPSW_3,
                        P2GPIO_PMAP_RONLY | P2GPIO_PMAP_REVERSE ),


    /*
     * LED(for debug)
     */

    /* LED-0 */
    P2GPIO_ENTRY_PMAP ( LED_0,
                        K298_GPIO_LED_0_N,
                        P2GPIO_PMAP_REVERSE ),

    /* LED-1 */
    P2GPIO_ENTRY_PMAP ( LED_1,
                        K298_GPIO_LED_1_N,
                        P2GPIO_PMAP_REVERSE ),

    /* LED-2 */
    P2GPIO_ENTRY_PMAP ( LED_2,
                        K298_GPIO_LED_2_N,
                        P2GPIO_PMAP_REVERSE ),

    /* LED-3 */
    P2GPIO_ENTRY_PMAP ( LED_3,
                        K298_GPIO_LED_3_N,
                        P2GPIO_PMAP_REVERSE ),


    /*
     * TEST ports
     */
    P2GPIO_ENTRY_PMAP ( TESTP_0, K298_GPIO_TESTP_0, 0 ),
    P2GPIO_ENTRY_PMAP ( TESTP_1, K298_GPIO_TESTP_1, 0 ),
    P2GPIO_ENTRY_PMAP ( TESTP_2, K298_GPIO_TESTP_2, 0 ),
    P2GPIO_ENTRY_PMAP ( TESTP_3, K298_GPIO_TESTP_3, 0 ),
    P2GPIO_ENTRY_PMAP ( TESTP_4, K298_GPIO_TESTP_4, 0 ),
    P2GPIO_ENTRY_PMAP ( TESTP_5, K298_GPIO_TESTP_5, 0 ),
    P2GPIO_ENTRY_PMAP ( TESTP_6, K298_GPIO_TESTP_6, 0 ),
    P2GPIO_ENTRY_PMAP ( TESTP_7, K298_GPIO_TESTP_7, 0 ),

    /* misc */
    P2GPIO_ENTRY_PMAP ( PCB_VER0, K298_GPIO_PCB_VER0, P2GPIO_PMAP_RONLY),
    P2GPIO_ENTRY_PMAP ( PCB_VER1, K298_GPIO_PCB_VER1, P2GPIO_PMAP_RONLY),

};

static int K298_ioctl( struct p2gpio_dev *dev, unsigned int cmd, unsigned long arg )
{
    int retval = 0;	/* return value */

    switch ( cmd ) {
        
        /* check PCB version number */
    case P2GPIO_IOC_K298_PCB_VER:
        {
            int val0,val1;
            retval = p2gpio_get_vport(P2GPIO_VPORT_PCB_VER0, &val0);
            if(retval<0){
                PERROR("invalid gpio port=0x%x\n",P2GPIO_VPORT_PCB_VER0);
                goto failed;
            }
            retval = p2gpio_get_vport(P2GPIO_VPORT_PCB_VER1, &val1);
            if(retval<0){
                PERROR("invalid gpio port=0x%x\n",P2GPIO_VPORT_PCB_VER1);
                goto failed;
            }
            retval = val0?(1<<0):0 + val1?(1<<1):0;
        }
        break;

    default:
        retval = -ENOTTY;


    } /* the end of switch */

 failed:
    return retval;
}


/*
 * p2gpio operations 
 */
static struct p2gpio_operations K298_ops = {
    .ioctl        = K298_ioctl, /* Ioctl(special commands) */
};

/*
 * initialize and get device information
 */
int __p2gpio_init_info( struct p2gpio_dev_info *info )
{
    if(unlikely(!info))
        return -EINVAL;

    info->nr_dipsw  = K298_MAXNR_DIPSW_BITS;
    info->nr_led    = K298_MAXNR_LED_BITS;
    info->ops       = &K298_ops;
    info->pmap      = K298_pmap;
    info->nr_pmap   = sizeof(K298_pmap)/sizeof(struct p2gpio_pmap);
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


