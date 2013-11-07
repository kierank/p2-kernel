/* -*- C -*-
 * buzzer-core.c --- BUZZER control driver core
 *
 * Copyright (C) 2010 Panasonic Co.,LTD.
 *
 * $Id: buzzer-core.c 5704 2010-03-15 01:18:50Z Noguchi Isao $
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
#ifdef CONFIG_MSUDEV_BUZZER_PROC
#include <linux/proc_fs.h>      /* /proc file system */
#endif  /* CONFIG_MSUDEV_BUZZER_PROC */
#include <linux/sched.h>
#include <linux/semaphore.h>      /* semaphore */
#include <asm/atomic.h>         /* atomic_t */
#include <asm/uaccess.h>        /* copy_from_user/copy_to_user */

#include <linux/p2msudev_user.h>
#include "buzzer.h"

#define MODULE_NAME "msudev-buzzer-core"
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
/* static volatile union bf_fpga_buzzer buff_reg; */

/* semaphore */
static struct semaphore sema;

#ifdef CONFIG_MSUDEV_BUZZER_PROC
/* proc entry */
static struct proc_dir_entry *proc_entry=NULL;
#define proc_buzzer_name "driver/p2msudev-buzzer"
#endif  /* CONFIG_MSUDEV_BUZZER_PROC */

/* file operations */
static struct msudev_buzzer_ops *buzzer_ops=NULL;


/*******************************************************************************
 ** functions for  /proc entry
 ******************************************************************************/

#ifdef CONFIG_MSUDEV_BUZZER_PROC

/*
 * /proc function
 */
static int procfunc_read(char *buff, char **start, off_t offset, int count, int *eof, void *data)
{
    int len=0;
    struct p2msudev_ioc_buzzer_ctrl ctrl;

    /* semaphore down */
    if(down_interruptible(&sema))
        return len;

    /* local proc func */
    if(buzzer_ops->procfunc_read){
        len = buzzer_ops->procfunc_read(buzzer_ops, buff, start, offset, count, eof, data);
        goto exit;
    }

    if(!buzzer_ops->get_buzzer){
        _WARN("can't get BUZZER setting\n");
        goto exit;
    }

    /* main loop */
    if(buzzer_ops->get_buzzer(buzzer_ops, &ctrl)<0){
        _ERR("get_buzzer_entry() is faibuzzer\n");
        goto exit;
    }
    len += sprintf(buff+len, "start=%d\n",ctrl.start?1:0);
    len += sprintf(buff+len, "repeat=%d\n",ctrl.repeat?1:0);
    len += sprintf(buff+len, "fmode=%d\n",ctrl.fmode?1:0);
    len += sprintf(buff+len, "beep period=%d\n",ctrl.beep_cnt?1:0);
    len += sprintf(buff+len, "silent period=%d\n",ctrl.silent_cnt?1:0);


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
         =create_proc_read_entry( proc_buzzer_name, 0, NULL, procfunc_read, NULL))){
        _ERR("Can't create \"/proc/" proc_buzzer_name "\".\n");
        ret = -EIO;
        goto fail;
    }
    
    /* SUCCESS */
    return 0;

 fail:

    if(proc_entry){
        remove_proc_entry(proc_buzzer_name, NULL);
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
    _DEBUG( "Remove \"/proc/" proc_buzzer_name "\".\n");
    if(proc_entry){
        remove_proc_entry(proc_buzzer_name, NULL);
        proc_entry=NULL;
    }
}

#endif  /* CONFIG_MSUDEV_BUZZER_PROC */

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
    struct p2msudev_ioc_buzzer_ctrl * ctrlbuff = NULL;

    _DEBUG("proccess %i (%s) going to ioctl the device (%d:%d)\n",
           current->pid, current->comm, imajor(inode), iminor(inode));

    /* field of IOC command */
    nr      = _IOC_NR(cmd);
    dir     = _IOC_DIR(cmd);
    size    = _IOC_SIZE(cmd);
    _DEBUG("nr=0x%02X, dir=%s, size=%d\n",nr,ioc_dir_msg(dir),size);

    /* semaphore down */
    if(down_interruptible(&sema))
        return -ERESTARTSYS;

    /* main proc */
    switch(nr){

    case NR_P2MSUDEV_IOC_BUZZER_SETVAL:

        if((dir&(_IOC_READ|_IOC_WRITE))!=_IOC_WRITE){
            _ERR("Invalid direction: %d\n", dir);
            retval = -ENOTTY;
            goto exit;
        }

        if(!capable(CAP_SYS_RAWIO)){
            retval = -EPERM;
            goto exit;
        }

        if(!buzzer_ops->set_buzzer)
            goto exit;

        ctrlbuff = (struct p2msudev_ioc_buzzer_ctrl *)kmalloc(size, GFP_KERNEL);
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
        retval = buzzer_ops->set_buzzer(buzzer_ops, ctrlbuff);
        if(retval<0){
            kfree(ctrlbuff);
            goto exit;
        }

        kfree(ctrlbuff);

        break;


    case NR_P2MSUDEV_IOC_BUZZER_GETVAL:

        if((dir&(_IOC_READ|_IOC_WRITE))!=(_IOC_READ|_IOC_WRITE)){
            _ERR("Invalid direction: %d\n", dir);
            retval = -ENOTTY;
            goto exit;
        }

        if(!capable(CAP_SYS_RAWIO)){
            retval = -EPERM;
            goto exit;
        }

        if(!buzzer_ops->get_buzzer){
            _ERR("can't get BUZZER setting\n");
            retval = -ENODEV;
            goto exit;
        }

        ctrlbuff = (struct p2msudev_ioc_buzzer_ctrl *)kmalloc(size, GFP_KERNEL);
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
        retval = buzzer_ops->get_buzzer(buzzer_ops, ctrlbuff);
        if(retval<0){
            kfree(ctrlbuff);
            goto exit;
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

        if(buzzer_ops->ioctl)
            retval = buzzer_ops->ioctl(buzzer_ops,cmd,arg);
        else
            retval = -ENOTTY;

    }

    /* finish */
 exit:
    up(&sema);
    return retval;
}

