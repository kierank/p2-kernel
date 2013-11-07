/******************************************************************************
 *** FILENAME	: mpc83xxdmac.c
 *** MODULE	: MPC83xx DMA Control Driver (mpc83xxdmac)
 *****************************************************************************/
/*
 *  Header files
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <asm/uaccess.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/ioctl.h>
#include <linux/interrupt.h>

#include <asm/irq.h>
#include <asm/mmu.h>
#include <asm-powerpc/dmac-ioctl.h>
#include "mpc83xxdmac.h"

#if defined(CONFIG_ZION_PCI)
# include <linux/zion.h>
#endif /* CONFIG_ZION_PCI */

extern phys_addr_t get_immrbase(void);

/*
 *  Prototype declarations
 */
int MPC83xxDmacIoctl(struct inode *pINode, struct file *pFile, unsigned int Cmd, unsigned long Arg);
int MPC83xxDmacOpen(struct inode *pINode, struct file *pFile);
int MPC83xxDmacClose(struct inode *pINode, struct file *pFile);
ssize_t MPC83xxDmacRead(struct file *filp, char *buf, size_t count, loff_t *offset);
ssize_t MPC83xxDmacWrite(struct file *filp, const char *buf, size_t count, loff_t *offset);
loff_t MPC83xxDmacSeek(struct file *filp, loff_t offset, int origin);
static irqreturn_t MPC83xxDmacInterrupt ( int irq, void *dev_id);
int MPC83xxDmacDirectMode(
	int dma_ch,unsigned int src, unsigned int dest,
	unsigned int size, unsigned int command);
static void MPC83xxDmacTimeout (unsigned long arg);
int MPC83xxDmacInstallHandler ( void );
int MPC83xxDmacRemoveHandler ( void );


/*
 *  Global valiables
 */


/* Read module parameters
 *   USE_CH : MPC83xx DMA available channels
 *             Set available channel bit.
 *             default value is 0x0000000f
 *              CH0 enable
 *              CH1 enable
 *              CH2 enable
 *              CH3 enable
 *
 *   DMA_IRQ_TEST : MPC83xx DMAC interrupt number
 *                   default value is 71
 */
static unsigned int USE_CH = 0x0000000f;
module_param(USE_CH,uint,S_IRUGO);

static unsigned int DMA_IRQ_TEST = MPC83XX_IRQ_DMA;
module_param(DMA_IRQ_TEST,uint,S_IRUGO);

/* DMA buffer */
static void *dma_buf = NULL;

/* File open flag */
static int in_use[MPC83XXDMA_CHNUM];

/* File open lock */
static spinlock_t use_lock[MPC83XXDMA_CHNUM];

/* DMA complation flag */
static int in_dma[MPC83XXDMA_CHNUM];

/* DMA lock */
static spinlock_t dma_lock[MPC83XXDMA_CHNUM];

/* Interrupt wait queues */
static wait_queue_head_t mpc83xxdmac_wq[MPC83XXDMA_CHNUM];

/* DMA status flag */
static int dma_status[MPC83XXDMA_CHNUM];

/* Timer handers */
static struct timer_list mpc83xxdmac_timer[MPC83XXDMA_CHNUM];

/* Character device structure */
struct cdev *MPC83xxDmacDev;

/* I/O mapped base address */
void *baseaddr;


/*
 *  File operation structure
 */
static struct file_operations MPC83xxDmacOperations= {
	owner	:	THIS_MODULE,
	ioctl	:	MPC83xxDmacIoctl,
	open	:	MPC83xxDmacOpen,
	release	:	MPC83xxDmacClose,
	read	:	MPC83xxDmacRead,
	write	:	MPC83xxDmacWrite,
	llseek	:	MPC83xxDmacSeek,
};


/*
 * Function
 */
static size_t get_buf_size(size_t size)
{
  size_t entry = ((size_t)1) << (sizeof(size_t)*8 - 1);
  
  if(!size)
    return 0;
  
  while(!(entry & size)){
    entry = (entry >> 1);
  }
  
  entry = (entry << 1);
  
  return entry;
}


