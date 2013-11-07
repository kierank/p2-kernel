/* -*- C -*-
 * keyev-core.c --- KEY
 *
 * Copyright (C) 2010 Panasonic Co.,LTD.
 *
 * $Id: keyev-core.c 5704 2010-03-15 01:18:50Z Noguchi Isao $
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>           /* everything... */
#include <linux/cdev.h>         /* struct cdev */
#include <linux/errno.h>        /* error codes */
#include <linux/types.h>        /* size_t */
#include <linux/ioctl.h>        /* ioctl */
#include <linux/fcntl.h>        /* O_ACCMODE */
#ifdef CONFIG_MSUDEV_KEYEV_PROC
#include <linux/proc_fs.h>      /* /proc file system */
#endif  /* CONFIG_MSUDEV_KEYEV_PROC */
#include <linux/sched.h>
#include <linux/spinlock.h>     /* spinlock */
#include <linux/wait.h>         /* wait_event(), wake_up() */
#include <linux/poll.h>         /* poll */
#include <linux/semaphore.h>      /* semaphore */
#include <asm/atomic.h>         /* atomic_t */
#include <asm/bitops.h>         /* set_bit(),clr_bit(),... */
#include <asm/uaccess.h>        /* copy_from_user/copy_to_user */

#include <linux/p2msudev_user.h>
#include "keyev.h"

#define MODULE_NAME "msudev-keyev-core"
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

/* spinlock */
static spinlock_t spinlock = SPIN_LOCK_UNLOCKED;

/* semaphore */
static struct semaphore sema;

/* wait queue */
static wait_queue_head_t wait_queue;

/* status flag */
static unsigned long status;

#ifdef CONFIG_MSUDEV_KEYEV_PROC
/* proc entry */
static struct proc_dir_entry *proc_entry=NULL;
#define proc_key_name "driver/p2msudev-keyev"
#endif  /* CONFIG_MSUDEV_KEYEV_PROC */

/* ring buffer */
#define NR_FIFO_SIZE    1024
struct fifo_t {
    unsigned int wp, rp;        /* write/read pointer */
    struct p2msudev_ioc_keyev_info *buff; /* buffer */
    unsigned int size;                    /* size of buffer */
};
static struct fifo_t fifo;

/* file operations */
static struct msudev_keyev_ops *keyev_ops=NULL;


/*******************************************************************************
 **  function to operate status flag atomicly
 ******************************************************************************/

/* initialize */
static inline void init_status(void) {status=0;}

/* set bit */
static inline void set_status_overflow(void) {set_bit(SHIFT_P2MSUDEV_IOC_KEYEV_STATUS_OVERRUN,&status);}
static inline void set_status_underflow(void) {set_bit(SHIFT_P2MSUDEV_IOC_KEYEV_STATUS_UNDERRUN,&status);}
static inline void set_status_start(void) {set_bit(SHIFT_P2MSUDEV_IOC_KEYEV_STATUS_START,&status);}

/* clear bit */
static inline void clr_status_overflow(void) {clear_bit(SHIFT_P2MSUDEV_IOC_KEYEV_STATUS_OVERRUN,&status);}
static inline void clr_status_underflow(void) {clear_bit(SHIFT_P2MSUDEV_IOC_KEYEV_STATUS_UNDERRUN,&status);}
static inline void clr_status_start(void) {clear_bit(SHIFT_P2MSUDEV_IOC_KEYEV_STATUS_START,&status);}

/* check bit */
static inline int chk_status_overflow(void) {return test_bit(SHIFT_P2MSUDEV_IOC_KEYEV_STATUS_OVERRUN,&status);}
static inline int chk_status_underflow(void) {return test_bit(SHIFT_P2MSUDEV_IOC_KEYEV_STATUS_UNDERRUN,&status);}
static inline int chk_status_start(void) {return test_bit(SHIFT_P2MSUDEV_IOC_KEYEV_STATUS_START,&status);}


/*******************************************************************************
 **  functions for ring buffer
 ******************************************************************************/

