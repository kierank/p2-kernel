/************************************************************
*
* pci.c : ZION DVCIF Driver
*
* $Id: dvcif.c,v 1.1.1.1 2006/02/27 09:20:56 nishikawa Exp $
*
************************************************************/

#define NEO_DEBUG
#define NEO_ERROR

#include <linux/autoconf.h>

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/fcntl.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/uaccess.h>

#include <linux/pci.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/stddef.h>
#include <linux/version.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/proc_fs.h>

#include <linux/vmalloc.h>

#include <linux/zion.h>
#include "zion_dvcif_regs.h"

#ifdef CONFIG_ZION_DMAIF
#warning You can NOT compile ZION driver including both DMAIF and DVCIF !
#error You must choose DMAIF or DVCIF
#endif

int zion_dvcif_open(zion_params_t *zion_params, struct inode *inode, struct file *filp)
{
  zion_dvcif_wait_info_t *wait_info;

  filp->private_data = kmalloc(sizeof(zion_dvcif_wait_info_t),GFP_KERNEL);
  if(!(filp->private_data))
    {
      PERROR("Getting Private Data Area Failed.\n");
      return -ENOMEM;
    }

  wait_info = (zion_dvcif_wait_info_t *)(filp->private_data);

  wait_info->condition = ZION_DVCIF_NORMAL;
  wait_info->int_status = 0;

  return 0;
}

int zion_dvcif_release(zion_params_t *zion_params, struct inode *inode, struct file *filp)
{
  kfree(filp->private_data);

  return 0;
}

unsigned int zion_dvcif_poll(zion_params_t *zion_params,
			     struct file *filp,
			     struct poll_table_struct *pts)
{
  int zion_minor = MINOR(filp->f_dentry->d_inode->i_rdev);
  unsigned long irq_flags;
  spinlock_t *queue_lock;
  wait_queue_head_t *wait_queue;
  zion_dvcif_wait_info_t *wait_info;

  queue_lock = &(DVCIF_PARAM(zion_params, zion_minor)->queue_lock);
  wait_queue = &(DVCIF_PARAM(zion_params, zion_minor)->zion_dvcif_wait_queue);
  wait_info = (zion_dvcif_wait_info_t *)filp->private_data;

  spin_lock_irqsave(queue_lock, irq_flags);

  if(wait_info->condition == ZION_DVCIF_AWAKE)
    {
      wait_info->condition = ZION_DVCIF_NORMAL;

      list_del(&(wait_info->zion_wait_threads));

      spin_unlock_irqrestore(queue_lock, irq_flags);

      return (POLLIN|POLLRDNORM);
    }

  poll_wait(filp, wait_queue, pts);

  if(wait_info->condition == ZION_DVCIF_NORMAL)
    {
      wait_info->condition = ZION_DVCIF_SLEEP;
      list_add_tail(&(wait_info->zion_wait_threads),
		    &(DVCIF_PARAM(zion_params, zion_minor)->zion_wait_threads));
    }

  spin_unlock_irqrestore(queue_lock, irq_flags);

  return 0;
}

static u16 zion_dvcif_wait_interruption(zion_params_t *zion_params,
					struct inode *inode, struct file *filp, int minor)
{
  zion_dvcif_wait_info_t wait_info;
  spinlock_t *queue_lock;
  unsigned long irq_flags;
  struct list_head *info_queue = &(DVCIF_PARAM(zion_params, minor)->zion_wait_threads);

  queue_lock = &(DVCIF_PARAM(zion_params, minor)->queue_lock); 
  wait_info.condition = ZION_DVCIF_SLEEP;

  spin_lock_irqsave(queue_lock, irq_flags);
  list_add_tail(&(wait_info.zion_wait_threads), info_queue);
  spin_unlock_irqrestore(queue_lock, irq_flags);