static void *get_dma_space(size_t *entry_size)
{
  void *ptr;
  
  if(!entry_size){
    printk(KERN_ERR "[%s:%d] argument is null pointer\n", __FUNCTION__, __LINE__);
    return NULL;
  }
  
  // *entry_size = pow(2,n) ?
  if((*entry_size & (*entry_size - 1)) || (*entry_size > MPC83XXDMAC_DMAC_MAX_SIZE)){
    printk(KERN_ERR "[%s:%d] Invalid Size.\n", __FUNCTION__, __LINE__);
    *entry_size = 0;
    return NULL;
  }
  
 RETRY:
  if(get_order(*entry_size) <= MPC83XXDMAC_PAGE_ORDER)
    return dma_buf;

  ptr = (void *)__get_dma_pages(GFP_KERNEL, get_order(*entry_size));
  if(ptr){
    return ptr;
  }
  
  if(*entry_size >= PAGE_SIZE){
    *entry_size >>= 1;
    goto RETRY;
  }
  
  *entry_size = 0;
  
  return NULL;
}


/******************************************************************************
 *** FUNCTION	: init_module
 *** INPUT	:
 *** OUTPUT	:
 *** RETURN	: 0  : success
 *** NOTE	: Initialize module
 ******************************************************************************/
int init_module(void)
{
  int Err;
  int i;
  int devno = MKDEV(MPC83XXDMAC_MAJOR,0);
  
  printk(KERN_INFO "MPC83xx DMA Control driver ver.%s\n", VERSION);
  
  /* Display supported DMA channels */
#ifdef DEBUG_MODE
  for(i = 0; i < MPC83XXDMA_CHNUM; i++){
    if( (USE_CH & (1 << i)) == 0 )
      continue;
    DbgPrint((" <DMA Ch%d>\n",i));
  }
#endif //DEBUG_MODE
  
  Err = register_chrdev_region(devno, MPC83XXDMA_CHNUM, MPC83XXDMAC_NAME);
  if(Err) {
    printk(KERN_ERR "[MPC83XXDMAC %s:%d] Error! Failed to register driver.\n", __FUNCTION__, __LINE__);
    return Err;
  }
  
  /* Charactor driver device */
  MPC83xxDmacDev = cdev_alloc();
  if(MPC83xxDmacDev == NULL){
    printk(KERN_ERR "[MPC83XXDMAC %s:%d] cdev_alloc failed\n", __FUNCTION__, __LINE__);
    unregister_chrdev_region(devno, MPC83XXDMA_CHNUM);
    return -ENOMEM;
  }
  MPC83xxDmacDev->ops = &MPC83xxDmacOperations;
  MPC83xxDmacDev->owner = THIS_MODULE;
  
  /* Register driver */
  Err = cdev_add(MPC83xxDmacDev, devno, MPC83XXDMA_CHNUM);
  
  /* legacy driver registration */
  //  Err = register_chrdev(MPC83XXDMAC_MAJOR, MPC83XXDMAC_NAME,&MPC83xxDmacOperations);
  
  if(Err < 0){
    printk(KERN_ERR "[MPC83CCDMAC %s:%d] Error! Failed to register driver.\n", __FUNCTION__, __LINE__);
    Err = -EIO;
    goto CDEV_DEL;
  }
  
  /* Install handler */
  Err = MPC83xxDmacInstallHandler();
  if(Err < 0){
    Err = -EIO;
    goto CDEV_DEL;
  }
  
  /* Initialize global variables */
  for(i = 0; i < MPC83XXDMA_CHNUM; i++){
    in_use[i] = 0;
    in_dma[i] = 0;
    dma_status[i] = 0;
    
    spin_lock_init(&use_lock[i]);
    spin_lock_init(&dma_lock[i]);
    
    /* Initializing wait queue */
    init_waitqueue_head(&mpc83xxdmac_wq[i]);
    
    /* Initializing timer */
    init_timer(&mpc83xxdmac_timer[i]);
    mpc83xxdmac_timer[i].data = (unsigned long)i;
    mpc83xxdmac_timer[i].function = MPC83xxDmacTimeout;
  }
  
  /* Set I/O region */
  if(request_mem_region(
			get_immrbase() + MPC83XX_DMAUNIT_START,
			MPC83XX_DMAUNIT_SIZE, "MPC83xx DMA Unit") == NULL){
    printk(KERN_ERR "[MPC83XXDMAC %s:%d] request_mem_region failed\n", __FUNCTION__, __LINE__);
    Err = -ENOMEM;
    goto CDEV_DEL;
  }
  
  /* I/O map DMA unit */
  baseaddr = ioremap_nocache(get_immrbase() + MPC83XX_DMAUNIT_START, MPC83XX_DMAUNIT_SIZE);
  if(baseaddr == NULL){
    printk(KERN_ERR "[MPC83XXDMAC %s:%d] ioremap_nocache failed\n", __FUNCTION__, __LINE__);
    Err = -ENOMEM;
    goto RELEASE_MEM_REGION;
  }
  DbgPrint(("[MPC83XXDMAC %s] baseaddr:0x%x\n",__FUNCTION__,(unsigned int)baseaddr));
  
  /* Get DMA buffer */
  if(MPC83XXDMAC_PAGE_ORDER){
    dma_buf = (void *)__get_dma_pages(GFP_KERNEL, MPC83XXDMAC_PAGE_ORDER);
    if(!dma_buf){
      printk(KERN_ERR "[%s:%d] cannot get buf\n", __FUNCTION__, __LINE__);
      Err = -ENOMEM;
      goto RELEASE_MEM_REGION;
    }
    DbgPrint(("[MPC83XXDMAC %s] get order%d pages\n",__FUNCTION__,MPC83XXDMAC_PAGE_ORDER));
  }
  return 0;
  
 RELEASE_MEM_REGION:
  release_mem_region(get_immrbase() + MPC83XX_DMAUNIT_START, MPC83XX_DMAUNIT_SIZE);
  
 CDEV_DEL:
  cdev_del(MPC83xxDmacDev);
  unregister_chrdev_region(devno, MPC83XXDMA_CHNUM);
  
  return Err;
}


