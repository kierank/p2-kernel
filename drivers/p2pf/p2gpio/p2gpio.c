/*
 * linux/drivers/p2pf/p2gpio/p2gpio.c
 *
 *   P2PF I/O port driver using GPIOLIB
 *   
 *     Copyright (C) 2010 Panasonic corp.
 *     All Rights Reserved.
 *
 */
/* $Id: p2gpio.c 14402 2011-05-18 02:49:52Z Noguchi Isao $ */

#include <linux/module.h>  /* for module */
#include <linux/kernel.h>  /* for kernel module */
#include <linux/init.h>    /* for init */
#include <linux/fs.h>
#include <linux/cdev.h>    /* for character driver */
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/proc_fs.h> /* for proc filesystem */
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/gpio.h>

#include <linux/p2gpio_user.h>
#include "p2gpio.h"


/** module infomation **/
#if defined(MODULE)
MODULE_AUTHOR("Panasonic");
MODULE_SUPPORTED_DEVICE(P2GPIO_DEVNAME);
MODULE_LICENSE("GPL");
#endif /* MODULE */


/* default device number */

#ifdef CONFIG_P2GPIO_FIXED_MAJOR
#define P2GPIO_MAJOR   CONFIG_P2GPIO_FIXED_MAJOR
#else  /* ! CONFIG_P2GPIO_FIXED_MAJOR */
#define P2GPIO_MAJOR    245
#endif  /* CONFIG_P2GPIO_FIXED_MAJOR */

#ifdef CONFIG_P2GPIO_FIXED_MINOR
#define P2GPIO_MINOR    CONFIG_P2GPIO_FIXED_MINOR
#else  /* ! CONFIG_P2GPIO_FIXED_MINOR */
#define P2GPIO_MINOR    0
#endif  /* CONFIG_P2GPIO_FIXED_MINOR */

/* private parameter */
static struct {
    spinlock_t lock;
    struct list_head head;
    dev_t   devno;
    struct cdev *cdev;
    struct p2gpio_dev_info info;
} p2gpio_priv;


/********************************* function *********************************/

static inline int __get_gpio( const unsigned int gpio, int *const p_val )
{
    int retval = 0;

    retval = gpio_get_value(gpio);
    if(unlikely(retval<0))
        return retval;
    *p_val = retval?1:0;
    return 0;
}

static inline int __set_gpio( const unsigned int gpio, const int val )
{
    gpio_set_value(gpio,val?1:0);
    return 0;
}


/*
 * p2gpio_get_vport
 */
int p2gpio_get_vport( int vport, int *p_val )
{
    int retval=0;
    int val=0;
    int i;
    struct p2gpio_dev_info *info = &p2gpio_priv.info;
    const struct p2gpio_pmap *p=NULL;;

    for(i=0, p=info->pmap; i<info->nr_pmap; i++, p++){
        if(p->vport==vport) {
            retval=__get_gpio(p->gpio,&val);
            if(unlikely(retval<0)){
                PERROR("invalid or not to get value, vports=%s(0x%08X,%d)\n",
                       p->name,p->vport,p->gpio);
                goto fail;
            }
            if(p->flag&P2GPIO_PMAP_REVERSE)
                val = val?0:1;  /* Reversed polarity in order to active-low signal */
            break;
        }
    }

    if(unlikely(i>=info->nr_pmap)){
        PERROR("unsupported virtual ports=0x%08X\n", vport);
        retval = -EINVAL;
    }

 fail:
    if(likely(retval>=0)){
        PDEBUG( "virtual port=%s(0x%08X,%d), val=%d\n",
                  p->name, p->vport, p->gpio, val);
        *p_val = val;
    }
    return retval;
}


/*
 * p2gpio_set_vport
 */
static int p2gpio_set_vport( int vport, int val )
{
    int retval=0;
    int i;
    struct p2gpio_dev_info *info = &p2gpio_priv.info;
    const struct p2gpio_pmap *p=NULL;;

    for(i=0, p=info->pmap; i<info->nr_pmap; i++, p++){
        if(p->vport==vport) {
            if(unlikely(p->flag & P2GPIO_PMAP_RONLY)) {
                PWARNING("cannot output to input vport=%s(0x%08X,%d)\n",
                         p->name,p->vport,p->gpio);
                goto fail;
            }
            retval=__set_gpio(p->gpio,
                              (p->flag & P2GPIO_PMAP_REVERSE)?(!val):val);
            if(unlikely(retval<0)){
                PERROR("invalid or not to set value, vports=%s(0x%08X,%d)\n",
                       p->name,p->vport,p->gpio);
                goto fail;
            }
            break;
        }
    }

 fail:
    if(likely(retval>=0)){
        PDEBUG( "virtual port=%s(0x%08X,%d), val=%d\n",
                  p->name, p->vport, p->gpio, val);
    }
    return retval;
}


