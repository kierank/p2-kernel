/************************************************************
*
* pci.c : ZION PCI Driver
*
* $Id: direct_bus_access.c,v 1.1.1.1 2006/02/27 09:20:56 nishikawa Exp $
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
//#include <asm/segment.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/irq.h>

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
#include "zion_pci_regs.h"


#ifndef CONFIG_ZION_SUPPRESS_MASTER_ACTION

static int neo_dma_prepare_bus_direct(zion_params_t *params, int ch, int cmd, unsigned long offset_addr)
{
  u16 tmp_16 = 0;
  unsigned long current_jiffies;
  void *chain = (void *)ZION_PCI_PARAM(params)->dma_params[ch].dma_chain;

  /* add check for DMA Run */
  pci_read_config_word(params->dev, NEO_PCI_DMA_COMMAND(ch),&tmp_16);
  current_jiffies = jiffies;
  while(tmp_16 & NEO_DMA_RUN)
    {
      interruptible_sleep_on_timeout(&(ZION_PCI_PARAM(params)->zion_pci_wait_queue),(100*HZ/1000));
	  if(current_jiffies + 2*HZ < jiffies)
	    {
	      PERROR("Timeout : DMA Busy (for 2 seconds)\n");
	      return -ETIME;
	    }
	  pci_read_config_word(params->dev, NEO_PCI_DMA_COMMAND(ch),&tmp_16);
    }
  
  /* set source address */
  pci_write_config_dword
    (params->dev, NEO_PCI_DMA_BUFFER_RW_POINTER(ch),
     (params->whole_sdram_addr) + offset_addr);
  
  /* set chain address */
  pci_write_config_dword
    (params->dev, NEO_PCI_DMA_CHAIN_ADDRESS(ch), virt_to_bus(chain));
  
  /* Clear DONE Status */
  pci_write_config_word(params->dev, NEO_PCI_INTERRUPT_STATUS, NEO_DMA_DONE(ch));
 
  /* Set Time out */
  ZION_PCI_PARAM(params)->dma_params[ch].condition = ZION_PCI_INT_DISPATCH_PENDING;  
  init_timer(&(ZION_PCI_PARAM(params)->dma_params[ch].timer));
  ZION_PCI_PARAM(params)->dma_params[ch].timer.function = neo_dma_timeout;
  ZION_PCI_PARAM(params)->dma_params[ch].timer.data = (unsigned long)params;
  
  return 0;
}

static int add_sg_table_bus_direct
(zion_params_t *params, int ch, void *space, unsigned long entry_size, unsigned long net_size)
{
  int entries = ZION_PCI_PARAM(params)->dma_params[ch].entries;
  neo_dma_t *chain = (neo_dma_t *)(ZION_PCI_PARAM(params)->dma_params[ch].dma_chain);
  neo_dma_ent_t *dma_entries = ZION_PCI_PARAM(params)->dma_params[ch].dma_entries;

  if(entries >= NEO_MAX_ENTRIES)
    {
      return -ENOSPC;
    }

  if( (net_size>DMA_MAX_ENTRY_SIZE) || (entry_size < net_size) || (net_size%4) )
    {
      PERROR("Invalid Size.\n");
      return -EINVAL;
    }
  
  if(chain==NULL)
    {
      PERROR("Invalid SG Table.\n");
      return -EINVAL;
    }

  (chain[entries]).distination = (u32)space;
  (chain[entries]).length = net_size;
  (chain[entries]).flags = DMA_CHANGE_CH;

  dma_entries[entries].data = space;
  dma_entries[entries].size = entry_size;
  
  (ZION_PCI_PARAM(params)->dma_params[ch].entries)++;

  return 0;
}

static int release_dma_entry_bus_direct(zion_params_t *params, int ch)
{
  ZION_PCI_PARAM(params)->dma_params[ch].entries = 0;

  up(&(ZION_PCI_PARAM(params)->dma_params[ch].dma_sem));

  return 0;
}

