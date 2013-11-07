/* -*- C -*-
 * led-core.c --- LED control driver core
 *
 * Copyright (C) 2010 Panasonic Co.,LTD.
 *
 * $Id: led-core.c 5704 2010-03-15 01:18:50Z Noguchi Isao $
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
#ifdef CONFIG_MSUDEV_LED_PROC
#include <linux/proc_fs.h>      /* /proc file system */
#endif  /* CONFIG_MSUDEV_LED_PROC */
#include <linux/sched.h>
#include <linux/semaphore.h>      /* semaphore */
#include <asm/atomic.h>         /* atomic_t */
#include <asm/uaccess.h>        /* copy_from_user/copy_to_user */

#include <linux/p2msudev_user.h>
#include "led.h"

#define MODULE_NAME "msudev-led-core"
#include "../debug.h"

/*******************************************************************************
 **  internal static variables
 ******************************************************************************/

/* device number */
static dev_t   devno=0;

/* character device */
static struct cdev *cdev=NULL;

/* count of open */
static atomic_t open_count;

/* /\* Buffer of register*\/ */
/* static volatile union bf_fpga_led buff_reg; */

/* semaphore */
static struct semaphore sema;

#ifdef CONFIG_MSUDEV_LED_PROC
/* proc entry */
static struct proc_dir_entry *proc_entry=NULL;
#define proc_led_name "driver/p2msudev-led"
#endif  /* CONFIG_MSUDEV_LED_PROC */

/* file operations */
static struct msudev_led_ops *led_ops=NULL;


/*******************************************************************************
 ** functions for  /proc entry
 ******************************************************************************/

#ifdef CONFIG_MSUDEV_LED_PROC

/*
 * /proc function
 */
static int procfunc_read(char *buff, char **start, off_t offset, int count, int *eof, void *data)
{
    int len=0;
    int no;
    struct p2msudev_ioc_led_ctrl ctrl;
    unsigned int nr_led;

    /* semaphore down */
    if(down_interruptible(&sema))
        return len;

    /* local proc func */
    if(led_ops->procfunc_read){
        len = led_ops->procfunc_read(led_ops, buff, start, offset, count, eof, data);
        goto exit;
    }

    if(!led_ops->get_led){
        _WARN("can't get LED setting\n");
        goto exit;
    }

    /* main loop */
    nr_led = led_ops->nr_led?led_ops->nr_led(led_ops):0;
    len += sprintf(buff+len, "LED number = %d\n", nr_led);
    for(no=0; no<nr_led; no++){
        int retval;
        ctrl.no=no;
        retval = led_ops->get_led(led_ops, &ctrl);
        if(retval<0){
            _ERR("get_led_entry() is failed\n");
            goto exit;
        }
        len += sprintf(buff+len, "LED[%d] : bright=%d, timing=%d\n",
                       no, ctrl.bright, ctrl.timing);
    }

    /* finished */
 exit:
    /* semaphore up */
    up(&sema);

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
         =create_proc_read_entry( proc_led_name, 0, NULL, procfunc_read, NULL))){
        _ERR("Can't create \"/proc/" proc_led_name "\".\n");
        ret = -EIO;
        goto fail;
    }
    
    /* SUCCESS */
    return 0;

 fail:

    if(proc_entry){
        remove_proc_entry(proc_led_name, NULL);
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
    _DEBUG( "Remove \"/proc/" proc_led_name "\".\n");
    if(proc_entry){
        remove_proc_entry(proc_led_name, NULL);
        proc_entry=NULL;
    }
}

#endif  /* CONFIG_MSUDEV_LED_PROC */

/*******************************************************************************
 **  file_operations function & structure
 ******************************************************************************/

/* open method */
static int open_method(struct inode *inode, struct file *filep)
{
    _DEBUG("proccess %i (%s) going to open the device (%d:%d)\n",
           current->pid, current->comm, imajor(inode), iminor(inode));
    atomic_inc(&open_count);
    return 0;
}

/* release method */
static int release_method(struct inode *inode, struct file *filep)
{
    _DEBUG("proccess %i (%s) going to close the device (%d:%d)\n",
           current->pid, current->comm, imajor(inode), iminor(inode));
    atomic_dec(&open_count);
    return 0;
}

