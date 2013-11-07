/************************************************************
*
* pci.c : ZION Audio Proc Driver
*
* $Id: audio_proc.c,v 1.1.1.1 2006/02/27 09:20:56 nishikawa Exp $
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
#include "zion_audio_proc_regs.h"


int zion_audio_proc_open(zion_params_t *zion_params, struct inode *inode, struct file *filp)
{
 // MOD_INC_USE_COUNT;
  return 0;
}

int zion_audio_proc_release(zion_params_t *zion_params, struct inode *inode, struct file *filp)
{
  //MOD_DEC_USE_COUNT;
  return 0;
}

int zion_audio_proc_ioctl(zion_params_t *zion_params,
		     struct inode *inode, struct file *filp,
		     unsigned int function, unsigned long arg)
{
  return -EINVAL;
}

struct zion_file_operations zion_audio_proc_fops = {
  ioctl : zion_audio_proc_ioctl,
  open : zion_audio_proc_open,
  release : zion_audio_proc_release,
};

void zion_audio_proc_event(zion_params_t *zion_params, int bit,
		      int irq, void *dev_id, u16 pci_status)
{
  u16 int_status_12;
  u16 int_status_34;

  /* Read Interrupt Status */
  int_status_12 = mbus_readw(MBUS_ADDR(zion_params, DMA12_Interrupt_Status));
  int_status_34 = mbus_readw(MBUS_ADDR(zion_params, DMA34_Interrupt_Status));

  /* Memorize it ! */
  zion_params->interrupt_bits.AUDIO_INT[0] |= int_status_12;
  zion_params->interrupt_bits.AUDIO_INT[1] |= int_status_34;

  /* Do Nothing Now... */


  /* Clear them */

  if(int_status_12)
    {
      mbus_writew(int_status_12, MBUS_ADDR(zion_params, DMA12_Interrupt_Clear));
    }

  if(int_status_34)
    {
      mbus_writew(int_status_34, MBUS_ADDR(zion_params, DMA34_Interrupt_Clear));
    }

  zion_mbus_int_clear(zion_params, AudioProc_Int);

  return;
}

int init_zion_audio_proc(void)
{
  zion_params_t *zion_params;
  int ret;

  /* get ZION parameters */
  zion_params = find_zion(0);
  if(zion_params == NULL)
    {
      return -ENODEV;
    }

  /* Check Fireball */
  if((zion_params->revision & 0xF000) == 0xF000)
    {
      PINFO("This is fireball! ZION AudioProc cannot be used!!\n");
      return 0;
    }

  /* register fuctions for operation */
  zion_params->zion_operations[ZION_AUDIO_PROC]=&zion_audio_proc_fops; 

  /* enable interruption */
  ret = zion_enable_mbus_interrupt(zion_params, AudioProc_Int, zion_audio_proc_event);
  if(ret)
    {
      PERROR("registering interruption failed.\n");
      return -EINVAL;
    }

  PINFO("ZION Audio Proc module Installed.\n");

  return 0;
}

void exit_zion_audio_proc(void)
{
  zion_params_t *zion_params;

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

  zion_disable_mbus_interrupt(zion_params, AudioProc_Int);

  return;
}
