/************************************************************
*
* zion_core.c : ZION Driver Framework
*
* $Id: zion_core.c,v 1.1.1.1 2006/02/27 09:20:56 nishikawa Exp $
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

#include <linux/interrupt.h>

#include <linux/zion.h>
#include "zion_regs.h"

zion_params_t zion_params;

zion_params_t *find_zion(int number)
{
  if(zion_params.dev==NULL)
    {
      return NULL;
    }

  return &zion_params;
}

loff_t zion_llseek(struct file *filp, loff_t offset, int origin)
{
  int zion_minor = MINOR(filp->f_dentry->d_inode->i_rdev);

  if(zion_params.zion_operations[zion_minor])
    {
      if(zion_params.zion_operations[zion_minor]->llseek)
	return zion_params.zion_operations[zion_minor]->llseek(&zion_params, filp, offset, origin);
    }

  return -ENODEV;
}

ssize_t zion_read(struct file *filp, char *buf, size_t count, loff_t *offset)
{
  int zion_minor = MINOR(filp->f_dentry->d_inode->i_rdev);

  if(zion_params.zion_operations[zion_minor])
    {
      if(zion_params.zion_operations[zion_minor]->read)
	return zion_params.zion_operations[zion_minor]->read(&zion_params, filp, buf, count, offset);
    }

  return -ENODEV;
}

ssize_t zion_write(struct file *filp, const char *buf, size_t count, loff_t *offset)
{
  int zion_minor = MINOR(filp->f_dentry->d_inode->i_rdev);

  if(zion_params.zion_operations[zion_minor])
    {
      if(zion_params.zion_operations[zion_minor]->write)
	return zion_params.zion_operations[zion_minor]->write(&zion_params, filp, buf, count, offset);
    }

  return -ENODEV;
}

int zion_ioctl(struct inode *inode, struct file *filp, unsigned int command, unsigned long arg)
{
  int zion_minor = MINOR(inode->i_rdev);

  if(zion_params.zion_operations[zion_minor])
    {
      if(zion_params.zion_operations[zion_minor]->ioctl)
	return zion_params.zion_operations[zion_minor]->ioctl(&zion_params, inode, filp, command, arg);
    }

  return -ENODEV;
}

unsigned int zion_poll(struct file *filp, struct poll_table_struct *pts)
{
  int zion_minor = MINOR(filp->f_dentry->d_inode->i_rdev);

  if(zion_params.zion_operations[zion_minor])
    {
      if(zion_params.zion_operations[zion_minor]->poll)
	return zion_params.zion_operations[zion_minor]->poll(&zion_params, filp, pts);
    }

  return -ENODEV;
}

int zion_open(struct inode *inode, struct file *filp)
{
  int zion_minor = MINOR(inode->i_rdev);

  if(zion_params.zion_operations[zion_minor])
    {
      if(zion_params.zion_operations[zion_minor]->open)
	return zion_params.zion_operations[zion_minor]->open(&zion_params, inode, filp);
    }

  return -ENODEV;
}

int zion_release(struct inode *inode, struct file *filp)
{
  int zion_minor = MINOR(inode->i_rdev);

  if(zion_params.zion_operations[zion_minor])
    {
      if(zion_params.zion_operations[zion_minor]->release)
	return zion_params.zion_operations[zion_minor]->release(&zion_params, inode, filp);
    }

  return -ENODEV;
}

int zion_mmap(struct file *filp, struct vm_area_struct *vas)
{
  int zion_minor = MINOR(filp->f_dentry->d_inode->i_rdev);

  if(zion_params.zion_operations[zion_minor])
    {
      if(zion_params.zion_operations[zion_minor]->mmap)
	return zion_params.zion_operations[zion_minor]->mmap(&zion_params, filp, vas);
    }

  return -ENODEV;
}

struct file_operations zion_fops = {
  llseek : zion_llseek,
  read : zion_read,
  write : zion_write,
  ioctl : zion_ioctl,
  poll : zion_poll,
  open : zion_open,
  release : zion_release,
  mmap : zion_mmap,
};

int init_zion(void)
{
  int result;
  u8 tmp_8 = 0;
  u16 tmp_16 = 0;

  /* initialize parameters */
  memset((void *)&zion_params,0,sizeof(zion_params_t));
  init_waitqueue_head(&(zion_params.zion_wait_queue));
  zion_params.wait_timeout = ZION_DEFAULT_TIMEOUT;

  /* having ZION ? */
  zion_params.dev = pci_get_device(ZION_VENDOR_ID, ZION_DEVICE_ID, zion_params.dev);
  if(zion_params.dev==NULL)
    {
      PERROR("Can't Find ZION.\n");
      return -ENODEV;
    }

  /* Enable ZION */
  result = pci_enable_device(zion_params.dev);
  if(result)
    {
      PERROR("pci_enable_device (bus = %d) failed.\n",(int)(zion_params.dev->bus->number));
      return -ENODEV;
    }

  /* Get Base Memory Address */
  zion_params.mbus_addr = (u32)ioremap(pci_resource_start(zion_params.dev,0),
				      pci_resource_len(zion_params.dev,0));

  zion_params.mbus_size = pci_resource_len(zion_params.dev,0);

  zion_params.wram_addr = (u32)ioremap(pci_resource_start(zion_params.dev,1),
				      pci_resource_len(zion_params.dev,1));

  zion_params.wram_size = pci_resource_len(zion_params.dev,1);

  zion_params.whole_sdram_addr = (u32)pci_resource_start(zion_params.dev,2);  /* no need to ioremap */

  zion_params.whole_sdram_size = pci_resource_len(zion_params.dev,2);
