/**
 *  drivers/net/gianfar_dsas.c
 */
/* $Id: gianfar_dsas.c 5237 2010-02-18 00:25:41Z Noguchi Isao $ */


#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/unistd.h>
#include <linux/slab.h>
#include <linux/interrupt.h>    /* tasklet */
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/timer.h>        /* kernel timer */
#include <linux/semaphore.h>

#include <linux/gfar_user.h>
#include "gianfar_dsas.h"

/**
 *
 */
static unsigned long gfar_dsas_calc_pktnum (struct gfar_dsas *const dsas, const unsigned int duration)
{ 
    unsigned long pkts;
    unsigned int n, idx;
    unsigned long nr = (duration + dsas->interval - 1) / dsas->interval;

    if(nr>=dsas->nr_max_period)
        nr=dsas->nr_max_period;

    for(pkts=0,n=0,idx=dsas->cur_ptr; n<nr; n++){
        pkts += dsas->ring_rx_pktnum_interval[idx];
        if(idx==0)
            idx=dsas->nr_max_period-1;
        else
            idx--;
    }

    return pkts;
}


/**
 *  handler of kernel timer
 */
static void gfar_dsas_timer_handler(unsigned long arg)
{
    struct gfar_dsas *dsas = (struct gfar_dsas *)arg;

    /* check parameters */
    if(NULL==dsas){
        printk(KERN_ERR "%s(%d): invalid parameters\n", __FILE__, __LINE__);
        return;
    }

    /* lock */
    spin_lock(&dsas->lock);

    /* update */
    dsas->ring_rx_pktnum_interval[dsas->cur_ptr++] = dsas->rx_pktnum_interval;
    dsas->rx_pktnum_interval=0;
    if(dsas->cur_ptr >= dsas->nr_max_period)
        dsas->cur_ptr = 0;

    /* unlock */
    spin_unlock(&dsas->lock);

    /* do bottom half (tasklet) */
    tasklet_hi_schedule(&dsas->tasklet);

    /* restart timer */
    dsas->timer.expires = jiffies + dsas->nr_interval;
    add_timer(&dsas->timer);
}

/**
 *  tasklet handler to calucurate rx packet number in duratin for
 *  threshold
 */
