/*****************************************************************************
 * linux/drivers/p2pf/p2ioport/K298.c
 *
 *   K298 I/O port and GPIO access low-level driver
 *   
 *     Copyright (C) 2009-2010 Panasonic corp.
 *     All Rights Reserved.
 *
 *****************************************************************************/
/* $Id: K298.c 6918 2010-05-14 10:28:21Z Noguchi Isao $ */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ioctl.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
/* #include <linux/timer.h> */
#include <linux/gpio.h>
#include <linux/p2ioport.h>

#include "p2ioport_gpio_lib.h"

/***** definitions *****/
/** constants **/
#define BOARDTYPE_NAME "K298"
#define K298_VER "0.04"

/* the number of devices */
enum K298_IOPORT_CONST {
  K298_MAXNR_DIPSW    = 1, /* 4bit-DIPSW x 1 */
  K298_MAXNR_LED      = 1, /* 4bit-LED x 1 */
  K298_MAXNR_ROTARYSW = 0, /* NOT existent */
  K298_MAXNR_DEVICE   = 0, /* NOT existent, using GPIO */
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

static struct p2ioport_gpio_pmap K298_pmap[] = {
    { P2IOPORT_VPORT_POWER_OFF, K298_GPIO_PWR_OFF_P, 0}, /* POWER-off */
    { P2IOPORT_VPORT_BATTERY, K298_GPIO_BATTERY_N, P2IOPORT_GPIO_PMAP_RONLY|P2IOPORT_GPIO_PMAP_REVERSE}, /* battery or AC-adaptor */
    /* P2CARD */
    { P2IOPORT_VPORT_RST_RICOH, K298_GPIO_RST_RICOH_N, P2IOPORT_GPIO_PMAP_REVERSE }, /* Reset RICOH */
    { P2IOPORT_VPORT_P2CARD_DETECT, K298_GPIO_CARD_DET_P, P2IOPORT_GPIO_PMAP_RONLY }, /* detecting signal for P2CARD  */
    /* SSD */
    { P2IOPORT_VPORT_SSD_DETECT, K298_GPIO_SSD_DET_P, P2IOPORT_GPIO_PMAP_RONLY }, /* detecting SSD */
    { P2IOPORT_VPORT_SSD_SEL_HOST, K298_GPIO_SSD_HOST_P, 0},
    { P2IOPORT_VPORT_SSD_PON, K298_GPIO_SSD_POFF_N, 0},
    { P2IOPORT_VPORT_SSD_OCI, K298_GPIO_SSD_OCI_N, P2IOPORT_GPIO_PMAP_RONLY|P2IOPORT_GPIO_PMAP_REVERSE},
    { P2IOPORT_VPORT_SSD_LED_SEL, K298_GPIO_SSD_LED_SEL, 0},
    /* USB */
    { P2IOPORT_VPORT_USB_SEL_HOST, K298_GPIO_USB_HOST_N, P2IOPORT_GPIO_PMAP_REVERSE},
    { P2IOPORT_VPORT_USB_PON, K298_GPIO_USB_POFF_N, 0},
    { P2IOPORT_VPORT_USB_OCI, K298_GPIO_USB_OCI_N, P2IOPORT_GPIO_PMAP_RONLY|P2IOPORT_GPIO_PMAP_REVERSE},
    /* DIPSW */
    { P2IOPORT_VPORT_DIPSW_0, K298_GPIO_DIPSW_0, P2IOPORT_GPIO_PMAP_RONLY|P2IOPORT_GPIO_PMAP_REVERSE }, /* DIPSW-0 */
    { P2IOPORT_VPORT_DIPSW_1, K298_GPIO_DIPSW_1, P2IOPORT_GPIO_PMAP_RONLY|P2IOPORT_GPIO_PMAP_REVERSE }, /* DIPSW-1 */
    { P2IOPORT_VPORT_DIPSW_2, K298_GPIO_DIPSW_2, P2IOPORT_GPIO_PMAP_RONLY|P2IOPORT_GPIO_PMAP_REVERSE }, /* DIPSW-2 */
    { P2IOPORT_VPORT_DIPSW_3, K298_GPIO_DIPSW_3, P2IOPORT_GPIO_PMAP_RONLY|P2IOPORT_GPIO_PMAP_REVERSE }, /* DIPSW-3 */
    /* LED(for debug) */
    { P2IOPORT_VPORT_LED_0, K298_GPIO_LED_0_N, P2IOPORT_GPIO_PMAP_REVERSE }, /* LED-0 */
    { P2IOPORT_VPORT_LED_1, K298_GPIO_LED_1_N, P2IOPORT_GPIO_PMAP_REVERSE }, /* LED-1 */
    { P2IOPORT_VPORT_LED_2, K298_GPIO_LED_2_N, P2IOPORT_GPIO_PMAP_REVERSE }, /* LED-2 */
    { P2IOPORT_VPORT_LED_3, K298_GPIO_LED_3_N, P2IOPORT_GPIO_PMAP_REVERSE }, /* LED-3 */
    /* TEST ports */
    { P2IOPORT_VPORT_TESTP_0, K298_GPIO_TESTP_0, 0 },
    { P2IOPORT_VPORT_TESTP_1, K298_GPIO_TESTP_1, 0 },
    { P2IOPORT_VPORT_TESTP_2, K298_GPIO_TESTP_2, 0 },
    { P2IOPORT_VPORT_TESTP_3, K298_GPIO_TESTP_3, 0 },
    { P2IOPORT_VPORT_TESTP_4, K298_GPIO_TESTP_4, 0 },
    { P2IOPORT_VPORT_TESTP_5, K298_GPIO_TESTP_5, 0 },
    { P2IOPORT_VPORT_TESTP_6, K298_GPIO_TESTP_6, 0 },
    { P2IOPORT_VPORT_TESTP_7, K298_GPIO_TESTP_7, 0 },
    { P2IOPORT_VPORT_TESTP_8, K298_GPIO_TESTP_8, 0 },
};

static unsigned char K298_DEBUG = 0;   /* for DEBUG */

static struct K298_param {
    wait_queue_head_t   queue;
    spinlock_t lock;
/*     struct timer_list timer; */
    int p2card_detect,ssd_detect;
} K298_param;


/* interval to check front door by jiffies */
#define INTERVAL_FDOOR  ((HZ * 30) / 1000) /* about 30 msec */


/** print messages **/
#define PINFO( fmt, args... )	printk( KERN_INFO "[p2ioport] " fmt, ## args)
#define PERROR( fmt, args... )	printk( KERN_ERR "[p2ioport](E)" fmt, ## args)
#define PDEBUG( fmt, args... )	do { if (K298_DEBUG) printk( KERN_INFO "[p2ioport:K298-l.%d] " fmt, __LINE__, ## args); } while(0)

/********************************* function *********************************/


/***************************************************************************
 * K298_get_vport
 * vport : virtual port numver
 * p_val : pointer of value
 **************************************************************************/
static int K298_get_vport( int vport, int *p_val )
{
    return __p2ioport_get_vport(K298_pmap, sizeof(K298_pmap)/sizeof(struct p2ioport_gpio_pmap),
                                  vport, p_val);
}


/***************************************************************************
 * K298_set_vport
 * vport : virtual port numver
 * p_val : pointer of value
 **************************************************************************/
static int K298_set_vport( int vport, int val )
{
    return __p2ioport_set_vport(K298_pmap, sizeof(K298_pmap)/sizeof(struct p2ioport_gpio_pmap),
                                  vport, val);
}


/***************************************************************************
 * K298_lock_vport
 * vport : virtual port numver
 * p_val : pointer of value
 **************************************************************************/
static int K298_lock_vport( int vport, int *p_val )
{
    return __p2ioport_lock_vport(K298_pmap, sizeof(K298_pmap)/sizeof(struct p2ioport_gpio_pmap),
                                 vport, p_val);
}


/***************************************************************************
 * K298_unlock_vport
 * vport : virtual port numver
 **************************************************************************/
static int K298_unlock_vport( int vport )
{
    return __p2ioport_unlock_vport(K298_pmap, sizeof(K298_pmap)/sizeof(struct p2ioport_gpio_pmap),
                                   vport);
}


/***************************************************************************
 * K298_get_dipsw
 *   - 0=On, 1=Off! (val: 0=Off, 1=On)
 *   - don't need to convert endian
 **************************************************************************/
static int K298_get_dipsw( int num, unsigned long *val )
{
    unsigned long tmpval = 0;
    int i;

    /* get DIPSW. */
    for(i=0;i<4;i++){
        int retval;
        int tmp=0;
        retval=K298_get_vport(P2IOPORT_VPORT_DIPSW_0+i,&tmp);
        if(retval<0)
            return retval;
        tmpval |= tmp?(1<<i):0;
    }
    if(val)
        *val = tmpval;
    return (0);
}


/***************************************************************************
 * K298_get_led
 *   - 0=Off, 1=On (val: 0=Off, 1=On)
 *   - don't need to convert endian
 **************************************************************************/
static int K298_get_led( int num, unsigned long *val )
{
    int i;
    unsigned long tmpval = 0;

    /* get LED. */
    for(i=0;i<4;i++){
        int retval;
        int tmp=0;
        retval=K298_get_vport(P2IOPORT_VPORT_LED_0+i,&tmp);
        if(retval<0)
            return retval;
        tmpval |= tmp?(1<<i):0;
    }
    if(val)
        *val = tmpval;
    return (0);
}


/***************************************************************************
 * K298_set_led
 *   - DWORD access
 *   - 0=Off, 1=On (val: 0=Off, 1=On)
 *   - don't need to convert endian
 **************************************************************************/
static int K298_set_led( int num, unsigned long val )
{
    int i;

    /* set LED On/Off. */
    for(i=0;i<4;i++){
        int retval;
        retval=K298_set_vport(P2IOPORT_VPORT_LED_0+i,(val&(1<<i))?1:0);
        if(retval<0)
            return retval;
    }
    return (0);
}


/***************************************************************************
 * K298_clr_led
 *   - DWORD access
 *   - 0=Off, 1=On (val: 0=Off, 1=On)
 **************************************************************************/
static int K298_clr_led( int num )
{
    int i;

    /* set Led Off. */
    for(i=0;i<4;i++){
        int retval;
        retval=K298_set_vport(P2IOPORT_VPORT_LED_0+i,0);
        if(retval<0)
            return retval;
    }
    return (0);
}


/***************************************************************************
 * K298_toggle_led
 *   - DWORD access
 *   - 0=Off, 1=On (val: 0=Off, 1=On)
 **************************************************************************/
static int K298_toggle_led( int num, unsigned long val )
{
    int i;

    /* toggle LED bit. */
    for(i=0;i<4;i++){
        if(val & (1<<i)){
            int retval;
            int tmp=0;
            retval=K298_get_vport(P2IOPORT_VPORT_LED_0+i,&tmp);
            if(retval<0)
                return retval;
            retval=K298_set_vport(P2IOPORT_VPORT_LED_0+i,tmp?0:1);
            if(retval<0)
                return retval;
        }
    }
    return (0);
}


#ifdef CONFIG_HEART_BEAT
/***************************************************************************
 * K298_heartbeat_led
 *   - word access!
 *   - 0=On, 1=Off! (val: 0=Off, 1=On)
 **************************************************************************/
static int K298_heartbeat_led( int num )
{
    return K298_toggle_led( num, (1<<0) );
}
#endif  /* CONFIG_HEART_BEAT */

/***************************************************************************
 * ioctl (special commands)
 **************************************************************************/
static int K298_ioctl( unsigned int cmd, unsigned long arg )
{
    int retval = 0;	/* return value */

    /*** main routine ***/
    switch ( cmd ) {

    case P2IOPORT_IOC_K298_TOGGLE_DEBUGMODE:
        K298_DEBUG ^= 1;
        PINFO( "%s mode\n", K298_DEBUG?"Debug":"Non-Debug" );
        break;

        /* check PCB version number */
    case P2IOPORT_IOC_K298_PCB_VER:
        {
            int val0,val1;
            retval = __p2ioport_get_gpio(K298_GPIO_PCB_VER0,&val0);
            if(retval<0){
                PERROR("invalid gpio port=%d\n",K298_GPIO_PCB_VER0);
                goto failed;
            }
            retval = __p2ioport_get_gpio(K298_GPIO_PCB_VER1,&val1);
            if(retval<0){
                PERROR("invalid gpio port=%d\n",K298_GPIO_PCB_VER1);
                goto failed;
            }
            retval = val0?(1<<0):0 + val1?(1<<1):0;
        }
        break;

    default:
        PERROR( "Unknown ioctl command!(0x%X)\n", cmd );
        retval = -EINVAL;

    } /* the end of switch */

 failed:
    return (retval);
}


/***************************************************************************
 * poll method
 **************************************************************************/

static unsigned int K298_poll(struct file *filp, struct poll_table_struct *wait)
{
    unsigned long flags=0;
    unsigned int mask=0;

    poll_wait(filp, &K298_param.queue, wait);

    spin_lock_irqsave(&K298_param.lock,flags);

    if(K298_param.p2card_detect){
        mask |= POLLIN;
        K298_param.p2card_detect = 0;
    }

    if(K298_param.ssd_detect){
        mask |= POLLOUT;
        K298_param.ssd_detect = 0;
    }

    spin_unlock_irqrestore(&K298_param.lock,flags);

    return mask;
}

/*** set p2ioport operations ***/
static struct p2ioport_operations K298_ops = {
  .get_dipsw   = K298_get_dipsw,   /* Get DIPSW */
  .get_led     = K298_get_led,     /* Get LED */
  .set_led     = K298_set_led,     /* Set LED */
  .clr_led     = K298_clr_led,     /* Clear LED */
  .toggle_led  = K298_toggle_led,  /* Toggle LED */
#ifdef CONFIG_HEART_BEAT
  .heartbeat_led  = K298_heartbeat_led, /* for Heartbeat LED(kernel only) */
#endif  /* CONFIG_HEART_BEAT */
  .get_vport   = K298_get_vport,   /* Get virtual port status */
  .set_vport   = K298_set_vport,   /* Set virtual port status */
  .lock_vport   = K298_lock_vport, /* Test and lock virtual port */
  .unlock_vport = K298_unlock_vport, /* Unlock virtual port */
  .ioctl       = K298_ioctl,       /* Ioctl(special commands) */
  .poll        = K298_poll,
};

/***************************************************************************
 * interrupt handler
 **************************************************************************/

static void handler_p2card(unsigned int gpio, void *data)
{
    K298_param.p2card_detect = 1;
    wake_up(&K298_param.queue);
}

static void handler_ssd(unsigned int gpio, void *data)
{
    K298_param.ssd_detect = 1;
    wake_up(&K298_param.queue);
}

/* static void timer_fn(unsigned long data) */
/* { */
/*     /\* TODO *\/ */

/*     K298_param.timer.expires = jiffies + INTERVAL_FDOOR; */
/*     add_timer(&K298_param.timer); */
/* } */


/***************************************************************************
 * K298_init
 **************************************************************************/
static inline int K298_init( struct p2ioport_info_s *info )
{
    int retval=0;

    /* print init Message. */
    PINFO( " board type: %s(ver. %s)\n", BOARDTYPE_NAME, K298_VER );

    /* init debug switch. */
    K298_DEBUG = 0;

    /* initialize parameter structure */
    memset(&K298_param,0,sizeof(struct K298_param));

    /* spinlock */
    spin_lock_init(&K298_param.lock);

    /* wait queue head */
    init_waitqueue_head(&K298_param.queue);
  
    /* interrupt handlers */
    if(!gpio_is_valid_irq(K298_GPIO_CARD_DET_P)||!gpio_is_valid_irq(K298_GPIO_SSD_DET_P)){
        PERROR("invalid irq gpio port\n");
        retval = -ENODEV;
        goto fail;
    }
    retval=gpio_request_irq(K298_GPIO_CARD_DET_P,handler_p2card,NULL);
    if(retval<0){
        PERROR("can't register IRQ for gpio(CARD_DET_P), retval=%d\n",retval);
        goto fail;
    }
    retval=gpio_request_irq(K298_GPIO_SSD_DET_P,handler_ssd,NULL);
    if(retval<0){
        PERROR("can't register IRQ for gpio(SSD_DET_P), retval=%d\n",retval);
        gpio_free_irq(K298_GPIO_CARD_DET_P);
        goto fail;
    }

/*     /\* timer_list for checking front door *\/ */
/*     setup_timer(&K298_param.timer, timer_fn, 0); */
/*     K298_param.timer.expires = jiffies + INTERVAL_FDOOR; */
/*     add_timer(&K298_param.timer); */
  
    /* set info. */
    info->nr_dipsw    = K298_MAXNR_DIPSW;
    info->nr_led      = K298_MAXNR_LED;
    info->nr_rotarysw = K298_MAXNR_ROTARYSW;
    info->nr_device   = K298_MAXNR_DEVICE;
    info->ops         = &K298_ops;
    strcpy( info->name, BOARDTYPE_NAME );

 fail:

    /* complete */
    return retval;

}


/***************************************************************************
 * K298_cleanup
 **************************************************************************/
static inline void K298_cleanup( void )
{
  PINFO( "Clean up %s\n", BOARDTYPE_NAME ); /* Message */

/*   /\* stop kernel timer *\/ */
/*   del_timer_sync(&K298_param.timer); */

  /* unregister interrupt handlers */
  gpio_disable_irq(K298_GPIO_CARD_DET_P);
  gpio_disable_irq(K298_GPIO_SSD_DET_P);
  gpio_free_irq(K298_GPIO_CARD_DET_P);
  gpio_free_irq(K298_GPIO_SSD_DET_P);

  /* clear debug switch. */
  K298_DEBUG = 0;
}


/** export getting operations function. **/
inline void __p2ioport_get_ops( struct p2ioport_operations *ops )
{
  ops->get_dipsw  = K298_get_dipsw;
  ops->get_led    = K298_get_led;
  ops->set_led    = K298_set_led;
  ops->clr_led    = K298_clr_led;
  ops->toggle_led = K298_toggle_led;
#ifdef CONFIG_HEART_BEAT
  ops->heartbeat_led = K298_heartbeat_led; /* for Heartbeat LED(kernel only) */
#endif  /* CONFIG_HEART_BEAT */
  ops->get_vport  = K298_get_vport;
  ops->set_vport  = K298_set_vport;
  ops->lock_vport = K298_lock_vport;
  ops->unlock_vport = K298_unlock_vport;
}


/** export init/exit function. **/
/*   CAUTION: Never change function name! */
inline int __p2ioport_init_info( struct p2ioport_info_s *info )
{
  return K298_init( info );
}


inline void __p2ioport_cleanup_info( struct p2ioport_info_s *info )
{
  memset( info, 0, sizeof(struct p2ioport_info_s) );
  K298_cleanup();
}


/******************** the end of the file "K298.c" ********************/
