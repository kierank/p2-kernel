/************************************************************
*
* pci.c : ZION PCI Driver
*
* $Id: zion_pci.c,v 1.1.1.1 2006/02/27 09:20:56 nishikawa Exp $
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

#include <asm/irq.h>

#include <linux/vmalloc.h>

#include <linux/zion.h>
#include "zion_pci_regs.h"


int ZION_pci_cache_clear(void)
{
  zion_params_t *zion_params;
  int timeout_cntr=0;
  u16 tmp_16;

  zion_params = find_zion(0);
  if(zion_params == NULL){
	return -ENODEV;
  }

  /* Cache Clear */
  mbus_writew(NEO_CACHE_CLR,MBUS_ADDR(zion_params,NEO_MBUS_CACHE_CONTROL));
  tmp_16 = mbus_readw(MBUS_ADDR(zion_params,NEO_MBUS_CACHE_CONTROL));
  while(tmp_16 & NEO_CACHE_CLR)
    {
      timeout_cntr++;
      if(timeout_cntr>100)
	{
	  PERROR("Line Cache Reset Failed.\n");
	  return -EBUSY;
	}
      udelay(100);
      tmp_16 = mbus_readw(MBUS_ADDR(zion_params,NEO_MBUS_CACHE_CONTROL));
    }

  return 0;
}

int ZION_check_addr_and_pci_cache_clear(unsigned long addr)
{
  zion_params_t *zion_params;
  u32 base_addr = 0;
  u32 size = 0;
  int ret_val = 0;

  zion_params = find_zion(0);
  if(zion_params == NULL){
    return -ENODEV;
  }

  /* Get SDRAM base address and size */
  base_addr = zion_params->whole_sdram_addr;
  size = zion_params->whole_sdram_size;

  /* Check address and clear PCI cache */
  if( (base_addr <= addr) && (addr < base_addr+size) ){
      ret_val = ZION_pci_cache_clear();
  }

  return ret_val;
}

int ZION_pci_line_reset(void)
{
  zion_params_t *zion_params;
  int timeout_cntr=0;
  u16 tmp_16;

  zion_params = find_zion(0);
  if(zion_params == NULL){
	return -ENODEV;
  }

  /* Cache Line Control */
  mbus_writew(NEO_LINE_RST,MBUS_ADDR(zion_params,NEO_MBUS_CACHE_CONTROL));
  tmp_16 = mbus_readw(MBUS_ADDR(zion_params,NEO_MBUS_CACHE_CONTROL));
  while(tmp_16 & NEO_LINE_RST)
    {
      timeout_cntr++;
      if(timeout_cntr>100)
	{
	  PERROR("Line Cache Reset Failed.\n");
	  return -EBUSY;
	}
      udelay(100);
      tmp_16 = mbus_readw(MBUS_ADDR(zion_params,NEO_MBUS_CACHE_CONTROL));
    }

  return 0;
}

#ifndef CONFIG_ZION_SUPPRESS_MASTER_ACTION

int neo_get_region
(zion_params_t *params, int dma_ch, unsigned long *lower, unsigned long *upper)
{
  if(dma_ch >= NEO_DMA_CH)
    {
      PERROR("Invalid DMA Channel.\n");
      return -EINVAL;
    }

  *lower = mbus_readl(MBUS_ADDR(params, NEO_MBUS_BUFFER_LOWER_ADDRESS(dma_ch)));

  *upper = mbus_readl(MBUS_ADDR(params, NEO_MBUS_BUFFER_UPPER_ADDRESS(dma_ch)));

  return 0;
}

int neo_set_region
(zion_params_t *params, int dma_ch, unsigned long lower, unsigned long upper)
{
  unsigned long lower_assurance = 0, upper_assurance = 0;

  if(lower%2 || upper%2 || (upper > params->whole_sdram_size) )
    {
      PERROR("Invalid Area.\n");
      return -EINVAL;
    }

  if(dma_ch >= NEO_DMA_CH)
    {
      PERROR("Invalid DMA Channel.\n");
      return -EINVAL;
    }

  mbus_writel(lower, MBUS_ADDR(params, NEO_MBUS_BUFFER_LOWER_ADDRESS(dma_ch)));

  mbus_writel(upper, MBUS_ADDR(params, NEO_MBUS_BUFFER_UPPER_ADDRESS(dma_ch)));

  /* Just for assurance */
  neo_get_region(params, dma_ch, &lower_assurance, &upper_assurance);

  return 0;
}

