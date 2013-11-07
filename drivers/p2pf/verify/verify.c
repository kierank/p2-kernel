/* -*- C -*-
 * verify.c --- P2VERIFY
 *
 * Copyright (C) 2005-2010 Panasonic Co.,LTD.
 *
 * $Id: verify.c 8600 2010-08-03 01:05:58Z Noguchi Isao $
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/fs.h>           /* everything... */
#include <linux/cdev.h>         /* struct cdev */
#include <linux/errno.h>        /* error codes */
#include <linux/types.h>        /* size_t */
#include <linux/ioctl.h>        /* ioctl */
#include <linux/fcntl.h>        /* O_ACCMODE */
#ifdef CONFIG_P2VERIFY_PROC
#include <linux/proc_fs.h>      /* /proc file system */
#endif /* CONFIG_P2VERIFY_PROC */
#include <linux/sched.h>
#include <linux/semaphore.h>    /* semaphore */
#include <linux/spinlock.h>     /* spinlock */
#include <linux/wait.h>         /* wait_event(), wake_up() */
#include <linux/poll.h>         /* poll */
#include <linux/interrupt.h>    /* mark_bh(),... */
#include <linux/workqueue.h>    /* workqueue */
#include <linux/dma-mapping.h>
#include <asm/atomic.h>         /* atomic_t */
#include <asm/bitops.h>         /* set_bit(),clr_bit(),... */
#include <asm/io.h>             /* memcpy_* */
#include <asm/uaccess.h>        /* copy_from_user/copy_to_user */
#include <linux/p2verify.h>

#include "verify_debug.h"
#include "verify.h"

#define MODULE_NAME "p2verify"


/*******************************************************************************
 **  internal static variables
 ******************************************************************************/

/* device number */
static dev_t   devno=0;

/* character device */
static struct cdev *cdev=NULL;

/* spinlock */
static spinlock_t spinlock = SPIN_LOCK_UNLOCKED;

/* semaphore */
static struct semaphore sema;

/* wait queue */
static wait_queue_head_t wait_queue_start;
static wait_queue_head_t wait_queue_done;

/* state flag */
static unsigned int state;

/* handle */
static atomic_t handle;

/* result */
static int result;

/* verify count */
static unsigned long v_cnt;

/* error data */
static unsigned short serr, derr;

#ifdef CONFIG_P2VERIFY_PROC
/* proc entry */
static struct proc_dir_entry *proc_entry=NULL;
#define proc_name "driver/p2verify"
#endif  /* CONFIG_P2VERIFY_PROC */



/*******************************************************************************
 **  verify task
 ******************************************************************************/

/* parameters for verify task */
static const unsigned int blk_size = 512;
static unsigned long phy_saddr=0, phy_daddr=0;
static unsigned long __iomem *reg_saddr=NULL, *reg_daddr=NULL;
static unsigned long reg_len=0;
static unsigned long reg_cnt=0;
static unsigned short reg_serr,reg_derr;
static int reg_start=0, reg_stop;
static int reg_err=0;
static int addrtype=0;

static void verify_handler(unsigned long data);
DECLARE_TASKLET(tasklet_handler,verify_handler,0);
static void verify_task(struct work_struct *work);
DECLARE_WORK(work_verify,verify_task);


/*
 * interrupt handler
 */
static void verify_handler(unsigned long data)
{

    //_INFO("ENTER: verify_handler\n");
    /* lock */
    spin_lock(&spinlock);

    /* check state */
    if(state != ST_P2VERIFY_BUSY)
        _CRIT("ERROR invalid state:%d\n",state);

    /* check in under operation*/
    if(reg_start)
        _CRIT("ERROR: It's still operating.\n");

    /* get error */
    result=reg_err?1:0;

    /* verify count */
    v_cnt = reg_cnt;

    /* error data */
    serr = reg_serr;
    derr = reg_derr;

    /* change state to DONE */
    state = ST_P2VERIFY_DONE;

    /* unlock */
    spin_unlock(&spinlock);

    /* wake up */
    wake_up(&wait_queue_done);

    //_INFO("EXIT: verify_handler\n");
}


/*
 * task  for verify
 */
