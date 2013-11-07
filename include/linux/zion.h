/************************

ZION CORE HEADERS  (by S.Horita)

---List of Minor Devices---

      0: ZION Common (only fundamental controls)
 16- 31: PCI-IF & SBUS
 32- 47: DMA-IF
 48- 63: DVC-IF
 64- 79: Audio-DSP
 80- 95: Audio-Proc
 96-111: MATRIX MPU No.1
112-127: MATRIX MPU No.2
128-143: MATRIX MPU No.3
144-159: MATRIX MPU No.4
160-175: DUELCORE
176-191: ROM-IF
192-207: NEO-Control
208-223: Host-IF

************************/

#ifndef _ZION_H
#define _ZION_H

#include <linux/types.h>
#include <linux/errno.h>

#define ZION_MAGIC (0xD2)

/* Version */
#define ZIONDRV_MAJOR_VER "1"
#define ZIONDRV_MINOR_VER "0"
#define ZIONDRV_PATCH_LEV "1"

/* Device Informations */
#define ZION_VENDOR_ID (0x10F7)
#define ZION_DEVICE_ID (0x820A)

/* Device Name */
#define ZION_DEV_NAME "ZION"

/* Major Number */
#define ZION_DEV_MAJOR (121)

/* Minor Numbers */
#define ZION_NR_MINOR   (256)
#define ZION_NR_PORTS               (16)

#define ZION_COMMON     (0)
#define ZION_PCI        (16)
#define ZION_DMAIF      (32)
#define ZION_DVCIF      (48)
#define ZION_AUDIO_DSP  (64)
#define ZION_AUDIO_PROC (80)
#define ZION_MATRIX1    (96)
#define ZION_MATRIX2    (112)
#define ZION_MATRIX3    (128)
#define ZION_MATRIX4    (144)
#define ZION_DUELCORE   (160)
#define ZION_ROMIF      (176)
#define ZION_NEOCTRL    (192)
#define ZION_HOSTIF     (208)

struct ZION_Interrupt_Bits
{
#define ZION_WAKEUP_FORCED  (((u16)1)<<(15))
#define ZION_WAKEUP_TIMEOUT (((u16)1)<<(14))
  __u16 PCI_INT;
  __u16 NEO_INT;
  __u32 DUEL_INT;
  __u16 DMA_INT[3];
  __u16 DVC_INT;
  __u16 MAT_INT[4];
  __u16 AUDIO_INT[2];
  __u16 NEOCTRL_INT;
}__attribute__((packed));

/** only for kernel modules **/
#ifdef __KERNEL__

#include <linux/interrupt.h>

/* MBUS Interruption Bits (0x0010 etc.) */
#define Selectedt_Int      (15)
#define Romif_Int          (14)
#define Neoctl_Int         (13)
#define Duelcore_Int       (12)
#define Trinity_Int        (11)
#define Dmaif_Int          (10)
#define AudioProc_Int      (9)
#define Dvcif_Int          (8)
#define Matrix4_Int        (7)
#define Matrix3_Int        (6)
#define Matrix2_Int        (5)
#define Matrix1_Int        (4)
#define PciInt2            (3)
#define UpInt2             (3)
#define PciInt1            (2)
#define UpInt1             (2)
#define Hostif_Int         (1)
#define Wrok_Done          (0)

#define Pciif_Int          (16)

struct zion_file_operations;
struct _ZION_PARAMS;

typedef void (*zion_event_handler_t)(struct _ZION_PARAMS *, int, int, void *, u16);

struct _ZION_PARAMS
{
  struct pci_dev *dev;
  u16 revision;
  u32 mbus_addr;
  u32 mbus_size;
  u32 wram_addr;
  u32 wram_size;
  u32 whole_sdram_addr;
  u32 whole_sdram_size;
  u32 partial_sdram_addr;
  u32 partial_sdram_size;
  struct zion_file_operations *zion_operations[ZION_NR_MINOR];
  void *zion_private[ZION_NR_MINOR];
  zion_event_handler_t interrupt_array[17];

  wait_queue_head_t zion_wait_queue;
  struct ZION_Interrupt_Bits interrupt_bits;
  struct ZION_Interrupt_Bits interrupt_enable;

#define ZION_DEFAULT_TIMEOUT (5*HZ)

  long wait_timeout;

} ;

#define zion_params_t struct _ZION_PARAMS

struct zion_file_operations {
  loff_t (*llseek) (zion_params_t *, struct file *, loff_t, int);
  ssize_t (*read) (zion_params_t*, struct file *, char *, size_t, loff_t *);
  ssize_t (*write) (zion_params_t *, struct file *, const char *, size_t, loff_t *);
  unsigned int (*poll) (zion_params_t *, struct file *, struct poll_table_struct *);
  int (*ioctl) (zion_params_t *, struct inode *, struct file *, unsigned int, unsigned long);
  int (*mmap) (zion_params_t *, struct file *, struct vm_area_struct *);
  int (*open) (zion_params_t *, struct inode *, struct file *);
  int (*flush) (zion_params_t *, struct file *);
  int (*release) (zion_params_t *, struct inode *, struct file *);
};