  wait_event(DVCIF_PARAM(zion_params, minor)->zion_dvcif_wait_queue,
	     wait_info.condition != ZION_DVCIF_SLEEP);

  spin_lock_irqsave(queue_lock, irq_flags);
  list_del(&(wait_info.zion_wait_threads));
  spin_unlock_irqrestore(queue_lock, irq_flags);

  if (wait_info.condition == ZION_DVCIF_FORCE_AWAKE)
    {
      return 0;
    }

  return wait_info.int_status;
}

static int zion_dvcif_wakeup_minor(zion_dvcif_params_t *dvcif_params, int condition, u16 stat)
{
  unsigned long irq_flags;
  struct list_head *walk;

  spin_lock_irqsave(&(dvcif_params->queue_lock), irq_flags);

  list_for_each(walk, &(dvcif_params->zion_wait_threads))
    {
      zion_dvcif_wait_info_t *wait_info;

      wait_info = list_entry(walk, zion_dvcif_wait_info_t, zion_wait_threads);

      wait_info->condition = condition;
      wait_info->int_status = stat;
    }

  wake_up(&(dvcif_params->zion_dvcif_wait_queue));

  spin_unlock_irqrestore(&(dvcif_params->queue_lock), irq_flags);

  return 0;
}

int zion_dvcif_ioctl(zion_params_t *zion_params,
		     struct inode *inode, struct file *filp,
		     unsigned int function, unsigned long arg)
{
  int zion_minor = MINOR(inode->i_rdev);
  u16 ret_stat; 

  switch(function)
    {
    case ZION_DVCIF_WAIT_INTERRUPTION:
      {
	ret_stat = zion_dvcif_wait_interruption(zion_params, inode, filp, zion_minor);
	if(copy_to_user((void *)arg, (void *)&ret_stat, sizeof(u16)))
		return -EFAULT;
	return 0;
      }
    case ZION_DVCIF_WAKEUP:
      {
	return zion_dvcif_wakeup_minor(DVCIF_PARAM(zion_params, zion_minor), ZION_DVCIF_FORCE_AWAKE, 0);
      }
    case  ZION_DVCIF_GET_INTERRUPT_STAT:
      {
	if(copy_to_user((void *)arg,
			(void *)&(((zion_dvcif_wait_info_t *)filp->private_data)->int_status), sizeof(u16)))
		return -EFAULT;
	((zion_dvcif_wait_info_t *)filp->private_data)->int_status = 0;
	return 0;
      }
    default:
      return -EINVAL;
    }

  return 0;
}

struct zion_file_operations zion_dvcif_fops = {
  ioctl : zion_dvcif_ioctl,
  open : zion_dvcif_open,
  release : zion_dvcif_release,
};

void zion_dvcif_event(zion_params_t *zion_params, int bit,
		      int irq, void *dev_id, u16 pci_status)
{
  u16 int_status;

  /* Read Interrupt Status */
  int_status = mbus_readw(MBUS_ADDR(zion_params, DV_Interrupt_Status));
  /* Memorize it ! */
  zion_params->interrupt_bits.DVC_INT |= int_status;

  if(int_status & RecFrame)
    {
      zion_dvcif_wakeup_minor(DVCIF_PARAM(zion_params, ZION_DVCIF+ZION_DVCIF_REC), ZION_DVCIF_AWAKE, RecFrame);

      mbus_writew(RecFrame, MBUS_ADDR(zion_params, DV_Interrupt_Clear));
      int_status &= ~RecFrame;
    }

  if(int_status & PbFrame)
    {
      zion_dvcif_wakeup_minor(DVCIF_PARAM(zion_params, ZION_DVCIF+ZION_DVCIF_PB), ZION_DVCIF_AWAKE, PbFrame);

      mbus_writew(PbFrame, MBUS_ADDR(zion_params, DV_Interrupt_Clear));
      int_status &= ~PbFrame;
    }

  if(int_status)
    {
      zion_dvcif_wakeup_minor(DVCIF_PARAM(zion_params, ZION_DVCIF+ZION_DVCIF_GENERIC),
			      ZION_DVCIF_AWAKE, int_status);

      mbus_writew(int_status, MBUS_ADDR(zion_params, DV_Interrupt_Clear));
    }

  zion_mbus_int_clear(zion_params, Dvcif_Int);

  return;
}