static int make_sg_table_bus_direct(zion_params_t *params, int ch, unsigned long *size, void *mem_space)
{
  unsigned long left_size = *size;
  unsigned long net_size;
  unsigned long entry_size=DMA_MAX_ENTRY_SIZE;
  int ret=0;

  while(left_size)
    {
      /* Size you use */
      net_size = min(entry_size, left_size);

      /* add the space to SG table */
      ret = add_sg_table_bus_direct(params, ch, mem_space, entry_size, net_size);
      if(ret<0)
	{
	  ret = 0;
	  if(ret!=-ENOSPC)
	    {
	      size =0;
	      left_size=0;
	    }
	  break;
	}

      left_size -= net_size;
      mem_space += net_size;
    }

  /* Terminate SG table */
  terminate_sg_table(params, ch);

  *size = left_size;

  return ret;
}

static unsigned long neo_sdram_dma_write_bus_direct
(zion_params_t *params, unsigned long offset_addr, void *buf, unsigned long size, int ch)
{
  unsigned long left_size;
  unsigned long upper = 0, lower = 0;
  int ret;

  /* check DWORD Alignment */
  if( (offset_addr%4)||(size%4) )
    {
      PERROR("Invalid Size or Address.\n");
      return 0;
    }

  /* check Setting of DMA Region */
  neo_get_region(params, ch, &lower, &upper);

  if(lower >= upper)
    {
      PERROR("No Region Specified.\n");
      return 0;
    }

  /* check size of area and size of IO */
  if(lower+offset_addr+size > upper)
    {
      size = upper - (lower + offset_addr);
    }

  if(size<=0)
    {
      return 0;
    }

  left_size = size;

  /* Initalize SG table etc. */
  init_dma_ch(params,ch);

 DMA_WRITE:

  ret = make_sg_table_bus_direct(params, ch, &left_size, buf);

  if(ret)
    {
      size=0;
      left_size = 0;
      goto WRITE_RELEASE;
    }

  /* Set Registers and Set Timeout */
  neo_dma_prepare_bus_direct(params, ch, ZION_DMA_WRITE, (lower+offset_addr) );

  /* WBack Cache */
//  dma_cache_wback(bus_to_virt((unsigned long)buf),size);
  dma_cache_sync(&params->dev->dev, bus_to_virt((unsigned long)buf),size, DMA_TO_DEVICE);

  disable_irq(params->dev->irq);

  /* DMA Run */
  pci_write_config_word(params->dev, NEO_PCI_DMA_COMMAND(ch), 
			NEO_IO_DERECTION_WRITE|NEO_DMA_RUN|NEO_DMA_OPEN);
  
  ZION_PCI_PARAM(params)->dma_params[ch].timer.expires = jiffies + NEO_DMA_TIMEOUT;  
  add_timer(&(ZION_PCI_PARAM(params)->dma_params[ch].timer));

  enable_irq(params->dev->irq);

  /* Sleep */
  wait_event(ZION_PCI_PARAM(params)->dma_params[ch].neo_dma_wait_queue,
	     ZION_PCI_PARAM(params)->dma_params[ch].condition != ZION_PCI_INT_DISPATCH_PENDING);
  
  /* DMA End */
  if(ZION_PCI_PARAM(params)->dma_params[ch].condition == ZION_PCI_INT_DISPATCH_TIMEOUT)
    {
      PERROR("NEO DMA Write Timeout.\n");
      size = 0;
      left_size = 0;
      goto WRITE_RELEASE;
    }

 WRITE_RELEASE:

    release_dma_entry_bus_direct(params, ch);

  if(left_size)
    {
//      ((u8 *)buf) += (size-left_size);
      buf += (size-left_size);
      goto  DMA_WRITE;
    }

  return size;
}