static int fifo_init(struct fifo_t *const fp, unsigned int size)
{
    int retval = 0;
    if(size==0){
        retval=-EINVAL;
        goto fail;
    }
    memset(fp,0,sizeof(struct fifo_t));
    fp->buff = (struct p2msudev_ioc_keyev_info *)kzalloc(sizeof(struct p2msudev_ioc_keyev_info) * size, GFP_KERNEL);
    if(NULL==fp->buff){
        retval = -ENOMEM;
        goto fail;
    }
    fp->size = size;
 fail:
    if(retval<0){
        if(NULL!=fp->buff){
            kfree(fp->buff);
            fp->buff=NULL;
        }
    }
    return retval;
}

static void fifo_cleanup(struct fifo_t *const fp)
{
    if(NULL!=fp->buff){
        kfree(fp->buff);
        fp->buff=NULL;
    }
}

static void fifo_reset(struct fifo_t *const fp)
{
    unsigned long flags;
    spin_lock_irqsave(&spinlock,flags); /* lock spin */
    fp->wp = fp->rp = 0;
    spin_unlock_irqrestore(&spinlock,flags); /* unlock spin */
}

static int fifo_chk_empty(const struct fifo_t *const fp)
{
    int val;
    unsigned long flags;
    spin_lock_irqsave(&spinlock,flags); /* lock spin */
    val = (fp->wp==fp->rp);
    spin_unlock_irqrestore(&spinlock,flags); /* unlock spin */
    return val;
}

static unsigned int fifo_chk_freespace(const struct fifo_t *const fp)
{
    unsigned int val;
    unsigned long flags;

    spin_lock_irqsave(&spinlock,flags); /* lock spin */

    if(fifo_chk_empty(fp))
        val = fp->size - 1;
    else
        val = (fp->rp + fp->size - fp->wp) % fp->size - 1;

    spin_unlock_irqrestore(&spinlock,flags); /* unlock spin */

    return val;
}

static inline unsigned int fifo_chk_nr_buff(const struct fifo_t *const fp)
{
    return (fp->size - 1) - fifo_chk_freespace(fp);
}

static inline int fifo_chk_full(const struct fifo_t *const fp)
{
    return fifo_chk_freespace(fp) == 0;
}

static void fifo_put_data(struct fifo_t *const fp, const struct p2msudev_ioc_keyev_info *dp)
{
    unsigned long flags;
    spin_lock_irqsave(&spinlock,flags); /* lock spin */
    memcpy(&fp->buff[fp->wp],dp,sizeof(struct p2msudev_ioc_keyev_info));
    fp->wp = (fp->wp + 1) % fp->size;
    spin_unlock_irqrestore(&spinlock,flags); /* unlock spin */
}

static void fifo_get_data(struct fifo_t *const fp, struct p2msudev_ioc_keyev_info *dp)
{
    unsigned long flags;
    spin_lock_irqsave(&spinlock,flags); /* lock spin */
    memcpy(dp,&fp->buff[fp->rp],sizeof(struct p2msudev_ioc_keyev_info));
    fp->rp = (fp->rp + 1) % fp->size;
    spin_unlock_irqrestore(&spinlock,flags); /* unlock spin */
}



/*******************************************************************************
 ** functions for  /proc entry
 ******************************************************************************/

#ifdef CONFIG_MSUDEV_KEYEV_PROC

/*
 * /proc function
 */