void neo_dma_timeout(unsigned long neo_params)
{
  zion_params_t *params;
  int ch;
  u16 tmp_16 = 0;

  params = (zion_params_t *)neo_params;
  
  PERROR("Timeout Occured. Terminating all transfers...\n");

  for(ch=0; ch<NEO_DMA_CH; ch++)
    {
      /* Clear RUN Status */
      pci_read_config_word(params->dev, NEO_PCI_DMA_COMMAND(ch),&tmp_16);
      pci_write_config_word(params->dev, NEO_PCI_DMA_COMMAND(ch), tmp_16 & ~NEO_DMA_RUN);
      /* Clear DONE Status */
      pci_read_config_word(params->dev, NEO_PCI_INTERRUPT_STATUS, &tmp_16);
      pci_write_config_word(params->dev, NEO_PCI_INTERRUPT_STATUS,
			    tmp_16 & ~(NEO_DMA_DONE(ch)));

      ZION_PCI_PARAM(params)->dma_params[ch].condition = ZION_PCI_INT_DISPATCH_TIMEOUT;
      wake_up(&(ZION_PCI_PARAM(params)->dma_params[ch].neo_dma_wait_queue));
    }

  return;
}


/* functions for DMA */

static int neo_copy_to_user(zion_params_t *params, int ch, void *data)
{
  neo_dma_t *chain = (neo_dma_t *)(ZION_PCI_PARAM(params)->dma_params[ch].dma_chain);
  neo_dma_ent_t *dma_entries = ZION_PCI_PARAM(params)->dma_params[ch].dma_entries;
  int entries = ZION_PCI_PARAM(params)->dma_params[ch].entries;
  int i;
  u8 *buf = (u8 *)data;

  for(i=0; i<entries; i++)
    {
		u16 length = le16_to_cpu( chain[i].length );
      if(copy_to_user((void *)buf, (const void *)(dma_entries[i].data), length))
	return -EIO;
      buf += length;
    }

  return 0;
}

static int neo_copy_from_user(zion_params_t *params, int ch, void *data)
{
  neo_dma_t *chain = (neo_dma_t *)(ZION_PCI_PARAM(params)->dma_params[ch].dma_chain);
  neo_dma_ent_t *dma_entries = ZION_PCI_PARAM(params)->dma_params[ch].dma_entries;
  int entries = ZION_PCI_PARAM(params)->dma_params[ch].entries;
  int i;
  u8 *buf = (u8 *)data;

  for(i=0; i<entries; i++)
    {
		u16 length = le16_to_cpu( chain[i].length );
      if(copy_from_user((void *)(dma_entries[i].data), (void *)buf, length))
	return -EIO;
      buf += length;
    }

  return 0;
}

static int neo_copy_from_fb(zion_params_t *params, int ch, void *data)
{
  neo_dma_t *chain = (neo_dma_t *)(ZION_PCI_PARAM(params)->dma_params[ch].dma_chain);
  neo_dma_ent_t *dma_entries = ZION_PCI_PARAM(params)->dma_params[ch].dma_entries;
  int entries = ZION_PCI_PARAM(params)->dma_params[ch].entries;
  int i;
  u8 *buf = (u8 *)data;

  for(i=0; i<entries; i++)
    {
		u16 length = le16_to_cpu( chain[i].length );
		memcpy((void *)(dma_entries[i].data), (void *)buf, length);
		buf += length;
    }

  return 0;
}

int init_dma_ch(zion_params_t *params, int ch)
{
  down(&(ZION_PCI_PARAM(params)->dma_params[ch].dma_sem));

  memset((void *)(ZION_PCI_PARAM(params)->dma_params[ch].dma_entries) , 0, sizeof(neo_dma_ent_t)*NEO_MAX_ENTRIES);
  memset((void *)(ZION_PCI_PARAM(params)->dma_params[ch].dma_chain) , 0, PAGE_SIZE);
  ZION_PCI_PARAM(params)->dma_params[ch].entries = 0;

  return 0;
}

static int neo_dma_prepare(zion_params_t *params, int ch, int cmd, unsigned long offset_addr)
{
  u16 tmp_16 = 0;
  unsigned long current_jiffies;
  void *chain = (void *)ZION_PCI_PARAM(params)->dma_params[ch].dma_chain;
  int i;
  int entries = ZION_PCI_PARAM(params)->dma_params[ch].entries;
  neo_dma_ent_t *dma_entries = ZION_PCI_PARAM(params)->dma_params[ch].dma_entries;

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
 
  /* WBack/Inv Cache Memory */
  for(i=0; i<entries; i++)
    {
      if(cmd==ZION_DMA_WRITE)
	{
//	  dma_cache_wback(dma_entries[i].data, dma_entries[i].size);
	  dma_cache_sync(&params->dev->dev, dma_entries[i].data, dma_entries[i].size, DMA_TO_DEVICE);
	}
      else if(cmd==ZION_DMA_READ)
	{
//	  dma_cache_inv(dma_entries[i].data, dma_entries[i].size);	  
	  dma_cache_sync(&params->dev->dev, dma_entries[i].data, dma_entries[i].size, DMA_FROM_DEVICE);
	}
      else
	{
	  PERROR("Invalid Command.\n");
	  return -EINVAL;
	}
    }

  /* Set Time out */
  ZION_PCI_PARAM(params)->dma_params[ch].condition = ZION_PCI_INT_DISPATCH_PENDING;  
  init_timer(&(ZION_PCI_PARAM(params)->dma_params[ch].timer));
  ZION_PCI_PARAM(params)->dma_params[ch].timer.function = neo_dma_timeout;
  ZION_PCI_PARAM(params)->dma_params[ch].timer.data = (unsigned long)params;
    
  return 0;
}