static void verify_task(struct work_struct *work)
{
    static const int blk_num = 1024;
    static const int max = blk_size/4;
    register unsigned long __iomem *sa;
    register unsigned long __iomem *da;
    register int m,n;

    /* aborted ? */
    if(reg_stop)
        goto done;

    sa = reg_saddr + reg_cnt/4;
    da = reg_daddr + reg_cnt/4;
    for(m=0; m<blk_num; m++){

        /* done ? */
        if(reg_cnt>=reg_len)
            goto done;

/*         /\* ivalidate cache *\/ */
/*         __dma_sync(sa,blk_size,DMA_FROM_DEVICE); */
/*         __dma_sync(da,blk_size,DMA_FROM_DEVICE); */

        /* compare */
        for(n=0; n<max; n++){

            if(unlikely(*sa++ != *da++)){
                sa--;
                da--;
                goto fail;
            }
            reg_cnt += 4;
        }
    }

    /* re-schedule */
    schedule_work(&work_verify);

    return;

 fail:

/*     if(((*sa)&0xffff)!=((*da)&0xffff)){ */
/*         reg_serr = (*sa)&0xffff; */
/*         reg_derr = (*da)&0xffff; */
/*     } else { */
/*         reg_cnt += 2; */
/*         reg_serr = ((*sa)>>16)&0xffff; */
/*         reg_derr = ((*da)>>16)&0xffff; */
/*     } */
    if((((*sa)>>16)&0xffff)!=(((*da)>>16)&0xffff)){
        reg_serr = ((*sa)>>16)&0xffff;
        reg_derr = ((*da)>>16)&0xffff;
    } else {
        reg_cnt += 2;
        reg_serr = (*sa)&0xffff;
        reg_derr = (*da)&0xffff;
    }
    reg_saddr = sa;
    reg_daddr = da;
    reg_err = 1;

 done:

 /*    _INFO("end=%ld\n",jiffies); */

    reg_stop = 0;
    reg_start = 0;

    /* call handler */
    tasklet_schedule(&tasklet_handler);
}

/*
 *  start verify
 */
static void do_start(void)
{
    if(!reg_start){
        reg_cnt = 0;
        reg_err = 0;
        reg_start = 1;
        reg_stop = 0;
        schedule_work(&work_verify);
    }

/*    _INFO("start=%ld\n",jiffies); */

}

/*
 *  stop verify
 */
static inline void do_stop(void)
{
    if(reg_start)
        reg_stop=1;
}



/*
 *  initialize
 */
static int init_verify_task(void)
{
    int retval=0;

    reg_start=0;
    reg_stop=0;
    reg_err=0;

    /* comprete */
    return retval;
}


/*
 *
 */
static void cleanup_verify_task(void)
{
}

/*******************************************************************************
 **  functions for misc
 ******************************************************************************/

/*
 * return string which means direction ioctl system call
 *
 * @param dir return value of _IOC_DIR()
 * @return string which means direction ioctl system call
 *          - "NONE": _IOC_NONE
 *          - "READ": _IOC_READ
 *          - "WRITE": _IOC_WRITE
 *          - "READ_WRITE": _IOC_READ|_IOC_WRITE
 */
static char __inline__ * ioc_dir_msg(const unsigned int dir)
{
    char *s;
    switch(dir){
    case _IOC_READ|_IOC_WRITE:
        s="READ_WRITE";
        break;
    case _IOC_READ:
        s="READ";
        break;
    case _IOC_WRITE:
        s="WRITE";
        break;
    default:
        s="NONE";
    }
    return s;
}


/*******************************************************************************
 ** functions for  /proc entry
 ******************************************************************************/

#ifdef CONFIG_P2VERIFY_PROC

/*
 * /proc function
 */