/******************************************************************************
 *** FUNCTION	: cleanup_module
 *** INPUT	:
 *** OUTPUT	:
 *** RETURN	: none
 *** NOTE	: remove kernel module
 ******************************************************************************/
void cleanup_module(void)
{
  int devno = MKDEV(MPC83XXDMAC_MAJOR,0);
  int i;
  
  //  unregister_chrdev(MPC83XXDMAC_MAJOR,MPC83XXDMAC_NAME);
  
  if( MPC83xxDmacRemoveHandler() < 0) {
    printk(KERN_ERR "[MPC83XXDMAC %s:%d] DMA CH close error\n", __FUNCTION__, __LINE__);
  }
  
  cdev_del(MPC83xxDmacDev);
  unregister_chrdev_region(devno, MPC83XXDMA_CHNUM);
  
  /* Unmap I/O area */
  iounmap(baseaddr);
  
  /* Release I/O region */
  release_mem_region(get_immrbase() + MPC83XX_DMAUNIT_START, MPC83XX_DMAUNIT_SIZE);
  
  /* Deleted timer */
  for(i = 0; i < MPC83XXDMA_CHNUM; i++){
    del_timer_sync(&mpc83xxdmac_timer[i]);
  }

  if(dma_buf){
    free_pages((unsigned long)dma_buf, MPC83XXDMAC_PAGE_ORDER);
    dma_buf = NULL;
  }
}


/******************************************************************************
 *** FUNCTION	: MPC83xxDmacOpen
 *** INPUT	:
 *** OUTPUT	:
 *** RETURN	:  0       : success
                :  -EINVAL : invalid device open
 	        :  -EBUSY  : device is in use
 *** NOTE	: Open DMA device
 ******************************************************************************/