static void *get_dma_space(unsigned long *entry_size)
{
  void *ptr;

  /* *entry_size = pow(2,n) ? */
  if( ((*entry_size)&((*entry_size)-1)) || (*entry_size > DMA_MAX_ENTRY_SIZE))
    {
      PERROR("Invalid Size.\n");
      (*entry_size)=0;
      return NULL;
    }

 RETRY:

  ptr = (void *)__get_dma_pages(GFP_KERNEL, get_order(*entry_size));
  if(ptr!=NULL)
    {
      return ptr;
    }

  if(*entry_size>=PAGE_SIZE)
    {
      (*entry_size)>>=1;
      goto RETRY;
    }

  *entry_size=0;

  return NULL;
}

static int add_sg_table
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

  (chain[entries]).distination = cpu_to_le32( virt_to_bus(space) );
  (chain[entries]).length = cpu_to_le16( net_size );
  (chain[entries]).flags = cpu_to_le16( DMA_CHANGE_CH );

  dma_entries[entries].data = space;
  dma_entries[entries].size = entry_size;
  
  (ZION_PCI_PARAM(params)->dma_params[ch].entries)++;

  return 0;
}

int terminate_sg_table(zion_params_t *params, int ch)
{
  int entries = ZION_PCI_PARAM(params)->dma_params[ch].entries;
  neo_dma_t *chain = (neo_dma_t *)(ZION_PCI_PARAM(params)->dma_params[ch].dma_chain);


  if(entries==0)
    {
      PERROR("IO with Invalid SG Table.\n");
      return -EINVAL;
    }

  if(chain==NULL)
    {
      PERROR("Invalid SG Table Space.\n");
      return -EINVAL;
    }

  chain[entries-1].flags |= cpu_to_le16( (DMA_CHAIN_END | DMA_CHANGE_CH) );

  //dma_cache_wback(chain, PAGE_SIZE);
  dma_cache_sync(&params->dev->dev, chain, PAGE_SIZE, DMA_TO_DEVICE);

  return 0;
}

static int release_dma_entry(zion_params_t *params, int ch)
{
  int i;
  int entries = ZION_PCI_PARAM(params)->dma_params[ch].entries;
  neo_dma_ent_t *dma_entries = ZION_PCI_PARAM(params)->dma_params[ch].dma_entries;

  for(i=0; i<entries; i++)
    {
      free_pages((unsigned long)(dma_entries[i].data), get_order(dma_entries[i].size));
    }

  ZION_PCI_PARAM(params)->dma_params[ch].entries = 0;

  up(&(ZION_PCI_PARAM(params)->dma_params[ch].dma_sem));

  return 0;
}

static int make_sg_table(zion_params_t *params, int ch, unsigned long *size)
{
  unsigned long left_size = *size;
  unsigned long net_size;
  unsigned long entry_size=DMA_MAX_ENTRY_SIZE;
  void *mem_space;
  int ret=0;

  while(left_size)
    {
      /* get memory space for DMA Buffer */
      mem_space = get_dma_space(&entry_size);
      if(mem_space==NULL)
	{
	  PERROR("Memory Full.\n");
	  ret = -ENOMEM;
	  break;
	}

      /* Size you use */
      net_size = min(entry_size, left_size);

      /* add the space to SG table */
      ret = add_sg_table(params, ch, mem_space, entry_size, net_size);
      if(ret<0)
	{
	  free_pages((unsigned long)mem_space, get_order(entry_size));
	  ret = 0;
	  if(ret!=-ENOSPC)
	    {
	      size =0;
	      left_size=0;
	    }
	  break;
	}

      left_size -= net_size;
    }

  /* Terminate SG table */
  terminate_sg_table(params, ch);

  *size = left_size;

  return ret;
}

unsigned long neo_sdram_dma_write
(zion_params_t *params, unsigned long offset_addr, const char *data, unsigned long size, int ch, int sw)
{
  unsigned long left_size;
  unsigned long upper = 0, lower = 0;
  int ret;
  u8 *buf = (u8 *)data;

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

  ret = make_sg_table(params, ch, &left_size);

  if(ret)
    {
      size=0;
      left_size = 0;
      goto WRITE_RELEASE;
    }

  if(ZION_PCI_DMA_USER == sw)
	  neo_copy_from_user(params, ch, buf);
  else
	  neo_copy_from_fb(params, ch, buf);

  /* Set Registers and Set Timeout */
  neo_dma_prepare(params, ch, ZION_DMA_WRITE, (lower+offset_addr) );

  disable_irq(params->dev->irq);

  /* DMA Run */
  pci_write_config_word(params->dev, NEO_PCI_DMA_COMMAND(ch), 
			NEO_IO_DERECTION_WRITE|NEO_DMA_RUN|NEO_DMA_OPEN);

  /* Run Timer */
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

    release_dma_entry(params, ch);

  if(left_size)
    {
      buf += (size-left_size);
      goto  DMA_WRITE;
    }

  return size;
}