static int procfunc_read(char *buff, char **start, off_t offset, int count, int *eof, void *data)
{
    int len=0;
    
    /* semaphore down */
    if(down_interruptible(&sema))
        return len;

    len += sprintf(buff+len, "\n[status]\n");
    len += sprintf(buff+len, "\tstate = ");
    switch(state){
    case ST_P2VERIFY_IDLE:
        len += sprintf(buff+len,"IDLE");
        break;
    case ST_P2VERIFY_BUSY:
        len += sprintf(buff+len,"BUSY");
        break;
    case ST_P2VERIFY_DONE:
        len += sprintf(buff+len,"DONE");
        break;
    default:
        len += sprintf(buff+len,"UNKNOWN");
    }
    len += sprintf(buff+len, "\n");
    len += sprintf(buff+len, "\thandle = %d\n", atomic_read(&handle));
    len += sprintf(buff+len, "\tsource address       = %08lX\n", phy_saddr);
    len += sprintf(buff+len, "\tdistination address  = %08lX\n", phy_daddr);
    len += sprintf(buff+len, "\tdata length          = %08lX\n", reg_len);
    len += sprintf(buff+len, "\tlast verify count    = %08lX\n", v_cnt);
    len += sprintf(buff+len, "\tlast error data (src)= %04X\n", serr);
    len += sprintf(buff+len, "\tlast error data (dst)= %04X\n", derr);
    len += sprintf(buff+len, "\tlast error result    = %s\n", result?"NG":"OK");

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
         = create_proc_read_entry( proc_name, 0, NULL, procfunc_read, NULL))){
        _ERR("Can't create \"/proc/" proc_name "\".\n");
        ret = -EIO;
        goto fail;
    }
    
 fail:

    if(ret<0 && proc_entry){
        remove_proc_entry(proc_name, NULL);
        proc_entry=NULL;
    }

    return ret;
}

/*
 * remove proc entry
 */
static void remove_proc(void)
{
    _DEBUG( "Remove \"/proc/" proc_name "\".\n");
    if(proc_entry){
        remove_proc_entry(proc_name, NULL);
        proc_entry=NULL;
    }
}

#endif  /* CONFIG_P2VERIFY_PROC */

/*******************************************************************************
 **  functions for ioctl sub-command
 ******************************************************************************/

/* 
 * ioctl(P2VERIFY_IOC_START)
 */
static int ioc_verify_start(const unsigned int cmd, const unsigned long p_start, const int flg_nonblock)
{
    int ret=0;
    struct p2verify_start start;
    unsigned long flags;
    unsigned int size=_IOC_SIZE(cmd);
    
    /* check direction */
    if(_IOC_DIR(cmd)!=(_IOC_WRITE)){
        _ERR("Invalid direction: %s\n", ioc_dir_msg(_IOC_DIR(cmd)));
        return -ENOTTY;
    }

    /* check size */
    if(size!=sizeof(struct p2verify_start) ){
        return -EINVAL;
    }

    /* check capability */
    if(!capable(CAP_SYS_RAWIO)){
        return -EPERM;
    }

    /* semaphore down */
    if(down_interruptible(&sema))
        return -ERESTARTSYS;

    while(state != ST_P2VERIFY_IDLE){

        /* semaphore up */
        up(&sema);

        /* O_NONBLOCK mode*/
        if(flg_nonblock){
            return -EAGAIN;
        }

        /* wait until data arrival */
        if(wait_event_interruptible(wait_queue_start, state == ST_P2VERIFY_IDLE))
            return -ERESTARTSYS;

        /* semaphore down */
        if(down_interruptible(&sema))
            return -ERESTARTSYS;
    }

    /* check */
    if(reg_start){
        _CRIT("ERROR: It has already operated\n");
        ret=-EIO;
        goto exit;
    }

    /* copy from user space */
    if(!p_start || copy_from_user(&start, (void __user *)p_start, size)){
        _ERR("PAGE fault\n");    /* clear interrupt */
        ret = -EFAULT;
        goto exit;
    }

    /* lock spin */
    spin_lock_irqsave(&spinlock,flags);

    if(NULL!=reg_saddr){
        _ERR("NOT iounmap source address: 0x%08lx\n", phy_saddr);
        ret = -ENOMEM;
        goto exit;
    }
    if(NULL!=reg_daddr){
        _ERR("NOT iounmap destination address: 0x%08lx\n", phy_daddr);
        ret = -ENOMEM;
        goto exit;
    }

    /* set parameters */
    addrtype = start.addrtype;
    reg_len = start.len & ~(blk_size-1);
    phy_saddr = start.sa & ~(blk_size-1);
    phy_daddr = start.da & ~(blk_size-1);
    switch(addrtype) {
    case P2VERIFY_ADDRTYPE_BUS:
        reg_saddr = bus_to_virt(phy_saddr);
        if(NULL==reg_saddr){
            _ERR("failed to change to virtual address: source = 0x%08lx\n", phy_saddr);
            ret = -ENOMEM;
            goto exit;
        }
        reg_daddr = bus_to_virt(phy_daddr);
        if(NULL==reg_daddr){
            _ERR("failed to change to virtual address: destination = 0x%08lx\n", phy_daddr);
            ret = -ENOMEM;
            goto exit;
        }
        break;
    case P2VERIFY_ADDRTYPE_MEM:
        reg_saddr = __va(phy_saddr);
        reg_daddr = __va(phy_daddr);
        break;
    case P2VERIFY_ADDRTYPE_IOMEM:     
        reg_saddr = ioremap(phy_saddr, reg_len);
        if(NULL==reg_saddr){
            _ERR("failed to ioremap source address: 0x%08lx\n", phy_saddr);
            ret = -ENOMEM;
            goto exit;
        }

        reg_daddr = ioremap(phy_daddr, reg_len);
        if(NULL==reg_daddr){
            _ERR("failed to ioremap destination address: 0x%08lx\n", phy_daddr);
            ret = -ENOMEM;
            goto exit;
        }
        break;
    default:
        _ERR("invalid parameter : addrtype=%d\n", addrtype);
        ret = -EINVAL;
        goto exit;
    }

    /* increment handle */
    atomic_inc(&handle);
    ret = atomic_read(&handle);

    /* Comparison start */
    do_start();

    /* change state to BUSY */
    state = ST_P2VERIFY_BUSY;

    /* unlock spin */
    spin_unlock_irqrestore(&spinlock,flags);

 exit:

    if(ret<0) {
        if(NULL!=reg_saddr){
            if(addrtype==P2VERIFY_ADDRTYPE_IOMEM)
                iounmap(reg_saddr);
            reg_saddr=NULL;
        }
        if(NULL!=reg_daddr){
            if(addrtype==P2VERIFY_ADDRTYPE_IOMEM)
                iounmap(reg_daddr);
            reg_daddr=NULL;
        }
    }

    /* semaphore up */
    up(&sema);

    return ret;
}