static int procfunc_read(char *buff, char **start, off_t offset, int count, int *eof, void *data)
{
    int len=0;
    int limit = count - 40;
    unsigned int rp;
    
    /* semaphore down */
    if(down_interruptible(&sema))
        return len;

    /* local proc func */
    if(keyev_ops->procfunc_read){
        len = keyev_ops->procfunc_read(keyev_ops, buff, start, offset, count, eof, data);
        goto exit;
    }

    len += sprintf(buff+len, "\n[status]\n");
    len += sprintf(buff+len, "\tstart flag  = %s\n", chk_status_start()?"START":"STOP");
    len += sprintf(buff+len, "\tovrflw flag = %s\n", chk_status_overflow()?"ON":"OFF");
    len += sprintf(buff+len, "\tudrflw flag = %s\n", chk_status_underflow()?"ON":"OFF");
    len += sprintf(buff+len, "\tnumber of data = %d\n", fifo_chk_nr_buff(&fifo));
    if(keyev_ops->sample_period)
        len += sprintf(buff+len, "\tsample period = %ld[usec]\n",keyev_ops->sample_period);
    else
        len += sprintf(buff+len, "\tsample period is UNKNOWN?\n");
    
    len += sprintf(buff+len, "\n[fifo]\n");
    len += sprintf(buff+len, "\tfree space     = %d\n", fifo_chk_freespace(&fifo));
    if(fifo_chk_full(&fifo))
        len += sprintf(buff+len, "\tfifo is FULL.\n");
    else if(fifo_chk_empty(&fifo))
        len += sprintf(buff+len, "\tfifo is EMPTY.\n");

    len += sprintf(buff+len, "\n[fifo_data]\n");
    for(rp=fifo.rp; rp!=fifo.wp; rp = (rp + 1 + fifo.size) % fifo.size){
        struct p2msudev_ioc_keyev_info *p = &fifo.buff[rp];
        if(len>limit){
            len += sprintf(buff+len, "\n*** Limit is over!! ***\n");
            goto exit;
        }
        len += sprintf(buff+len, "\t%08lx/%08lx:%08lx\n",
                       p->key_sample_count, p->jiffies, p->key_bit_pattern);
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
         =create_proc_read_entry( proc_key_name, 0, NULL, procfunc_read, NULL))){
        _ERR("Can't create \"/proc/" proc_key_name "\".\n");
        ret = -EIO;
        goto fail;
    }
    
 fail:

    if(ret<0){
        if(proc_entry){
            remove_proc_entry(proc_key_name, NULL);
            proc_entry=NULL;
        }
    }

    return ret;
}

/*
 * remove proc entry
 */
static void remove_proc(void)
{
    if(proc_entry){
        remove_proc_entry(proc_key_name, NULL);
        proc_entry=NULL;
    }
}

#endif  /* CONFIG_MSUDEV_KEYEV_PROC */

/*******************************************************************************
 **  handler
 ******************************************************************************/

/*
 *  handler
 */
void __msudev_keyev_handler(void)
{
    struct p2msudev_ioc_keyev_info key_info;
    unsigned long key_map, sample_counter;
    static volatile unsigned long last_key_map=0;
    static int flg_int_start=0;


    /* get key_map */
    if(keyev_ops->keyscan)
        keyev_ops->keyscan(keyev_ops, &key_map, &sample_counter);

    /* interrupt is 1st time */
    if(!flg_int_start)
        goto exit;

    /* check start bit in status flag */
    if(!chk_status_start()){
        wake_up(&wait_queue);
        goto exit;
    }

   /* put key info to buffer */
    if(last_key_map != key_map ){
        /* check overflow. */
        if(fifo_chk_full(&fifo)){
            set_status_overflow();
            wake_up(&wait_queue);
            goto exit;
        }
        key_info.key_sample_count = sample_counter;
        key_info.key_bit_pattern = key_map;
        key_info.jiffies = jiffies;
        do_gettimeofday(&key_info.tv);
        fifo_put_data(&fifo, &key_info);
    }
    wake_up(&wait_queue);

 exit:
    /* interrupt is enabled */
    if(!flg_int_start)
        flg_int_start = -1;

    /* update */
    last_key_map = key_map;

}


/*******************************************************************************
 **  functions for ioctl sub-command
 ******************************************************************************/
/*
 * ioctl(P2MSUDEV_IOC_KEYEV_CTRL)
 */
static int ioc_keyev_ctrl(const unsigned int cmd, const unsigned long ctrl)
{
    int ret=0;

    /* check direction */
    if(_IOC_DIR(cmd)!=_IOC_NONE){
        _ERR("Invalid direction: %d\n", _IOC_DIR(cmd));
        return  -ENOTTY;
    }

    /* check capability */
    if(!capable(CAP_SYS_RAWIO)){
        return -EPERM;
    }

    /* semaphore down */
    if(down_interruptible(&sema))
        return -ERESTARTSYS;

    /* initailize buffer */
    if(ctrl&P2MSUDEV_IOC_KEYEV_CTRL_INIBUFF){
        fifo_reset(&fifo);
    }

    /* clear error flags */
    if(ctrl&P2MSUDEV_IOC_KEYEV_CTRL_CLRERR){
        clr_status_underflow();
        clr_status_overflow();
    }

    /* start/stop */
    if(ctrl & P2MSUDEV_IOC_KEYEV_CTRL_STOP){
        clr_status_start();
    }else if(ctrl & P2MSUDEV_IOC_KEYEV_CTRL_START){
        set_status_start();
    }
    
    /* semaphore up */
    up(&sema);
    return ret;
}