typedef struct __ZION_NAMEPLATE
{
  unsigned int pid;
  zion_params_t *zion_params;
  struct list_head task_list;
  struct timer_list timer;

  struct ZION_Interrupt_Bits interrupt_bits;

#define ZION_WAKEUP         (1)
#define ZION_FORCE_WAKEUP   (((u8)1)<<(1))
#define ZION_TIMEOUT_WAKEUP (((u8)1)<<(2))

  u8 status_flags;

} zion_wait_nameplate_t;

/* for convinience */

#include <asm/byteorder.h>
#include <asm/io.h>

#ifndef CF_LE_W
#define CF_LE_W(v) __le16_to_cpu(v) /* convert From Little Endian(Word) */
#endif

#ifndef CF_LE_L
#define CF_LE_L(v) __le32_to_cpu(v) /* convert From Little Endian(Long) */
#endif

#ifndef CF_BE_W
#define CF_BE_W(v) __be16_to_cpu(v) /* convert From Big Endian(Word) */
#endif

#ifndef CF_BE_L
#define CF_BE_L(v) __be32_to_cpu(v) /* convert From Big Endian(Long) */
#endif

#ifndef CT_LE_W
#define CT_LE_W(v) __cpu_to_le16(v) /* convert To Little Endian(Word) */
#endif

#ifndef CT_LE_L
#define CT_LE_L(v) __cpu_to_le32(v) /* convert To Little Endian(Long) */
#endif

#ifndef CT_BE_W
#define CT_BE_W(v) __cpu_to_be16(v) /* convert To Big Endian(Word) */
#endif

#ifndef CT_BE_L
#define CT_BE_L(v) __cpu_to_be32(v) /* convert To Big Endian(Long) */
#endif

#define MBUS_ADDR(params,offset) ((params->mbus_addr)+offset)
#define WRAM_ADDR(params,offset) ((params->wram_addr)+offset)
#define SDRAM_PARTIAL_ADDR(params,offset) ((params->partial_sdram_addr)+offset)

static inline unsigned char mbus_readb(unsigned long addr)
{
	return ioread8((void *)addr);
}

static inline unsigned short mbus_readw(unsigned long addr)
{
	return ioread16be((void *)addr);
}

static inline unsigned long mbus_readl(unsigned long addr)
{
	return ioread32be((void *)addr);
}

static inline void mbus_writeb(unsigned char b, unsigned long addr)
{
	iowrite8(b, (void *)addr);
}

static inline void mbus_writew(unsigned short b, unsigned long addr)
{
	iowrite16be(b, (void *)addr);
}

static inline void mbus_writel(unsigned long b, unsigned long addr)
{
	iowrite32be(b, (void *)addr);
}

/* zion_core.c */
zion_params_t *find_zion(int number);

/* zion_interrupt.c */
int zion_enable_mbus_interrupt(zion_params_t *zion_params, int bit, zion_event_handler_t handler);
int zion_disable_mbus_interrupt(zion_params_t *zion_params, int bit);
int zion_mbus_int_clear(zion_params_t *zion_params, int bit);
int zion_pci_dma_int_clear(zion_params_t *zion_params, int ch);
int zion_backend_pci_int_clear(zion_params_t *params);
irqreturn_t int_zion_event(int irq, void *dev_id);
void zion_goto_bed(zion_params_t *zion_params, struct ZION_Interrupt_Bits *zion_interrupt_bits);
void zion_rout_them_up(zion_params_t *zion_params);
void zion_set_enable_bits(zion_params_t *zion_params, struct ZION_Interrupt_Bits *zion_interrupt);
void zion_get_enable_bits(zion_params_t *zion_params, struct ZION_Interrupt_Bits *zion_interrupt);

/* zion_init_list.c */
int zion_init_modules(void);
void zion_exit_modules(void);

/* for debug */
#undef PDEBUG
#ifdef NEO_DEBUG
#define PDEBUG(fmt, args...) printk(KERN_NOTICE "%s-l.%d : " fmt, __FUNCTION__, __LINE__, ## args)
#else
#define PDEBUG(fmt, args...)
#endif /* PDEBUG */

#undef PERROR
#ifdef NEO_ERROR
#define PERROR(fmt, args...) printk(KERN_ERR "%s-l.%d : " fmt, __FUNCTION__, __LINE__, ## args)
#else
#define PERROR(fmt, args...)
#endif /* PDEBUG */

#define NEO_INFO

#undef PINFO
#ifdef NEO_INFO
#define PINFO(fmt, args...) printk(KERN_WARNING fmt, ## args)
#else
#define PINFO(fmt, args...)
#endif /* PINFO */

#include <linux/zion_common.h>
#include <linux/zion_pci.h>
#include <linux/zion_dvcif.h>
#include <linux/zion_neoctrl.h>
#include <linux/zion_vga.h>

#else  /* __KERNEL__ */

#include <linux-include/linux/zion_common.h>
#include <linux-include/linux/zion_pci.h>
#include <linux-include/linux/zion_dvcif.h>
#include <linux-include/linux/zion_neoctrl.h>
#include <linux-include/linux/zion_vga.h>

#endif /* __KERNEL__ */

/** for each modules **/

#endif /* __ZION_H__ */
