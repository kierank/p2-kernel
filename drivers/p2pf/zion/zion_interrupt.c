/************************************************************
*
* zion_interrupt.c : Misc Function for Interruption of ZION Driver Framework
*
* $Id: zion_interrupt.c,v 1.1.1.1 2006/02/27 09:20:56 nishikawa Exp $
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
#include <linux/bitops.h>

#include <linux/zion.h>
#include "zion_regs.h"


LIST_HEAD(zion_wait_interrupt_list);
spinlock_t zion_wait_list_lock = SPIN_LOCK_UNLOCKED;

int zion_enable_mbus_interrupt(zion_params_t *zion_params, int bit, zion_event_handler_t handler)
{
  u16 tmp_16;

  if(bit<0 || bit>16)
    {
      return -EINVAL;
    }

  /* Register Event Handler */
  zion_params->interrupt_array[bit]=handler;

  if(bit!=Pciif_Int)
    {
      /* Enable Interrupt */
      tmp_16 = mbus_readw(MBUS_ADDR(zion_params, NEO_Interrupt_Mask_B));
      mbus_writew(tmp_16|(((u16)1)<<bit),MBUS_ADDR(zion_params, NEO_Interrupt_Mask_B));

      /* Just for Assurance */
      tmp_16 = mbus_readw(MBUS_ADDR(zion_params, NEO_Interrupt_Mask_B));
    }

  return 0;
}

int zion_disable_mbus_interrupt(zion_params_t *zion_params, int bit)
{
  u16 tmp_16;

  if(bit<0 || bit>16)
    {
      return -EINVAL;
    }

  if(bit!=Pciif_Int)
    {
      /* Disable Interrupt */
      tmp_16 = mbus_readw(MBUS_ADDR(zion_params, NEO_Interrupt_Mask_B));
      mbus_writew(tmp_16&~(((u16)1)<<bit),MBUS_ADDR(zion_params, NEO_Interrupt_Mask_B));

      /* Just for Assurance */
      tmp_16 = mbus_readw(MBUS_ADDR(zion_params, NEO_Interrupt_Mask_B));
    }

  zion_params->interrupt_array[bit]=NULL;

  return 0;
}

int zion_mbus_int_clear(zion_params_t *zion_params, int bit)
{
  u16 tmp_16;
  
  mbus_writew( ((u16)1)<<bit, MBUS_ADDR(zion_params, NEO_Interrupt_Clear_B) );  

  /* Just for Assurance */
  tmp_16 = mbus_readw( MBUS_ADDR(zion_params, NEO_Interrupt_Clear_B) );  

  return 0;
}

int zion_pci_dma_int_clear(zion_params_t *zion_params, int ch)
{
  pci_write_config_word(zion_params->dev, NEO_PCI_INTERRUPT_STATUS, NEO_DMA_DONE(ch));
  return 0;
}

int zion_backend_pci_int_clear(zion_params_t *zion_params)
{
  pci_write_config_word(zion_params->dev, NEO_PCI_INTERRUPT_STATUS, NEO_BACKEND_INT_REQ);
  return 0;
}

static int survey_interrupt_bits(zion_params_t *zion_params)
{
  int i;
  u8 *bits   = (u8 *)&(zion_params->interrupt_bits);
  u8 *enable = (u8 *)&(zion_params->interrupt_enable);

  for(i=0; i<sizeof(struct ZION_Interrupt_Bits); i++)
    {
      if( (*bits) & (*enable) )
	{
	  return 1;
	}

      bits++;
      enable++;
    }

  return 0;
} 

irqreturn_t int_zion_event(int irq, void *dev_id)
{	
  u16 int_status = 0;
  zion_params_t *zion_params;


  /* Get (First) ZION */
  zion_params = find_zion(0);
  if(zion_params->dev==NULL)
    {
      return IRQ_NONE;
    }

  /* Read PCI Interrupt Status */
  pci_read_config_word(zion_params->dev, NEO_PCI_INTERRUPT_STATUS, &int_status);
  /* Memorize it */
  zion_params->interrupt_bits.PCI_INT |= int_status;

  if(int_status & NEO_BACKEND_INT_REQ)
    {
      u16 mbus_int_status, check_flag;
      int i;

      /* Read Interrupt Status B */
      mbus_int_status = mbus_readw(MBUS_ADDR(zion_params, NEO_Interrupt_Status_B));
      /* Memorize it */
      zion_params->interrupt_bits.NEO_INT |= mbus_int_status;

#ifdef CONFIG_ZCOM
      if(mbus_int_status == (1<< UpInt1))
	{
	  goto CHECK_THREADS;  /* Do Nothing if ZCOM Driver is Supported. */
	}
#endif  /* CONFIG_ZCOM */

      for(i=0; i<16; i++)
	{
	  check_flag = ((u16)1)<<i;

#ifdef CONFIG_ZCOM
	  if(i==UpInt1)
	    {
	      continue;
	    }
#endif  /* CONFIG_ZCOM */

	  if(check_flag & mbus_int_status)
	    {
	      if(zion_params->interrupt_array[i])
		{
		  /* Don't forget to memorize Interrupt Status in each modules */
		  (zion_params->interrupt_array[i])(zion_params, i, irq, dev_id, int_status);
		}
	      else
		{
		  PERROR("Event Handler is Not Registered! (%04X)\n", check_flag);
		}
	    }
	}
      zion_backend_pci_int_clear(zion_params);
    }
  else if (int_status & NEO_DMA_DONE_MASK)
    {
      if(zion_params->interrupt_array[Pciif_Int])
	{
	  (zion_params->interrupt_array[Pciif_Int])(zion_params, Pciif_Int, irq, dev_id, int_status);
	}   
    }

#ifdef CONFIG_ZCOM
 CHECK_THREADS:
#endif  /* CONFIG_ZCOM */

  /** Wake threads up **/
  spin_lock(&zion_wait_list_lock);

  if( waitqueue_active(&(zion_params->zion_wait_queue)) )
    {
      if( survey_interrupt_bits(zion_params) )
	{
	  struct list_head *walk;
	  zion_wait_nameplate_t *nameplate;

	  list_for_each(walk, &zion_wait_interrupt_list)
	    {
	      nameplate = list_entry(walk, zion_wait_nameplate_t, task_list);

	      nameplate->status_flags |= ZION_WAKEUP;
	      memcpy(&(nameplate->interrupt_bits), &(zion_params->interrupt_bits),
		     sizeof(struct ZION_Interrupt_Bits));
	    }

	  memset(&(zion_params->interrupt_bits), 0, sizeof(struct ZION_Interrupt_Bits));
	  wake_up(&(zion_params->zion_wait_queue));
	}
    }
  else
    {
      memset(&(zion_params->interrupt_bits), 0, sizeof(struct ZION_Interrupt_Bits));
    }

  spin_unlock(&zion_wait_list_lock);

  return IRQ_HANDLED;
}