/*
 * ioctl(P2MSUDEV_IOC_KEYEV_STATUS)
 */
static int ioc_keyev_status(const unsigned int cmd, const unsigned long p_status)
{
    unsigned long ret_status=0;

    /* check direction */
    if(_IOC_DIR(cmd)!=_IOC_READ){
        _ERR("Invalid direction: %d\n", _IOC_DIR(cmd));
        return -ENOTTY;
    }

    /* check capability */
    if(!capable(CAP_SYS_RAWIO)){
        return -EPERM;
    }

    /* semaphore down */
    if(down_interruptible(&sema))
        return -ERESTARTSYS;

    /* status  flag */
    ret_status = (status & ~P2MSUDEV_IOC_KEYEV_STATUS_NRBUFF)
        | (fifo_chk_nr_buff(&fifo) & P2MSUDEV_IOC_KEYEV_STATUS_NRBUFF);

    /* copy to user space */
    if(!p_status || put_user(ret_status, (unsigned long *)p_status)){
        _ERR("PAGE fault\n");
        up(&sema);
        return -EFAULT;
    }

    /* semaphore up */
    up(&sema);

    return 0;
}

/*
 * ioctl(P2MSUDEV_IOC_KEYEV_GETINFO)
 */
static int ioc_keyev_getinfo(const unsigned int cmd, const unsigned long p_infobuff, const int flg_nonblock)
{
    int ret=0;
    unsigned int n, nr_buff,nr_data;
    unsigned int size=_IOC_SIZE(cmd);
    static struct p2msudev_ioc_keyev_info infobuff[P2MSUDEV_NR_KEYEV_INFO];
    
    /* check direction */
    if(_IOC_DIR(cmd)!=(_IOC_READ)){
        _ERR("Invalid direction: %d\n", _IOC_DIR(cmd));
        return -ENOTTY;
    }

    /* check buffer size */
    nr_buff = size/sizeof(struct p2msudev_ioc_keyev_info);
    if(nr_buff<1 || nr_buff>P2MSUDEV_NR_KEYEV_INFO){
        return -EINVAL;
    }

    /* check capability */
    if(!capable(CAP_SYS_RAWIO)){
        return -EPERM;
    }

    /* semaphore down */
    if(down_interruptible(&sema))
        return -ERESTARTSYS;

    /* waiting loop */
    while(fifo_chk_empty(&fifo) && chk_status_start()){

        /* semaphore up */
        up(&sema);

        /* O_NONBLOCK mode*/
        if(flg_nonblock && fifo_chk_empty(&fifo)){
            return -EAGAIN;
        }

        /* wait until data arrival */
        if(wait_event_interruptible(wait_queue, !fifo_chk_empty(&fifo) || !chk_status_start() ))
            return -ERESTARTSYS;

        /* semaphore down */
        if(down_interruptible(&sema))
            return -ERESTARTSYS;
    }

    /* if ring buffer is empty and status is STOP, then return 0 */
    if(fifo_chk_empty(&fifo) && !chk_status_start()){
        ret=0;
        goto exit;
    }

    /* check I/O error */
    if(chk_status_overflow() || chk_status_underflow()){
        ret = -EIO;
        goto exit;
    }

    /* read from ring buffer */
    nr_data = min(nr_buff, fifo_chk_nr_buff(&fifo));
/*     _INFO("nr_data=%d\n",nr_data); */
    for(n=0; n<nr_data; n++){
        fifo_get_data(&fifo, &infobuff[n]);
/*         _INFO("%08lX:%08lX\n",infobuff[n].key_sample_count,infobuff[n].key_bit_pattern); */
    }

    /* copy to user space */
    if(!p_infobuff ||
       copy_to_user((void*)p_infobuff, infobuff, nr_data*sizeof(struct p2msudev_ioc_keyev_info))){
        _ERR("PAGE fault\n");
        ret = -EFAULT;
        goto exit;
    }
    ret=nr_data;

 exit:
    /* semaphore up */
    up(&sema);
    return ret;
}


