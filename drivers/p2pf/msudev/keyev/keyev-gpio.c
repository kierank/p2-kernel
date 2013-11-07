/*
 *  key_gpio.c  --- lower driver using gpio
 */
/* $Id: keyev-gpio.c 5088 2010-02-09 02:49:16Z Sawada Koji $ */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/timer.h>        /* timer */
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_gpio.h>

#include <linux/p2msudev_user.h>
#include "keyev.h"

#define MODULE_NAME "msudev-keyev-gpio"
#include "../debug.h"

/* for ioctl(P2MSUDEV_IOC_KEYEV_GETINFO) */
#define SAMPLING_PERIOD 10 /* 10msec */


/* struct for mapping to gpios */ 
struct gpio_maps {
    int keyno;
    int gpio;
#define FLAG_REVERSE    (1<<0)  /* reverse signal */
    unsigned long flags;
#define MAX_SCAN_CNT 3
    int scan_cnt;
    int last_value;
};


/* timer_list */
static struct key_param {

    struct timer_list timer;

    struct gpio_maps *maps;
    int nr_map;

    unsigned long keymap;
    unsigned long smpl_cnt;

} key_param;

/* for of */
static const char compatible[] = "p2pf,gpio-key";
static const char devtype[] = "key";

/* cyclic period for timer */
static const unsigned long timer_period = 
    ((HZ * SAMPLING_PERIOD) / 1000);

/* timer handler */
static void timer_fn(unsigned long data)
{
    int i;
    static unsigned int err_count=16;
    struct key_param *kp = (struct key_param *)data;

    /* scan key */
    for (i=0; i<kp->nr_map; i++) {
        struct gpio_maps *p = &(kp->maps[i]);
        int value = gpio_get_value(p->gpio)?1:0;
        if(p->flags&FLAG_REVERSE)
            value = value?0:1;  /* reverse signal */
        if(value<0){
            if(err_count){
                _ERR("cant't get gpio(=%d) value\n",p->gpio);
                err_count--;
            }
            goto exit_handler;
        }

        if(value!=p->last_value){
            p->scan_cnt=0;
        } else {
            if(p->scan_cnt>=MAX_SCAN_CNT){
                if(value)
                    kp->keymap |= (1<<p->keyno);
                else
                    kp->keymap &= ~(1<<p->keyno);
            } else {
                p->scan_cnt++;
            }
        }
        p->last_value = value;
    }


    /* call handler */
    __msudev_keyev_handler();

 exit_handler:

    /*  */
    kp->timer.expires = jiffies + timer_period;
    add_timer(&(kp->timer));

    /* update sample count */
    kp->smpl_cnt++;

    return;
}


/* initilazie */
static int init_key(struct msudev_keyev_ops *ops)
{
    int retval = 0;
    struct key_param *kp = (struct key_param *)ops->data;
    memset(kp,0,sizeof(struct key_param));

    /* get key mapping to gpio */
    {
        struct device_node *np;
        const u32 *prop;
        u32 len,i;
        

        np = of_find_compatible_node(NULL,devtype,compatible);
        if(!np){
            _ERR("NOT found a node : dev_type=\"%s\", compatible=\"%s\"\n",
                 devtype, compatible);
            retval = -EINVAL;
            goto fail;
        }

        prop = of_get_property(np,"key-map",&len);
        len /= sizeof(u32);
        if(!prop||len<3||(len%3)){
            _ERR("NOT found \"key-map\" property or too few cells in this property\n");
            retval = -EINVAL;
            goto fail;
        }

        kp->nr_map = len/3;
        kp->maps = (struct gpio_maps *)kzalloc(sizeof(struct gpio_maps) * kp->nr_map,
                                               GFP_KERNEL);
        if(NULL==kp->maps){
            _ERR("failed to allocate memory\n");
            retval=-ENOMEM;
            goto fail;
        }

        for(i=0;i<len;i+=3){
            struct gpio_maps *p = &kp->maps[i/3];
            p->keyno = prop[i];
            if(p->keyno >= (sizeof(kp->keymap)*8)){
                _ERR("invalid key number = %d\n", p->keyno);
                retval = -EINVAL;
                goto fail;
            }
            p->gpio = of_get_gpio(np,prop[i+1]);
            if(p->gpio<0){
                _ERR("NOT foud gpio port");
                retval=p->gpio;
                goto fail;
            }
            if(!gpio_is_valid_port(p->gpio)){
                _ERR("invalid gpio = %d\n",p->gpio);
                retval=-ENODEV;
                goto fail;
            }
            if(prop[i+2])
                p->flags |= FLAG_REVERSE;
            _INFO("key[%d] --> gpio[%d]%s\n",p->keyno,p->gpio,(p->flags&FLAG_REVERSE)?" : REVERSE":"");
        }

    }

    /* setup timer */
    setup_timer(&(kp->timer),timer_fn,(unsigned long)&key_param);
    kp->timer.expires = jiffies + timer_period;
    add_timer(&(kp->timer));

 fail:

    if(retval<0){

        /* deallocate memory */
        if(kp->maps){
            kfree(kp->maps);
            kp->maps=NULL;
            kp->nr_map = 0;
        }

    }

    /* complete */
    return retval;
}

/* cleanup */
static void cleanup_key(struct msudev_keyev_ops *ops)
{
    struct key_param *kp = (struct key_param *)ops->data;

    /* delete timer */
    del_timer_sync(&(kp->timer));

    /* deallocate memory */
    if(kp->maps){
        kfree(kp->maps);
        kp->maps=NULL;
        kp->nr_map = 0;
    }
}

/* get key map data */
static int keyscan(struct msudev_keyev_ops *ops,
                   unsigned long *keymap, unsigned long *smpl_cnt)
{
    struct key_param *kp = (struct key_param *)ops->data;
    if(keymap)
        *keymap = kp->keymap;
    if(smpl_cnt)
        *smpl_cnt = kp->smpl_cnt;
    return 0;
}


/*
 *  structure of operational functions
 */
static struct msudev_keyev_ops key_gpio_ops = {

    .init_key       = init_key,
    .cleanup_key    = cleanup_key,
    .keyscan        = keyscan,
    .sample_period = (SAMPLING_PERIOD * 1000),
    .data           = (void*)&key_param,
};

/*
 *  get operational functions
 */
struct msudev_keyev_ops *  __keyev_get_ops(void)
{
    return &key_gpio_ops;
}
