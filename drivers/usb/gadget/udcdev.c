/* -*- C -*-
 * udcdev.c
 *
 * Copyright (C) 2011 Panasonic Co.,LTD.
 *
 * $Id: udcdev.c 14816 2011-06-08 01:02:14Z Noguchi Isao $
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>           /* everything... */
#include <linux/cdev.h>         /* struct cdev */
#include <linux/errno.h>        /* error codes */
#include <linux/types.h>        /* size_t */
#include <linux/ioctl.h>        /* ioctl */
#include <linux/fcntl.h>        /* O_ACCMODE */
#include <linux/poll.h>

#ifdef CONFIG_USB_GADGET_UDCDEV_PROC
#include <linux/proc_fs.h>      /* /proc file system */
#endif  /* CONFIG_USB_GADGET_UDCDEV_PROC */

#include <linux/sched.h>
#include <linux/semaphore.h>      /* semaphore */
#include <asm/atomic.h>         /* atomic_t */
#include <asm/uaccess.h>        /* copy_from_user/copy_to_user */

#include <linux/device.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>

#include <linux/usb/udcdev_user.h>
#include "udcdev.h"


/*******************************************************************************
 **  DEBUG MACROs
 ******************************************************************************/

#define MODULE_NAME "udcdev"

#define _MSG(level, fmt, args...) \
    printk( level  "[%s] " fmt, MODULE_NAME, ## args )

/* #define _MSG_LOCATION(level, fmt, args...) \ */
/*     printk( level  "[%s] %s(%d): " fmt, MODULE_NAME, __FILE__, __LINE__, ## args ) */
#define _MSG_LOCATION _MSG

#if defined(UDCDEV_DEBUG)
#   define _DEBUG(fmt, args...) _MSG(KERN_DEBUG,"DEBUG: " fmt, ## args)
#else  /* ! defined(UDCDEV_DEBUG)  */
#   define _DEBUG(fmt, args...)
#endif /* defined(UDCDEV_DEBUG) */

#define _INFO(fmt, args...)  _MSG(KERN_INFO, "INFO: "  fmt, ## args)

#define _WARN(fmt, args...)  _MSG_LOCATION(KERN_WARNING, "WARNING: " fmt, ## args)

#define _ERR(fmt, args...)   _MSG_LOCATION(KERN_ERR,  "ERROR: " fmt, ## args )

#define _CRIT(fmt, args...)  _MSG_LOCATION(KERN_CRIT, "CRIT: " fmt, ## args )

/*******************************************************************************
 **  internal static variables
 ******************************************************************************/

static struct {

    dev_t           devno;
    struct cdev     *cdev;
    
    /* semaphore */
    struct semaphore sema;

    spinlock_t      lock;
    struct list_head head;
    struct usb_gadget *gadget;

} udcdev_priv;

/* structure for opend devices */
struct udcdev {
    struct list_head entry;
    spinlock_t      lock;
    wait_queue_head_t   queue;
    int         event_connect;
};


#ifdef CONFIG_USB_GADGET_UDCDEV_PROC
/* proc entry */
static struct proc_dir_entry *proc_entry=NULL;
#define proc_udcdev_name "driver/udcdev"
#endif  /* CONFIG_USB_GADGET_UDCDEV_PROC */


/*******************************************************************************
 ** functions for  /proc entry
 ******************************************************************************/

#ifdef CONFIG_USB_GADGET_UDCDEV_PROC

/*
 * /proc function
 */
static int procfunc_read(char *buff, char **start, off_t offset, int count, int *eof, void *data)
{
    int len=0;
    struct usb_gadget *gadget = udcdev_priv.gadget;

    /* semaphore down */
    if(down_interruptible(&udcdev_priv.sema))
        return len;

    /* connection */
    if(udcdev_priv.gadget->ops && udcdev_priv.gadget->ops->ioctl) {
        int connection = gadget->ops->check_connection(gadget);
        len += sprintf(buff,"connection: %s\n", connection?"ATTACHED":"DETACHED");
    }

    /* finished */
// exit:
    /* semaphore up */
    up(&udcdev_priv.sema);

    *eof=1;
    return len;
}

/*
 * create proc entry
 */
static int create_proc(void)
{
    int ret=0;

    if(!(proc_entry
         =create_proc_read_entry( proc_udcdev_name, 0, NULL, procfunc_read, NULL))){
        _ERR("Can't create \"/proc/" proc_udcdev_name "\".\n");
        ret = -EIO;
        goto fail;
    }
    
    /* SUCCESS */
    return 0;

 fail:

    if(proc_entry){
        remove_proc_entry(proc_udcdev_name, NULL);
        proc_entry=NULL;
    }

    return ret;
}

/*
 * remove proc entry
 */
static void remove_proc(void)
{
    /* Remove /proc entry */
    _DEBUG( "Remove \"/proc/" proc_udcdev_name "\".\n");
    if(proc_entry){
        remove_proc_entry(proc_udcdev_name, NULL);
        proc_entry=NULL;
    }
}

#endif  /* CONFIG_USB_GADGET_UDCDEV_PROC */

/*******************************************************************************
 **  file_operations function & structure
 ******************************************************************************/

/* open method */
static int open_method(struct inode *inode, struct file *filp)
{
    int retval=0;
    struct udcdev *dev = NULL;
    unsigned long flags;

    _DEBUG("proccess %i (%s) going to open the device (%d:%d)\n",
           current->pid, current->comm, imajor(inode), iminor(inode));

    /* allocate private device data */
    dev = (struct udcdev *)kzalloc(sizeof(struct udcdev), GFP_KERNEL);
    if(NULL==dev) {
        _ERR("Can't allocate memory\n");
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
    spin_lock_irqsave( &udcdev_priv.lock, flags);

    /* add to link list */
    list_add_tail(&dev->entry, &udcdev_priv.head);

    /* unlock for link list */
    spin_unlock_irqrestore( &udcdev_priv.lock, flags);

    /* set to private data */
    filp->private_data = (void*)dev;

 failed:
    if(retval<0) {

        if(NULL!=dev) {

            /* lock for link list */
            spin_lock_irqsave( &udcdev_priv.lock, flags );

            /* remove from link list */
            list_del(&dev->entry);

            /* unlock for link list */
            spin_unlock_irqrestore( &udcdev_priv.lock, flags );

            /* free private device data */
            kfree(dev);
        }

        /* clear to private data */
        filp->private_data = NULL;

    }

    return retval;
}

/* release method */
static int release_method(struct inode *inode, struct file *filp)
{
    int retval=0;
    struct udcdev *dev = (struct udcdev *)filp->private_data;
    unsigned long flags;

    _DEBUG("proccess %i (%s) going to close the device (%d:%d)\n",
           current->pid, current->comm, imajor(inode), iminor(inode));

    /* check */
    if(NULL==dev){
        _ERR("NOT found private device data\n");
        retval = -EINVAL;
        goto failed;
    }

    /* lock for link list */
    spin_lock_irqsave( &udcdev_priv.lock, flags );

    /* remove from link list */
    list_del(&dev->entry);

    /* unlock for link list */
    spin_unlock_irqrestore( &udcdev_priv.lock, flags );

    /* free private device data */
    kfree(dev);

     /* clear to private data */
    filp->private_data = NULL;

 failed:
    return retval;
}

/* ioctl method */
static int ioctl_method(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg)
{
    int retval=0;

    _DEBUG("proccess %i (%s) going to ioctl the device (%d:%d)\n",
           current->pid, current->comm, imajor(inode), iminor(inode));

    /* semaphore down */
    if(down_interruptible(&udcdev_priv.sema))
        return -ERESTARTSYS;

    /* main proc */
    switch(cmd){

    default:
        if(udcdev_priv.gadget->ops && udcdev_priv.gadget->ops->ioctl) 
            retval = udcdev_priv.gadget->ops->ioctl(udcdev_priv.gadget, (unsigned)cmd, arg);
        else
            retval = -ENOTTY;
    }

    /* finish */
// exit:
    up(&udcdev_priv.sema);
    return retval;
}

/*
 * poll method
 */
static unsigned int poll_method(struct file *filp, struct poll_table_struct *wait)
{
    struct udcdev *dev = (struct udcdev *)filp->private_data;
    unsigned int mask=0;
    unsigned long flags=0;

    poll_wait(filp, &dev->queue, wait);

    spin_lock_irqsave(&dev->lock,flags);
    if (dev->event_connect) {
        mask |= POLLPRI;
        dev->event_connect=0;
    }
    spin_unlock_irqrestore(&dev->lock,flags);

    return mask;
}


/* fops */
static struct file_operations udcdev_fops = {
    .owner  = THIS_MODULE,
    .open   = open_method,
    .release = release_method,
    .ioctl  = ioctl_method,
    .poll   = poll_method,
};


/*******************************************************************************
 **  udcdev methods
 ******************************************************************************/

void udcdev_change_connection(void)
{
    struct list_head *entry=NULL;
    unsigned long   flags;

    /* lock for link list */
    spin_lock_irqsave( &udcdev_priv.lock, flags );

    /* remove all entry from link list */
    list_for_each(entry, &udcdev_priv.head) {
        unsigned long   f;
        struct udcdev *dev
            = list_entry(entry, struct udcdev, entry);

        spin_lock_irqsave(&dev->lock,f);
        dev->event_connect = 1;
        spin_unlock_irqrestore(&dev->lock,f);

        wake_up(&dev->queue);
    }

    /* unlock for link list */
    spin_unlock_irqrestore( &udcdev_priv.lock, flags);
}



/*******************************************************************************
 **  initialize/clean-up
 ******************************************************************************/

/*
 * @brief   Initialize
 */
int __init udcdev_init (struct usb_gadget *g)
{

    int retval=0;

   _INFO("USB-DEVICE Controller device (UDCDEV) driver\n");

   /* spinlock */
   spin_lock_init(&udcdev_priv.lock);

   /* semaphore */
   sema_init(&udcdev_priv.sema,1);

   /* list */
   INIT_LIST_HEAD(&udcdev_priv.head);

   if (!g) {
       retval=-EINVAL;
       goto fail;
   }
   udcdev_priv.gadget = g;

#ifdef CONFIG_USB_GADGET_UDCDEV_PROC
    /* create /proc entry */
    if((retval=create_proc())<0)
        goto fail;
#endif  /* CONFIG_USB_GADGET_UDCDEV_PROC */

    /* allocate device number */
#ifdef CONFIG_USB_GADGET_UDCDEV_FIXED_DEVNUM
    udcdev_priv.devno = MKDEV(CONFIG_USB_GADGET_UDCDEV_FIXED_MAJOR,CONFIG_USB_GADGET_UDCDEV_FIXED_MINOR);
    retval = register_chrdev_region(udcdev_priv.devno, 1, MODULE_NAME);
#else  /* !CONFIG_USB_GADGET_UDCDEV_FIXED_DEVNUM */
    retval = alloc_chrdev_region(&devno, 0, 1,MODULE_NAME);
#endif  /* CONFIG_USB_GADGET_UDCDEV_FIXED_DEVNUM */
    if(retval<0){
        _ERR("can't allocate device number: retval=%d\n", retval);
        udcdev_priv.devno=0;
        goto fail;
    }

    /* initialize character device */
    udcdev_priv.cdev = cdev_alloc();
    if(NULL==udcdev_priv.cdev){
        _ERR("can't allocate cdev\n");
        retval = -ENOMEM;
        goto fail;
    }
    udcdev_priv.cdev->ops = &udcdev_fops;
    udcdev_priv.cdev->owner = THIS_MODULE;
    retval = cdev_add(udcdev_priv.cdev,udcdev_priv.devno,1);
    if(retval<0){
        _ERR("failed to add cdev: retval=%d\n",retval);
        goto fail;
    }

     /* initialize */



 fail:

    if(retval<0){

        /* remove cdev */
        if(udcdev_priv.cdev)
            cdev_del(udcdev_priv.cdev);

        /* release device number */
        if(udcdev_priv.devno)
            unregister_chrdev_region(udcdev_priv.devno,1);

#ifdef CONFIG_USB_GADGET_UDCDEV_PROC
        /* Remove proc entry */
        remove_proc();
#endif  /* CONFIG_USB_GADGET_UDCDEV_PROC */

        /*  */
        udcdev_priv.gadget=NULL;

    }

    return retval;
}

/*
 * @brief   Clean-up
 */
void __exit udcdev_cleanup (void)
{
    /* clean-up lower level driver */


    /* remove cdev */
    if(udcdev_priv.cdev)
        cdev_del(udcdev_priv.cdev);

    /* release device number */
    if(udcdev_priv.devno)
        unregister_chrdev_region(udcdev_priv.devno,1);

#ifdef CONFIG_USB_GADGET_UDCDEV_PROC
    /* Remove proc entry */
    remove_proc();
#endif  /* CONFIG_USB_GADGET_UDCDEV_PROC */

        /*  */
        udcdev_priv.gadget=NULL;

    _INFO("cleanup USB-DEVICE Controller device (UDCDEV) driver\n");

}
