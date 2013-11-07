/**
 * adc-core.c --- AD/C driver core
 *
 * Copyright (C) 2010 Panasonic Co.,LTD.
 *
 * $Id: adc-core.c 5704 2010-03-15 01:18:50Z Noguchi Isao $
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
#ifdef CONFIG_MSUDEV_ADC_PROC
#include <linux/proc_fs.h>      /* /proc file system */
#endif  /* CONFIG_MSUDEV_ADC_PROC */
#include <linux/sched.h>
#include <linux/semaphore.h>      /* semaphore */
#include <asm/atomic.h>         /* atomic_t */
#include <asm/uaccess.h>        /* copy_from_user/copy_to_user */

#include <linux/p2msudev_user.h>
#include "adc.h"

#define MODULE_NAME "msudev-adc-core"
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

/* semaphore */
static struct semaphore sema;

#ifdef CONFIG_MSUDEV_ADC_PROC
/* proc entry */
static struct proc_dir_entry *proc_entry=NULL;
#define proc_adc_name "driver/p2msudev-adc"
#endif  /* CONFIG_MSUDEV_ADC_PROC */

/* file operations */
static struct msudev_adc_ops *adc_ops=NULL;


/*******************************************************************************
 ** functions for  /proc entry
 ******************************************************************************/

#ifdef CONFIG_MSUDEV_ADC_PROC

/*
 * /proc function
 */
static int procfunc_read(char *buff, char **start, off_t offset, int count, int *eof, void *data)
{
    int len=0;
    unsigned int nr_chan;

    /* semaphore down */
    if(down_interruptible(&sema))
        return len;

    /* local proc func */
    if(adc_ops->procfunc_read){
        len = adc_ops->procfunc_read(adc_ops, buff, start, offset, count, eof, data);
        goto exit;
    }

    /* display */
    nr_chan = adc_ops->nr_chan?adc_ops->nr_chan(adc_ops):0;
    len += sprintf(buff+len, "A/DC port number = %d\n", nr_chan);
    if(adc_ops->get_value){
        int chan;
        for(chan=0; chan<nr_chan; chan++){
            unsigned long value;
            adc_ops->get_value(adc_ops,chan,&value);
            len += sprintf(buff+len, "ADC(%d) = 0x%08lX\n", chan,value);
        }
    }

 exit:
    /* semaphore up */
    up(&sema);

    /* finished */
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
         = create_proc_read_entry( proc_adc_name, 0, NULL, procfunc_read, NULL))){
        _ERR("Can't create \"/proc/" proc_adc_name "\".\n");
        ret = -EIO;
        goto fail;
    }
    
 fail:

    if(ret<0 && proc_entry){
        remove_proc_entry(proc_adc_name, NULL);
        proc_entry=NULL;
    }

    return ret;
}

/*
 * remove proc entry
 */
static void remove_proc(void)
{
    _DEBUG( "Remove \"/proc/" proc_adc_name "\".\n");
    if(proc_entry){
        remove_proc_entry(proc_adc_name, NULL);
        proc_entry=NULL;
    }
}

#endif  /* CONFIG_MSUDEV_ADC_PROC */

/*******************************************************************************
 **  functions for ioctl sub-command
 ******************************************************************************/

/** 
 *  ioctl(P2MSUDEV_IOC_ADC_READ)
 */
static int ioc_adc_read(unsigned int cmd, int chan)
{
    int retval=0;

    /* check direction */
    if(_IOC_DIR(cmd)!=_IOC_NONE){
        _ERR("Invalid direction: %d\n", _IOC_DIR(cmd));
        retval = -ENOTTY;
        goto exit;
    }

    /* check capability */
    if(!capable(CAP_SYS_RAWIO)){
        retval = -EPERM;
        goto exit;
    }

    /* read value */
    if(adc_ops->get_value){
        unsigned long value;
        retval = adc_ops->get_value(adc_ops,chan,&value);
        if(retval<0)
            goto exit;
        retval = (int)value;
    }

 exit:
    return retval;
        
}


/** 
 *  ioctl(P2MSUDEV_IOC_ADC_RESET)
 */
static int ioc_adc_reset(unsigned int cmd, int reset)
{
    /* check direction */
    if(_IOC_DIR(cmd)!=_IOC_NONE){
        _ERR("Invalid direction: %d\n", _IOC_DIR(cmd));
        return  -ENOTTY;
    }

    /* check capability */
    if(!capable(CAP_SYS_RAWIO)){
        return -EPERM;
    }

    /* do reset */
    if(adc_ops->do_reset)
        return adc_ops->do_reset(adc_ops,reset);
    else
        return 0;
}

/* 
 * ioctl(P2MSUDEV_IOC_ADC_RESET_CHECK)
 */