static unsigned long neo_sdram_dma_read
(zion_params_t *params, unsigned long offset_addr, void *data, unsigned long size, int ch)
{
  unsigned long left_size;
  unsigned long entry_size;
  unsigned long upper, lower;
  int ret;
  u8 *buf = (u8 *)data;

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

  ret = make_sg_table(params, ch, &left_size);

  if(ret)
    {
      size=0;
      left_size = 0;
      goto READ_RELEASE;
    }

  /* Set Registers and Set Timeout */
  neo_dma_prepare(params, ch, ZION_DMA_READ, (lower+offset_addr));

  disable_irq(params->dev->irq);

  /* DMA Run */
  pci_write_config_word(params->dev, NEO_PCI_DMA_COMMAND(ch), 
			NEO_IO_DERECTION_READ|NEO_DMA_RUN|NEO_DMA_OPEN);

  /* Run Timer */
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
/*
      size = 0;
      left_size = 0;
      goto READ_RELEASE;
*/
    }

  /* Copy data to User Space */
  neo_copy_to_user(params, ch, buf);

 READ_RELEASE:

  release_dma_entry(params, ch);

  if(left_size)
    {
      buf += (size-left_size);
      goto  DMA_READ;
    }

  return size;
}

#else /* CONFIG_ZION_SUPPRESS_MASTER_ACTION */

int neo_set_region
(zion_params_t *params, int dma_ch, unsigned long lower, unsigned long upper)
{
  return -EPERM;
}

int neo_get_region
(zion_params_t *params, int dma_ch, unsigned long *lower, unsigned long *upper)
{
  return -EPERM;
}

unsigned long neo_sdram_dma_write
(zion_params_t *params, unsigned long offset_addr, const char *buf, unsigned long size, int ch, int sw)
{
  return -EPERM;
}

static unsigned long neo_sdram_dma_read
(zion_params_t *params, unsigned long offset_addr, void *buf, unsigned long size, int ch)
{
  return -EPERM;
}

#endif /* CONFIG_ZION_SUPPRESS_MASTER_ACTION */

static unsigned long neo_sdram_pio_read
(zion_params_t *params, unsigned long offset_addr, void *buf, unsigned long size)
{
  void *data;
  unsigned long scope_top;
  unsigned long scope_bottom;
  unsigned long buf_current, sdram_current;
  unsigned long left_size;
  unsigned long object_size;
  unsigned long scope_size = params->partial_sdram_size; 
  unsigned long scope_offset;   /* SDRAM BASE ADDRESS */

  if( (offset_addr+size) > (params->whole_sdram_size) )
    {
      size = params->whole_sdram_size - offset_addr;
    }

  if( (offset_addr%4) || (size%4) )
    {
      PERROR("Bad offset or size.\n");
      return 0;
    }

  if(size<=0)
    {
      return 0;
    }

  data = vmalloc(size);
  if(data==NULL)
    {
      PERROR("vmalloc failed.\n");
      return 0;
    }

  buf_current = (unsigned long)data;
  sdram_current = offset_addr;
  left_size = size;

  while(left_size)
    {
      unsigned long skipped_scope;
      unsigned long  ret;

      scope_offset = sdram_current / SDRAM_BASE_GRADUATION;

      scope_top = scope_offset * SDRAM_BASE_GRADUATION;
      scope_bottom = scope_top + scope_size;

      if(scope_bottom > params->whole_sdram_size)
	{
	  scope_bottom = params->whole_sdram_size;

	  scope_top = scope_bottom - scope_size;
	  if(scope_top % SDRAM_BASE_GRADUATION)
	    {
	      PERROR("Unexpected Error!\n");
              vfree(data);
	      return -EINVAL;
	    }

	  scope_offset = scope_top / SDRAM_BASE_GRADUATION; 
	}

      object_size = scope_bottom - sdram_current;
      object_size = min(object_size, left_size);

      skipped_scope = sdram_current - scope_top;

      down(&(ZION_PCI_PARAM(params)->zion_sdram_semaphore));

      pci_write_config_word(params->dev, NEO_PCI_SDRAM_BASE_ADDRESS, (u16)scope_offset);
  
      ret = memcpy_fromio_dword
	      ((void *)buf_current, SDRAM_PARTIAL_ADDR(params,skipped_scope), object_size);

      up(&(ZION_PCI_PARAM(params)->zion_sdram_semaphore));

      if(!ret)
	{
	  vfree(data);
	  return 0;
	}

      buf_current += object_size;
      left_size -= object_size;
      sdram_current = scope_bottom;      
    }

  if(copy_to_user((void *)buf, (const void *)data, size))
    {
      vfree(data);
      return 0;
    }

  vfree(data);

  return size;
}