/* 
 * ioctl(P2VERIFY_IOC_DONE)
 */
static int ioc_verify_done(const unsigned int cmd, const int hndl, const int flg_nonblock)
{
    int ret=0;
    unsigned long flags;
    
    /* check direction */
    if(_IOC_DIR(cmd)!=(_IOC_NONE)){
        _ERR("Invalid direction: %s\n", ioc_dir_msg(_IOC_DIR(cmd)));
        return -ENOTTY;
    }

    /* check capability */
    if(!capable(CAP_SYS_RAWIO)){
        return -EPERM;
    }

    /* check handle */
    if(hndl!=atomic_read(&handle))
        return -EINVAL;

    /* semaphore down */
    if(down_interruptible(&sema))
        return -ERESTARTSYS;

    /* invalid state */
    if(state==ST_P2VERIFY_IDLE){
        /* semaphore up */
        up(&sema);
        return -EINVAL;
    }

    while(state==ST_P2VERIFY_BUSY){

        /* semaphore up */
        up(&sema);

        /* check handle */
        if(hndl!=atomic_read(&handle))
            return -EINVAL;

        /* O_NONBLOCK mode*/
        if(flg_nonblock){
            return -EAGAIN;
        }

        /* wait until data arrival */
        if(wait_event_interruptible(wait_queue_done, state!=ST_P2VERIFY_BUSY)) 
            return -ERESTARTSYS;

        /* semaphore down */
        if(down_interruptible(&sema))
            return -ERESTARTSYS;
    }

    /* terminate forcely */
    if(state==ST_P2VERIFY_IDLE){
            ret = 2;
            goto exit;
    }

    /* check under operation */
    if(reg_start){
        _CRIT("ERROR: It's still operating.\n");
        ret=-EIO;
        goto exit;
    }

    /* lock spin */
    spin_lock_irqsave(&spinlock,flags);

    /* check error */
    ret = result;

    /* change state to IDLE */
    state = ST_P2VERIFY_IDLE;

    /* unmap */
    if(NULL!=reg_saddr){
        if(addrtype==P2VERIFY_ADDRTYPE_IOMEM)
            iounmap(reg_saddr);
        reg_saddr=NULL;
    }
    if(NULL!=reg_daddr){
        if(addrtype==P2VERIFY_ADDRTYPE_IOMEM)
            iounmap(reg_daddr);
        reg_daddr=NULL;
    }

     /* unlock spin */
    spin_unlock_irqrestore(&spinlock,flags);

    /* wake up */
    wake_up(&wait_queue_start);

exit:
    /* semaphore up */
    up(&sema);

    return ret;
}


