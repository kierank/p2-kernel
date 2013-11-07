/*
 P2card driver common header for kernel modules
 $Id: spd.h 250 2006-10-24 09:31:36Z hiraoka $
 */
#ifndef _SPD_H
#define _SPD_H

#include "spd_ioctl.h"

#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/wait.h>
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/blkdev.h>
#include <linux/genhd.h>
#include <linux/hdreg.h>
#include <linux/cdev.h>
#include <linux/version.h>
#include <linux/semaphore.h>
#include <asm/atomic.h>

typedef struct _spd_area_info_t {
  u32 start;
  u32 end;
  u32 wsize;
  u32 ausize;
} spd_area_info_t;


enum SPD_DEFAULT_ENUM {
  SPD_HARDSECT_SIZE        = 512,
  SPD_N_AREA               = 2,
  SPD_N_RETRY              = 4,
  SPD_NO_RETRY             = 0,
  SPD_DMA_TIMEOUT          = (1*HZ),
  SPD_CMD_TIMEOUT          = (3*HZ + HZ/2),
  SPD_CMD_LONG_TIMEOUT     = (15*HZ),
  SPD_CMD_LONGLONG_TIMEOUT = (40*HZ),

  SPD_CARD_ID_SIZE  = 16,
  SPD_MODEL_ID_SIZE = 32,

  SPD_SG_N_ENTRY   = 8192,
  SPD_SG_SIZE      = sizeof(spd_scatterlist_t) * SPD_SG_N_ENTRY,
  SPD_SG_MAX_COUNT = 0x10000, /* 64KB -> 64KB x 8192 = 512MB */
  SPD_SG_ENDMARK   = 0x80000000,

/* Don't use the following macro for kernel. */
  /* SPD_CACHE_BUFFER_ORDER = 7, */
  /* SPD_CACHE_BUFFER_SIZE  = PAGE_SIZE * 128, */
  /* SPD_CACHE_N_BUFFER is defined at arch/defs-XXX.h */
  /* SPD_CACHE_MAX_SIZE     = SPD_CACHE_BUFFER_SIZE * SPD_CACHE_N_BUFFER, */
  /* SPD_CACHE_SG_N_ENTRY   = SPD_CACHE_MAX_SIZE / SPD_SG_MAX_COUNT, */
  /* SPD_CACHE_SG_SIZE      = SPD_CACHE_SG_N_ENTRY * sizeof(spd_scatterlist_t), */

  SPD_WSIZE_16K  = 0x20,
  SPD_WSIZE_64K  = 0x80,
  SPD_WSIZE_512K = 0x400,
  SPD_WSIZE_2M   = 0x1000,
  SPD_WSIZE_4M   = 0x2000,
  SPD_WSIZE_16M  = 0x8000,

  SPD_DIR_READ  = 0x00,
  SPD_DIR_WRITE = 0x01,

  SPD_TARGET_LOCAL = 0,
  SPD_TARGET_ZION  = 1,
  SPD_TARGET_FPGA  = 2,

  /* for Interface Condition */
  SPD_IO_LOCK   = 0x00,
  SPD_IO_UNLOCK = 0x01,
};


typedef struct _spd_dev_t {
  struct device *dev;
  int id;
  dev_t devnum;

  spinlock_t lock;
  struct semaphore io_sem;

  atomic_t status;

  u8 spec_version;
  u8 is_p2;
  u8 is_up2;
  u8 is_lk;
  u8 is_over;
  int capacity;
  spd_area_info_t area[SPD_N_AREA];
  int n_area;
  u32 dma_timeout;
  int dma_retry;

  int errcode;
  int retry;
  unsigned long time_stamp;
  unsigned long ticks;
  unsigned long timeout;
  void (*complete_handler)(struct _spd_dev_t *dev);  

  spd_scatterlist_t *sg;
  int sg_n_entry;
  void *tmp_buf;
  struct _spd_cache_t *cache;

  struct _spd_hwif_private_t *hwif;
  struct _spd_bdev_private_t *bdev;
  struct _spd_rdev_private_t *rdev;
  struct _spd_udev_private_t *udev;

  struct work_struct wq; /* bdev.c */
  struct p2_directw_list *directw_list; /* adpt.c */
} spd_dev_t;

#endif /* __KERNEL__ */
#endif /* _SPD_H */