/* ioctl method */
static int ioctl_method(struct inode *inode, struct file *filep, unsigned int cmd, unsigned long arg)
{
    int retval=0;
    unsigned int nr, dir, size;
    unsigned int n, nr_buff;
    struct p2msudev_ioc_led_ctrl * ctrlbuff = NULL;

    _DEBUG("proccess %i (%s) going to ioctl the device (%d:%d)\n",
           current->pid, current->comm, imajor(inode), iminor(inode));

    /* field of IOC command */
    nr      = _IOC_NR(cmd);
    dir     = _IOC_DIR(cmd);
    size    = _IOC_SIZE(cmd);
    _DEBUG("nr=0x%02X, dir=%s, size=%d\n",nr,ioc_dir_msg(dir),size);
    nr_buff = size/sizeof(struct p2msudev_ioc_led_ctrl);

    /* semaphore down */
    if(down_interruptible(&sema))
        return -ERESTARTSYS;

    /* main proc */
    switch(nr){

    case NR_P2MSUDEV_IOC_LED_SETVAL:

        if((dir&(_IOC_READ|_IOC_WRITE))!=_IOC_WRITE){
            _ERR("Invalid direction: %d\n", dir);
            retval = -ENOTTY;
            goto exit;
        }

        if(nr_buff<1||nr_buff>P2MSUDEV_NR_LED_PARAM){
            retval = -EINVAL;
            goto exit;
        }

        if(!capable(CAP_SYS_RAWIO)){
            retval = -EPERM;
            goto exit;
        }

        if(!led_ops->set_led)
            goto exit;

        ctrlbuff = (struct p2msudev_ioc_led_ctrl *)kmalloc(size, GFP_KERNEL);
        if(NULL==ctrlbuff){
            retval = -ENOMEM;
            goto exit;
        }

        /* copy from user space */
        if(!arg || copy_from_user(ctrlbuff, (void __user *)arg, size)){
            _ERR("PAGE fault\n");
            kfree(ctrlbuff);
            retval = -EFAULT;
            goto exit;
        }

        /* set value to buffer */
        for(n=0; n<nr_buff; n++){
            retval = led_ops->set_led(led_ops, &ctrlbuff[n]);
            if(retval<0){
                kfree(ctrlbuff);
                goto exit;
            }
        }

        kfree(ctrlbuff);

        break;


    case NR_P2MSUDEV_IOC_LED_GETVAL:

        if((dir&(_IOC_READ|_IOC_WRITE))!=(_IOC_READ|_IOC_WRITE)){
            _ERR("Invalid direction: %d\n", dir);
            retval = -ENOTTY;
            goto exit;
        }

        if(nr_buff<1||nr_buff>P2MSUDEV_NR_LED_PARAM){
            retval = -EINVAL;
            goto exit;
        }

        if(!capable(CAP_SYS_RAWIO)){
            retval = -EPERM;
            goto exit;
        }

        if(!led_ops->get_led){
            _ERR("can't get LED setting\n");
            retval = -ENODEV;
            goto exit;
        }

        ctrlbuff = (struct p2msudev_ioc_led_ctrl *)kmalloc(size, GFP_KERNEL);
        if(NULL==ctrlbuff){
            retval = -ENOMEM;
            goto exit;
        }

        /* copy from user space */
        if(!arg || copy_from_user(ctrlbuff, (void __user *)arg, size)){
            _ERR("PAGE fault\n");
            kfree(ctrlbuff);
            retval = -EFAULT;
            goto exit;
        }

        /* get value from buffer */
        for(n=0; n<nr_buff; n++){
            retval = led_ops->get_led(led_ops, &ctrlbuff[n]);
            if(retval<0){
                kfree(ctrlbuff);
                goto exit;
            }
        }

        /* copy to user space */
        if(copy_to_user((void*)arg, (void*)ctrlbuff, size)){
            _ERR("PAGE fault\n");
             retval = -EFAULT;
             kfree(ctrlbuff);
             goto exit;
        }

        kfree(ctrlbuff);

        break;

    default:

        if(led_ops->ioctl)
            retval = led_ops->ioctl(led_ops,cmd,arg);
        else
            retval = -ENOTTY;

    }

    /* finish */
 exit:
    up(&sema);
    return retval;
}