/* 
 * ioctl(P2VERIFY_IOC_STOP)
 */
static int ioc_verify_stop(const unsigned int cmd, const int hndl)
{
    int ret=0;
    unsigned long flags;
    
    /* check direction */
    if(_IOC_DIR(cmd)!=(_IOC_NONE)){
        _ERR("Invalid direction: %s\n", ioc_dir_msg(_IOC_DIR(cmd)));
        return -ENOTTY;
    }

    /* check capability */
    if(!capable(CAP_SYS_RAWIO)){
        return -EPERM;
    }

    /* semaphore down */
    if(down_interruptible(&sema))
        return -ERESTARTSYS;

    /* check handle */
    if(hndl!=atomic_read(&handle)){
        ret = -EINVAL;
        goto exit;
    }

    /* check state is IDLE */
    if(state == ST_P2VERIFY_IDLE){
        ret = 0;
        goto exit;
    }

    /* lock spin */
    spin_lock_irqsave(&spinlock,flags);

    /* stop forcely */
    if(state == ST_P2VERIFY_BUSY){
        do_stop();
    }

    /* check under operation */
    if(reg_start){
        spin_unlock_irqrestore(&spinlock,flags);
        _CRIT("ERROR: It's still operating.\n");
        ret=-EIO;
        goto exit;
    }

    /* change state to IDLE */
    state = ST_P2VERIFY_IDLE;

    /* unmap */
    if(NULL!=reg_saddr){
        iounmap(reg_saddr);
        reg_saddr=NULL;
    }
    if(NULL!=reg_daddr){
        iounmap(reg_daddr);
        reg_daddr=NULL;
    }

     /* unlock spin */
    spin_unlock_irqrestore(&spinlock,flags);

    /* wake up */
    wake_up(&wait_queue_start);
    wake_up(&wait_queue_done);

exit:
    /* semaphore up */
    up(&sema);

    return ret;
}


/* 
 * ioctl(P2VERIFY_IOC_STATUS)
 */
static int ioc_verify_status(const unsigned int cmd, const unsigned long p_status)
{
    int ret=0;
    struct p2verify_status status;
    unsigned long flags;
    unsigned int size=_IOC_SIZE(cmd);

    /* check direction */
    if(_IOC_DIR(cmd)!=_IOC_READ){
        _ERR("Invalid direction: %s\n", ioc_dir_msg(_IOC_DIR(cmd)));
        return -ENOTTY;
    }

    /* check capability */
    if(!capable(CAP_SYS_RAWIO)){
        return -EPERM;
    }

    /* semaphore down */
    if(down_interruptible(&sema))
        return -ERESTARTSYS;

    /* lock spin */
    spin_lock_irqsave(&spinlock,flags);

    /* get para,eters */
    status.state = state;
    status.handle = atomic_read(&handle);
    status.result = result;
    status.nr_end = v_cnt;
    status.src_err = serr;
    status.dst_err = derr;

     /* unlock spin */
    spin_unlock_irqrestore(&spinlock,flags);

    /* copy to user space */
    if(!p_status || copy_to_user((void __user *)p_status, &status, size)){
        _ERR("PAGE fault\n");
        ret = -EFAULT;
        goto exit;
    }

 exit:
    /* semaphore up */
    up(&sema);

    return 0;
}



/*******************************************************************************
 **  file_operations function & structure
 ******************************************************************************/

/* open method */
static int open_method(struct inode *inode, struct file *filep)
{
    _DEBUG("proccess %i (%s) going to open the device (%d:%d)\n", 
           current->pid, current->comm, MAJOR(inode->i_rdev), MINOR(inode->i_rdev));
    return 0;
}

/* release method */
static int release_method(struct inode *inode, struct file *filep)
{
    _DEBUG("proccess %i (%s) going to close the device (%d:%d)\n", 
           current->pid, current->comm, MAJOR(inode->i_rdev), MINOR(inode->i_rdev));
    return 0;
}

