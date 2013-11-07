#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <asm/irq.h>
#include <asm/mmu.h>
#include <asm/io.h>

#include <linux/p2ioport.h>
#include "mpc83xxgpioint.h"

extern phys_addr_t get_immrbase(void);
static struct p2ioport_operations p2io_ops;

/*
 *  Prototype declarations
 */
int MPC83xxGpioIoctl(struct inode *pINode, struct file *pFile, unsigned int Cmd, unsigned long Arg);
int MPC83xxGpioOpen(struct inode *pINode, struct file *pFile);
int MPC83xxGpioClose(struct inode *pINode, struct file *pFile);
static irqreturn_t MPC83xxGpioInterrupt ( int irq, void *dev_id);
int MPC83xxGpioInstallHandler ( void );
int MPC83xxGpioRemoveHandler ( void );
void MPC83xxGpioInitReg(void);


/*
 *  Global valiables
 */


/*
 *  Read module parameters
 */
/* GPIO_DIR     : GPIO Direction */
static unsigned int GPIO_DIR = 0x00000000;
module_param(GPIO_DIR,uint,S_IRUGO);

/* GPIO_ODR     : GPIO open drain */
static unsigned int GPIO_ODR = 0x00000000;
module_param(GPIO_ODR,uint,S_IRUGO);

/* GPIO_IMR     : GPIO Interrupt mask register */
unsigned int GPIO_IMR = 0x00000000;
module_param(GPIO_IMR,uint,S_IRUGO);

/* Interrupt wait queues */
static wait_queue_head_t mpc83xxgpio_wq;

/* GPIO status flag */
static unsigned long gpio_status;

/* I/O mapped base address */
void *gpio_baseaddr;

/* In use flag */
int in_use;


/*
 * Function
 */
int MPC83xxGpioInitModule(void)
{
  int Err;
  
  printk(KERN_INFO "MPC83xx GPIO Control driver ver.%s\n", MPC83XXGPIO_VERSION);
  
  /* Get P2 ioport operations */
  memset( &p2io_ops, 0, sizeof(p2io_ops));
  p2ioport_get_operations( &p2io_ops );

  /* Install handler */
  Err = MPC83xxGpioInstallHandler();
  if(Err < 0){
    Err = -EIO;
    goto EXIT;
  }
  
  /* Initialize global variables */
  gpio_status = 0;
  in_use = 0;

  /* Initializing wait queue */
  init_waitqueue_head(&mpc83xxgpio_wq);
    
  /* Set I/O region */
  if(request_mem_region( get_immrbase() + MPC83XX_GPIOUNIT_START, MPC83XX_GPIOUNIT_SIZE, "MPC83xx GPIO Unit") == NULL) {
    printk(KERN_ERR "[MPC83XXGPIO %s:%d] request_mem_region failed\n", __FUNCTION__, __LINE__);
    Err = -ENOMEM;
    goto EXIT;
  }
  
  /* I/O map GPIO unit */
  gpio_baseaddr = ioremap_nocache(get_immrbase() + MPC83XX_GPIOUNIT_START, MPC83XX_GPIOUNIT_SIZE);
  if(gpio_baseaddr == NULL){
    printk(KERN_ERR "[MPC83XXGPIO %s:%d] ioremap_nocache failed\n", __FUNCTION__, __LINE__);
    Err = -ENOMEM;
    goto RELEASE_MEM_REGION;
  }
  DbgPrint(("[MPC83XXGPIO %s] gpio_baseaddr:0x%x\n",__FUNCTION__,(unsigned int)gpio_baseaddr));

  /* Initialize GPIO controller. */
  MPC83xxGpioInitReg();

  return 0;
  
 RELEASE_MEM_REGION:
  release_mem_region(get_immrbase() + MPC83XX_GPIOUNIT_START, MPC83XX_GPIOUNIT_SIZE);
  
 EXIT:
  return Err;
}


