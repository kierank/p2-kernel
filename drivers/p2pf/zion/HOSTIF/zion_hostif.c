/************************************************************
*
* pci.c : ZION HOSTIF Driver
*
* $Id: zion_hostif.c,v 1.1.1.1 2006/02/27 09:20:56 nishikawa Exp $
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


void zion_hostif_event_one(zion_params_t *zion_params, int bit,
			   int irq, void *dev_id, u16 pci_status)
{
  zion_mbus_int_clear(zion_params, UpInt1);
  return;
}

void zion_hostif_event_two(zion_params_t *zion_params, int bit,
			   int irq, void *dev_id, u16 pci_status)
{
  zion_mbus_int_clear(zion_params, UpInt2);
  return;
}

int init_zion_hostif(void)
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
      PINFO("This is fireball! ZION HOST IF cannot be used!!\n");
      return 0;
    }

  /* enable interruption */
  ret = zion_enable_mbus_interrupt(zion_params, UpInt1, zion_hostif_event_one);
  if(ret)
    {
      PERROR("registering interruption failed.\n");
      return -EINVAL;
    }

  ret = zion_enable_mbus_interrupt(zion_params, UpInt2, zion_hostif_event_two);
  if(ret)
    {
      PERROR("registering interruption failed.\n");
      return -EINVAL;
    }

  PINFO("ZION HOSTIF module Installed.\n");

  return 0;
}

void exit_zion_hostif(void)
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

  zion_disable_mbus_interrupt(zion_params, UpInt1);
  zion_disable_mbus_interrupt(zion_params, UpInt2);

  return;
}