int MPC83xxDmacOpen(struct inode *pINode, struct file *pFile)
{
  int dma_ch;
  unsigned int minor = MINOR( pINode -> i_rdev);
  
  dma_ch = minor;
  DbgPrint(("[%s] dma_ch=%d\n",__FUNCTION__,dma_ch));
  
  /* Supported DMA channel? */
  if( (USE_CH & (1 << dma_ch)) == 0 ) {
    printk(KERN_ERR "[%s:%d] Invalid DMA ch!\n",__FUNCTION__,__LINE__);
    return -EINVAL;
  }
  
  spin_lock(&use_lock[dma_ch]);
  
  /* 
   * Is this device in use?
   * Only one-process is able to open this device.
   */
  if( in_use[dma_ch] ) {
    printk(KERN_ERR "[%s:%d] DMA CH%d busy\n", __FUNCTION__, __LINE__, dma_ch);
    spin_unlock(&use_lock[dma_ch]);
    return -EBUSY;
  }      
  
  /* now, in use. */
  in_use[dma_ch] = 1;
  
  spin_unlock(&use_lock[dma_ch]);
  
  return 0;
}


/******************************************************************************
 *** FUNCTION	: MPC83xxDmacClose
 *** INPUT	:
 *** OUTPUT	:
 *** RETURN	:  0       : success
                :  -EINVAL : invalid device close
 *** NOTE	: Close DMA device.
 ******************************************************************************/
int MPC83xxDmacClose(struct inode *pINode, struct file *pFile)
{
  int dma_ch;
  unsigned int minor = MINOR( pINode -> i_rdev);
  
  dma_ch = minor;
  
  DbgPrint(("[%s] dma_ch=%d\n",__FUNCTION__,dma_ch));
  
  /* Supported DMA channel? */
  if( (USE_CH & (1 << dma_ch)) == 0 ) {
    printk(KERN_ERR "[%s:%d] Invalid DMA ch!\n",__FUNCTION__,__LINE__);
    return -EINVAL;
  }
  
  spin_lock(&use_lock[dma_ch]);
  
  /* 
   * Is this device in use?
   * Only one-process is able to open this device.
   */
  if( in_use[dma_ch] == 0 ) {
    DbgPrint(("[%s] DMA CH%d is not use\n", __FUNCTION__, dma_ch));
    spin_unlock(&use_lock[dma_ch]);
    return -EINVAL;
  }      
  
  /* Not in use. */
  in_use[dma_ch] = 0;
  
  spin_unlock(&use_lock[dma_ch]);
  
  return 0;
}


/******************************************************************************
 *** FUNCTION	: MPC83xxDmacIoctl
 *** INPUT	:
 *** OUTPUT	:
 *** RETURN	:  0       : success
                :  -EINVAL : invalid parameter
 	        :  -EFAULT : memory error
 *** NOTE	: Device ioctl
 ******************************************************************************/
int MPC83xxDmacIoctl(struct inode *pINode, struct file *pFile, unsigned int cmd, unsigned long arg)
{
  int ret = 0;
  DmaStruct param;
  unsigned int minor = MINOR( pINode -> i_rdev);
  
  DbgPrint(("[%s] arg=%x\n",__FUNCTION__,(unsigned int)arg));
  
  switch (cmd) {
    
  case IOCTL_DMAC_DIRECT_DMA:
    /* DMA Transfer in Direct mode */
    if (copy_from_user(&param, (DmaStruct *)arg, sizeof(DmaStruct))) {
      return -EFAULT;
    }
    
    DbgPrint(("[MPC83xx DMA driver] Direct mode transfer\n"));
    DbgPrint(("   src_addr  = 0x%08x\n",param.src_addr));
    DbgPrint(("   dest_addr = 0x%08x\n",param.dest_addr));
    
    ret = MPC83xxDmacDirectMode(minor,
				param.src_addr,
				param.dest_addr,
				param.size,
				param.command);
    
    if(ret < 0){
      printk(KERN_ERR "[%s:%d] cannot transfer\n", __FUNCTION__, __LINE__);
      return -EIO;
    }
    
    return 0;
    
  default:
    return -EINVAL;		
  }
}