void MPC83xxGpioInitReg(void)
{
  unsigned long gpiosr = 0L;
  
  /* Get Interrupt event status. */
  gpiosr = ioread32be(gpio_baseaddr + MPC83XX_GPIO_INTEVENT);

  /* Clear DMA interrupt status. */
  DbgPrint(("[%s] Clear DMA interrupt status.\n", __FUNCTION__));
  iowrite32be( gpiosr, gpio_baseaddr + MPC83XX_GPIO_INTEVENT);

  /* Set Direction */
/*   DbgPrint(("[%s] set direction reg. 0x%08X.\n", __FUNCTION__, GPIO_DIR)); */
/*   iowrite32be( GPIO_DIR, gpio_baseaddr + MPC83XX_GPIO_DIRECTION); */

  /* Set open drain register */
/*   DbgPrint(("[%s] set drain reg. 0x%08X.\n", __FUNCTION__, GPIO_ODR)); */
/*   iowrite32be( GPIO_ODR, gpio_baseaddr + MPC83XX_GPIO_OPENDRAIN); */

  /* Set interrupt mask register */
  DbgPrint(("[%s] set int mask reg. 0x%08X.\n", __FUNCTION__, GPIO_IMR));
  iowrite32be( GPIO_IMR, gpio_baseaddr + MPC83XX_GPIO_INTMASK);

}



void MPC83xxGpioCleanupModule(void)
{
  if( MPC83xxGpioRemoveHandler() < 0) {
    printk(KERN_ERR "[MPC83XXGPIO %s:%d] GPIO close error\n", __FUNCTION__, __LINE__);
  }
  
  /* Unmap I/O area */
  iounmap(gpio_baseaddr);
  
  /* Release I/O region */
  release_mem_region(get_immrbase() + MPC83XX_GPIOUNIT_START, MPC83XX_GPIOUNIT_SIZE);

}


int MPC83xxGpioGetStatus(unsigned long *status)
{ 
  int ret_val = 0;
  
  *status = ioread32be(gpio_baseaddr + MPC83XX_GPIO_DATA);
  
  return ret_val;
}


static irqreturn_t MPC83xxGpioInterrupt (int irq, void *dev_id)
{
  unsigned long gpiosr = 0L;
  
  /* Undefined IRQ */
  if( MPC83XX_IRQ_GPIO != irq )
    return IRQ_RETVAL(IRQ_NONE);
  
  /* Get Interrupt event status. */
  gpiosr = ioread32be(gpio_baseaddr + MPC83XX_GPIO_INTEVENT);
  
  DbgPrint(("[%s] GPIO Interrupt! : 0x%08lx\n",__FUNCTION__,gpiosr));
  
  /* Clear DMA interrupt status. */
  DbgPrint(("[%s] Clear DMA interrupt status.\n", __FUNCTION__));
  iowrite32be( gpiosr, gpio_baseaddr + MPC83XX_GPIO_INTEVENT);
  
  /* Nofity INT to P2 ioport low-level driver. */
  if (p2io_ops.notify_int)
    p2io_ops.notify_int(gpiosr);

  /* Wake up! */
  wake_up( &mpc83xxgpio_wq);
  
  return IRQ_RETVAL(IRQ_HANDLED);
}



int MPC83xxGpioInstallHandler ( void )
{
  int result;
  
  result = request_irq( MPC83XX_IRQ_GPIO, MPC83xxGpioInterrupt, IRQF_DISABLED, "MPC83xxGPIO", NULL);
  DbgPrint(("[%s] result=%d\n", __FUNCTION__, result));

  if ( result < 0) {
    printk(KERN_ERR "[%s:%d] can't get assigned irq %d\n", __FUNCTION__, __LINE__, MPC83XX_IRQ_GPIO);
    return result;
  }
  DbgPrint(("[%s] interrupt %d hooked\n", __FUNCTION__, MPC83XX_IRQ_GPIO));

  return result;
}



int MPC83xxGpioRemoveHandler ( void )
{
  /* release IRQ */
  free_irq ( MPC83XX_IRQ_GPIO, NULL);
  DbgPrint(("[%s] interrupt %d disabled\n", __FUNCTION__, MPC83XX_IRQ_GPIO));
  return 0;
}

module_init(MPC83xxGpioInitModule);
