/************************************************************
*
* neoctrl.c : ZION NEO control Driver
*
* $Id: neoctrl.c,v 1.1.1.1 2006/02/27 09:20:56 nishikawa Exp $
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
#include "zion_neoctrl_regs.h"

#define NEOCTRL_MAJORVER (0)
#define NEOCTRL_MINORVER (1)


/***************************************************************************
 * zion_neoctrl_open
 **************************************************************************/
int zion_neoctrl_open(zion_params_t *zion_params, struct inode *inode, struct file *filp)
{
  return 0;
}

/***************************************************************************
 * zion_neoctrl_release
 **************************************************************************/
int zion_neoctrl_release(zion_params_t *zion_params, struct inode *inode, struct file *filp)
{
  return 0;
}


struct zion_file_operations zion_neoctrl_fops = {
	open : zion_neoctrl_open,
	release : zion_neoctrl_release,
};


/***************************************************************************
 * initialize_zion_neoctrl_private_space
 **************************************************************************/
static void initialize_zion_neoctrl_private_space(zion_neoctrl_params_t *neoctrl_params)
{

  memset((void *)neoctrl_params,0,sizeof(zion_neoctrl_params_t));
  init_waitqueue_head(&(neoctrl_params->zion_neoctrl_wait_queue));
  spin_lock_init(&(neoctrl_params->params_lock));

  return;
}


/***************************************************************************
 * free_zion_neoctrl_private_space
 **************************************************************************/
static void free_zion_neoctrl_private_space(zion_neoctrl_params_t *neoctrl_params)
{
  if(neoctrl_params==NULL)
    {
      return;
    }

  wake_up(&(neoctrl_params->zion_neoctrl_wait_queue));

  kfree((void *)neoctrl_params);

  /* wait until all threads awake ?? */

  return;
}


/***************************************************************************
 * zion_neoctrl_event
 **************************************************************************/
void zion_neoctrl_event(zion_params_t *zion_params, int bit,
			int irq, void *dev_id, u16 pci_status)
{
  u16 int_status;

  int_status = mbus_readw(MBUS_ADDR(zion_params, NEOCTRL_INTERRUPT_STATUS_A));

  zion_params->interrupt_bits.NEOCTRL_INT |= int_status;

  mbus_writew(int_status, MBUS_ADDR(zion_params, NEOCTRL_INTERRUPT_CLEAR));
  mbus_readw(MBUS_ADDR(zion_params, NEOCTRL_INTERRUPT_CLEAR));

  zion_mbus_int_clear(zion_params, Neoctl_Int);

  return;
}


/***************************************************************************
 * init_zion_neoctrl
 **************************************************************************/
int init_zion_neoctrl(void)
{
  zion_params_t *zion_params;
  int i, ret;

  /* get ZION parameters */
  zion_params = find_zion(0);
  if(zion_params == NULL)
    {
      return -ENODEV;
    }

  /* Check Fireball */
  if((zion_params->revision & 0xF000) == 0xF000)
    {
      PINFO("This is fireball! ZION NEOctrl cannot be used!!\n");
      return 0;
    }

  /* register fuctions for operation */
  for(i=0; i<ZION_NEOCTRL_PORTS; i++)
    {
      zion_params->zion_operations[ZION_NEOCTRL+i]=&zion_neoctrl_fops; 
    }

  /* Set Private Data Area */
  for(i=0; i<ZION_NEOCTRL_PORTS; i++)
    {
	//don't use macro for gcc.
//      NEOCTRL_PARAM(zion_params, ZION_NEOCTRL+i) = kmalloc(sizeof(zion_neoctrl_params_t),GFP_KERNEL);
      (zion_params)->zion_private[ZION_NEOCTRL+i] = kmalloc(sizeof(zion_neoctrl_params_t),GFP_KERNEL);

      if(NEOCTRL_PARAM(zion_params,ZION_NEOCTRL+i)==NULL)
	{
	  PERROR("Can't get enough space for private data.\n");
	  return -ENOMEM;
	}

      initialize_zion_neoctrl_private_space(NEOCTRL_PARAM(zion_params, ZION_NEOCTRL+i));
    }

  /* Clear All Interruption */
  mbus_writew(0, MBUS_ADDR(zion_params, NEOCTRL_INTERRUPT_MASK_A));
  mbus_readw(MBUS_ADDR(zion_params, NEOCTRL_INTERRUPT_MASK_A));

  mbus_writew(0xffff, MBUS_ADDR(zion_params, NEOCTRL_INTERRUPT_CLEAR));
  mbus_readw(MBUS_ADDR(zion_params, NEOCTRL_INTERRUPT_CLEAR));

  /* enable interruption */
  ret = zion_enable_mbus_interrupt(zion_params, Neoctl_Int, zion_neoctrl_event);
  if(ret)
    {
      PERROR("registering interruption failed.\n");
      return -EINVAL;
    }

  /* Set Interrupt Mask */
  mbus_writew(0x0180, MBUS_ADDR(zion_params, NEOCTRL_INTERRUPT_MASK_A));
  mbus_readw(MBUS_ADDR(zion_params, NEOCTRL_INTERRUPT_MASK_A));

  PINFO("ZION NEO-control module ver. %d.%d Installed.\n",
		 NEOCTRL_MAJORVER, NEOCTRL_MINORVER);

  return 0;
}


/***************************************************************************
 * exit_zion_neoctrl
 **************************************************************************/
void exit_zion_neoctrl(void)
{
  zion_params_t *zion_params;
  int i;

  PINFO("cleanup ZION NEO-control module ...");
  
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

  for(i=0; i<ZION_NEOCTRL_PORTS; i++)
    {
      free_zion_neoctrl_private_space(NEOCTRL_PARAM(zion_params, ZION_NEOCTRL+i));
    }

  PINFO( "Done.\n" );
  
  return;
}