static unsigned long neo_sdram_pio_write
(zion_params_t *params, unsigned long offset_addr, void *buf, unsigned long size)
{
  void *data;
  unsigned long scope_top;
  unsigned long scope_bottom;
  unsigned long buf_current, sdram_current;
  unsigned long left_size;
  unsigned long object_size;
  unsigned long scope_size = params->partial_sdram_size; 
  unsigned long scope_offset;   /* SDRAM BASE ADDRESS */

  if( (offset_addr+size) > (params->whole_sdram_size) )
    {
      size = params->whole_sdram_size - offset_addr;
    }

  if( (offset_addr%4) || (size%4) )
    {
      PERROR("Bad offset or size.\n");
      return 0;
    }

  if(size<=0)
    {
      return 0;
    }

  data = vmalloc(size);
  if(data==NULL)
    {
      PERROR("vmalloc failed.\n");
      return 0;
    }

  if(copy_from_user((void *)data, (void *)buf, size))
    {
      vfree(data);
      return 0;
    }

  buf_current = (unsigned long)data;
  sdram_current = offset_addr;
  left_size = size;

  while(left_size)
    {
      unsigned long skipped_scope;
      unsigned long  ret;

      scope_offset = sdram_current / SDRAM_BASE_GRADUATION;

      scope_top = scope_offset * SDRAM_BASE_GRADUATION;
      scope_bottom = scope_top + scope_size;

      if(scope_bottom > params->whole_sdram_size)
	{
	  scope_bottom = params->whole_sdram_size;

	  scope_top = scope_bottom - scope_size;
	  if(scope_top % SDRAM_BASE_GRADUATION)
	    {
	      PERROR("Unexpected Error!\n");
              vfree(data);
	      return -EINVAL;
	    }

	  scope_offset = scope_top / SDRAM_BASE_GRADUATION; 
	}

      object_size = scope_bottom - sdram_current;
      object_size = min(object_size, left_size);

      skipped_scope = sdram_current - scope_top;

      down(&(ZION_PCI_PARAM(params)->zion_sdram_semaphore));

      pci_write_config_word(params->dev, NEO_PCI_SDRAM_BASE_ADDRESS, (u16)scope_offset);
  
      ret = memcpy_toio_dword
	      (SDRAM_PARTIAL_ADDR(params,skipped_scope), (void *)buf_current, object_size);

      up(&(ZION_PCI_PARAM(params)->zion_sdram_semaphore));

      if(!ret)
	{
	  vfree(data);
	  return 0;
	}

      buf_current += object_size;
      left_size -= object_size;
      sdram_current = scope_bottom;      
    }

  vfree(data);

  return size;
}

