/************************************************************
*
* zion_core.c : ZION Driver Framework
*
* $Id: zion_init_list.c,v 1.1.1.1 2006/02/27 09:20:56 nishikawa Exp $
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

#include <linux/zion.h>
#include <linux/zion_dmaif.h>
#include <linux/zion_audio_proc.h>
#include <linux/zion_romif.h>
#include <linux/zion_matrix.h>
#include <linux/zion_hostif.h>
#include <linux/zion_audio_dsp.h>
#include <linux/zion_duel.h>

typedef int (*zion_init_t)(void);
typedef void (*zion_exit_t)(void);

static zion_init_t ZION_INIT_LIST[]={

  zion_common_init,

#ifdef CONFIG_ZION_PCI
  init_zion_pci,
#endif /* CONFIG_ZION_PCI */

#ifdef CONFIG_ZION_DVCIF
  init_zion_dvcif,
#endif /* CONFIG_ZION_DVCIF */

#ifdef CONFIG_ZION_DMAIF
  init_zion_dmaif,
#endif /* CONFIG_ZION_DMAIF */

#ifdef CONFIG_ZION_AUDIOPROC
  init_zion_audio_proc,
#endif /* CONFIG_ZION_AUDIOPROC */

#ifdef CONFIG_ZION_NEOCTRL
  init_zion_neoctrl,
#endif /* CONFIG_ZION_NEOCTRL */

#ifdef CONFIG_ZION_ROMIF
  init_zion_romif,
#endif /* CONFIG_ZION_ROMIF */

#ifdef CONFIG_ZION_MATRIX
  init_zion_matrix,
#endif /* CONFIG_ZION_MATRIX */

#ifdef CONFIG_ZION_HOSTIF
  init_zion_hostif,
#endif /* CONFIG_ZION_HOSTIF */

#ifdef CONFIG_ZION_AUDIODSP
  init_zion_audio_dsp,
#endif /* CONFIG_ZION_AUDIODSP */

#ifdef CONFIG_ZION_DUELCORE
  init_zion_duel,
#endif /* CONFIG_ZION_DUELCORE */

#ifdef CONFIG_ZION_VGA
  init_zion_vga,
#endif /* CONFIG_ZION_VGA */
  
  NULL
};

static zion_exit_t ZION_EXIT_LIST[]={

  zion_common_exit,

#ifdef CONFIG_ZION_PCI
  exit_zion_pci,
#endif /* CONFIG_ZION_PCI */

#ifdef CONFIG_ZION_DVCIF
  exit_zion_dvcif,
#endif /* CONFIG_ZION_DVCIF */

#ifdef CONFIG_ZION_DMAIF
  exit_zion_dmaif,
#endif /* CONFIG_ZION_DMAIF */

#ifdef CONFIG_ZION_AUDIOPROC
  exit_zion_audio_proc,
#endif /* CONFIG_ZION_AUDIOPROC */

#ifdef CONFIG_ZION_NEOCTRL
  exit_zion_neoctrl,
#endif /* CONFIG_ZION_NEOCTRL */

#ifdef CONFIG_ZION_ROMIF
  exit_zion_romif,
#endif /* CONFIG_ZION_ROMIF */

#ifdef CONFIG_ZION_MATRIX
  exit_zion_matrix,
#endif /* CONFIG_ZION_MATRIX */

#ifdef CONFIG_ZION_HOSTIF
  exit_zion_hostif,
#endif /* CONFIG_ZION_MATRIX */

#ifdef CONFIG_ZION_AUDIODSP
  exit_zion_audio_dsp,
#endif /* CONFIG_ZION_AUDIODSP */

#ifdef CONFIG_ZION_DUELCORE
  exit_zion_duel,
#endif /* CONFIG_ZION_DUELCORE */

#ifdef CONFIG_ZION_VGA
  exit_zion_vga,
#endif /* CONFIG_ZION_VGA */

  NULL
};

int zion_init_modules(void)
{
  int i=0;
  int ret;

  while(ZION_INIT_LIST[i]!=NULL)
    {
      ret = ZION_INIT_LIST[i]();
      if(ret)
	{
	  return ret;
	}
      i++;
    }

  return 0;
}

void zion_exit_modules(void)
{
  int i=0;

  while(ZION_EXIT_LIST[i]!=NULL)
    {
      ZION_EXIT_LIST[i]();
      i++;
    }

  return;
}