static inline int __lock_gpio( const unsigned int gpio, int *const p_val )
{
    int retval = 0;
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

static inline int __unlock_gpio( const unsigned int gpio )
{
    gpio_free(gpio);
    return 0;
}


/*
 * p2gpio_lock_vport
 */
static int p2gpio_lock_vport( int vport , int *p_val)
{
    int retval=0;
    int val=0;
    int i;
    struct p2gpio_dev_info *info = &p2gpio_priv.info;
    const struct p2gpio_pmap *p=NULL;;

    for(i=0, p=info->pmap; i<info->nr_pmap; i++, p++){
        if(p->vport==vport) {
            retval=__lock_gpio(p->gpio,&val);
            if(unlikely(retval<0)){
                PERROR("failed in gpio_request(), vports=%s(0x%08X,%d)\n",
                       p->name,p->vport,p->gpio);
                goto fail;
            }
            break;
        }
    }

    if(unlikely(i>=info->nr_pmap)){
        PERROR("unsupported virtual ports=0x%08X\n", vport);
        retval = -EINVAL;
    }

 fail:
    if(likely(retval>=0)){
        PERROR( "virtual vports=%s(0x%08X,%d), val=%d\n",
                       p->name,p->vport,p->gpio,val);
        *p_val = val;
    }
    return retval;
}

/*
 * p2gpio_unlock_vport
 */
static int p2gpio_unlock_vport( int vport )
{
    int retval=0;
    int i;
    struct p2gpio_dev_info *info = &p2gpio_priv.info;
    const struct p2gpio_pmap *p=NULL;;

    for(i=0, p=info->pmap; i<info->nr_pmap; i++, p++){
        if(p->vport==vport) {
            __unlock_gpio(p->gpio);
            break;
        }
    }

    if(unlikely(i>=info->nr_pmap)){
        PERROR("unsupported virtual vports=%s(0x%08X,%d)\n",
               p->name,p->vport,p->gpio);
        retval = -EINVAL;
    }

    return retval;
}


int p2gpio_vport_by_name( const char * const name, unsigned int * const p_vport)
{
    int retval=0;
    int i;
    struct p2gpio_dev_info *info = &p2gpio_priv.info;
    const struct p2gpio_pmap *p=NULL;;

    for(i=0, p=info->pmap; i<info->nr_pmap; i++, p++){

        if(!strcmp(p->name,name)){
            *p_vport = p->vport;
            break;
        }

    }

    if(unlikely(i>=info->nr_pmap)){
        PDEBUG("Not found the virtual ports named '%s'.\n", name);
        retval = -ENODEV;
    }

    return retval;
}


/*
 * p2gpio_get_dipsw
 */
static int p2gpio_get_dipsw( int num, unsigned long *p_val )
{
    struct p2gpio_dev_info *info = &p2gpio_priv.info;
    unsigned long tmpval = 0;
    int i;

    /* check port bits */
    if(unlikely(info->nr_dipsw<1)){
        PERROR("not support DIPSW : port bits of DIPSW = %d\n", info->nr_dipsw);
        return -ENODEV;
    }

    /* get DIPSW. */
    for(i=0;i<info->nr_dipsw;i++){
        int retval;
        int tmp=0;
        retval=p2gpio_get_vport(P2GPIO_VPORT_DIPSW_0+i, &tmp);
        if(unlikely(retval<0))
            return retval;
        tmpval |= tmp?(1<<i):0;
    }
    if(likely(p_val))
        *p_val = tmpval;

    return 0;

}


/*
 * p2gpio_get_rotsw
 */
static int p2gpio_get_rotsw( int num, unsigned long *p_val )
{
    struct p2gpio_dev_info *info = &p2gpio_priv.info;
    unsigned long tmpval = 0;
    int i;

    /* check port bits */
    if(unlikely(info->nr_rotsw<1)){
        PERROR("not support ROTSW : port bits of ROTSW = %d\n", info->nr_rotsw);
        return -ENODEV;
    }

    /* get ROTSW. */
    for(i=(info->nr_rotsw-1); i>=0;i--){
        int retval;
        int tmp=0;
        retval=p2gpio_get_vport(P2GPIO_VPORT_ROTSW_0+i, &tmp);
        if(unlikely(retval<0))
            return retval;
        if(tmp) {
            tmpval=i;
            break;
        }
    }

    if(i<0){
        PERROR("invalid decode value of ROTSW\n");
        return -EIO;
    }

    if(likely(p_val))
        *p_val = tmpval;

    return 0;

}


/*
 * p2gpio_get_led
 */
static int p2gpio_get_led( int num, unsigned long *p_val )
{
    struct p2gpio_dev_info *info = &p2gpio_priv.info;
    int i;
    unsigned long tmpval = 0;

    /* check port bits */
    if(unlikely(info->nr_led<1)){
        PERROR("not support LED : port bits of LED = %d\n", info->nr_led);
        return -ENODEV;
    }

    /* get LED. */
    for(i=0;i<info->nr_led;i++){
        int retval;
        int tmp=0;
        retval=p2gpio_get_vport(P2GPIO_VPORT_LED_0+i,&tmp);
        if(unlikely(retval<0))
            return retval;
        tmpval |= tmp?(1<<i):0;
    }
    if(likely(p_val))
        *p_val = tmpval;
    return (0);
}


/*
 * p2gpio_set_led
 */
static int p2gpio_set_led( int num, unsigned long val )
{
    struct p2gpio_dev_info *info = &p2gpio_priv.info;
    int i;

    /* check port bits */
    if(unlikely(info->nr_led<1)){
        PERROR("not support LED : port bits of LED = %d\n", info->nr_led);
        return -ENODEV;
    }

    /* set LED On/Off. */
    for(i=0; i<info->nr_led; i++){
        int retval;
        if(val&(1<<i)){
            retval=p2gpio_set_vport(P2GPIO_VPORT_LED_0+i, 1);
            if(unlikely(retval<0))
                return retval;
        }
    }

    return 0;
}


/*
 * p2gpio_clr_led
 */
static int p2gpio_clr_led( int num, unsigned long val)
{
    struct p2gpio_dev_info *info = &p2gpio_priv.info;
    int i;

    /* check port bits */
    if(unlikely(info->nr_led<1)){
        PERROR("not support LED : port bits of LED = %d\n", info->nr_led);
        return -ENODEV;
    }

    /* set Led Off. */
    for(i=0;i<info->nr_led;i++){
        int retval;
        if(val&(1<<i)){
            retval=p2gpio_set_vport(P2GPIO_VPORT_LED_0+i, 0);
            if(unlikely(retval<0))
                return retval;
        }
    }

    return 0;
}


/*
 * p2gpio_toggle_led
 */
static int p2gpio_toggle_led( int num, unsigned long val )
{
    struct p2gpio_dev_info *info = &p2gpio_priv.info;
    int i;

    /* check port bits */
    if(unlikely(info->nr_led<1)){
        PERROR("not support LED : port bits of LED = %d\n", info->nr_led);
        return -ENODEV;
    }

    /* toggle LED bit. */
    for(i=0;i<info->nr_led;i++){
        if(val & (1<<i)){
            int retval;
            int tmp=0;
            retval=p2gpio_get_vport(P2GPIO_VPORT_LED_0+i, &tmp);
            if(unlikely(retval<0))
                return retval;
            retval=p2gpio_set_vport(P2GPIO_VPORT_LED_0+i, tmp?0:1);
            if(unlikely(retval<0))
                return retval;
        }
    }

    return 0;
}


/*
 * interrupt handler
 */
static void handler(unsigned int gpio, void *data)
{
    const struct p2gpio_pmap *p
        = (struct p2gpio_pmap *)data;
    struct list_head *entry=NULL;

    /* call-back */
    if(p->fn_callback)
        p->fn_callback(gpio,p);

    if(p->events) {

        /* lock for link list */
        spin_lock( &p2gpio_priv.lock );

        /* remove all entry from link list */
        list_for_each(entry, &p2gpio_priv.head) {
            
            struct p2gpio_dev *dev
                = list_entry(entry, struct p2gpio_dev, entry);

            spin_lock(&dev->lock);
            dev->detect |= p->events;
            spin_unlock(&dev->lock);

            wake_up(&dev->queue);
        }

        /* unlock for link list */
        spin_unlock( &p2gpio_priv.lock );

    }

}


/*
 * ioctl method
 */
static int p2gpio_ioctl( struct inode *inode, struct file *filp,
                         unsigned int cmd, unsigned long arg )
{
    int retval = 0;	/* return value */
    struct p2gpio_dev *dev = (struct p2gpio_dev *)filp->private_data;
    struct p2gpio_dev_info *info = &p2gpio_priv.info;

    /* board-dependent ioctl */
    if(_IOC_TYPE(cmd)!=P2GPIO_IOC_MAGIC_COMMON) {

        if ( unlikely(!info->ops) || !info->ops->ioctl ) {
            PERROR( "Unknown ioctl command!(0x%X)\n", _IOC_NR(cmd));
            retval = -ENOTTY;
        } else {
            retval = info->ops->ioctl( dev, cmd, arg );
        }
        goto failed;
    }

    /*** main routine ***/
    switch ( _IOC_NR(cmd) ) {

        /** get virtual port. **/
    case NR_P2GPIO_IOC_GET_VPORT:
        {
            struct p2gpio_val_s param;
            int val = 0;

            /* get and init values. */
            if (unlikely(
                         copy_from_user((void *)&param,
                                        (void __user *)arg,
                                        sizeof(struct p2gpio_val_s)) )) {
                PERROR( "copy_from_user failed at %s(%d)\n",
                        __FUNCTION__, __LINE__);
                retval = -EFAULT;
                goto failed;
            }

            /* get ports. */
            retval = p2gpio_get_vport( param.num, &val );
            param.val = val?1:0;

            /* put values. */
            if (unlikely(
                         copy_to_user((void __user *)arg,
                                      (void *)&param,
                                      sizeof(struct p2gpio_val_s)) )) {
                PERROR( "copy_to_user failed at %s(%d)\n",
                        __FUNCTION__, __LINE__);
                retval = -EFAULT;
                goto failed;
            }

            PDEBUG( "Get virtual port: port=0x%08X, val=0x%08lX\n",
                    param.num, param.val );

        } /** the end of get vport **/

        break;


        /** set virtual port. **/
    case NR_P2GPIO_IOC_SET_VPORT:
        {
            struct p2gpio_val_s param;

            /* get and init values. */
            if (unlikely(
                         copy_from_user((void *)&param,
                                        (void __user *)arg,
                                        sizeof(struct p2gpio_val_s)) )) {
                PERROR( "copy_from_user failed at %s(%d)\n",
                        __FUNCTION__, __LINE__);
                retval = -EFAULT;
                goto failed;
            }

            /* set ports. */
            retval = p2gpio_set_vport( param.num, param.val?1:0 );

            PDEBUG( "Set virtual port: port=0x%08X, val=0x%lX\n", param.num, param.val );

        } /** the end of set vport **/

        break;


        /** test and lock virtual port. **/
    case NR_P2GPIO_IOC_LOCK_VPORT:
        {
            struct p2gpio_val_s param;
            int val;

            /* get and init values. */
            if (unlikely(
                         copy_from_user((void *)&param,
                                        (void __user *)arg,
                                        sizeof(struct p2gpio_val_s)) )) {
                PERROR( "copy_from_user failed at %s(%d)\n",
                        __FUNCTION__, __LINE__);
                retval = -EFAULT;
                goto failed;
            }

            /* lock ports. */
            retval = p2gpio_lock_vport( param.num, &val);
            if(retval) {
                PERROR( "copy_to_user failed at P2GPIO_IOC_LOCK_VPORT!\n" );
                goto failed;
            }
            param.val = val;

            /* put values. */
            if (unlikely(
                         copy_to_user((void __user *)arg,
                                      (void *)&param,
                                      sizeof(struct p2gpio_val_s)) )) {
                PERROR( "copy_to_user failed at %s(%d)\n",
                        __FUNCTION__, __LINE__);
                retval = -EFAULT;
                goto failed;
            }
            
            PDEBUG( "lock virtual port: port=0x%08X, %s\n", param.num, param.val?"BUSY":"SUCCESS" );

        } /** the end of lock vport **/

        break;


        /** unlock virtual port. **/
    case NR_P2GPIO_IOC_UNLOCK_VPORT:
        {
            struct p2gpio_val_s param;

            /* get and init values. */
            if (unlikely(
                         copy_from_user((void *)&param,
                                        (void __user *)arg,
                                        sizeof(struct p2gpio_val_s)) )) {
                PERROR( "copy_from_user failed at %s(%d)\n",
                        __FUNCTION__, __LINE__);
                retval = -EFAULT;
                goto failed;
            }

            /* unlock ports. */
            retval = p2gpio_unlock_vport( param.num);
            if(unlikely(retval)) {
                PERROR( "copy_to_user failed at P2GPIO_IOC_UNLOCK_VPORT!\n" );
                goto failed;
            }

            PDEBUG( "unlock virtual port: port=0x%08X\n", param.num);

        } /** the end of lock vport **/

        break;


    case NR_P2GPIO_IOC_VPORT_BY_NAME:
        {
            struct p2gpio_val_s param;
            char name[32];
            int len;
            unsigned int vport;

            /* filled '\0' */
            memset(name,0,sizeof(name));

            /* get inpout values from user. */
            if (unlikely(
                         copy_from_user((void *)&param,
                                        (void __user *)arg,
                                        sizeof(struct p2gpio_val_s)) )) {
                PERROR( "copy_from_user failed at P2GPIO_IOC_VPORT_BY_NAME!\n" );
                retval = -EFAULT;
                goto failed;
            }

            /* max size of name */
            len = param.num;
            if(unlikely(len<1)){
                PERROR( "invalid name size = %d\n", len);
                retval = -EINVAL;
                goto failed;
            }
            if(len>(sizeof(name)-1))
                len = sizeof(name)-1;

            /*  */
            if(unlikely(
                        copy_from_user((void*)name,(void*)param.val, len) )) {
                PERROR( "copy_from_user failed at P2GPIO_IOC_VPORT_BY_NAME!\n" );
                retval = -EFAULT;
                goto failed;
            }

            /* get VPORT from name */
            retval = p2gpio_vport_by_name(name, &vport);
            if(unlikely(retval<0)) {
                if(retval != -ENODEV)
                    goto failed;
                retval = 0;
                vport = 0;
            }

            /* put result to user */
            param.num = vport;
            if (unlikely(
                         copy_to_user ( (void __user*)arg,
                                        (void *)&param,
                                        sizeof(struct p2gpio_val_s)) )) {
                PERROR( "copy_to_user failed at P2GPIO_IOC_VPORT_BY_NAME!\n" );
                retval = -EFAULT;
                goto failed;
            }

        }

        break;


    case NR_P2GPIO_IOC_GET_VPORT_NAME:
        {
            struct p2gpio_val_s param;
            char *name = NULL;
            int len;
            int idx = _IOC_SIZE(cmd);
            const struct p2gpio_pmap *entry;

            /* check */
            if(unlikely(idx<0)){
                PERROR( "invalid index = %d\n", idx);
                retval = -EINVAL; 
                goto failed;
            }
            if(unlikely(idx>=info->nr_pmap)){
                retval = 0;
                break;
            }
            entry = &info->pmap[idx];

            /* get inpout values from user. */
            if (unlikely(
                         copy_from_user((void *)&param,
                                        (void __user *)arg,
                                        sizeof(struct p2gpio_val_s)) )) {
                PERROR( "copy_from_user failed at P2GPIO_IOC_GET_VPORT_NAME!\n" );
                retval = -EFAULT;
                goto failed;
            }

            /* max copy size */
            len = param.num;
            if(unlikely(len<1)){
                PERROR( "invalid name size = %d\n", len);
                retval = -EINVAL;
                goto failed;
            }

            /* allocate name buffer */
            name = (char*)kzalloc(len,GFP_KERNEL);
            if(unlikely(NULL==name)) {
                PERROR("NO memory\n");
                retval = -ENOMEM;
                goto failed;
            }

            /* get name */
            strncpy(name, entry->name, len-1);

            /* copy name to user space  */
            if(unlikely(
                        copy_to_user((void __user *)param.val, (void*)name, len) )) {
                PERROR( "copy_to_user failed at P2GPIO_IOC_GET_VPORT_NAME!\n" );
                retval = -EFAULT;
                kfree(name);
                goto failed;
            }

            /* free name buffer */
            kfree(name);

            /* next index */
            retval = idx+1;

        }

        break;


        /** get DIPSW. **/
    case NR_P2GPIO_IOC_GET_DIPSW:
        {
            struct p2gpio_val_s param;

            /* get and init values. */
            if ( copy_from_user((void *)&param,
                                (void __user *)arg,
                                sizeof(struct p2gpio_val_s)) ) {
                PERROR( "copy_from_user failed at %s(%d)\n",
                        __FUNCTION__, __LINE__);
                retval = -EFAULT;
                goto failed;
            }
            param.val = 0;

            /* get DIPSW. */
            retval = p2gpio_get_dipsw( param.num, &(param.val) );

            /* put values. */
            if ( copy_to_user((void __user *)arg,
                              (void *)&param,
                              sizeof(struct p2gpio_val_s)) ) {
                PERROR( "copy_to_user failed at %s(%d)\n",
                        __FUNCTION__, __LINE__);
                retval = -EFAULT;
                goto failed;
            }

            PDEBUG( "Get DIPSW num=%d, val=0x%lX\n", param.num, param.val );

        }

        break;

    
        /** get ROTSW. **/
    case NR_P2GPIO_IOC_GET_ROTSW:
        {
            struct p2gpio_val_s param;

            /* get and init values. */
            if ( copy_from_user((void *)&param,
                                (void __user *)arg,
                                sizeof(struct p2gpio_val_s)) ) {
                PERROR( "copy_from_user failed at %s(%d)\n",
                        __FUNCTION__, __LINE__);
                retval = -EFAULT;
                goto failed;
            }
            param.val = 0;

            /* get ROTSW. */
            retval = p2gpio_get_rotsw( param.num, &(param.val) );

            /* put values. */
            if ( copy_to_user((void __user *)arg,
                              (void *)&param,
                              sizeof(struct p2gpio_val_s)) ) {
                PERROR( "copy_to_user failed at %s(%d)\n",
                        __FUNCTION__, __LINE__);
                retval = -EFAULT;
                goto failed;
            }

            PDEBUG( "Get ROTSW num=%d, val=0x%lX\n", param.num, param.val );

        }

        break;

    
        /** get LED. **/
    case NR_P2GPIO_IOC_GET_LED:
        {
            struct p2gpio_val_s param;

            /* get and init values. */
            if ( copy_from_user((void *)&param,
                                (void __user *)arg,
                                sizeof(struct p2gpio_val_s)) ) {
                PERROR( "copy_from_user failed at %s(%d)\n",
                        __FUNCTION__, __LINE__);
                retval = -EFAULT;
                goto failed;
            }
            param.val = 0;

            /* get LED. */
            retval = p2gpio_get_led( param.num, &(param.val) );
      
            /* put values. */
            if ( copy_to_user((void __user *)arg,
                              (void *)&param,
                              sizeof(struct p2gpio_val_s)) ) {
                PERROR( "copy_to_user failed at %s(%d)\n",
                        __FUNCTION__, __LINE__);
                retval = -EFAULT;
                goto failed;
            }

            PDEBUG( "Get LED num=%d, val=0x%lX\n", param.num, param.val );

        } /** the end of get LED **/

        break;


        /** set LED. **/
    case NR_P2GPIO_IOC_SET_LED:
        {
            struct p2gpio_val_s param;

            /* get and init values. */
            if ( copy_from_user((void *)&param,
                                (void __user *)arg,
                                sizeof(struct p2gpio_val_s)) ) {
                PERROR( "copy_from_user failed at %s(%d)\n",
                        __FUNCTION__, __LINE__);
                retval = -EFAULT;
                goto failed;
            }

            /* set LED. */
            retval = p2gpio_set_led( param.num, param.val );
            if (cmd == P2IOPORT_IOC_SET_LED)
                retval = p2gpio_clr_led( param.num, ~param.val );

            PDEBUG( "Set LED num=%d, val=0x%lX\n", param.num, param.val );

        } /** the end of set LED **/

        break;


        /** clear LED. **/
    case NR_P2GPIO_IOC_CLR_LED:
        {
            struct p2gpio_val_s param;

            if (cmd==P2IOPORT_IOC_CLR_LED) {

                /* get and init values. */
                if ( copy_from_user((void *)&param.num,
                                    (void __user *)arg,
                                    sizeof(int)) ) {
                    PERROR( "copy_from_user failed at %s(%d)\n",
                            __FUNCTION__, __LINE__);
                    retval = -EFAULT;
                    goto failed;
                }
                param.val = (1<<info->nr_led)-1;

            } else {

                /* get and init values. */
                if ( copy_from_user((void *)&param,
                                    (void __user *)arg,
                                    sizeof(struct p2gpio_val_s)) ) {
                    PERROR( "copy_from_user failed at %s(%d)\n",
                            __FUNCTION__, __LINE__);
                    retval = -EFAULT;
                    goto failed;
                }

            }

            /* clear LED. */
            retval = p2gpio_clr_led( param.num, param.val );

            PDEBUG( "Clear LED num=%d, val=0x%lX\n", param.num, param.val );

        } /** the end of clear LED **/

        break;


        /** toggle LED. **/
    case NR_P2GPIO_IOC_TOGGLE_LED:
        {
            struct p2gpio_val_s param;

            /* get and init values. */
            if ( copy_from_user((void *)&param,
                                (void __user *)arg,
                                sizeof(struct p2gpio_val_s)) ) {
                PERROR( "copy_from_user failed at %s(%d)\n",
                        __FUNCTION__, __LINE__);
                retval = -EFAULT;
                goto failed;
            }

            /* toggle LED. */
            retval = p2gpio_toggle_led( param.num, param.val );

            PDEBUG( "Toggle LED num=%d, val=0x%lX\n", param.num, param.val );

        } /** the end of toggle LED **/

        break;


        /** special commands **/
    default:
        PERROR( "Unknown ioctl command!(0x%08X)\n", cmd);
        retval = -ENOTTY;

    } /* the end of switch */

 failed:
    return (retval);

}


/*
 * poll method
 */
static unsigned int p2gpio_poll(struct file *filp, struct poll_table_struct *wait)
{
    struct p2gpio_dev *dev = (struct p2gpio_dev *)filp->private_data;
    unsigned long flags=0;
    unsigned int mask=0;
    int n;

    poll_wait(filp, &dev->queue, wait);

    spin_lock_irqsave(&dev->lock,flags);

    for(n=0; n< P2GPIO_PMAP_MAX_EVENTS; n++){
        short event = 1<<n;
        if(dev->detect & event){
            mask |= event;
            dev->detect &= ~event;
        }
    }

    spin_unlock_irqrestore(&dev->lock,flags);

    return mask;
}


/*
 * open method
 */
static int p2gpio_open( struct inode *inode, struct file *filp )
{
    int retval = 0;
    struct p2gpio_dev *dev = NULL;
    unsigned long flags;

    PDEBUG( "Open(%d:%d)\n", MAJOR(inode->i_rdev), MINOR(inode->i_rdev) );
    
    /* allocate private device data */
    dev = (struct p2gpio_dev *)kzalloc(sizeof(struct p2gpio_dev), GFP_KERNEL);
    if(NULL==dev) {
        PERROR("Can't allocate memory\n");
        retval = -ENOMEM;
        goto failed;
    }

    /* spinlock */
    spin_lock_init(&dev->lock);

    /* wait queue head */
    init_waitqueue_head(&dev->queue);

    /* entry of link list */
    INIT_LIST_HEAD(&dev->entry);

    /* lock for link list */
    spin_lock_irqsave( &p2gpio_priv.lock, flags);

    /* add to link list */
    list_add_tail(&dev->entry, &p2gpio_priv.head);

    /* unlock for link list */
    spin_unlock_irqrestore( &p2gpio_priv.lock, flags);

    /* set to private data */
    filp->private_data = (void*)dev;

 failed:
    if(retval<0) {

        if(NULL!=dev) {

            /* lock for link list */
            spin_lock_irqsave( &p2gpio_priv.lock, flags );

            /* remove from link list */
            list_del(&dev->entry);

            /* unlock for link list */
            spin_unlock_irqrestore( &p2gpio_priv.lock, flags );

            /* free private device data */
            kfree(dev);
        }

        /* clear to private data */
        filp->private_data = NULL;

    }

    return retval;
}


/*
 * release method
 */
static int p2gpio_release( struct inode *inode, struct file *filp )
{
    int retval = 0;
    unsigned long flags;
    struct p2gpio_dev *dev = (struct p2gpio_dev *)filp->private_data;

    PDEBUG( "Release(%d:%d)\n", MAJOR(inode->i_rdev), MINOR(inode->i_rdev) );

    /* check */
    if(NULL==dev){
        PERROR("NOT found private device data\n");
        retval = -EINVAL;
        goto failed;
    }

    /* lock for link list */
    spin_lock_irqsave( &p2gpio_priv.lock, flags );

    /* remove from link list */
    list_del(&dev->entry);

    /* unlock for link list */
    spin_unlock_irqrestore( &p2gpio_priv.lock, flags );

    /* free private device data */
    kfree(dev);

     /* clear to private data */
    filp->private_data = NULL;

 failed:
    return retval;
}


/*
 * file operations
 */
static struct file_operations p2gpio_fops = {
    .owner    = THIS_MODULE,
    .open     = p2gpio_open,
    .release  = p2gpio_release,
    .ioctl    = p2gpio_ioctl,
    .poll     = p2gpio_poll,
  /* nothing more, fill with NULLs */
};


/***************************************************************************
 * p2gpio_init
 **************************************************************************/
int __init p2gpio_init( void )
{
    int	retval = 0; /* return value */
    const struct p2gpio_pmap *p=NULL;
    int i;

    /* print init Message. */
    PINFO( "P2GPIO driver ver. %s\n", P2GPIO_DRV_VERSION );

    /* set parameters. */
    memset( &p2gpio_priv, 0, sizeof(p2gpio_priv) );

    /* spinlock */
    spin_lock_init(&p2gpio_priv.lock);

    /* link list head */
    INIT_LIST_HEAD(&p2gpio_priv.head);

    /* initialize and get device informations */
    if ( __p2gpio_init_info(&p2gpio_priv.info) ) {
        PERROR( "Init p2gpio info is failed!!\n" );
        retval = -ENODEV;
        goto err;
    }
    PDEBUG( "%s dipsw=%d rotsw=%d led=%d\n",
            p2gpio_priv.info.name,
            p2gpio_priv.info.nr_dipsw,
            p2gpio_priv.info.nr_rotsw,
            p2gpio_priv.info.nr_led);

    /* setting all ports */
    for(i=0, p=p2gpio_priv.info.pmap; i<p2gpio_priv.info.nr_pmap; i++, p++){

        if(!gpio_is_valid_port(p->gpio)){
            PERROR("invalid gpio port = %s(0x%08X,%d)\n",
                   p->name, p->vport, p->gpio);
            retval = -ENODEV;
            goto err;
        }

        if(!(p->flag&P2GPIO_PMAP_INTERRUPT))
            continue;

        if(!gpio_is_valid_irq(p->gpio)){
            PERROR("invalid irq gpio port = %s(0x%08X,%d)\n",
                   p->name, p->vport, p->gpio);
            retval = -ENODEV;
            goto err;
        }

        retval=gpio_request_irq(p->gpio, handler, (void*)p);
        if(retval<0){
            PERROR("can't register IRQ for gpio port = %s(0x%08X,%d), retval = %d\n",
                   p->name, p->vport, p->gpio, retval);
            goto err;
        }

    }


    /* allocate device number */
#ifdef CONFIG_P2GPIO_FIXED_DEVNUM
    p2gpio_priv.devno = MKDEV(P2GPIO_MAJOR, P2GPIO_MINOR);
    retval = register_chrdev_region(p2gpio_priv.devno, 1, P2GPIO_DEVNAME);
#else  /* !CONFIG_P2GPIO_FIXED_DEVNUM */
    retval = alloc_chrdev_region(&p2gpio_priv.devno, 0, 1, P2GPIO_DEVNAME);
#endif  /* CONFIG_P2GPIO_FIXED_DEVNUM */
    if(retval<0 ){
        PERROR("can't allocate device number: retval=%d\n", retval);
        p2gpio_priv.devno=0;
        goto err;
    }
    PDEBUG("allocate device number = (%d:%d)\n", MAJOR(p2gpio_priv.devno), MINOR(p2gpio_priv.devno));

    /* init cdev structure. */
    p2gpio_priv.cdev = cdev_alloc();
    if(NULL==p2gpio_priv.cdev){
        PERROR("can't allocate cdev\n");
        retval = -ENOMEM;
        goto err;
    }
    p2gpio_priv.cdev->ops = &p2gpio_fops;
    p2gpio_priv.cdev->owner = THIS_MODULE;
    retval = cdev_add( p2gpio_priv.cdev, p2gpio_priv.devno, 1 );
    if ( unlikely(retval < 0) ) {
        PERROR( "cdev_add failed(%d)!\n", retval );
        goto err;
    }

 err:
    if(retval<0) {
        if(p2gpio_priv.cdev)
            cdev_del(p2gpio_priv.cdev);
        if(p2gpio_priv.devno)
            unregister_chrdev_region( p2gpio_priv.devno, 1 );
    }

    return retval;
}


/***************************************************************************
 * p2gpio_cleanup
 **************************************************************************/
static void __exit p2gpio_cleanup( void )
{
    int i;
    const struct p2gpio_pmap *p=NULL;
    struct list_head *entry=NULL, *next=NULL;
    unsigned long flags;

    PINFO( "Clean up P2PF I/O port driver\n" ); /* Message */

    /* unregister the character device driver. */
    if(p2gpio_priv.cdev)
        cdev_del( p2gpio_priv.cdev );
    if(p2gpio_priv.devno)
        unregister_chrdev_region( p2gpio_priv.devno, 1 );

    /* cleanup all ports */
    for(i=0, p=p2gpio_priv.info.pmap; i<p2gpio_priv.info.nr_pmap; i++, p++){

        if(!p->events)
            continue;

        /* unregister interrupt handlers */
        gpio_disable_irq(p->gpio);
        gpio_free_irq(p->gpio);

    }

    /* lock for link list */
    spin_lock_irqsave( &p2gpio_priv.lock, flags );

    /* remove all entry from link list */
    list_for_each_safe(entry, next, &p2gpio_priv.head) {
        list_del(entry);
    }

    /* unlock for link list */
    spin_unlock_irqrestore( &p2gpio_priv.lock, flags );

    /* cleanup parameters. */
    __p2gpio_cleanup_info( &p2gpio_priv.info );
}

#if defined(MODULE)
module_init(p2gpio_init);
module_exit(p2gpio_cleanup);
#else /* ! MODULE */
postcore_initcall(p2gpio_init);
#endif /* MODULE */


/** Export symbols **/
EXPORT_SYMBOL(p2gpio_get_dipsw);
EXPORT_SYMBOL(p2gpio_get_rotsw);
EXPORT_SYMBOL(p2gpio_get_led);
EXPORT_SYMBOL(p2gpio_set_led);
EXPORT_SYMBOL(p2gpio_clr_led);
EXPORT_SYMBOL(p2gpio_toggle_led);
EXPORT_SYMBOL(p2gpio_get_vport);
EXPORT_SYMBOL(p2gpio_set_vport);