#if defined(CONFIG_ZION_REDUCE_SDRAM_BY_HALF)
  zion_params.whole_sdram_size = zion_params.whole_sdram_size >> 1;
#endif /* CONFIG_ZION_REDUCE_SDRAM_BY_HALF */

  zion_params.partial_sdram_addr = (u32)ioremap(pci_resource_start(zion_params.dev,3),
					       pci_resource_len(zion_params.dev,3));

  zion_params.partial_sdram_size = pci_resource_len(zion_params.dev,3);

  /* Register this Device */
  result = register_chrdev(ZION_DEV_MAJOR, ZION_DEV_NAME, &zion_fops);
  if(result<0)
    {
      iounmap((void *)zion_params.mbus_addr);
      iounmap((void *)zion_params.wram_addr);
      iounmap((void *)zion_params.partial_sdram_addr);
      PERROR("register_chdev failed.\n");
      return -ENODEV;
    }

  /* Enable Memory Space */
  pci_read_config_word(zion_params.dev, NEO_PCI_COMMAND, &tmp_16);
  pci_write_config_word(zion_params.dev, NEO_PCI_COMMAND,(tmp_16|NEO_MEMORY_SPACE_ENABLE));

  
  /* Enable Interrupts */
  mbus_writew(0, MBUS_ADDR((&zion_params), NEO_Interrupt_Mask_B));
  mbus_readw(MBUS_ADDR((&zion_params), NEO_Interrupt_Mask_B)); /* For Assurance */
  pci_write_config_byte(zion_params.dev, NEO_PCI_INTERRUPT_CONTROL, 0);

  mbus_writew(0xFFFF, MBUS_ADDR((&zion_params), NEO_Interrupt_Clear_B));
  mbus_readw(MBUS_ADDR((&zion_params), NEO_Interrupt_Clear_B)); /* For Assurance */
  pci_write_config_byte(zion_params.dev, NEO_PCI_INTERRUPT_STATUS, 0xFF);

  result = request_irq(zion_params.dev->irq, int_zion_event, 
		       IRQF_SHARED, ZION_DEV_NAME, &zion_params);
  if(result)
    {
      PERROR("request_irq (bus=%d) failed.\n",(int)(zion_params.dev->bus->number));
      iounmap((void *)zion_params.mbus_addr);
      iounmap((void *)zion_params.wram_addr);
      iounmap((void *)zion_params.partial_sdram_addr);
      free_irq(zion_params.dev->irq,&zion_params);
      unregister_chrdev(ZION_DEV_MAJOR, ZION_DEV_NAME);
      return -ENODEV;
    }

  pci_read_config_byte(zion_params.dev, NEO_PCI_INTERRUPT_CONTROL, &tmp_8);
  pci_write_config_byte(zion_params.dev, NEO_PCI_INTERRUPT_CONTROL, 
			tmp_8 | NEO_MBUS_ERROR_INT_ENABLE | NEO_DMA_INT_ENABLE | NEO_BACKEND_INT_ENABLE);

  pci_read_config_byte(zion_params.dev, NEO_PCI_INTERRUPT_CONTROL, &tmp_8);

  create_proc_entry("zion",S_IFDIR,0);

  PINFO("ZION Framework Installed.\n");

  return zion_init_modules();
}

void cleanup_zion(void)
{
  u8 tmp_8;

  zion_exit_modules();

  unregister_chrdev(ZION_DEV_MAJOR, ZION_DEV_NAME);

  /* hey, shut up, boy! */
  pci_read_config_byte(zion_params.dev, NEO_PCI_INTERRUPT_CONTROL, &tmp_8);
  pci_write_config_byte(zion_params.dev, NEO_PCI_INTERRUPT_CONTROL,
		      tmp_8 & ~(NEO_DMA_INT_ENABLE | NEO_BACKEND_INT_ENABLE));

  free_irq(zion_params.dev->irq,&zion_params);

  mbus_writew(0xFFFF, MBUS_ADDR((&zion_params), NEO_Interrupt_Clear_B));
  mbus_readw(MBUS_ADDR((&zion_params), NEO_Interrupt_Clear_B)); /* For Assurance */
  pci_write_config_byte(zion_params.dev, NEO_PCI_INTERRUPT_STATUS, 0xFF);

  iounmap((void *)zion_params.mbus_addr);
  iounmap((void *)zion_params.wram_addr);
  iounmap((void *)zion_params.partial_sdram_addr);

  remove_proc_entry("zion",0);

  return;
}

module_init(init_zion);
module_exit(cleanup_zion);