static void gfar_dsas_tasklet(unsigned long arg)
{
    struct gfar_dsas *dsas = (struct gfar_dsas *)arg;

    /* lock */
    spin_lock(&dsas->lock);

    /* calcurate rx-packet number got in estimate period */
    dsas->rx_pktnum = gfar_dsas_calc_pktnum(dsas,dsas->estimate_period);

    /* moving state */
    switch(dsas->state){

    case ST_DSAS_NORMAL:        /* normal mode */

        /* running ? */
        if(!dsas->is_running)
            break;

        /* move to block mode */
        if( (dsas->th_pktnum_hwblock>0) && (dsas->rx_pktnum >= dsas->th_pktnum_hwblock)
            && dsas->hwblock){
            printk(KERN_DEBUG "%s : Move to h/w-blocking mode\n",dsas->name);
            (*dsas->hwblock)(1,dsas->data);
            dsas->hwblock_count = dsas->nr_hwblock_period;
            dsas->state = ST_DSAS_HWBLOCK;
        } else if( (dsas->th_pktnum_block>0) && (dsas->rx_pktnum >= dsas->th_pktnum_block) ){
            printk(KERN_DEBUG "%s : Move to blocking mode\n",dsas->name);
            dsas->state = ST_DSAS_BLOCK;
        }

        break;

    case ST_DSAS_BLOCK:        /* block mode */

        if(!dsas->is_running || (dsas->th_pktnum_block<1))
            dsas->state = ST_DSAS_NORMAL;
        else if( (dsas->th_pktnum_hwblock>0) && (dsas->rx_pktnum >= dsas->th_pktnum_hwblock)
            && dsas->hwblock){
            dsas->hwblock_count = dsas->nr_hwblock_period;
            (*dsas->hwblock)(1,dsas->data);
            dsas->state = ST_DSAS_HWBLOCK;
        } else if(dsas->rx_pktnum < dsas->th_pktnum_block){
            if(dsas->nr_moratorium_period>0){
                dsas->state = ST_DSAS_BLOCK_TO_NORMAL;
                dsas->moratorium_count = dsas->nr_moratorium_period;
            }else{
                dsas->state = ST_DSAS_NORMAL;
            }
        }

        if(dsas->state==ST_DSAS_NORMAL)
            printk(KERN_DEBUG "%s : Move to normal mode\n",dsas->name);
        else if(dsas->state==ST_DSAS_BLOCK_TO_NORMAL)
            printk(KERN_DEBUG "%s : In moratorium move to normal mode\n",dsas->name);
        else if(dsas->state==ST_DSAS_HWBLOCK)
            printk(KERN_DEBUG "%s : Move to h/w-blocking mode\n",dsas->name);

        break;

    case ST_DSAS_HWBLOCK:        /* h/w-block mode */

        if(dsas->hwblock_count>0)
            dsas->hwblock_count--;

        if(!dsas->is_running){
            (*dsas->hwblock)(0,dsas->data);
            dsas->state = ST_DSAS_NORMAL;
        } else if( (dsas->hwblock_count==0) || (dsas->th_pktnum_hwblock==0)){
            (*dsas->hwblock)(0,dsas->data);
            dsas->state = ST_DSAS_BLOCK;
        }

        if(dsas->state==ST_DSAS_NORMAL)
            printk(KERN_DEBUG "%s : Move to normal mode\n",dsas->name);
        else if(dsas->state==ST_DSAS_BLOCK)
            printk(KERN_DEBUG "%s : Move to blocking mode\n",dsas->name);

        break;

    case ST_DSAS_BLOCK_TO_NORMAL:        /* moving to normal mode from block mode */

        dsas->moratorium_count--;

        if(!dsas->is_running)
            dsas->state = ST_DSAS_NORMAL;
        else if( (dsas->th_pktnum_hwblock>0) && (dsas->rx_pktnum >= dsas->th_pktnum_hwblock)
            && dsas->hwblock){
            (*dsas->hwblock)(1,dsas->data);
            dsas->hwblock_count = dsas->nr_hwblock_period;
            dsas->state = ST_DSAS_HWBLOCK;
        } else if(dsas->th_pktnum_block<1)
            dsas->state = ST_DSAS_NORMAL;
        else if( (dsas->th_pktnum_normal>0) && (dsas->rx_pktnum < dsas->th_pktnum_normal) )
            dsas->state = ST_DSAS_NORMAL;
        else if(dsas->rx_pktnum >= dsas->th_pktnum_block )
            dsas->state = ST_DSAS_BLOCK;
        else if(dsas->moratorium_count==0)
                dsas->state = ST_DSAS_NORMAL;

        if(dsas->state==ST_DSAS_NORMAL)
            printk(KERN_DEBUG "%s : Move to normal mode\n",dsas->name);
        else if(dsas->state==ST_DSAS_BLOCK)
            printk(KERN_DEBUG "%s : Move to blocking mode\n",dsas->name);
        else if(dsas->state==ST_DSAS_HWBLOCK)
            printk(KERN_DEBUG "%s : Move to h/w-blocking mode\n",dsas->name);

        break;

    }

    /* unlock */
    spin_unlock(&dsas->lock);

}


/* 2009/9/15,  added by panasonic >>>> */
#ifdef CONFIG_GIANFAR_EXTENTION_PROC_FS