/*
 * ioctl(P2MSUDEV_IOC_KEYEV_SCAN)
 */
static int ioc_keyev_scan(const unsigned int cmd, const unsigned long p_pattern)
{
    unsigned long ret_pattern=0;
    unsigned long key_map;
    unsigned long flags;


    /* check direction */
    if(_IOC_DIR(cmd)!=_IOC_READ){
        _ERR("Invalid direction: %d\n", _IOC_DIR(cmd));
        return -ENOTTY;
    }

    /* check capability */
    if(!capable(CAP_SYS_RAWIO)){
        return -EPERM;
    }

    /* KEY pattern */
    if(NULL==keyev_ops->keyscan)
        return -EINVAL;

    /* lock spin */
    spin_lock_irqsave(&spinlock,flags);

    /* scan key maps */
    keyev_ops->keyscan(keyev_ops, &key_map, NULL);
    ret_pattern = key_map;

    /* unlock spin */
    spin_unlock_irqrestore(&spinlock,flags);

    /* copy to user space */
    if(!p_pattern || put_user(ret_pattern, (unsigned long *)p_pattern)){
        _ERR("PAGE fault\n");
        return -EFAULT;
    }

    return 0;
}


/*
 * ioctl(P2MSUDEV_IOC_KEYEV_PERIOD)
 */
static int ioc_keyev_period(const unsigned int cmd, const unsigned long p_period)
{
    unsigned long period;

    /* check direction */
    if(_IOC_DIR(cmd)!=_IOC_READ){
        _ERR("Invalid direction: %d\n", _IOC_DIR(cmd));
        return -ENOTTY;
    }

    /* check capability */
    if(!capable(CAP_SYS_RAWIO)){
        return -EPERM;
    }

    /* scan key maps */
    period = keyev_ops->sample_period;

    /* copy to user space */
    if(!p_period || put_user(period, (unsigned long *)p_period)){
        _ERR("PAGE fault\n");
        return -EFAULT;
    }

    return 0;
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

    /* main proc */
    switch(_IOC_NR(cmd)){
    case NR_P2MSUDEV_IOC_KEYEV_CTRL:
        ret=ioc_keyev_ctrl(cmd,arg);
        break;
    case NR_P2MSUDEV_IOC_KEYEV_STATUS:
        ret=ioc_keyev_status(cmd,arg);
        break;
    case NR_P2MSUDEV_IOC_KEYEV_GETINFO:
        ret=ioc_keyev_getinfo(cmd,arg, filp->f_flags & O_NONBLOCK);
        break;
    case NR_P2MSUDEV_IOC_KEYEV_SCAN:
        ret=ioc_keyev_scan(cmd,arg);
        break;
    case NR_P2MSUDEV_IOC_KEYEV_PERIOD :
        ret=ioc_keyev_period(cmd,arg);
        break;
    default:
        if(keyev_ops->ioctl)
            ret = keyev_ops->ioctl(keyev_ops,cmd,arg);
        else
            ret = -ENOTTY;
    }

    /* finish */
    return ret;
}

/* poll method */
static unsigned int poll_method(struct file *filp, poll_table *wait)
{
    unsigned int mask=0;

    /*  */
    poll_wait(filp, &wait_queue, wait);

    /* check data arrival */
    if(!fifo_chk_empty(&fifo))
        mask |= (POLLIN|POLLRDNORM);

    /* check status is STOP */
    if(!chk_status_start())
        mask |= POLLHUP;

    /* check i/o error */
    if(chk_status_underflow() || chk_status_overflow())
        mask |= POLLERR;

    return mask;
}

/* fops */
static struct file_operations msudev_keyev_fops = {
    owner:      THIS_MODULE,
    open:       open_method,
    release:    release_method,
    ioctl:      ioctl_method,
    poll:       poll_method,
};


/*******************************************************************************
 **  initialize/clean-up
 ******************************************************************************/