/******************************************************************************
 *** FUNCTION	: MPC83xxDmacRead
 *** INPUT	:
 *** OUTPUT	:
 *** RETURN	:  Read Size : success
                :  -ENOMEM   : No Memory
                :  -EIO      : I/O Error
 *** NOTE	: Device Read
 ******************************************************************************/
ssize_t MPC83xxDmacRead(struct file *filp, char *buf, size_t count, loff_t *offset)
{
  size_t read_size = 0;
  unsigned int minor = MINOR(filp->f_dentry->d_inode->i_rdev);
  ssize_t ret_val;
  
  while(count){
    size_t buf_size, net_size;
    void *kbuf;
    
    buf_size = get_buf_size(count);
    buf_size = min(buf_size, (size_t)MPC83XXDMAC_DMAC_MAX_SIZE);
    
    kbuf = get_dma_space(&buf_size);
    if(!kbuf){
      printk(KERN_ERR "[%s:%d] cannot get buf\n", __FUNCTION__, __LINE__);
      return -ENOMEM;
    }
    
    net_size = min(count, buf_size);
    
    ret_val = MPC83xxDmacDirectMode(
				    minor,
				    (unsigned int)*offset,
				    (unsigned int)virt_to_bus(kbuf),
				    net_size,
				    DMAC_READ);
    if(ret_val < 0){
      printk(KERN_ERR "[%s:%d] cannot transfer\n", __FUNCTION__, __LINE__);
      if(get_order(buf_size) > MPC83XXDMAC_PAGE_ORDER)
	free_pages((unsigned long)kbuf, get_order(buf_size));
      return -EIO;
    }
    
    if(copy_to_user(buf, kbuf, net_size)){
      printk(KERN_ERR "[%s:%d] copy_to_user failed\n", __FUNCTION__, __LINE__);
      if(get_order(buf_size) > MPC83XXDMAC_PAGE_ORDER)
	free_pages((unsigned long)kbuf, get_order(buf_size));
      return -EFAULT;
    }
    buf += net_size;
    
    *offset += net_size;
    read_size += net_size;
    
    count -= net_size;
    
    if(get_order(buf_size) > MPC83XXDMAC_PAGE_ORDER)
      free_pages((unsigned long)kbuf, get_order(buf_size));
  }
  
  return read_size;
}


/******************************************************************************
 *** FUNCTION	: MPC83xxDmacWrite
 *** INPUT	:
 *** OUTPUT	:
 *** RETURN	:  write size  : success
                :  -ENOMEM     : No Memory
                :  -EIO        : I/O Error
 *** NOTE	: Device Write
 ******************************************************************************/
ssize_t MPC83xxDmacWrite(struct file *filp, const char *buf, size_t count, loff_t *offset)
{
  size_t written_size = 0;
  unsigned int minor = MINOR(filp->f_dentry->d_inode->i_rdev);
  ssize_t ret_val;
  
  while(count){
    size_t buf_size, net_size;
    void *kbuf;
    
    buf_size = get_buf_size(count);
    buf_size = min(buf_size, (size_t)MPC83XXDMAC_DMAC_MAX_SIZE);
    
    kbuf = get_dma_space(&buf_size);
    if(!kbuf){
      printk(KERN_ERR "[%s:%d] cannot get buf\n",__FUNCTION__, __LINE__);
      return -ENOMEM;
    }
    
    net_size = min(count, buf_size);
    
    if(copy_from_user(kbuf, buf, net_size)){
      printk(KERN_ERR "[%s:%d] copy_from_user failed\n", __FUNCTION__, __LINE__);
      if(get_order(buf_size) > MPC83XXDMAC_PAGE_ORDER)
	free_pages((unsigned long)kbuf, get_order(buf_size));
      return -EFAULT;
    }
    buf += net_size;
    
    ret_val = MPC83xxDmacDirectMode(
				    minor,
				    (unsigned int)virt_to_bus(kbuf),
				    (unsigned int)*offset,
				    net_size,
				    DMAC_WRITE);
    
    if(ret_val < 0){
      printk(KERN_ERR "[%s:%d] cannot transfer\n", __FUNCTION__, __LINE__);
      if(get_order(buf_size) > MPC83XXDMAC_PAGE_ORDER)
	free_pages((unsigned long)kbuf, get_order(buf_size));
      return -EIO;
    }
    
    *offset += net_size;
    written_size += net_size;
    
    count -= net_size;
    
    if(get_order(buf_size) > MPC83XXDMAC_PAGE_ORDER)
      free_pages((unsigned long)kbuf, get_order(buf_size));
  }
  
  return written_size;
}