/* handler for procfs */
int gfar_dsas_procfunc(char *buff, char **start, off_t offset, int count, int *eof, void *data)
{
    int len=0;
    unsigned long flags;
    struct gfar_dsas *dsas = (struct gfar_dsas *)data;

    down(&dsas->sema);
    spin_lock_irqsave(&dsas->lock,flags);

    len += sprintf(buff+len,
                   "running : %s\n", gfar_dsas_running(dsas)?"YES":"NO");
    
    switch(gfar_dsas_state(dsas)){
    case ST_DSAS_NORMAL:
        len += sprintf(buff+len,
                       "state : %s\n", "normal");
        break;
    case ST_DSAS_BLOCK:
        len += sprintf(buff+len,
                       "state : %s\n", "blocking");
        break;
    case ST_DSAS_HWBLOCK:
        len += sprintf(buff+len,
                       "state : %s\n", "h/w-blocking");
        break;
    case ST_DSAS_BLOCK_TO_NORMAL:
        len += sprintf(buff+len,
                       "state : %s\n", "moving to normal");
        break;
    }

    {
        unsigned long pkt1=gfar_dsas_calc_pktnum(dsas,1000); /* duration: 1 sec */
        unsigned long pkt2=gfar_dsas_calc_pktnum(dsas,2000); /* duration: 2 sec */
        unsigned long pkt5=gfar_dsas_calc_pktnum(dsas,5000); /* duration: 5 sec */
        len += sprintf(buff+len,
                       "rx-packets: %ld(%ld) %ld(%ld) %ld(%ld)\n",
                       pkt1,pkt1,pkt2,pkt2/2,pkt5,pkt5/5);
    }

    len += sprintf(buff+len,
                   "rx-packets for check: %ld\n",
                   gfar_dsas_rxpkts(dsas));

    spin_unlock_irqrestore(&dsas->lock,flags);
    up(&dsas->sema);



    *eof=1;
    return len;
} 

#endif  /* CONFIG_GIANFAR_EXTENTION_PROC_FS */
/* <<<< 2009/9/15,  added by panasonic */


/**
 *  set parameters for DSAS
 */
int gfar_dsas_set_param(struct gfar_dsas *const dsas, const struct gfar_dsas_param * const param)
{
    int retval = 0;
    unsigned long flags;

    /* check parameters */
    if(NULL==dsas||NULL==param){
        printk(KERN_ERR "%s(%d): NULL parameters\n", __FILE__, __LINE__);
        retval = -EINVAL;
        goto fail;
    }
    if( (param->estimate_period < dsas->interval)
        || (param->th_pktnum_hwblock < param->th_pktnum_block)
        || (param->th_pktnum_block < param->th_pktnum_normal)
        || (param->moratorium_period < dsas->interval)
        || (param->hwblock_period < dsas->interval)
        ){
        printk(KERN_ERR "%s(%d): invalid params "
               ": estimate_period=%d,th_pktnum_block=%ld, th_pktnum_normal=%ld, moratorium_period=%d\n",
               __FILE__, __LINE__,
               param->estimate_period, param->th_pktnum_block, param->th_pktnum_normal, param->moratorium_period);
        retval = -EINVAL;
        goto fail;
    }

    /* down sema */
    if(down_interruptible(&dsas->sema)){
        retval = -ERESTARTSYS;
        goto fail;
    }

    /* lock */
    spin_lock_irqsave(&dsas->lock,flags);

    /* initilize members */
    dsas->th_pktnum_block = param->th_pktnum_block;
    dsas->th_pktnum_hwblock = param->th_pktnum_hwblock;
    dsas->th_pktnum_normal = param->th_pktnum_normal;
    dsas->moratorium_period = param->moratorium_period;
    dsas->nr_moratorium_period = ((dsas->moratorium_period + dsas->interval - 1) / dsas->interval);
    dsas->hwblock_period = param->hwblock_period;
    dsas->nr_hwblock_period = ((dsas->hwblock_period + dsas->interval - 1) / dsas->interval);
    dsas->estimate_period = param->estimate_period;
    dsas->nr_estimate_period = ((dsas->estimate_period + dsas->interval - 1) / dsas->interval);

    /* unlock */
    spin_unlock_irqrestore(&dsas->lock,flags);

     /* up sema */
    up(&dsas->sema);

fail:

    return retval;
}

/**
 *  get parameters for DSAS
 */