static void initialize_zion_dvcif_private_space(zion_dvcif_params_t *dvcif_params)
{
  memset((void *)dvcif_params,0,sizeof(zion_dvcif_params_t));

  init_waitqueue_head(&(dvcif_params->zion_dvcif_wait_queue));
  spin_lock_init(&(dvcif_params->queue_lock));
  INIT_LIST_HEAD(&(dvcif_params->zion_wait_threads));

  return;
}

static void free_zion_dvcif_private_space(zion_dvcif_params_t *dvcif_params)
{
  if(dvcif_params==NULL)
    {
      return;
    }

  zion_dvcif_wakeup_minor(dvcif_params, ZION_DVCIF_FORCE_AWAKE, 0);

  kfree((void *)dvcif_params);

  /* wait until all threads awake ?? */

  return;
}

static void zion_dvcif_enable_interrupt(zion_params_t *zion_params)
{
  u16 int_status;

  int_status = PbFrame | RecFrame;

  mbus_writew(int_status, MBUS_ADDR(zion_params, DV_Interrupt_Mask));

  return;
}

int init_zion_dvcif(void)
{
  zion_params_t *zion_params;
  int ret,i;

  /* get ZION parameters */
  zion_params = find_zion(0);
  if(zion_params == NULL)
    {
      return -ENODEV;
    }

  /* Check Fireball */
  if((zion_params->revision & 0xF000) == 0xF000)
    {
      PINFO("This is fireball! ZION DVCIF cannot be used!!\n");
      return 0;
    }

  /* register fuctions for operation */
  for(i=0; i<ZION_DVCIF_PORTS; i++)
    {
      zion_params->zion_operations[ZION_DVCIF+i]=&zion_dvcif_fops; 
    }

  /* Set Private Data Area */
  for(i=0; i<ZION_DVCIF_PORTS; i++)
    {
	//don't use macro for gcc.
//      DVCIF_PARAM(zion_params, ZION_DVCIF+i) = kmalloc(sizeof(zion_dvcif_params_t),GFP_KERNEL);
	zion_params->zion_private[ZION_DVCIF+i] = kmalloc(sizeof(zion_dvcif_params_t),GFP_KERNEL);

      if(DVCIF_PARAM(zion_params,ZION_DVCIF+i)==NULL)
	{
	  PERROR("Can't get enough space for private data.\n");
	  return -ENOMEM;
	}

      initialize_zion_dvcif_private_space(DVCIF_PARAM(zion_params, ZION_DVCIF+i));
    }

  /* enable interruption */
  zion_dvcif_enable_interrupt(zion_params);
  ret = zion_enable_mbus_interrupt(zion_params, Dvcif_Int, zion_dvcif_event);
  if(ret)
    {
      PERROR("registering interruption failed.\n");
      return -EINVAL;
    }

  PINFO("ZION DVCIF module Installed.\n");

  return 0;
}

void exit_zion_dvcif(void)
{
  zion_params_t *zion_params;
  int i;

  /* get ZION parameters */
  zion_params = find_zion(0);
  if(zion_params == NULL)
    {
      return;
    }

  /* Check Fireball */
  if((zion_params->revision & 0xF000) == 0xF000)
    {
      return;
    }

  zion_disable_mbus_interrupt(zion_params, Dvcif_Int);

  for(i=0; i<ZION_DVCIF_PORTS; i++)
    {
      free_zion_dvcif_private_space(DVCIF_PARAM(zion_params, ZION_DVCIF+i));
    }

  return;
}