/* fops */
static struct file_operations msudev_led_fops = {
    owner:      THIS_MODULE,
    open:       open_method,
    release:    release_method,
    ioctl:      ioctl_method,
};


/*******************************************************************************
 **  initialize/clean-up
 ******************************************************************************/

/*
 * @brief   Initialize
 */
static int __init msudev_led_init (void)
{

    int retval=0;

   _INFO("LED control driver for P2MSU\n");

    /* semaphore */
    sema_init(&sema,1);

    /* set the open count to 0*/
    atomic_set(&open_count, 0);

    /* get operational functions */
    led_ops = __led_get_ops();
    if(led_ops==NULL){
        retval = -ENODEV;
        _ERR("can't get operational functions: retval=%d\n",retval);
        goto fail;
    }

#ifdef CONFIG_MSUDEV_LED_PROC
    /* create /proc entry */
    if((retval=create_proc())<0)
        goto fail;
#endif  /* CONFIG_MSUDEV_LED_PROC */


    /* allocate device number */
#ifdef CONFIG_MSUDEV_LED_FIXED_DEVNUM
    devno = MKDEV(CONFIG_MSUDEV_LED_FIXED_MAJOR,CONFIG_MSUDEV_LED_FIXED_MINOR);
    retval = register_chrdev_region(devno, 1, MODULE_NAME);
#else  /* !CONFIG_MSUDEV_LED_FIXED_DEVNUM */
    retval = alloc_chrdev_region(&devno, 0, 1,MODULE_NAME);
#endif  /* CONFIG_MSUDEV_LED_FIXED_DEVNUM */
    if(retval<0){
        _ERR("can't allocate device number: retval=%d\n", retval);
        devno=0;
        goto fail;
    }

    /* initialize character device */
    cdev = cdev_alloc();
    if(NULL==cdev){
        _ERR("can't allocate cdev\n");
        retval = -ENOMEM;
        goto fail;
    }
    cdev->ops = &msudev_led_fops;
    cdev->owner = THIS_MODULE;
    retval = cdev_add(cdev,devno,1);
    if(retval<0){
        _ERR("failed to add cdev: retval=%d\n",retval);
        goto fail;
    }

     /* initialize */
    if(led_ops->init_led){
        retval = led_ops->init_led(led_ops);
        if(retval<0){
            _ERR("can't initialize lower level driver: retval=%d\n",retval);
            goto fail;
        }
    }
    if(!led_ops->nr_led || led_ops->nr_led(led_ops)<1){
        retval = -EINVAL;
        _ERR("NO LEDs\n");
        goto fail;
    }

 fail:

    if(retval<0){

        /* clean-up lower level driver */
        if(led_ops){
            if(led_ops->cleanup_led)
                led_ops->cleanup_led(led_ops);
            led_ops=NULL;
        }

        /* remove cdev */
        if(cdev)
            cdev_del(cdev);

        /* release device number */
        if(devno)
            unregister_chrdev_region(devno,1);

#ifdef CONFIG_MSUDEV_LED_PROC
        /* Remove proc entry */
        remove_proc();
#endif  /* CONFIG_MSUDEV_LED_PROC */
    }

    return retval;
}
module_init(msudev_led_init);

/*
 * @brief   Clean-up
 */
static void __exit msudev_led_cleanup (void)
{
    /* clean-up lower level driver */
    if(led_ops){
        if(led_ops->cleanup_led)
            led_ops->cleanup_led(led_ops);
        led_ops=NULL;
    }

    /* remove cdev */
    if(cdev)
        cdev_del(cdev);

    /* release device number */
    if(devno)
        unregister_chrdev_region(devno,1);

#ifdef CONFIG_MSUDEV_LED_PROC
    /* Remove proc entry */
    remove_proc();
#endif  /* CONFIG_MSUDEV_LED_PROC */

    _INFO("cleanup LED control driver for P2MSU\n");

}
module_exit(msudev_led_cleanup);
