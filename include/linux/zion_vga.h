#ifndef _ZION_VGA_H_
#define _ZION_VGA_H_

#include <asm/page.h>

struct zionvga_reset_arg {
  int pal_line;
  int spl_line;
};

struct zionvga_init_arg {
  unsigned short red;
  unsigned short green;
  unsigned short blue;
};

struct zionvga_phase_arg
{
  unsigned short v_phase;
  unsigned short h_phase;
};

#define ZIONVGA_IOC_MAGIC 'x'

#define ZIONVGA_VRAM_ORDER     8 /* 1MB */
#define ZIONVGA_VRAM_SIZE      (PAGE_SIZE << ZIONVGA_VRAM_ORDER)

#define ZIONVGA_IOC_HARDRESET _IOW(ZIONVGA_IOC_MAGIC, 0, struct zionvga_reset_arg)
#define ZIONVGA_IOC_INIT_FB   _IOW(ZIONVGA_IOC_MAGIC, 1, struct zionvga_init_arg)
#define ZIONVGA_IOC_PURGE_FB  _IOR(ZIONVGA_IOC_MAGIC, 2, int)
#define ZIONVGA_IOC_UPDATE    _IOW(ZIONVGA_IOC_MAGIC, 3, int)
#define ZIONVGA_IOC_INIT_PHASE _IOW(ZIONVGA_IOC_MAGIC, 4, struct zionvga_phase_arg)
#define ZIONVGA_IOC_RSTR_STOP  _IO(ZIONVGA_IOC_MAGIC, 5)
#define ZIONVGA_IOC_RSTR_START  _IO(ZIONVGA_IOC_MAGIC, 6)

#ifdef __KERNEL__

int init_zion_vga(void);
void exit_zion_vga(void);

#endif /* __KERNEL__ */

#endif /* _ZION_VGA_H_ */
