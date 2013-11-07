/*
 *  linux/drivers/p2pf/p2ioport/p2ioport_lib.c
 */
/* $Id: p2ioport_gpio_lib.c 10439 2010-11-16 04:47:46Z Noguchi Isao $ */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include "p2ioport_gpio_lib.h"

int __p2ioport_get_gpio( const unsigned int gpio, int *const p_val )
{
    int retval = 0;

    if(!gpio_is_valid_port(gpio)){
        retval = -ENODEV;
        goto fail;
    }
    retval = gpio_get_value(gpio);
    if(retval<0)
        goto fail;
    *p_val = retval?1:0;

 fail:
    return retval;
}

int __p2ioport_set_gpio( const unsigned int gpio, const int val )
{
    int retval = 0;

    if(!gpio_is_valid_port(gpio)){
        retval = -ENODEV;
        goto fail;
    }
    gpio_set_value(gpio,val?1:0);

 fail:
    return retval;
}


int __p2ioport_get_vport( struct p2ioport_gpio_pmap *pmap, int nr_pmap,
                          unsigned int vport, int *p_val )
{
    int retval=0;
    int val=0;
    int i;
    struct p2ioport_gpio_pmap *p=NULL;;

    for(i=0, p=pmap; i<nr_pmap; i++, p++){
        if(p->vport==vport) {
            retval=__p2ioport_get_gpio(p->gpio,&val);
            if(retval<0){
                pr_err("*** ERROR: [p2ioport] invalid or not to get value, vports=0x%08X\n", vport);
                goto fail;
            }
            if(p->flag&P2IOPORT_GPIO_PMAP_REVERSE)
                val = val?0:1;  /* Reversed polarity in order to active-low signal */
            break;
        }
    }

    if(i>=nr_pmap){
        pr_err("*** ERROR: [p2ioport] unsupported virtual ports=0x%08X\n", vport);
        retval = -EINVAL;
    }

 fail:
    if(retval>=0){
        pr_debug( "[p2ioport] virtual port=0x%08X, val=%d\n", vport, val);
        *p_val = val;
    }
    return retval;
}


int __p2ioport_set_vport( struct p2ioport_gpio_pmap *pmap, int nr_pmap,
                          unsigned int vport, int val )
{
    int retval=0;
    int i;
    struct p2ioport_gpio_pmap *p=NULL;;

    for(i=0, p=pmap; i<nr_pmap; i++, p++){
        if(p->vport==vport) {
            if(p->flag & P2IOPORT_GPIO_PMAP_RONLY)
                goto fail;
            if(p->flag & P2IOPORT_GPIO_PMAP_REVERSE)
                val = val?0:1;  /* Reversed polarity in order to active-low signal */
            retval=__p2ioport_set_gpio(p->gpio,val);
            if(retval<0){
                pr_err("*** ERROR: [p2ioport] invalid or not to set value, vports=0x%08X\n", vport);
                goto fail;
            }
            break;
        }
    }

 fail:
    return retval;
}


int __p2ioport_lock_gpio( const unsigned int gpio, int *const p_val )
{
    int retval = 0;

    if(!gpio_is_valid_port(gpio)){
        retval = -ENODEV;
        goto fail;
    }

    {
        int r = gpio_request(gpio,__FUNCTION__);
        switch(r){
        case 0:
            *p_val = 0;
            break;
        case -EBUSY:
            *p_val = r;
            break;
        default:
            retval = r;
            goto fail;
        }
    }

 fail:
    return retval;
}

int __p2ioport_unlock_gpio( const unsigned int gpio )
{
    int retval = 0;

    if(!gpio_is_valid_port(gpio)){
        retval = -ENODEV;
        goto fail;
    }
    gpio_free(gpio);

 fail:
    return retval;
}


int __p2ioport_lock_vport( struct p2ioport_gpio_pmap *pmap, int nr_pmap,
                          unsigned int vport, int *p_val )
{
    int retval=0;
    int val=0;
    int i;
    struct p2ioport_gpio_pmap *p=NULL;;

    for(i=0, p=pmap; i<nr_pmap; i++, p++){
        if(p->vport==vport) {
            retval=__p2ioport_lock_gpio(p->gpio,&val);
            if(retval<0){
                pr_err("*** ERROR: [p2ioport] failed in gpio_request(), vports=0x%08X\n", vport);
                goto fail;
            }
            break;
        }
    }

    if(i>=nr_pmap){
        pr_err("*** ERROR: [p2ioport] unsupported virtual ports=0x%08X\n", vport);
        retval = -EINVAL;
    }

 fail:
    if(retval>=0){
        pr_debug( "[p2ioport] virtual port=0x%08X, val=%d\n", vport, val);
        *p_val = val;
    }
    return retval;
}


int __p2ioport_unlock_vport( struct p2ioport_gpio_pmap *pmap, int nr_pmap,
                          unsigned int vport )
{
    int retval=0;
    int i;
    struct p2ioport_gpio_pmap *p=NULL;;

    for(i=0, p=pmap; i<nr_pmap; i++, p++){
        if(p->vport==vport) {
            __p2ioport_unlock_gpio(p->gpio);
            break;
        }
    }

    if(i>=nr_pmap){
        pr_err("*** ERROR: [p2ioport] unsupported virtual ports=0x%08X\n", vport);
        retval = -EINVAL;
    }

    return retval;
}