static unsigned long neo_sdram_dma_read_bus_direct
(zion_params_t *params, unsigned long offset_addr, void *buf, unsigned long size, int ch)
{
  unsigned long left_size;
  unsigned long entry_size;
  unsigned long upper = 0, lower = 0;
  int ret;

  /* check DWORD Alignment */
  if( (offset_addr%4)||(size%4) )
    {
      PERROR("Invalid Size or Address.\n");
      return 0;
    }

  /* check Setting of DMA Region */
  neo_get_region(params, ch, &lower, &upper);

  if(lower >= upper)
    {
      PERROR("No Region Specified.\n");
      return 0;
    }

  /* check size of area and size of IO */
  if(lower+offset_addr+size > upper)
    {
      size = upper - (lower + offset_addr);
    }

  if(size<=0)
    {
      return 0;
    }

  /* size to be read */
  left_size = size;

  /* Initalize SG table etc. */
  init_dma_ch(params,ch);

 DMA_READ:

  entry_size = DMA_MAX_ENTRY_SIZE;

  ret = make_sg_table_bus_direct(params, ch, &left_size, buf);

  if(ret)
    {
      size=0;
      left_size = 0;
      goto READ_RELEASE;
    }

  /* Set Registers and Set Timeout */
  neo_dma_prepare_bus_direct(params, ch, ZION_DMA_READ, (lower+offset_addr));

  /* Inv Cache */
//  dma_cache_inv(bus_to_virt((unsigned long)buf), size);
  dma_cache_sync(&params->dev->dev, bus_to_virt((unsigned long)buf), size, DMA_FROM_DEVICE);

  disable_irq(params->dev->irq);

  /* DMA Run */
  pci_write_config_word(params->dev, NEO_PCI_DMA_COMMAND(ch), 
			NEO_IO_DERECTION_READ|NEO_DMA_RUN|NEO_DMA_OPEN);

  ZION_PCI_PARAM(params)->dma_params[ch].timer.expires = jiffies + NEO_DMA_TIMEOUT;  
  add_timer(&(ZION_PCI_PARAM(params)->dma_params[ch].timer));

  enable_irq(params->dev->irq);
  
  /* Sleep */
  wait_event(ZION_PCI_PARAM(params)->dma_params[ch].neo_dma_wait_queue,
	     ZION_PCI_PARAM(params)->dma_params[ch].condition != ZION_PCI_INT_DISPATCH_PENDING);
  
  /* DMA End */
  if(ZION_PCI_PARAM(params)->dma_params[ch].condition == ZION_PCI_INT_DISPATCH_TIMEOUT)
    {
      PERROR("ZION DMA READ Timeout.\n");
      size = 0;
      left_size = 0;
      goto READ_RELEASE;
    }

 READ_RELEASE:

  release_dma_entry_bus_direct(params, ch);

  if(left_size)
    {
      //((u8 *)buf) += (size-left_size);
      buf += (size-left_size);
      goto  DMA_READ;
    }

  return size;
}

#endif /* CONFIG_ZION_SUPPRESS_MASTER_ACTION */

int zion_pci_ioctl_for_debug(zion_params_t *zion_params,
 struct inode *inode, struct file *filp,
 unsigned int function, unsigned long arg)
{
  switch(function)
    {
#ifndef CONFIG_ZION_SUPPRESS_MASTER_ACTION

      struct zion_buf zion_buf;

    case ZION_SDRAM_DMA_READ_BUS_ADDR:

      if(copy_from_user((void *)&zion_buf, (void *)arg, sizeof(struct zion_buf)))
	return -EFAULT;

      if((zion_buf.size = neo_sdram_dma_read_bus_direct
	  (zion_params, zion_buf.addr, zion_buf.buf, zion_buf.size, zion_buf.dma_ch))==0)
	return -EINVAL;

      if(copy_to_user((void *)arg, (void *)&zion_buf, sizeof(struct zion_buf)))
	return -EFAULT;

      break;

    case ZION_SDRAM_DMA_WRITE_BUS_ADDR:

      if(copy_from_user((void *)&zion_buf, (void *)arg, sizeof(struct zion_buf)))
	return -EFAULT;

      if((zion_buf.size = neo_sdram_dma_write_bus_direct
	  (zion_params, zion_buf.addr, zion_buf.buf, zion_buf.size, zion_buf.dma_ch))==0)
	return -EINVAL;

      if(copy_to_user((void *)arg, (void *)&zion_buf, sizeof(struct zion_buf)))
	return -EFAULT;

      break;

#endif /* CONFIG_ZION_SUPPRESS_MASTER_ACTION */

    default:
      PERROR("No such IOCTL.\n");      
      return -EINVAL;
    }

  return 0;
}