/******************************************************************************
 *** FUNCTION	: MPC83xxDmacSeek
 *** INPUT	:
 *** OUTPUT	:
 *** RETURN	:  offset  : success
                :  -EINVAL : invalid parameter
 *** NOTE	: Device llseek 
 ******************************************************************************/
loff_t MPC83xxDmacSeek(struct file *filp, loff_t offset, int origin)
{
  switch(origin){
  case 2:
    printk(KERN_ERR "[%s:%d] You Can't Use SEEK_END.\n", __FUNCTION__,__LINE__);
    return -EINVAL;
    
  case 1:
    offset += filp->f_pos;
  }
  
  if(offset != filp->f_pos){
    filp->f_pos = offset;
  }
  
  return offset;
}


/******************************************************************************
 *** FUNCTION	: MPC83xxDmacDirectMode
 *** INPUT	:
 *** OUTPUT	:
 *** RETURN	: 0   : success
	        : -1  : failed
 *** NOTE	: DMA Transfer in Direct Mode
 ******************************************************************************/
int MPC83xxDmacDirectMode(
	int dma_ch,unsigned int src, unsigned int dest, unsigned int size, unsigned int command)
{ 
  //  int counter=0;
  int ret_val = 0;
  
  DbgPrint(("[%s] ch=%d src_addr=0x%08x dest_addr=0x%08x size=0x%08x command=%d\n"
	    ,__FUNCTION__
	    ,(unsigned int)dma_ch
	    ,(unsigned int)src
	    ,(unsigned int)dest
	    ,(unsigned int)size
	    ,(unsigned int)command ));
  
  if(dma_ch > MPC83XXDMA_CHNUM)
    return -1;
  
  spin_lock(&dma_lock[dma_ch]);
  
  /*
   * Check Channel busy
   */
  
  if((ioread32(baseaddr + MPC83xxDmaStatusReg[dma_ch]) && DMASR_CB) != 0){
    printk(KERN_ERR "[%s:%d] DMA ch%d is busy!\n", __FUNCTION__, __LINE__, dma_ch);
    spin_unlock(&dma_lock[dma_ch]);
    return -EBUSY;
  }
  
  /*
   * Set DMA source/destination address and DMA transmit counter.
   */
  if(command == DMAC_READ){
    /* DMA Read */

#if defined(CONFIG_ZION_PCI)
    ret_val = ZION_check_addr_and_pci_cache_clear(src);
    if(ret_val<0)
      {
	printk(KERN_ERR "[%s:%d] ZION_pci_cache_clear Failed.\n", __FUNCTION__, __LINE__);
	return ret_val;
      }
#endif /* CONFIG_ZION_PCI */
    
    //    dma_cache_inv(bus_to_virt(dest), size);
    dma_cache_sync(NULL, bus_to_virt(dest), size, DMA_FROM_DEVICE);
    iowrite32( src, baseaddr + MPC83xxDmaSourceAddressReg[dma_ch]);
    iowrite32( __pa(bus_to_virt(dest)), baseaddr + MPC83xxDmaDestinationAddressReg[dma_ch]);
  }
  else{
    /* DMA Write */
    //    dma_cache_wback(bus_to_virt(src), size);
    dma_cache_sync(NULL, bus_to_virt(src), size, DMA_TO_DEVICE);
    iowrite32( __pa(bus_to_virt(src)), baseaddr + MPC83xxDmaSourceAddressReg[dma_ch]);
    iowrite32( dest, baseaddr + MPC83xxDmaDestinationAddressReg[dma_ch]);
  }
  
  iowrite32( size, baseaddr + MPC83xxDmaCountReg[dma_ch]);
  
  /* Print Registers */
  DbgPrint((" SrcReg   : %08x\n",ioread32(baseaddr + MPC83xxDmaSourceAddressReg[dma_ch])));
  DbgPrint((" DestReg  : %08x\n",ioread32(baseaddr + MPC83xxDmaDestinationAddressReg[dma_ch])));
  DbgPrint((" CountReg : %08x\n",ioread32(baseaddr + MPC83xxDmaCountReg[dma_ch])));
  DbgPrint((" ModeReg  : %08x\n",ioread32(baseaddr + MPC83xxDmaModeReg[dma_ch])));
  DbgPrint((" StatusReg: %08x\n",ioread32(baseaddr + MPC83xxDmaStatusReg[dma_ch])));
  
  /*
   * Start DMA
   */
  in_dma[dma_ch] = 1;
  dma_status[dma_ch] = MPC83XXDMAC_DMA_NONE; /* Clear DMA status */

  /* Start timer */
  if (mpc83xxdmac_timer[dma_ch].function){
    mpc83xxdmac_timer[dma_ch].expires = MPC83XXDMAC_TIMEOUT + jiffies;
    add_timer(&mpc83xxdmac_timer[dma_ch]);
  }

  iowrite32( (DMAMR_DMSEN|DMAMR_CTM_DIRECT|DMAMR_CS|DMAMR_EOTIE_INT|DMAMR_TEM_ERROR), baseaddr + MPC83xxDmaModeReg[dma_ch]);

  /*
   * Wait DMA completion flag.
   */

  /*  
  while((ioread32(baseaddr + MPC83xxDmaStatusReg[dma_ch]) && DMASR_CB) != 0){
    counter++;
  }
  */ 
  wait_event( mpc83xxdmac_wq[dma_ch], (in_dma[dma_ch]==0));

  in_dma[dma_ch] = 0;

  /* Check DMA status */
  DbgPrint(("[%s] dma_status = %d\n",__FUNCTION__,dma_status[dma_ch]));

  switch(dma_status[dma_ch]){
  case MPC83XXDMAC_DMA_ERROR:
    printk(KERN_ERR "[%s:%d] transfer failed!\n", __FUNCTION__, __LINE__);
    ret_val = -EIO;
    break;

  case MPC83XXDMAC_DMA_TIMEOUT:
    printk(KERN_ERR "[%s:%d] transfer timeout!\n", __FUNCTION__, __LINE__);
    ret_val = -ETIMEDOUT;
    break;

  case MPC83XXDMAC_DMA_NONE:
    printk(KERN_WARNING "[%s:%d] transfer not completed!\n", __FUNCTION__, __LINE__);
    break;

  default: /* success */
    break;
  }

  /* Clear DMA status */
  dma_status[dma_ch] = MPC83XXDMAC_DMA_NONE;

//  DbgPrint(("counter = %d\n",counter));
  DbgPrint((" CountReg : %08x\n",ioread32(baseaddr + MPC83xxDmaCountReg[dma_ch])));

  spin_unlock(&dma_lock[dma_ch]);

  return ret_val;
}


