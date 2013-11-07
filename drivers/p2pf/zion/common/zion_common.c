/************************************************************
*
* zion_common.c : ZION Common Driver
*
* $Id: zion_common.c,v 1.1.1.1 2006/02/27 09:20:56 nishikawa Exp $
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
#include "zion_common_regs.h"


static unsigned long neo_wram_pio_read
(zion_params_t *params, unsigned long offset_addr, void *buf, unsigned long size, char access_type)
{
  void *data;
  unsigned long (*selected_memcpy_fromio)(void *, unsigned long, unsigned long);

  if( (params->wram_size) < (offset_addr + size) )
    {
      size = params->wram_size - offset_addr;
    }

  if(size<=0)
    {
      return 0;
    }

  if(access_type=='b')
    {
      selected_memcpy_fromio = memcpy_fromio_byte;
    }
  else if(access_type=='w')
    {
      if(offset_addr % sizeof(u16) || size % sizeof(u16))
	{
	  PERROR("Invalid pointer of size\n");
	  return 0;
	}
      selected_memcpy_fromio = memcpy_fromio_word;
    }
  else if(access_type=='d')
    {
      if(offset_addr % sizeof(u32) || size % sizeof(u32))
	{
	  PERROR("Invalid pointer of size\n");
	  return 0;
	}
      selected_memcpy_fromio = memcpy_fromio_dword;
    }
  else
    {
      PERROR("Invalid Access Type.\n");
      return 0;
    }

  data = vmalloc(size);

  if(!selected_memcpy_fromio(data, WRAM_ADDR(params,offset_addr), size))
    {
      vfree(data);
      return 0;
    }

  if(copy_to_user((void *)buf, (const void *)data, size))
    {
      vfree(data);
      return 0;
    }

  vfree(data);

  return size;
}

static unsigned long neo_wram_pio_write
(zion_params_t *params, unsigned long offset_addr, void *buf, unsigned long size, char access_type)
{
  void *data;
  unsigned long (*selected_memcpy_toio)(unsigned long, const void*, unsigned long);

  if( (params->wram_size) < (offset_addr + size) )
    {
      size = params->wram_size - offset_addr;
    }

  if(size<=0)
    {
      return 0;
    }

  if(access_type=='b')
    {
      selected_memcpy_toio = memcpy_toio_byte;
    }
  else if(access_type=='w')
    {
      if(offset_addr % sizeof(u16) || size % sizeof(u16))
	{
	  PERROR("Invalid pointer of size\n");
	  return 0;
	}
      selected_memcpy_toio = memcpy_toio_word;
    }
  else if(access_type=='d')
    {
      if(offset_addr % sizeof(u32) || size % sizeof(u32))
	{
	  PERROR("Invalid pointer of size\n");
	  return 0;
	}
      selected_memcpy_toio = memcpy_toio_dword;
    }
  else
    {
      PERROR("Invalid Access Type.\n");
      return 0;
    }

  data = vmalloc(size);

  if(copy_from_user((void *)data, (void *)buf, size))
    {
      vfree(data);
      return 0;
    }
 
  if(!selected_memcpy_toio(WRAM_ADDR(params, offset_addr), data, size))
    {
      vfree(data);
      return 0;
    }

  vfree(data);

  return size;
}


static loff_t zion_common_llseek(zion_params_t *zion_params, struct file *filp, loff_t offset, int origin)
{
  long long retval;

  switch(origin)
    {
    case 2:
      offset += zion_params->wram_size;
      break;
    case 1:
      offset += filp->f_pos;
    }

  retval = -EINVAL;

  if( (offset>=0) && (offset<=zion_params->wram_size) )
    {
      if(offset != filp->f_pos)
	{
	  filp->f_pos = offset;
	}
      retval = offset;
    }

  return retval;
}


int zion_common_mmap(zion_params_t *zion_params, struct file *filp, struct vm_area_struct *vma)
{
  unsigned long physaddr = pci_resource_start(zion_params->dev,1);
  unsigned long vsize    = vma->vm_end - vma->vm_start;

#ifdef __SH4__
  pgprot_val(vma->vm_page_prot) &= ~_PAGE_CACHABLE;
#endif

#ifdef __powerpc__
  pgprot_val(vma->vm_page_prot) |= _PAGE_NO_CACHE|_PAGE_GUARDED;
#endif

  vma->vm_flags |= VM_RESERVED;

  if(remap_pfn_range(vma, vma->vm_start, (physaddr >> PAGE_SHIFT), vsize, vma->vm_page_prot))
  {
    return -EAGAIN;
  }

  return 0;
}

static int zion_common_ioctl(zion_params_t *zion_params,
			     struct inode *inode, struct file *filp,
			     unsigned int function, unsigned long arg)
{
  struct zion_config_byte zion_config_byte = {0};
  struct zion_config_word zion_config_word = {0};
  struct zion_config_dword zion_config_dword = {0};
  struct zion_buf zion_buf;
  struct ZION_Interrupt_Bits zion_interrupt = {0};
  u16 revision;

  switch(function)
    {
    case ZION_MBUS_READ_CONFIG_BYTE:

      if(copy_from_user((void *)&zion_config_byte, (void *)arg, sizeof(struct zion_config_byte)))
	return -EFAULT;

      zion_config_byte.val = (u8) mbus_readb( MBUS_ADDR(zion_params, zion_config_byte.where) );

      if(copy_to_user((void *)arg, (void *)&zion_config_byte, sizeof(struct zion_config_byte)))
	return -EFAULT;

      break;

    case ZION_MBUS_READ_CONFIG_WORD:

      if(copy_from_user((void *)&zion_config_word, (void *)arg, sizeof(struct zion_config_word)))
	return -EFAULT;

      zion_config_word.val = (u16) mbus_readw( MBUS_ADDR(zion_params,zion_config_word.where) );

      if(copy_to_user((void *)arg, (void *)&zion_config_word, sizeof(struct zion_config_word)))
	return -EFAULT;

      break;

    case ZION_MBUS_READ_CONFIG_DWORD:

      if(copy_from_user((void *)&zion_config_dword, (void *)arg, sizeof(struct zion_config_dword)))
	return -EFAULT;

      zion_config_dword.val = (u32) mbus_readl( MBUS_ADDR(zion_params,zion_config_dword.where) );

      if(copy_to_user((void *)arg, (void *)&zion_config_dword, sizeof(struct zion_config_dword)))
	return -EFAULT;

      break;

    case ZION_MBUS_WRITE_CONFIG_BYTE:

      if(copy_from_user((void *)&zion_config_byte, (void *)arg, sizeof(struct zion_config_byte)))
	return -EFAULT;

       mbus_writeb(zion_config_byte.val, MBUS_ADDR(zion_params, zion_config_byte.where) );

      if(copy_to_user((void *)arg, (void *)&zion_config_byte, sizeof(struct zion_config_byte)))
	return -EFAULT;

      break;
	
    case ZION_MBUS_WRITE_CONFIG_WORD:

      if(copy_from_user((void *)&zion_config_word, (void *)arg, sizeof(struct zion_config_word)))
	return -EFAULT;

       mbus_writew(zion_config_word.val, MBUS_ADDR(zion_params, zion_config_word.where) );

      if(copy_to_user((void *)arg, (void *)&zion_config_word, sizeof(struct zion_config_word)))
	return -EFAULT;

      break;

    case ZION_MBUS_WRITE_CONFIG_DWORD:

      if(copy_from_user((void *)&zion_config_dword, (void *)arg, sizeof(struct zion_config_dword)))
	return -EFAULT;

       mbus_writel(zion_config_dword.val, MBUS_ADDR(zion_params, zion_config_dword.where) );

      if(copy_to_user((void *)arg, (void *)&zion_config_dword, sizeof(struct zion_config_dword)))
	return -EFAULT;

      break;

    case ZION_WRAM_READ:

      if(copy_from_user((void *)&zion_buf, (void *)arg, sizeof(struct zion_buf)))
	return -EFAULT;

      if((zion_buf.size = neo_wram_pio_read(zion_params, zion_buf.addr, zion_buf.buf, zion_buf.size, zion_buf.access_type))==0)
	return -EINVAL;

      if(copy_to_user((void *)arg, (void *)&zion_buf, sizeof(struct zion_buf)))
	return -EFAULT;

      break;

    case ZION_WRAM_WRITE:

      if(copy_from_user((void *)&zion_buf, (void *)arg, sizeof(struct zion_buf)))
	return -EFAULT;

      if((zion_buf.size = neo_wram_pio_write(zion_params, zion_buf.addr, zion_buf.buf, zion_buf.size, zion_buf.access_type))==0)
	return -EINVAL;

      if(copy_to_user((void *)arg, (void *)&zion_buf, sizeof(struct zion_buf)))
	return -EFAULT;

      break;

    case ZION_WAIT_INTERRUPT:

      zion_goto_bed(zion_params, &zion_interrupt);  /* zion_interrupt.c */

      if(copy_to_user((void *)arg, (void *)&zion_interrupt, sizeof(struct ZION_Interrupt_Bits)))
	return -EFAULT;

      break;

    case ZION_WAKE_THREADS_UP:

      zion_rout_them_up(zion_params);    /* zion_interrupt.c */

      break;

    case ZION_SET_ENABLE_BITS:

      if(copy_from_user((void *)&zion_interrupt, (void *)arg, sizeof(struct ZION_Interrupt_Bits)))
	return -EFAULT;

      zion_set_enable_bits(zion_params, &zion_interrupt);  /* zion_interrupt.c */

      break;

    case ZION_GET_ENABLE_BITS:

      zion_get_enable_bits(zion_params, &zion_interrupt);  /* zion_interrupt.c */

      if(copy_to_user((void *)arg, (void *)&zion_interrupt, sizeof(struct ZION_Interrupt_Bits)))
	return -EFAULT;

      break;

    case ZION_SET_TIMEOUT:

      zion_params->wait_timeout = arg;  /* by jiffies */

      break;

    case ZION_GET_REVISION:
      revision = zion_params->revision;
      if(copy_to_user((void *)arg, (void *)&revision, sizeof(u16)))
	{
	  return -EFAULT;
	}
      break;

    default:
      PERROR("No such IOCTL.\n");
      return -EINVAL;
    }

  return 0;
}