int zion_pci_ioctl(zion_params_t *zion_params,
 struct inode *inode, struct file *filp,
 unsigned int function, unsigned long arg)
{
  struct zion_config_byte zion_config_byte;
  struct zion_config_word zion_config_word;
  struct zion_config_dword zion_config_dword;
  struct zion_sdram_region zion_sdram_region;
  struct zion_buf zion_buf;
  unsigned long bus_addr;
  unsigned long sdram_size;

  switch(function)
    {
    case ZION_PCI_READ_CONFIG_BYTE:

      if(copy_from_user((void *)&zion_config_byte, (void *)arg, sizeof(struct zion_config_byte)))
	return -EFAULT;

      pci_read_config_byte(zion_params->dev, zion_config_byte.where, &(zion_config_byte.val));

      if(copy_to_user((void *)arg, (void *)&zion_config_byte, sizeof(struct zion_config_byte)))
	return -EFAULT;

      break;
      
    case ZION_PCI_READ_CONFIG_WORD:

      if(copy_from_user((void *)&zion_config_word, (void *)arg, sizeof(struct zion_config_word)))
	return -EFAULT;

      pci_read_config_word(zion_params->dev, zion_config_word.where, &(zion_config_word.val));

      if(copy_to_user((void *)arg, (void *)&zion_config_word, sizeof(struct zion_config_word)))
	return -EFAULT;

      break;
      
    case ZION_PCI_READ_CONFIG_DWORD:

      if(copy_from_user((void *)&zion_config_dword, (void *)arg, sizeof(struct zion_config_dword)))
	return -EFAULT;

      pci_read_config_dword(zion_params->dev, zion_config_dword.where, &(zion_config_dword.val));

      if(copy_to_user((void *)arg, (void *)&zion_config_dword, sizeof(struct zion_config_dword)))
	return -EFAULT;

      break;
      
    case ZION_PCI_WRITE_CONFIG_BYTE:

      if(copy_from_user((void *)&zion_config_byte, (void *)arg, sizeof(struct zion_config_byte)))
	return -EFAULT;

      pci_write_config_byte(zion_params->dev, zion_config_byte.where, zion_config_byte.val);

      if(copy_to_user((void *)arg, (void *)&zion_config_byte, sizeof(struct zion_config_byte)))
	return -EFAULT;

      break;
      
    case ZION_PCI_WRITE_CONFIG_WORD:

      if(copy_from_user((void *)&zion_config_word, (void *)arg, sizeof(struct zion_config_word)))
	return -EFAULT;

      pci_write_config_word(zion_params->dev, zion_config_word.where, zion_config_word.val);

      if(copy_to_user((void *)arg, (void *)&zion_config_word, sizeof(struct zion_config_word)))
	return -EFAULT;

      break;
      
    case ZION_PCI_WRITE_CONFIG_DWORD:

      if(copy_from_user((void *)&zion_config_dword, (void *)arg, sizeof(struct zion_config_dword)))
	return -EFAULT;

      pci_write_config_dword(zion_params->dev, zion_config_dword.where, zion_config_dword.val);

      if(copy_to_user((void *)arg, (void *)&zion_config_dword, sizeof(struct zion_config_dword)))
	return -EFAULT;

      break;

    case ZION_SDRAM_PIO_READ:

      if(copy_from_user((void *)&zion_buf, (void *)arg, sizeof(struct zion_buf)))
	return -EFAULT;

      if((zion_buf.size = neo_sdram_pio_read(zion_params, zion_buf.addr, zion_buf.buf, zion_buf.size))==0)
	return -EINVAL;

      if(copy_to_user((void *)arg, (void *)&zion_buf, sizeof(struct zion_buf)))
	return -EFAULT;

      break;

    case ZION_SDRAM_PIO_WRITE:

      if(copy_from_user((void *)&zion_buf, (void *)arg, sizeof(struct zion_buf)))
	return -EFAULT;

      if((zion_buf.size = neo_sdram_pio_write(zion_params, zion_buf.addr, zion_buf.buf, zion_buf.size))==0)
	return -EINVAL;

      if(copy_to_user((void *)arg, (void *)&zion_buf, sizeof(struct zion_buf)))
	return -EFAULT;

      break;

    case ZION_SDRAM_DMA_READ:

      if(copy_from_user((void *)&zion_buf, (void *)arg, sizeof(struct zion_buf)))
	return -EFAULT;

      if((zion_buf.size = neo_sdram_dma_read(zion_params, zion_buf.addr, zion_buf.buf, zion_buf.size, zion_buf.dma_ch))==0)
	return -EINVAL;

      if(copy_to_user((void *)arg, (void *)&zion_buf, sizeof(struct zion_buf)))
	return -EFAULT;

      break;

    case ZION_SDRAM_DMA_WRITE:

      if(copy_from_user((void *)&zion_buf, (void *)arg, sizeof(struct zion_buf)))
	return -EFAULT;

      if((zion_buf.size = neo_sdram_dma_write(zion_params, zion_buf.addr, zion_buf.buf, zion_buf.size, zion_buf.dma_ch, ZION_PCI_DMA_USER))==0)
	return -EINVAL;

      if(copy_to_user((void *)arg, (void *)&zion_buf, sizeof(struct zion_buf)))
	return -EFAULT;

      break;

    case ZION_SET_DMA_REGION:

      if(copy_from_user((void *)&zion_sdram_region, (void *)arg, sizeof(struct zion_sdram_region)))
	return -EFAULT;

      return neo_set_region(zion_params, zion_sdram_region.dma_ch,
			  zion_sdram_region.lower,zion_sdram_region.upper);

    case ZION_GET_DMA_REGION:
      if(copy_from_user((void *)&zion_sdram_region, (void *)arg, sizeof(struct zion_sdram_region)))
	return -EFAULT;

      if(neo_get_region(zion_params, zion_sdram_region.dma_ch,
			  &zion_sdram_region.lower, &zion_sdram_region.upper))
	return -EINVAL;

      if(copy_to_user((void *)arg, (void *)&zion_sdram_region, sizeof(struct zion_sdram_region)))
	return -EFAULT;

      break;

    case ZION_GET_SDRAM_BUS_ADDR:
      bus_addr = (unsigned long)(zion_params->whole_sdram_addr);
      if(copy_to_user((void *)arg, (void *)&bus_addr, sizeof(unsigned long)))
	{
	  return -EFAULT;
	}
      return 0;

    case ZION_GET_SDRAM_SIZE:
      sdram_size = (unsigned long)(zion_params->whole_sdram_size);
      if(copy_to_user((void *)arg, (void *)&sdram_size, sizeof(unsigned long)))
	{
	  return -EFAULT;
	}
      return 0;

    case ZION_PCI_RESET:

      return ZION_pci_cache_clear();

    default:

      return zion_pci_ioctl_for_debug(zion_params, inode, filp, function, arg);
    }

  return 0;
}

