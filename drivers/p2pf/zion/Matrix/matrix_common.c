/************************************************************
*
* pci.c : ZION Matrix Driver (Common)
*
* $Id: matrix_common.c,v 1.1.1.1 2006/02/27 09:20:56 nishikawa Exp $
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
#include "matrix_regs.h"


void zion_matrix_event(zion_params_t *zion_params, int bit,
		       int irq, void *dev_id, u16 pci_status)
{
  int i;

  for(i=0; i<4; i++)
    {
      u16 int_reg;
      u16 status;

      /* Read Interrupt Status */
      int_reg = mbus_readw(MBUS_ADDR(zion_params, MATRIX_Interrupt(i)));
      status = (int_reg & MATRIX_IntStatus_Mask);
      if(status)
	{
	  /* Memorize it ! */
	  zion_params->interrupt_bits.MAT_INT[i] |= status;

	  /* Do Nothing Now... */

	  /* Clear them */
	  int_reg |= ( status >> 8 );
	  mbus_writew(int_reg, MBUS_ADDR(zion_params, MATRIX_Interrupt(i)));
	  zion_mbus_int_clear(zion_params, Matrix1_Int+i);
	}
    }

  return;
}

int init_zion_matrix(void)
{
  zion_params_t *zion_params;
  int ret, i;

  /* get ZION parameters */
  zion_params = find_zion(0);
  if(zion_params == NULL)
    {
      return -ENODEV;
    }

  /* Check Fireball */
  if((zion_params->revision & 0xF000) == 0xF000)
    {
      PINFO("This is fireball! ZION Matrix cannot be used!!\n");
      return 0;
    }

  /* enable interruption */
  for(i=0; i<4; i++)
    {
      ret = zion_enable_mbus_interrupt(zion_params, Matrix1_Int+i, zion_matrix_event);
      if(ret)
	{
	  PERROR("registering interruption failed.\n");
	  return -EINVAL;
	}
    }

  PINFO("ZION MATRIX module Installed.\n");

  return 0;
}

void exit_zion_matrix(void)
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

  for(i=0; i<4; i++)
    {
      zion_disable_mbus_interrupt(zion_params, Matrix1_Int+i);
    }

  return;
}