/** Interrupt Waiting Routine **/

static void zion_wait_interrupt_timeout(unsigned long arg)
{
  zion_wait_nameplate_t *nameplate = (zion_wait_nameplate_t *)arg;
  unsigned long flags;

  spin_lock_irqsave(&zion_wait_list_lock, flags);

  nameplate->status_flags |= (ZION_WAKEUP|ZION_TIMEOUT_WAKEUP);

  wake_up(&(nameplate->zion_params->zion_wait_queue));

  spin_unlock_irqrestore(&zion_wait_list_lock, flags);
  
  return;
}

void zion_goto_bed(zion_params_t *zion_params, struct ZION_Interrupt_Bits *zion_interrupt_bits)
{
  zion_wait_nameplate_t nameplate;
  unsigned long flags;

  nameplate.pid = current->pid;
  nameplate.zion_params = zion_params;
  nameplate.status_flags = 0;
  memset(&(nameplate.interrupt_bits),0, sizeof(struct ZION_Interrupt_Bits));
  init_timer(&(nameplate.timer));

  spin_lock_irqsave(&zion_wait_list_lock, flags);
  list_add_tail(&(nameplate.task_list), &zion_wait_interrupt_list);
  spin_unlock_irqrestore(&zion_wait_list_lock, flags);

  nameplate.timer.function = zion_wait_interrupt_timeout;
  nameplate.timer.data = (unsigned long)&nameplate;
  nameplate.timer.expires = jiffies + zion_params->wait_timeout;

  add_timer(&(nameplate.timer));
  
  wait_event( zion_params->zion_wait_queue, (nameplate.status_flags & ZION_WAKEUP) );

  spin_lock_irqsave(&zion_wait_list_lock, flags);
  list_del(&(nameplate.task_list));
  spin_unlock_irqrestore(&zion_wait_list_lock, flags);

  if(!(nameplate.status_flags & ZION_TIMEOUT_WAKEUP))
    {
      del_timer_sync(&(nameplate.timer));
    }

  memcpy(zion_interrupt_bits, &(nameplate.interrupt_bits), sizeof(struct ZION_Interrupt_Bits));

  if(nameplate.status_flags & ZION_FORCE_WAKEUP)
    {
      zion_interrupt_bits->PCI_INT |= ZION_WAKEUP_FORCED;
    }

  if(nameplate.status_flags & ZION_TIMEOUT_WAKEUP)
    {
      zion_interrupt_bits->PCI_INT |= ZION_WAKEUP_TIMEOUT;
    }

  return;
}

void zion_rout_them_up(zion_params_t *zion_params)
{
  unsigned long flags;
  struct list_head *walk;
  zion_wait_nameplate_t *nameplate;

  spin_lock_irqsave(&zion_wait_list_lock, flags);

  list_for_each(walk, &zion_wait_interrupt_list)
    {
      nameplate = list_entry(walk, zion_wait_nameplate_t, task_list);
      nameplate->status_flags |= (ZION_WAKEUP|ZION_FORCE_WAKEUP);
    }

  wake_up(&(zion_params->zion_wait_queue));

  spin_unlock_irqrestore(&zion_wait_list_lock, flags);

  return;
}

void zion_set_enable_bits(zion_params_t *zion_params, struct ZION_Interrupt_Bits *zion_interrupt)
{
  unsigned long flags;

  spin_lock_irqsave(&zion_wait_list_lock, flags);
  memcpy(&(zion_params->interrupt_enable), zion_interrupt, sizeof(struct ZION_Interrupt_Bits));

  spin_unlock_irqrestore(&zion_wait_list_lock, flags);

  return;
}

void zion_get_enable_bits(zion_params_t *zion_params, struct ZION_Interrupt_Bits *zion_interrupt)
{
  unsigned long flags;

  spin_lock_irqsave(&zion_wait_list_lock, flags);

  memcpy(zion_interrupt, &(zion_params->interrupt_enable), sizeof(struct ZION_Interrupt_Bits));

  spin_unlock_irqrestore(&zion_wait_list_lock, flags);

  return;
}