void zion_pci_event(zion_params_t *zion_params, int bit, int irq, void *dev_id, u16 int_status)
{
  int ch;

  for(ch=0; ch<NEO_DMA_CH; ch++)
    {
      if(int_status & NEO_DMA_DONE(ch))
	{
	  u16 tmp_16;

	  del_timer_sync(&(ZION_PCI_PARAM(zion_params)->dma_params[ch].timer));

	  /* Clear RUN Status */
	  pci_read_config_word(zion_params->dev, NEO_PCI_DMA_COMMAND(ch),&tmp_16);
	  pci_write_config_word(zion_params->dev, NEO_PCI_DMA_COMMAND(ch), tmp_16 & ~NEO_DMA_RUN);

	  /* Clear DONE Status */
	  zion_pci_dma_int_clear(zion_params, ch);

	  /* Terminate DMA */
	  pci_read_config_word(zion_params->dev, NEO_PCI_DMA_COMMAND(ch),&tmp_16);
	  pci_write_config_word(zion_params->dev, NEO_PCI_DMA_COMMAND(ch), tmp_16 & ~NEO_DMA_OPEN);

	  ZION_PCI_PARAM(zion_params)->dma_params[ch].condition = ZION_PCI_INT_DISPATCH_DONE;

	  wake_up(&(ZION_PCI_PARAM(zion_params)->dma_params[ch].neo_dma_wait_queue));
	}
    }

  return;
}

static loff_t zion_pci_llseek(zion_params_t *zion_params, struct file *filp, loff_t offset, int origin)
{
  long long retval;

  switch(origin)
    {
    case 2:
      offset += zion_params->whole_sdram_size;
      break;
    case 1:
      offset += filp->f_pos;
    }

  retval = -EINVAL;

  if( (offset>=0) && (offset<=zion_params->whole_sdram_size) )
    {
      if(offset != filp->f_pos)
	{
	  filp->f_pos = offset;
	}
      retval = offset;
    }

  return retval;
}

int zion_pci_open(zion_params_t *zion_params, struct inode *inode, struct file *filp)
{
  return 0;
}

int zion_pci_release(zion_params_t *zion_params, struct inode *inode, struct file *filp)
{
  return 0;
}

static ssize_t zion_pci_write(zion_params_t *zion_params, struct file *filp, const char *buf, size_t count, loff_t *offset)
{
  ssize_t ret;

  ret = neo_sdram_pio_write(zion_params, *offset, (void *)buf, count);
  if(ret>0)
    {
      *offset += ret;
    }

  return ret;
}

static ssize_t zion_pci_read(zion_params_t *zion_params, struct file *filp, char *buf, size_t count, loff_t *offset)
{
  ssize_t ret;

  ret = neo_sdram_pio_read(zion_params, *offset, (void *)buf, count);
  if(ret>0)
    {
      *offset += ret;
    }

  return ret;
}

static ssize_t zion_pci_port_write(zion_params_t *zion_params, struct file *filp, const char *buf, size_t count, loff_t *offset)
{
  ssize_t ret;
  int zion_minor = MINOR(filp->f_dentry->d_inode->i_rdev);
  int ch;

  ch = zion_minor - ZION_PCI_PORT_OFFSET;

  ret = neo_sdram_dma_write(zion_params, *offset, buf, count, ch, ZION_PCI_DMA_USER);
  if(ret>0)
    {
      *offset += ret;
    }

  return ret;
}

static ssize_t zion_pci_port_read(zion_params_t *zion_params, struct file *filp, char *buf, size_t count, loff_t *offset)
{
  ssize_t ret;
  int zion_minor = MINOR(filp->f_dentry->d_inode->i_rdev);
  int ch;

  ch = zion_minor - ZION_PCI_PORT_OFFSET;

  ret = neo_sdram_dma_read(zion_params, *offset, (void *)buf, count, ch);
  if(ret>0)
    {
      *offset += ret;
    }

  return ret;
}

int zion_pci_port_ioctl(zion_params_t *zion_params,
 struct inode *inode, struct file *filp,
 unsigned int function, unsigned long arg)
{
  int zion_minor = MINOR(filp->f_dentry->d_inode->i_rdev);
  int ch;
  struct zion_sdram_region zion_sdram_region;

  ch = zion_minor - ZION_PCI_PORT_OFFSET;

  switch(function)
    {
    case ZION_SET_DMA_REGION:

      if(copy_from_user((void *)&zion_sdram_region, (void *)arg, sizeof(struct zion_sdram_region)))
	return -EFAULT;

      return neo_set_region(zion_params, ch,
			  zion_sdram_region.lower,zion_sdram_region.upper);

    case ZION_GET_DMA_REGION:

      if(copy_from_user((void *)&zion_sdram_region, (void *)arg, sizeof(struct zion_sdram_region)))
	return -EFAULT;

      zion_sdram_region.dma_ch = ch;

      if(neo_get_region(zion_params, ch,
			  &zion_sdram_region.lower, &zion_sdram_region.upper))
	return -EINVAL;

      if(copy_to_user((void *)arg, (void *)&zion_sdram_region, sizeof(struct zion_sdram_region)))
	return -EFAULT;

      return 0;

    }

  return -EINVAL;
}