/* fops */
static struct file_operations msudev_buzzer_fops = {
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
static int __init msudev_buzzer_init (void)
{

    int retval=0;

   _INFO("BUZZER control driver for P2MSU\n");

    /* semaphore */
    sema_init(&sema,1);

    /* set the open count to 0*/
    atomic_set(&open_count, 0);

    /* get operational functions */
    buzzer_ops = __buzzer_get_ops();
    if(buzzer_ops==NULL){
        retval = -ENODEV;
        _ERR("can't get operational functions: retval=%d\n",retval);
        goto fail;
    }

#ifdef CONFIG_MSUDEV_BUZZER_PROC
    /* create /proc entry */
    if((retval=create_proc())<0)
        goto fail;
#endif  /* CONFIG_MSUDEV_BUZZER_PROC */

    /* allocate device number */
#ifdef CONFIG_MSUDEV_BUZZER_FIXED_DEVNUM
    devno = MKDEV(CONFIG_MSUDEV_BUZZER_FIXED_MAJOR,CONFIG_MSUDEV_BUZZER_FIXED_MINOR);
    retval = register_chrdev_region(devno, 1, MODULE_NAME);
#else  /* !CONFIG_MSUDEV_BUZZER_FIXED_DEVNUM */
    retval = alloc_chrdev_region(&devno, 0, 1,MODULE_NAME);
#endif  /* CONFIG_MSUDEV_BUZZER_FIXED_DEVNUM */
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
    cdev->ops = &msudev_buzzer_fops;
    cdev->owner = THIS_MODULE;
    retval = cdev_add(cdev,devno,1);
    if(retval<0){
        _ERR("faibuzzer to add cdev: retval=%d\n",retval);
        goto fail;
    }

     /* initialize */
    if(buzzer_ops->init_buzzer){
        retval = buzzer_ops->init_buzzer(buzzer_ops);
        if(retval<0){
            _ERR("can't initialize lower level driver: retval=%d\n",retval);
            goto fail;
        }
    }

 fail:

    if(retval<0){

        /* clean-up lower level driver */
        if(buzzer_ops){
            if(buzzer_ops->cleanup_buzzer)
                buzzer_ops->cleanup_buzzer(buzzer_ops);
            buzzer_ops=NULL;
        }

        /* remove cdev */
        if(cdev)
            cdev_del(cdev);

        /* release device number */
        if(devno)
            unregister_chrdev_region(devno,1);

#ifdef CONFIG_MSUDEV_BUZZER_PROC
        /* Remove proc entry */
        remove_proc();
#endif  /* CONFIG_MSUDEV_BUZZER_PROC */
    }

    return retval;
}
module_init(msudev_buzzer_init);

/*
 * @brief   Clean-up
 */
static void __exit msudev_buzzer_cleanup (void)
{
    /* clean-up lower level driver */
    if(buzzer_ops){
        if(buzzer_ops->cleanup_buzzer)
            buzzer_ops->cleanup_buzzer(buzzer_ops);
        buzzer_ops=NULL;
    }

    /* remove cdev */
    if(cdev)
        cdev_del(cdev);

    /* release device number */
    if(devno)
        unregister_chrdev_region(devno,1);

#ifdef CONFIG_MSUDEV_BUZZER_PROC
    /* Remove proc entry */
    remove_proc();
#endif  /* CONFIG_MSUDEV_BUZZER_PROC */

    _INFO("cleanup BUZZER control driver for P2MSU\n");

}
module_exit(msudev_buzzer_cleanup);