/******************************************************************************
 *** FUNCTION	: MPC83xxDmacInterrupt
 *** INPUT	:
 *** OUTPUT	:
 *** RETURN	: 
 *** NOTE	: DMAC Interrupt handler
 ******************************************************************************/
static irqreturn_t MPC83xxDmacInterrupt (int irq, void *dev_id)
{
  int i;
  int dma_ch;
  unsigned long dmasr = 0L;
  
  /* Undefined IRQ */
  if( DMA_IRQ_TEST != irq )
    return IRQ_RETVAL(IRQ_NONE);
  
  /* Probe interrupt channel. */
  for(i=0;i<MPC83XXDMA_CHNUM;i++){
    dmasr = ioread32(baseaddr + MPC83xxDmaStatusReg[i]);
    if(dmasr)
      break;
  }
  
  /* Undefined IRQ */
  if ( i == MPC83XXDMA_CHNUM )
    return IRQ_RETVAL(IRQ_NONE);
  
  dma_ch=i;
  
  /* Delete timer */
  del_timer(&mpc83xxdmac_timer[dma_ch]);
  
  DbgPrint(("[%s] DMAC %d Interrupt!\n",__FUNCTION__,dma_ch));
  
  /* Clear DMA interrupt status. */
  DbgPrint(("[%s] Clear DMA interrupt status.\n", __FUNCTION__));
  iowrite32( DMASR_EOCDI|DMASR_EOSI, baseaddr + MPC83xxDmaStatusReg[dma_ch]);
  
  /* DMA flag */
  in_dma[dma_ch] = 0;
  
  /* Set DMA status */
  if (!dma_status[dma_ch]){
    if(dmasr&DMASR_TE){
      /* DMA error occurred */
      dma_status[dma_ch] = MPC83XXDMAC_DMA_ERROR;
    } else {
      /* DMA success */
      dma_status[dma_ch] = MPC83XXDMAC_DMA_SUCCESS;
    }
  }
  
  /* Wake up! */
  wake_up( &mpc83xxdmac_wq[dma_ch]);
  
  return IRQ_RETVAL(IRQ_HANDLED);
}