static int zion_common_open(zion_params_t *zion_params, struct inode *inode, struct file *filp)
{
  return 0;
}

static int zion_common_release(zion_params_t *zion_params, struct inode *inode, struct file *filp)
{
  return 0;
}

static ssize_t zion_common_write(zion_params_t *zion_params, struct file *filp, const char *buf, size_t count, loff_t *offset)
{
  char access_type;
  unsigned long ret;

  if(!count%4)
    {
      access_type='d';
    }
  else if(!count%2)
    {
      access_type='w';
    }
  else
    {
      access_type='b';
    }

  ret = neo_wram_pio_write(zion_params, *offset, (void *)buf, count, access_type);

  if(ret)
    {
      *offset += ret;
    }

  return (ssize_t)ret;
}

static ssize_t zion_common_read(zion_params_t *zion_params, struct file *filp, char *buf, size_t count, loff_t *offset)
{
  char access_type;
  unsigned long ret;

  if(!count%4)
    {
      access_type='d';
    }
  else if(!count%2)
    {
      access_type='w';
    }
  else
    {
      access_type='b';
    }

  ret = neo_wram_pio_read(zion_params, *offset, (void *)buf, count, access_type);

  if(ret)
    {
      *offset += ret;
    }

  return (ssize_t)ret;
}