int gfar_dsas_get_param(struct gfar_dsas *const dsas, struct gfar_dsas_param * const param)
{
    int retval = 0;
    unsigned long flags;

    /* check parameters */
    if(NULL==dsas||NULL==param){
        printk(KERN_ERR "%s(%d): NULL parameters\n", __FILE__, __LINE__);
        retval = -EINVAL;
        goto fail;
    }

    /* down sema */
    if(down_interruptible(&dsas->sema)){
        retval = -ERESTARTSYS;
        goto fail;
    }

    /* lock */
    spin_lock_irqsave(&dsas->lock,flags);

    /* initilize members */
    param->th_pktnum_block      = dsas->th_pktnum_block;
    param->th_pktnum_hwblock    = dsas->th_pktnum_hwblock;
    param->th_pktnum_normal     = dsas->th_pktnum_normal;
    param->moratorium_period    = dsas->moratorium_period;
    param->hwblock_period       = dsas->hwblock_period;
    param->estimate_period      = dsas->estimate_period;

    /* unlock */
    spin_unlock_irqrestore(&dsas->lock,flags);

     /* up sema */
    up(&dsas->sema);

fail:

    return retval;
}

/**
 *  start
 */
int gfar_dsas_start(struct gfar_dsas *const dsas)
{
    int retval = 0;
    int i;
    unsigned long flags;

    /* check parameters */
    if(NULL==dsas){
        printk(KERN_ERR "%s(%d): NULL parameters\n", __FILE__, __LINE__);
        retval = -EINVAL;
        goto fail;
    }

    /* down sema */
    if(down_interruptible(&dsas->sema)){
        retval = -ERESTARTSYS;
        goto fail;
    }

    /* lock */
    spin_lock_irqsave(&dsas->lock,flags);

    /* initilize members */
    dsas->cur_ptr = 0;
    dsas->rx_pktnum_interval = 0;
    for (i=0; i<dsas->nr_max_period; i++)
        dsas->ring_rx_pktnum_interval[i]=0;

    /* start now */
    dsas->state = ST_DSAS_NORMAL;
    dsas->is_running = 1;

    /* unlock */
    spin_unlock_irqrestore(&dsas->lock,flags);

    /* start timer handler */
    dsas->timer.expires = jiffies + dsas->nr_interval;
    add_timer(&dsas->timer);

     /* up sema */
    up(&dsas->sema);

fail:

    return retval;
}

/**
 *  stop
 */
int gfar_dsas_stop(struct gfar_dsas *const dsas)
{
    int retval = 0;
    unsigned long flags;

    /* check parameters */
    if(NULL==dsas){
        printk(KERN_ERR "%s(%d): invalid parameters\n", __FILE__, __LINE__);
        retval = -EINVAL;
        goto fail;
    }

    /* stop H/W blocking rx-packets */
    if(dsas->hwblock)
        (*dsas->hwblock)(0,dsas->data);

    /* down sema */
    if(down_interruptible(&dsas->sema)){
        retval = -ERESTARTSYS;
        goto fail;
    }

    /* stop timer handler */
    del_timer_sync(&dsas->timer);

    /* tasklet */
    tasklet_kill(&dsas->tasklet);

    /* lock */
    spin_lock_irqsave(&dsas->lock,flags);

    /* change flag to stop */
    dsas->state = ST_DSAS_NORMAL;
    dsas->is_running = 0;

    /* unlock */
    spin_unlock_irqrestore(&dsas->lock,flags);

    /* up sema */
    up(&dsas->sema);

 fail:

    return retval;
}


void gfar_dsas_countup(struct gfar_dsas *const dsas)
{
    unsigned long flags=0;

    if(in_interrupt()){
        spin_lock(&dsas->lock);
    } else {
        down(&dsas->sema);
        spin_lock_irqsave(&dsas->lock,flags);
    }

    dsas->rx_pktnum_interval++;

    if(in_interrupt()){
        spin_unlock(&dsas->lock);
    } else {
        spin_unlock_irqrestore(&dsas->lock,flags);
        up(&dsas->sema);
    }
}