static int ioc_adc_reset_check(unsigned int cmd)
{
    int retval=0;

    /* check direction */
    if(_IOC_DIR(cmd)!=_IOC_NONE){
        _ERR("Invalid direction: %d\n", _IOC_DIR(cmd));
        retval = -ENOTTY;
        goto exit;
    }

    /* check capability */
    if(!capable(CAP_SYS_RAWIO)){
        retval = -EPERM;
        goto exit;
    }

    /* do reset */
    if(adc_ops->check_reset){
        int check;
        retval = adc_ops->check_reset(adc_ops,&check);
        if(retval<0)
            goto exit;
        retval = check?1:0;
    }
 exit:
    return retval;
}


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
static int ioctl_method(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg)
{
    int ret=0;

    _DEBUG("proccess %i (%s) going to ioctl the device (%d:%d)\n", 
           current->pid, current->comm, imajor(inode), iminor(inode));

    /* semaphore down */
    if(down_interruptible(&sema))
        return -ERESTARTSYS;

    /* main proc */
    switch(_IOC_NR(cmd)){
    case NR_P2MSUDEV_IOC_ADC_READ:
        ret=ioc_adc_read(cmd,(int)arg);
        break;
    case NR_P2MSUDEV_IOC_ADC_RESET:
        ret=ioc_adc_reset(cmd,(int)arg);
        break;
    case NR_P2MSUDEV_IOC_ADC_RESET_CHECK:
        ret=ioc_adc_reset_check(cmd);
        break;
    default:
        if(adc_ops->ioctl)
            ret = adc_ops->ioctl(adc_ops,cmd,arg);
        else
            ret = -ENOTTY;
    }

    /* finish */
    up(&sema);
    return ret;
}


/* fops */
static struct file_operations msudev_adc_fops = {
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
static int __init msudev_adc_init (void)
{

    int retval=0;

    _INFO("A/D converter driver for P2MSU\n");

    /* semaphore */
    sema_init(&sema,1);

    /* set the open count to 0 */
    atomic_set(&open_count, 0);

    /* get operational functions */
    adc_ops = __adc_get_ops();
    if(adc_ops==NULL){
        retval = -ENODEV;
        _ERR("can't get operational functions: retval=%d\n",retval);
        goto fail;
    }

#ifdef CONFIG_MSUDEV_ADC_PROC
    /* create /proc entry */
    if((retval=create_proc())<0)
        goto fail;
#endif  /* CONFIG_MSUDEV_ADC_PROC */

    /* allocate device number */
#ifdef CONFIG_MSUDEV_ADC_FIXED_DEVNUM
    devno = MKDEV(CONFIG_MSUDEV_ADC_FIXED_MAJOR,CONFIG_MSUDEV_ADC_FIXED_MINOR);
    retval = register_chrdev_region(devno, 1, MODULE_NAME);
#else  /* !CONFIG_MSUDEV_ADC_FIXED_DEVNUM */
    retval = alloc_chrdev_region(&devno, 0, 1,MODULE_NAME);
#endif  /* CONFIG_MSUDEV_ADC_FIXED_DEVNUM */
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
    cdev->ops = &msudev_adc_fops;
    cdev->owner = THIS_MODULE;
    retval = cdev_add(cdev,devno,1);
    if(retval<0){
        _ERR("failed to add cdev: retval=%d\n",retval);
        goto fail;
    }

     /* initialize */
    if(adc_ops->init_adc){
        retval = adc_ops->init_adc(adc_ops);
        if(retval<0){
            _ERR("can't initialize lower level driver: retval=%d\n",retval);
            goto fail;
        }
    }
    if(!adc_ops->nr_chan || adc_ops->nr_chan(adc_ops)<1){
        retval = -ENODEV;
        _ERR("NO channels\n");
        goto fail;
    }


fail:

    if(retval<0){

        /* clean-up lower level driver */
        if(adc_ops){
            if(adc_ops->cleanup_adc)
                adc_ops->cleanup_adc(adc_ops);
            adc_ops=NULL;
        }

        /* remove cdev */
        if(cdev)
            cdev_del(cdev);

        /* release device number */
        if(devno)
            unregister_chrdev_region(devno,1);

#ifdef CONFIG_MSUDEV_ADC_PROC
        /* Remove proc entry */
        remove_proc();
#endif  /* CONFIG_MSUDEV_ADC_PROC */
    }

    return retval;
}
module_init(msudev_adc_init);

/*
 * @brief   Clean-up
 */
static void __exit msudev_adc_cleanup(void)
{
    /* clean-up lower level driver */
    if(adc_ops){
        if(adc_ops->cleanup_adc)
            adc_ops->cleanup_adc(adc_ops);
        adc_ops=NULL;
    }

    /* remove cdev */
    if(cdev)
        cdev_del(cdev);

    /* release device number */
    if(devno)
        unregister_chrdev_region(devno,1);

#ifdef CONFIG_MSUDEV_ADC_PROC
    /* Remove proc entry */
    remove_proc();
#endif  /* CONFIG_MSUDEV_ADC_PROC */

    _INFO("cleanup A/D converter driver for P2MSU\n");

}
module_exit(msudev_adc_cleanup);