struct zion_file_operations zion_common_fops ={
  llseek : zion_common_llseek,
  ioctl : zion_common_ioctl,
  open : zion_common_open,
  release : zion_common_release,
  write : zion_common_write,
  read : zion_common_read,
  mmap : zion_common_mmap,
};

/** make proc filesystem **/
int zion_proc_read(char *buf, char **start, off_t offset,
		   int length, int *eof, void *data)
{
  zion_params_t *zion_params;
  int j;
  u32 val=0;
  u16 revision;
  int len=0;

  zion_params = find_zion(0);
  if(zion_params==NULL)
    {
      return 0;
    }

  revision = mbus_readw(MBUS_ADDR(zion_params,0));

  len += sprintf(buf+len,
		 "ZION (Rev. %04X):\n"
		 "   Base Address:\n"
		 "      MBusAddr=0x%X(%d byte) WorkRAM=0x%X(%d byte)\n"
		 "      SDRAM=0x%X(%d byte) Partial-SDRAM=0x%X(%d byte)\n"
		 "   Resources:\n"
		 "      Bus=%d Dev=%d Func=%d IRQ=%d\n"
		 "   Driver:\n"
		 "      Version: %s.%s.%s\n",
		 revision,
		 zion_params->mbus_addr, zion_params->mbus_size,
		 zion_params->wram_addr, zion_params->wram_size,
		 zion_params->whole_sdram_addr, zion_params->whole_sdram_size,
		 zion_params->partial_sdram_addr, zion_params->partial_sdram_size,
		 (zion_params->dev->bus)->number, (zion_params->dev->devfn) >> 3,
		 (zion_params->dev->devfn) & 0x07,zion_params->dev->irq,
		 ZIONDRV_MAJOR_VER, ZIONDRV_MINOR_VER, ZIONDRV_PATCH_LEV);
  
  len += sprintf(buf + len,"   (VENDOR_ID=%04X,",   zion_params->dev->vendor);
  len += sprintf(buf + len," DEVICE_ID=%04X)\n",   zion_params->dev->device);

  len+= sprintf(buf+len, "   PCI Config Regs. : \n");

  for (j = 0; j < 256; j += 4) {
    if ((j % 32) == 0) {
      len += sprintf(buf + len,"0x%02X : ", j);
    }
    pci_read_config_dword(zion_params->dev, j, &val);
    len += sprintf(buf + len,"%08X ", val);
    if ((j % 32) == 28) {
      len += sprintf(buf + len,"\n");
    }
  }

  len -= offset;

  if(len < length)
    {
      *eof = 1;
      if(len <= 0) return 0;
    }
  else
    {
      len = length;
    }

  *start = buf + offset;

  return len;
}

int zion_common_init(void)
{
  zion_params_t *zion_params;
  struct proc_dir_entry *pentry;

  zion_params = find_zion(0);
  if(zion_params==NULL)
    {
      PERROR("ZION : not found\n");
      return -ENODEV;
    }

  zion_params->zion_operations[ZION_COMMON]=&zion_common_fops;


  /* Make proc Entry */
  pentry = create_proc_entry("zion/pcicreg",0,0);
  if(pentry)
    {
      pentry->read_proc = zion_proc_read;
    }

  if(1)
  {
    u16 revision = mbus_readw(MBUS_ADDR(zion_params,0));
    zion_params->revision = revision;
    PINFO("ZION detected (Rev. %04X):\n"
	   "   Driver(Common Part): Version %s.%s.%s\n",
	   revision,
	   ZIONDRV_MAJOR_VER, ZIONDRV_MINOR_VER, ZIONDRV_PATCH_LEV);
  }

  return 0;
}

void zion_common_exit(void)
{
  PINFO("cleanup ZION Common module ... ");
  remove_proc_entry("zion/pcicreg",0);
  PINFO("DONE.\n");
  return;
}