/* ioctl method */
static int ioctl_method(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg)
{
    int ret=0;

    _DEBUG("proccess %i (%s) going to ioctl the device (%d:%d)\n", 
           current->pid, current->comm, MAJOR(inode->i_rdev), MINOR(inode->i_rdev));

    /* main proc */
    switch(cmd){
    case P2VERIFY_IOC_START:
        ret=ioc_verify_start(cmd, arg,filp->f_flags & O_NONBLOCK);
        break;
    case P2VERIFY_IOC_DONE:
        ret=ioc_verify_done(cmd, (int)arg, filp->f_flags & O_NONBLOCK);
        break;
    case P2VERIFY_IOC_STOP:
        ret=ioc_verify_stop(cmd,(int)arg);
        break;
    case P2VERIFY_IOC_STATUS:
        ret=ioc_verify_status(cmd,arg);
        break;
    default:
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
    poll_wait(filp, &wait_queue_start, wait);
    poll_wait(filp, &wait_queue_done, wait);

    /* check whether a comparison start can be carried out */
    if(state==ST_P2VERIFY_IDLE)
        mask |= (POLLOUT|POLLWRNORM);

    /* check whether the comparison end was carried out */
    if(state==ST_P2VERIFY_DONE)
        mask |= (POLLIN|POLLRDNORM);

    return mask;
}

/* fops */
static struct file_operations p2verify_fops = {
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
static int __init p2verify_init (void)
{

    int ret=0;

    /* init task */
    if((ret=init_verify_task()))
        goto fail;

    /* spinlock */
    spin_lock_init(&spinlock);

    /* semaphore */
    sema_init(&sema,1);

    /* wait_queue_head_t */
    init_waitqueue_head(&wait_queue_start);
    init_waitqueue_head(&wait_queue_done);

    /* state */
    state = ST_P2VERIFY_IDLE;

    /* set zero to handle */
    atomic_set(&handle, 0);

#ifdef CONFIG_P2VERIFY_PROC
    /* create /proc entry */
    if((ret=create_proc())<0)
        goto fail;
#endif /* CONFIG_P2VERIFY_PROC */

    /* allocate device number */
#ifdef CONFIG_P2VERIFY_FIXED_DEVNUM
    devno = MKDEV(CONFIG_P2VERIFY_FIXED_MAJOR,CONFIG_P2VERIFY_FIXED_MINOR);
    ret = register_chrdev_region(devno, 1, MODULE_NAME);
#else  /* !CONFIG_P2VERIFY_FIXED_DEVNUM */
    ret = alloc_chrdev_region(&devno, 0, 1,MODULE_NAME);
#endif  /* CONFIG_P2VERIFY_FIXED_DEVNUM */
    if(ret<0){
        _ERR("can't allocate device number: retval=%d\n", ret);
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
    cdev->ops = &p2verify_fops;
    cdev->owner = THIS_MODULE;
    ret = cdev_add(cdev,devno,1);
    if(ret<0){
        _ERR("failed to add cdev: retval=%d\n",ret);
        goto fail;
    }

  fail:

    if(ret<0) {

        /* remove cdev */
        if(cdev)
            cdev_del(cdev);

        /* release device number */
        if(devno)
            unregister_chrdev_region(devno,1);

#ifdef CONFIG_P2VERIFY_PROC
        /* Remove proc entry */
        remove_proc();
#endif /* CONFIG_P2VERIFY_PROC */

        /* cleanup verify task */
        cleanup_verify_task();

    }

    return ret;
}
module_init(p2verify_init);

/*
 * @brief   Clean-up
 */
static void __exit p2verify_cleanup (void)
{
    /* remove cdev */
    if(cdev)
        cdev_del(cdev);

    /* release device number */
    if(devno)
        unregister_chrdev_region(devno,1);

#ifdef CONFIG_P2VERIFY_PROC
    /* Remove proc entry */
    remove_proc();
#endif /* CONFIG_P2VERIFY_PROC */

    /* cleanup verify task */
    cleanup_verify_task();

}
module_exit(p2verify_cleanup);

MODULE_AUTHOR("GPL");
MODULE_LICENSE("GPL");