struct zion_file_operations zion_pci_fops = {
  ioctl : zion_pci_ioctl,
  open : zion_pci_open,
  release : zion_pci_release,
  llseek : zion_pci_llseek,
  write : zion_pci_write,
  read : zion_pci_read,
};

struct zion_file_operations zion_pci_port_fops = {
  open : zion_pci_open,
  release : zion_pci_release,
  write : zion_pci_port_write,
  read : zion_pci_port_read,
  llseek : zion_pci_llseek,
  ioctl : zion_pci_port_ioctl,
};

static int initialize_zion_pci_private_space(zion_pci_params_t *zion_pci_params)
{
  int counter;

  memset((void *)zion_pci_params,0,sizeof(zion_pci_params_t));

  /* Initialize Private DATA */
  init_waitqueue_head(&(zion_pci_params->zion_pci_wait_queue));
  init_MUTEX(&(zion_pci_params->zion_sdram_semaphore));

  /* Initialize DATA on DMA */

  /* Get Spece for DMA Chain Table */
  for(counter=0; counter< NEO_DMA_CH; counter++)
    {
      zion_pci_params->dma_params[counter].dma_chain=(void *)kmalloc(PAGE_SIZE, GFP_KERNEL);

      if(zion_pci_params->dma_params[counter].dma_chain==NULL)
	{
	  int i;
	  for(i=0; i<counter; i++)
	    {
	      kfree(zion_pci_params->dma_params[i].dma_chain);
	    }
	  PERROR("Failed to get enough space for DMA chain table.\n");
	  return -ENOMEM;
	}

      init_waitqueue_head(&(zion_pci_params->dma_params[counter].neo_dma_wait_queue));
      init_MUTEX(&(zion_pci_params->dma_params[counter].dma_sem));
    }

  return 0;
}

static void free_zion_pci_private_space(zion_pci_params_t *zion_pci_params)
{
  int counter;

  if(zion_pci_params==NULL)
    {
      return;
    }

  for(counter=0; counter< NEO_DMA_CH; counter++)
    {
      if(zion_pci_params->dma_params[counter].dma_chain!=NULL)
	kfree((void *)(zion_pci_params->dma_params[counter].dma_chain));
    }

  kfree((void *)zion_pci_params);

  return;
}

int init_zion_pci(void)
{
  zion_params_t *zion_params;
  int ret;

  zion_params = find_zion(0);
  if(zion_params == NULL)
    {
      return -ENODEV;
    }

#ifndef CONFIG_ZION_SUPPRESS_MASTER_ACTION
  /* Bus Master Enable */
  {
    u16 tmp_16 = 0;

    pci_read_config_word(zion_params->dev, NEO_PCI_COMMAND, &tmp_16);
    pci_write_config_word(zion_params->dev, NEO_PCI_COMMAND,(tmp_16|NEO_BUS_MASTER_ENABLE));
  }
#endif  /* CONFIG_ZION_SUPPRESS_MASTER_ACTION */

  pci_write_config_byte(zion_params->dev, NEO_PCI_LATENCY_TIMER, NEO_LATENCY);

  ZION_pci_line_reset();

  /* register fuctions for operation */
  zion_params->zion_operations[ZION_PCI]=&zion_pci_fops;
#ifndef CONFIG_ZION_SUPPRESS_MASTER_ACTION
  {
    int i;

    for(i=0; i<NEO_DMA_CH; i++)
      {
	zion_params->zion_operations[ZION_PCI_PORT_OFFSET+i]=&zion_pci_port_fops;
      }
  }
#endif

  /* Set Private Data Area */
  zion_params->zion_private[ZION_PCI] = kmalloc(sizeof(zion_pci_params_t),GFP_KERNEL);

  if(ZION_PCI_PARAM(zion_params)==NULL)
    {
      PERROR("Can't get enough space for private data.\n");
      return -ENOMEM;
    }

  ret = initialize_zion_pci_private_space(ZION_PCI_PARAM(zion_params));
  if(ret)
    {
      return ret;
    }

  /* enable interruption */
  ret = zion_enable_mbus_interrupt(zion_params, Pciif_Int, zion_pci_event);
  if(ret)
    {
      PERROR("registering interruption failed.\n");
      return -EINVAL;
    }

#ifdef CONFIG_ZION_SUPPRESS_MASTER_ACTION
  PINFO("ZION PCI IF Driver Installed.(TARGET ONLY MODE)\n");
#else
  PINFO("ZION PCI IF Driver Installed.(MASTER ENABLE MODE)\n");
#endif /* CONFIG_ZION_SUPPRESS_MASTER_ACTION */

  return 0;
}

void exit_zion_pci(void)
{
  zion_params_t *zion_params;

  PINFO("cleanup ZION PCI module ...");

  zion_params = find_zion(0);
  if(zion_params==NULL)
    {
      return;
    }

  zion_disable_mbus_interrupt(zion_params, Pciif_Int);

  free_zion_pci_private_space(ZION_PCI_PARAM(zion_params));

  PINFO("DONE.\n");

  return;
}