int gfar_dsas_info(struct gfar_dsas *const dsas, struct gfar_dsas_info *const info)
{
    int retval=0;
    unsigned long flags=0;

    /* down sema */
    if(down_interruptible(&dsas->sema)){
        retval = -ERESTARTSYS;
        goto fail;
    }

    /* lock */
    spin_lock_irqsave(&dsas->lock,flags);

    /* get information */
    info->is_running = dsas->is_running;
    info->state = dsas->state;
    info->rx_pktnum = dsas->rx_pktnum;;

    /* unlock */
    spin_unlock_irqrestore(&dsas->lock,flags);

    /* up sema */
    up(&dsas->sema);


 fail:
    return retval;
}

int gfar_dsas_init(struct gfar_dsas *const dsas, const char * const name, unsigned long data)
{
    int retval = 0;

    /* check parameters */
    if(NULL==dsas){
        printk(KERN_ERR "%s(%d): invalid parameters\n", __FILE__, __LINE__);
        retval = -EINVAL;
        goto fail;
    }

    /* zero clear */
    memset((void*)dsas, 0, sizeof(struct gfar_dsas));

    /* private data */
    dsas->data = data;

    /* name */
    strncpy(dsas->name,name,GFAR_DSAS_SZ_NAME-1);

    /* tasklet */
    tasklet_init(&dsas->tasklet,gfar_dsas_tasklet,(unsigned long)dsas);

    /* timer */
    init_timer(&dsas->timer);
    dsas->timer.data = (unsigned long)dsas;
    dsas->timer.function = gfar_dsas_timer_handler;

    /* spinlock */
	spin_lock_init(&dsas->lock);

    /* semaphore */
    sema_init(&dsas->sema,1);

    /* stoping now */
    dsas->is_running = 0;

    /* config parameters */
    dsas->interval = DEFAULT_DSAS_IVAL;
    dsas->nr_interval = ((HZ * dsas->interval) / 1000);
    dsas->max_period = DEFAULT_DSAS_MAX_MEASURE_PERIOD;
    dsas->nr_max_period = ((dsas->max_period + dsas->interval - 1) / dsas->interval);
    dsas->estimate_period = DEFAULT_DSAS_ESTIMATE_PERIOD;
    dsas->nr_estimate_period = ((dsas->estimate_period + dsas->interval - 1) / dsas->interval);
    dsas->hwblock_period = DEFAULT_DSAS_HWBLOCK_PERIOD;
    dsas->nr_hwblock_period = ((dsas->hwblock_period + dsas->interval - 1) / dsas->interval);
    

    /* allocation */
    dsas->ring_rx_pktnum_interval
        = (unsigned long *)kmalloc(sizeof(unsigned long) * dsas->nr_max_period, GFP_KERNEL);
    if(NULL==dsas->ring_rx_pktnum_interval){
        printk(KERN_ERR "%s(%d): Could not allocate ring_rx_pktnum_interval\n", 
               __FILE__, __LINE__);
        retval = -EINVAL;
        goto fail;
    }
    memset((void*)dsas->ring_rx_pktnum_interval,0,sizeof(unsigned long) * dsas->nr_max_period);

 fail:

    if(retval && NULL!=dsas){

        /* deallocate */
        if(NULL!=dsas->ring_rx_pktnum_interval)
            kfree(dsas->ring_rx_pktnum_interval);
        dsas->ring_rx_pktnum_interval = NULL;

    }

    return retval;
}


int gfar_dsas_exit(struct gfar_dsas *const dsas)
{
    int retval = 0;

    /* check parameters */
    if(NULL==dsas){
        printk(KERN_DEBUG "%s(%d): invalid parameters\n", __FILE__, __LINE__);
        retval = -EINVAL;
        goto fail;
    }

    /* stop if running */
    if(dsas->is_running)
        gfar_dsas_stop(dsas);

    /* dealocate */
    if(NULL!=dsas->ring_rx_pktnum_interval){
        kfree(dsas->ring_rx_pktnum_interval);
        dsas->ring_rx_pktnum_interval = NULL;
    }

 fail:

    return retval;
}