/******************************************************************************
 *** FUNCTION	: MPC83xxDmacTimeout
 *** INPUT	: arg : DMA channel number
 *** OUTPUT	: 
 *** RETURN	: 
 *** NOTE	: DMAC timer handler
 ******************************************************************************/
static void MPC83xxDmacTimeout (unsigned long arg)
{
  int dma_ch = (int)arg;

  /* FIXME: cannot stop interrupt. */

  /* Set DMA status */
  dma_status[dma_ch] = MPC83XXDMAC_DMA_TIMEOUT;

  /* Delete timer */
  del_timer(&mpc83xxdmac_timer[dma_ch]);

  DbgPrint(("[%s] DMAC %d Timeout!\n",__FUNCTION__,dma_ch));

  /* DMA flag */
  in_dma[dma_ch] = 0;

  /* Wake up! */
  wake_up(&mpc83xxdmac_wq[dma_ch]);

  return;
}


/******************************************************************************
 *** FUNCTION	: MPC83xxDmacInstallHandler
 *** INPUT	:
 *** OUTPUT	:
 *** RETURN	: 0   : success
	        : -1  : failed
 *** NOTE	: Register interrupt handler
 ******************************************************************************/
int MPC83xxDmacInstallHandler ( void )
{
  int result;
  
  result = request_irq( DMA_IRQ_TEST, MPC83xxDmacInterrupt, IRQF_DISABLED, "MPC83xxDMAC", MPC83xxDmacDev);

  DbgPrint(("[%s] result=%d\n", __FUNCTION__, result));

  if ( result < 0) {
    printk(KERN_ERR "[%s:%d] can't get assigned irq %d\n", __FUNCTION__, __LINE__, DMA_IRQ_TEST);
    return result;
  }

  DbgPrint(("[%s] interrupt %d hooked\n", __FUNCTION__, DMA_IRQ_TEST));
/*   DbgPrint(("[%s] Clear DMA interrupt status.\n")); */

  return result;
}


/******************************************************************************
 *** FUNCTION	: MPC83xxDmacRemoveHandler
 *** INPUT	:
 *** OUTPUT	:
 *** RETURN	: 0   : success
	        : -1  : failed
 *** NOTE	: Remove interrupt handler
 ******************************************************************************/
int MPC83xxDmacRemoveHandler ( void )
{
  /* release IRQ */
  free_irq ( DMA_IRQ_TEST, NULL);
  DbgPrint(("[%s] interrupt %d disabled\n", __FUNCTION__, DMA_IRQ_TEST));
  
  return 0;
}


MODULE_LICENSE("GPL");

module_init(init_module);
module_exit(cleanup_module);
