#ifndef __ZION_PCI_H__
#define __ZION_PCI_H__

struct zion_sdram_region
{
  int dma_ch;
  unsigned long lower;
  unsigned long upper;
};

/* PCI Config Reg. Read/Write */
#define ZION_PCI_READ_CONFIG_BYTE _IOR(ZION_MAGIC,0, struct zion_config_byte)
#define ZION_PCI_READ_CONFIG_WORD _IOR(ZION_MAGIC,1, struct zion_config_word)
#define ZION_PCI_READ_CONFIG_DWORD _IOR(ZION_MAGIC,2, struct zion_config_dword)
#define ZION_PCI_WRITE_CONFIG_BYTE _IOW(ZION_MAGIC,3, struct zion_config_byte)
#define ZION_PCI_WRITE_CONFIG_WORD _IOW(ZION_MAGIC,4, struct zion_config_word)
#define ZION_PCI_WRITE_CONFIG_DWORD _IOW(ZION_MAGIC,5, struct zion_config_dword)

/* SDRAM Read/Write (PIO) */
#define ZION_SDRAM_PIO_READ _IOR(ZION_MAGIC,14, struct zion_buf)
#define ZION_SDRAM_PIO_WRITE _IOW(ZION_MAGIC,15, struct zion_buf)

/* SDRAM Read/Write (DMA) */
#define ZION_DMA_READ  0
#define ZION_DMA_WRITE 1
#define ZION_SDRAM_DMA_READ _IOR(ZION_MAGIC,16, struct zion_buf)
#define ZION_SDRAM_DMA_WRITE _IOW(ZION_MAGIC,17, struct zion_buf)

#define ZION_SET_DMA_REGION _IOW(ZION_MAGIC,18, struct zion_sdram_region)
#define ZION_GET_DMA_REGION _IOR(ZION_MAGIC,19, struct zion_sdram_region)

/* Reset ZION */
#define ZION_PCI_RESET _IO(ZION_MAGIC,20)

/* Get Resource Address and Size */
#define ZION_GET_SDRAM_BUS_ADDR _IOR(ZION_MAGIC,21, unsigned long)
#define ZION_GET_SDRAM_SIZE     _IOR(ZION_MAGIC,22, unsigned long)

/* For Debug */
#define ZION_SDRAM_DMA_READ_BUS_ADDR  _IOR(ZION_MAGIC,50, struct zion_buf)
#define ZION_SDRAM_DMA_WRITE_BUS_ADDR _IOW(ZION_MAGIC,51, struct zion_buf)

#ifdef __KERNEL__

/** Params **/

#define ZION_PCI_PORT_OFFSET (ZION_PCI+1)

#define NEO_LATENCY 32
#define NEO_DMA_CH 8

/* DMA Timeout (2sec) */
#define NEO_DMA_TIMEOUT (2*HZ)

/* DMA Chain Table */
#define DMA_MAX_ENTRY_SIZE (32768)

#define DMA_CHAIN_END 0x0200
#define DMA_CHANGE_CH 0x0100

typedef struct _NEO_DMA_ELEMENT
{
  u32 distination;
  u16 length;
  u16 flags;
}__attribute__ ((packed)) neo_dma_t;

typedef struct _NEO_DMA_ENT
{
  void *data;
  ssize_t size;
} neo_dma_ent_t;

#define ZION_PCI_INT_DISPATCH_DONE     0
#define ZION_PCI_INT_DISPATCH_PENDING  1
#define ZION_PCI_INT_DISPATCH_TIMEOUT  2

#define NEO_MAX_ENTRIES (PAGE_SIZE/8)

typedef struct _NEO_DMA_PARAM
{
  void *dma_chain;
  int entries;
  neo_dma_ent_t dma_entries[NEO_MAX_ENTRIES];
  struct timer_list timer;
  wait_queue_head_t neo_dma_wait_queue;
  int condition;
  struct semaphore dma_sem;
} neo_dma_params_t;

typedef struct _ZION_PCI_PARAM
{
  neo_dma_params_t dma_params[NEO_DMA_CH];
  struct semaphore zion_sdram_semaphore;
  wait_queue_head_t zion_pci_wait_queue;
} zion_pci_params_t;

#define ZION_PCI_PARAM(param) ((zion_pci_params_t *)((param)->zion_private[ZION_PCI]))

#define ZION_PCI_DMA_USER (0)
#define ZION_PCI_DMA_KERNEL (1)

/* init & exit modules */
int init_zion_pci(void);
void exit_zion_pci(void);

int ZION_pci_cache_clear(void);
int ZION_check_addr_and_pci_cache_clear(unsigned long addr);

/* For Debug Functions */
int zion_pci_ioctl_for_debug
(zion_params_t *zion_params, struct inode *inode, struct file *filp,
 unsigned int function, unsigned long arg);

int terminate_sg_table(zion_params_t *params, int ch);
int neo_get_region(zion_params_t *params, int dma_ch, unsigned long *lower, unsigned long *upper);
int neo_set_region(zion_params_t *params, int dma_ch, unsigned long lower, unsigned long upper); /* Add 2006/03/15 for ZION VGA */
int init_dma_ch(zion_params_t *params, int ch);
void neo_dma_timeout(unsigned long neo_params);

#endif /* __KERNEL__ */

#endif  /* __ZION_PCI_H__ */