/*
 * @brief   Initialize
 */
static int __init msudev_keyev_init (void)
{
    int ret=0;

    _INFO("key-event driver for P2MSU\n");

    /* spinlock */
    spin_lock_init(&spinlock);

    /* semaphore */
    sema_init(&sema,1);

    /* wait_queue_head_t */
    init_waitqueue_head(&wait_queue);

    /* set the open count to 0 */
    atomic_set(&open_count, 0);

    /* status flag */
    init_status();

    /* get operational functions */
    keyev_ops = __keyev_get_ops();
    if(keyev_ops==NULL){
        ret = -ENODEV;
        _ERR("can't get operational functions: ret=%d\n",ret);
        goto fail;
    }

    /* ring buffer */
    ret=fifo_init(&fifo, NR_FIFO_SIZE);
    if(ret<0)
        goto fail;

#ifdef CONFIG_MSUDEV_KEYEV_PROC
    /* create /proc entry */
    if((ret=create_proc())<0)
        goto fail;
#endif  /* CONFIG_MSUDEV_KEYEV_PROC */

    /* start !! */
    set_status_start();

    /* allocate device number */
#ifdef CONFIG_MSUDEV_KEYEV_FIXED_DEVNUM
    devno = MKDEV(CONFIG_MSUDEV_KEYEV_FIXED_MAJOR,CONFIG_MSUDEV_KEYEV_FIXED_MINOR);
    ret = register_chrdev_region(devno, 1, MODULE_NAME);
#else  /* !CONFIG_MSUDEV_KEYEV_FIXED_DEVNUM */
    ret = alloc_chrdev_region(&devno, 0, 1,MODULE_NAME);
#endif  /* CONFIG_MSUDEV_KEYEV_FIXED_DEVNUM */
    if(ret<0){
        _ERR("can't allocate device number: ret=%d\n", ret);
        devno=0;
        goto fail;
    }

    /* initialize character device */
    cdev = cdev_alloc();
    if(NULL==cdev){
        _ERR("can't allocate cdev\n");
        ret = -ENOMEM;
        goto fail;
    }
    cdev->ops = &msudev_keyev_fops;
    cdev->owner = THIS_MODULE;
    ret = cdev_add(cdev,devno,1);
    if(ret<0){
        _ERR("failed to add cdev: ret=%d\n",ret);
        goto fail;
    }

    /* initialize */
    if(keyev_ops->init_key){
        ret = keyev_ops->init_key(keyev_ops);
        if(ret<0){
            _ERR("can't initialize lower level driver: ret=%d\n",ret);
            goto fail;
        }
    }

 fail:
    if(ret<0){

        /* clean-up lower level driver */
        if(keyev_ops){
            if(keyev_ops->cleanup_key)
                keyev_ops->cleanup_key(keyev_ops);
            keyev_ops=NULL;
        }

        /* remove cdev */
        if(cdev)
            cdev_del(cdev);

        /* release device number */
        if(devno)
            unregister_chrdev_region(devno,1);

#ifdef CONFIG_MSUDEV_KEYEV_PROC
        /* Remove proc entry */
        remove_proc();
#endif  /* CONFIG_MSUDEV_KEYEV_PROC */

        /* ring buffer */
        fifo_cleanup(&fifo);

    }

    /* complete */
    return ret;
}
module_init(msudev_keyev_init);

/*
 * @brief   Clean-up
 */
static void __exit msudev_keyev_cleanup (void)
{
    /* clean-up lower level driver */
    if(keyev_ops->cleanup_key)
        keyev_ops->cleanup_key(keyev_ops);
    keyev_ops=NULL;

    /* remove cdev */
    if(cdev)
        cdev_del(cdev);

    /* release device number */
    if(devno)
        unregister_chrdev_region(devno,1);

    /* status flag */
    init_status();

#ifdef CONFIG_MSUDEV_KEYEV_PROC
    /* Remove proc entry */
    remove_proc();
#endif  /* CONFIG_MSUDEV_KEYEV_PROC */

    /* ring buffer */
    fifo_cleanup(&fifo);

    _INFO("cleanup key-event driver for P2MSU\n");
}

module_exit(msudev_keyev_cleanup);
