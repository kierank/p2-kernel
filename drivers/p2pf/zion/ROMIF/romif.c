/************************************************************
*
* romif.c : ZION ROM I/F driver
*
* $Id: romif.c,v 1.1.1.1 2006/02/27 09:20:56 nishikawa Exp $
*
************************************************************/

/* #define NEO_DEBUG */
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
#include <linux/delay.h>

#include <linux/zion.h>
#include <linux/zion_romif.h>
#include "zion_romif_regs.h"

#define ROMIF_MAJORVER (0)
#define ROMIF_MINORVER (15)

static void zion_romif_timeout( unsigned long prms );

/***************************************************************************
 * zion_romif_open
 **************************************************************************/
int zion_romif_open( zion_params_t *zion_params, struct inode *inode, struct file *filp )
{
  int zion_minor = MINOR( inode->i_rdev );
  unsigned long irq_flags;
  zion_romif_params_t *romif_prms = ROMIF_PARAM( zion_params, ZION_ROMIF );

  /* Lock spin lock */
  spin_lock_irqsave( &(ROMIF_PARAM(zion_params, zion_minor)->params_lock), irq_flags );

  /* Check ROM I/F count (ref. Linux Device Driver pp.178-179) */
  if ( romif_prms->count ) {
	  /* Already opened */
	  spin_unlock_irqrestore( &(ROMIF_PARAM(zion_params, zion_minor)->params_lock), irq_flags );
	  return (-EBUSY);
  }

  /* Increment ROM I/F counter */
  romif_prms->count++;
  
  /* Initialize ROM I/F parameters */
  romif_prms->condition = ZION_ROMIF_DISPATCH_PENDING;

  /* Use private data of file structure */
  filp->private_data = (void *)romif_prms;
  
  /* Unlock spin lock */
  spin_unlock_irqrestore( &(ROMIF_PARAM(zion_params, zion_minor)->params_lock), irq_flags );

 // MOD_INC_USE_COUNT;
  
  return 0;
}

/***************************************************************************
 * zion_romif_release
 **************************************************************************/
int zion_romif_release( zion_params_t *zion_params, struct inode *inode, struct file *filp )
{
	int zion_minor = MINOR( inode->i_rdev );
	zion_romif_params_t *romif_prms = ROMIF_PARAM( zion_params, ZION_ROMIF );
	unsigned long irq_flags;

	/* Lock spin lock */
	spin_lock_irqsave( &(ROMIF_PARAM(zion_params, zion_minor)->params_lock), irq_flags );
	
	/* Decrement ROM I/F count */
	romif_prms->count--;
	
	/* Unlock spin lock */
	spin_unlock_irqrestore( &(ROMIF_PARAM(zion_params, zion_minor)->params_lock), irq_flags );
	
//	MOD_DEC_USE_COUNT;
	return 0;
}

/****************** Ioctl method ******************/
/********** Reset **********/
void zion_romif_reset( zion_params_t *params )
{
	/* for DEBUG */
	PDEBUG( "Reset\n" );
		
	/* Reset */
	mbus_writew( 0x0080, MBUS_ADDR(params, ZION_MBUS_ROMIF_CTRL) );
}

/********** Set ClockDiv **********/
int zion_romif_set_clkdiv( zion_params_t *params, unsigned char ucClkDiv )
{
	unsigned short usReg = 0;
	
	/* Read ROM I/F control register for ClockDiv */
	usReg = mbus_readw( MBUS_ADDR(params, ZION_MBUS_ROMIF_CTRL) );
	
	/**  Set clock div **/
	usReg = ( usReg & (~0x3F00) ) | ( ucClkDiv << 8 );
	
	/* for DEBUG */
	PDEBUG( "ClockDiv: 0x%02X, usReg: 0x%04X\n", ucClkDiv, usReg );
	
	/* Write ROM I/F control register */
	mbus_writew( usReg, MBUS_ADDR(params, ZION_MBUS_ROMIF_CTRL) );

	/* Success */
	return (0);
}


/********** SCI (Asahi Kasei) ROM read **********/
int zion_romif_sci_read( zion_params_t *params,
						 zion_romif_params_t *romif_prms,
						 struct zion_romif_sci_buf *sp_romif_buf )
{
	unsigned long irq_flags = 0;
	unsigned short usClkDiv = 0;
	unsigned short usWriteBack = 0;

	/* for DEBUG */
	PDEBUG( "ROM I/F SCI Read(adr 0x%04X)\n", sp_romif_buf->adr );

	/* Set read command */
	mbus_writew( (ZION_ROMIF_SCI_CMD_READ | sp_romif_buf->adr),
				 MBUS_ADDR(params, ZION_MBUS_ROMIF_CMD) );
	
	/* Write back -- command */
	usWriteBack = mbus_readw( MBUS_ADDR(params, ZION_MBUS_ROMIF_CMD) );
	PDEBUG( "Writeback CMD: 0x%04X\n", usWriteBack );
	
	/* Read ROM I/F control register for ClockDiv */
	usClkDiv = mbus_readw( MBUS_ADDR(params, ZION_MBUS_ROMIF_CTRL) );
	usClkDiv &= 0x3F00; /* Get ClockDiv */
	
	/* for DEBUG */
	PDEBUG( "ClockDiv: 0x%04X\n", usClkDiv );
	
	/* Set condition */
	spin_lock_irqsave( &(romif_prms->params_lock), irq_flags ); /* Spin lock */
	romif_prms->condition = ZION_ROMIF_DISPATCH_PENDING;
	
	/* Set timed out */
	init_timer( &(romif_prms->timer) );
	romif_prms->timer.function = zion_romif_timeout;
	romif_prms->timer.data = (unsigned long)params;
	romif_prms->timer.expires = jiffies + ZION_ROMIF_TIMEOUT;
	add_timer( &(romif_prms->timer) );
	
	spin_unlock_irqrestore( &(romif_prms->params_lock), irq_flags ); /* Unlock spin lock */
	
	/* Execute */
	mbus_writew( (usClkDiv | 0x0002),
				 MBUS_ADDR(params, ZION_MBUS_ROMIF_CTRL) );
	
	/* Sleep */
	wait_event( romif_prms->zion_romif_wait_queue,
				(romif_prms->condition != ZION_ROMIF_DISPATCH_PENDING) );
	
	/* Read End -- check timedout */
	if ( ZION_ROMIF_DISPATCH_TIMEOUT == romif_prms->condition ) {
		PERROR( "ZION ROM I/F SCI read timedout!\n" );
		return (-ETIME);
	}
	
	/** Success **/
	sp_romif_buf->dat = (unsigned short)ioread16( (void*)MBUS_ADDR(params, ZION_MBUS_ROMIF_RDAT) );
	
	PDEBUG( "Read dat: 0x%04X\n", sp_romif_buf->dat );

	/* Success */
	return (0);
}

/********** SCI (Asahi Kasei) ROM write enable **********/
int zion_romif_sci_writeenb( zion_params_t *params,
							 zion_romif_params_t *romif_prms )
{
	unsigned long irq_flags = 0;
	unsigned short usClkDiv = 0;
	unsigned short usWriteBack = 0;
		
	/* for DEBUG */
	PDEBUG( "ROM I/F SCI WriteEnb\n" );

	/* Read ROM I/F control register for ClockDiv */
	usClkDiv = mbus_readw( MBUS_ADDR(params, ZION_MBUS_ROMIF_CTRL) );
	usClkDiv &= 0x3F00; /* Get ClockDiv */

	/* for DEBUG */
	PDEBUG( "ClockDiv: 0x%04X\n", usClkDiv );
		
	/* Set write enable */
	mbus_writew( ZION_ROMIF_SCI_CMD_WENB,
				 MBUS_ADDR(params, ZION_MBUS_ROMIF_CMD) );
	
	/* Write back -- command */
	usWriteBack = mbus_readw( MBUS_ADDR(params, ZION_MBUS_ROMIF_CMD) );
	PDEBUG( "Writeback CMD: 0x%04X\n", usWriteBack );
	
	/* Set condition */
	spin_lock_irqsave( &(romif_prms->params_lock), irq_flags ); /* Spin lock */
	romif_prms->condition = ZION_ROMIF_DISPATCH_PENDING;
	
	/* Set timed out */
	init_timer( &(romif_prms->timer) );
	romif_prms->timer.function = zion_romif_timeout;
	romif_prms->timer.data = (unsigned long)params;
	romif_prms->timer.expires = jiffies + ZION_ROMIF_TIMEOUT;
	add_timer( &(romif_prms->timer) );
	
	spin_unlock_irqrestore( &(romif_prms->params_lock), irq_flags ); /* Unlock spin lock */
	
	/* Execute */
	mbus_writew( (usClkDiv | 0x4001),
				 MBUS_ADDR(params, ZION_MBUS_ROMIF_CTRL) );
	
	/* Sleep */
	wait_event( romif_prms->zion_romif_wait_queue,
				(romif_prms->condition != ZION_ROMIF_DISPATCH_PENDING) );
	
	/* Write enable End -- check timedout */
	if ( ZION_ROMIF_DISPATCH_TIMEOUT == romif_prms->condition ) {
		PERROR( "ZION ROM I/F SCI write enable timedout!\n" );
		return (-ETIME);
	}

	/* Success */
	return (0);
}


/********** SCI (Asahi Kasei) ROM write **********/
int zion_romif_sci_write( zion_params_t *params,
						  zion_romif_params_t *romif_prms,
						  struct zion_romif_sci_buf *sp_romif_buf )
{
	unsigned long irq_flags = 0;
	unsigned short usClkDiv = 0;
	unsigned short usWriteBack = 0;
	
	/* for DEBUG */
	PDEBUG( "ROM I/F SCI Write(adr: 0x%04X, dat: 0x%04X)\n",
			sp_romif_buf->adr, sp_romif_buf->dat );
	
	/* Read ROM I/F control register for ClockDiv */
	usClkDiv = mbus_readw( MBUS_ADDR(params, ZION_MBUS_ROMIF_CTRL) );
	usClkDiv &= 0x3F00; /* Get ClockDiv */

	/* for DEBUG */
	PDEBUG( "usClockDiv: 0x%04X\n", usClkDiv );
	
	/* Set write data */
	iowrite16( sp_romif_buf->dat,
			(void*)MBUS_ADDR(params, ZION_MBUS_ROMIF_WDAT) );
	
	/* Write back -- write data */
	usWriteBack = mbus_readw( MBUS_ADDR(params, ZION_MBUS_ROMIF_WDAT) );
	PDEBUG( "Writeback WDat: 0x%04X\n", usWriteBack );
	
	/* Set write command */
	mbus_writew( (ZION_ROMIF_SCI_CMD_WRITE | sp_romif_buf->adr),
				 MBUS_ADDR(params, ZION_MBUS_ROMIF_CMD) );
	
	/* Write back -- command */
	usWriteBack = mbus_readw( MBUS_ADDR(params, ZION_MBUS_ROMIF_CMD) );
	PDEBUG( "Writeback CMD: 0x%04X\n", usWriteBack );
	
	/* Set condition */
	spin_lock_irqsave( &(romif_prms->params_lock), irq_flags ); /* Spin lock */
	romif_prms->condition = ZION_ROMIF_DISPATCH_PENDING;
	
	/* Set timed out */
	init_timer( &(romif_prms->timer) );
	romif_prms->timer.function = zion_romif_timeout;
	romif_prms->timer.data = (unsigned long)params;
	romif_prms->timer.expires = jiffies + ZION_ROMIF_TIMEOUT;
	add_timer( &(romif_prms->timer) );
	
	spin_unlock_irqrestore( &(romif_prms->params_lock), irq_flags ); /* Unlock spin lock */
	
	/* Execute */
	mbus_writew( (usClkDiv | 0x4001),
				 MBUS_ADDR(params, ZION_MBUS_ROMIF_CTRL) );
	
	/* Sleep */
	wait_event( romif_prms->zion_romif_wait_queue,
				(romif_prms->condition != ZION_ROMIF_DISPATCH_PENDING) );

	/* Write End -- check timedout */
	if ( ZION_ROMIF_DISPATCH_TIMEOUT == romif_prms->condition ) {
		PERROR( "ZION ROM I/F SCI write timedout!\n" );
		return (-ETIME);
	}

	/* Success */
	mdelay( 5 );	/* XXX */
	return (0);
}

/********** SCI (Asahi Kasei) ROM read to MBUS **********/
int zion_romif_sci_read2mbus( zion_params_t *params,
							  zion_romif_params_t *romif_prms,
							  struct zion_romif_trns *sp_romif_trns )
{
	unsigned long irq_flags = 0;
	unsigned short usClkDiv = 0;
	unsigned short usWriteBack = 0;
	
	/* for DEBUG */
	PDEBUG( "ROM I/F SCI Read to MBUS(src: 0x%04X, adr: 0x%04X, size: 0x%04X, inc: 0x%02X)\n", sp_romif_trns->src, sp_romif_trns->dest, sp_romif_trns->size, sp_romif_trns->inc );
	
	/* Read ROM I/F control register for ClockDiv */
	usClkDiv = mbus_readw( MBUS_ADDR(params, ZION_MBUS_ROMIF_CTRL) );
	usClkDiv &= 0x3F00; /* Get ClockDiv */

	/* for DEBUG */
	PDEBUG( "usClockDiv: 0x%04X\n", usClkDiv );
	
	/* Set destination address */
	mbus_writew( sp_romif_trns->dest,
				 MBUS_ADDR(params, ZION_MBUS_ROMIF_DSTADR) );

	/*  Write back -- destination address */
	usWriteBack = mbus_readw( MBUS_ADDR(params, ZION_MBUS_ROMIF_DSTADR) );
	
	/* Set RomIncValue */
	mbus_writew( (sp_romif_trns->inc & 0x0F),
				 MBUS_ADDR(params, ZION_MBUS_ROMIF_INCVAL) );
	
	/*  Write back -- RomIncValue */
	usWriteBack = mbus_readw( MBUS_ADDR(params, ZION_MBUS_ROMIF_INCVAL) );

	/* Set RomDmaLength */
	mbus_writew( sp_romif_trns->size,
				 MBUS_ADDR(params, ZION_MBUS_ROMIF_LENGTH) );
	
	/*  Write back -- RomDmaLength */
	usWriteBack = mbus_readw( MBUS_ADDR(params, ZION_MBUS_ROMIF_LENGTH) );
	

	/* Set read command */
	mbus_writew( (ZION_ROMIF_SCI_CMD_READ | sp_romif_trns->src),
					 MBUS_ADDR(params, ZION_MBUS_ROMIF_CMD) );
	
	/* Set condition */
	spin_lock_irqsave( &(romif_prms->params_lock), irq_flags ); /* Spin lock */
	romif_prms->condition = ZION_ROMIF_DISPATCH_PENDING;
	
	/* Set timed out */
	init_timer( &(romif_prms->timer) );
	romif_prms->timer.function = zion_romif_timeout;
	romif_prms->timer.data = (unsigned long)params;
	romif_prms->timer.expires = jiffies + ZION_ROMIF_TIMEOUT;
	add_timer( &(romif_prms->timer) );
	
	spin_unlock_irqrestore( &(romif_prms->params_lock), irq_flags ); /* Unlock spin lock */
	
	/* Execute */
	mbus_writew( (usClkDiv | 0x0010),
				 MBUS_ADDR(params, ZION_MBUS_ROMIF_CTRL) );
	
	/* Sleep */
	wait_event( romif_prms->zion_romif_wait_queue,
				(romif_prms->condition != ZION_ROMIF_DISPATCH_PENDING) );
	
	/* Read to MBUS End -- check timedout */
	if ( ZION_ROMIF_DISPATCH_TIMEOUT == romif_prms->condition ) {
		PERROR( "ZION ROM I/F read to MBUS timedout!\n" );
		return (-ETIME);
	}

	/* Success */
	return (0);
}


/********** SPI (Renesas) ROM read **********/
int zion_romif_spi_read( zion_params_t *params,
						 zion_romif_params_t *romif_prms,
						 struct zion_romif_spi_buf *sp_romif_buf )
{
	unsigned long irq_flags = 0;
	unsigned short usClkDiv = 0;
	unsigned short usWriteBack = 0;
	
	/* for DEBUG */
	PDEBUG( "ROM I/F SPI Read(adr: 0x%04X)\n", sp_romif_buf->adr );
	
	/* Set access address */
	mbus_writew( sp_romif_buf->adr, MBUS_ADDR(params, ZION_MBUS_ROMIF_SPI_ADR) );
		
	/*  Write back -- access address */
	usWriteBack = mbus_readw( MBUS_ADDR(params, ZION_MBUS_ROMIF_SPI_ADR) );
	
	/* Set read command */
	mbus_writew( ZION_ROMIF_SPI_CMD_READ, MBUS_ADDR(params, ZION_MBUS_ROMIF_CMD) );
	
	/*  Write back -- read command */
	usWriteBack = mbus_readw( MBUS_ADDR(params, ZION_MBUS_ROMIF_CMD) );
	
	/* Read ROM I/F control register for ClockDiv */
	usClkDiv = mbus_readw( MBUS_ADDR(params, ZION_MBUS_ROMIF_CTRL) );
	usClkDiv &= 0x3F00; /* Get ClockDiv */
	
	/* for DEBUG */
	PDEBUG( "ClockDiv: 0x%04X\n", usClkDiv );
	
	/* Set condition */
	spin_lock_irqsave( &(romif_prms->params_lock), irq_flags ); /* Spin lock */
	romif_prms->condition = ZION_ROMIF_DISPATCH_PENDING;
	
	/* Set timed out */
	init_timer( &(romif_prms->timer) );
	romif_prms->timer.function = zion_romif_timeout;
	romif_prms->timer.data = (unsigned long)params;
	romif_prms->timer.expires = jiffies + ZION_ROMIF_TIMEOUT;
	add_timer( &(romif_prms->timer) );
	
	spin_unlock_irqrestore( &(romif_prms->params_lock), irq_flags ); /* Unlock spin lock */
	
	/* Execute */
	mbus_writew( (usClkDiv | 0x0022),
				 MBUS_ADDR(params, ZION_MBUS_ROMIF_CTRL) );

	/* Sleep */
	wait_event( romif_prms->zion_romif_wait_queue,
				(romif_prms->condition != ZION_ROMIF_DISPATCH_PENDING) );
	
	/* Read End -- check timedout */
	if ( ZION_ROMIF_DISPATCH_TIMEOUT == romif_prms->condition ) {
		PERROR( "ZION ROM I/F SPI read timedout!\n" );
		return (-ETIME);
	}
	
	/** Success **/
	sp_romif_buf->dat = (unsigned short)ioread16( (void*)MBUS_ADDR(params, ZION_MBUS_ROMIF_RDAT) );
	
	PDEBUG( "Read dat: 0x%04X\n", sp_romif_buf->dat );
	

	/* Success */
	return (0);
}


/********** SPI (Renesas) ROM status read **********/
int zion_romif_spi_status_read( zion_params_t *params,
								zion_romif_params_t *romif_prms,
								struct zion_romif_spi_stts *sp_romif_buf )
{
	unsigned long irq_flags = 0;
	unsigned short usClkDiv = 0;
	unsigned short usWriteBack = 0;
	
	/* for DEBUG */
	PDEBUG( "ROM I/F SPI Status Read\n" );
	
	/* Clear access address */
	mbus_writew( 0, MBUS_ADDR(params, ZION_MBUS_ROMIF_SPI_ADR) );
	
	/*  Write back -- access address */
	usWriteBack = mbus_readw( MBUS_ADDR(params, ZION_MBUS_ROMIF_SPI_ADR) );
	
	/* Set status read command */
	mbus_writew( ZION_ROMIF_SPI_CMD_SREAD, MBUS_ADDR(params, ZION_MBUS_ROMIF_CMD) );
	
	/*  Write back -- status read command */
	usWriteBack = mbus_readw( MBUS_ADDR(params, ZION_MBUS_ROMIF_CMD) );
	
	/* Read ROM I/F control register for ClockDiv */
	usClkDiv = mbus_readw( MBUS_ADDR(params, ZION_MBUS_ROMIF_CTRL) );
	usClkDiv &= 0x3F00; /* Get ClockDiv */
	
	/* for DEBUG */
	PDEBUG( "ClockDiv: 0x%04X\n", usClkDiv );
	
	/* Set condition */
	spin_lock_irqsave( &(romif_prms->params_lock), irq_flags ); /* Spin lock */
	romif_prms->condition = ZION_ROMIF_DISPATCH_PENDING;
	
	/* Set timed out */
	init_timer( &(romif_prms->timer) );
	romif_prms->timer.function = zion_romif_timeout;
	romif_prms->timer.data = (unsigned long)params;
	romif_prms->timer.expires = jiffies + ZION_ROMIF_TIMEOUT;
	add_timer( &(romif_prms->timer) );
	
	spin_unlock_irqrestore( &(romif_prms->params_lock), irq_flags ); /* Unlock spin lock */
	
	/* Execute */
	mbus_writew( (usClkDiv | 0x0028),
				 MBUS_ADDR(params, ZION_MBUS_ROMIF_CTRL) );
	
	/* Sleep */
	wait_event( romif_prms->zion_romif_wait_queue,
				(romif_prms->condition != ZION_ROMIF_DISPATCH_PENDING) );
	
	/* Status read End -- check timedout */
	if ( ZION_ROMIF_DISPATCH_TIMEOUT == romif_prms->condition ) {
		PERROR( "ZION ROM I/F SPI status read timedout!\n" );
		return (-ETIME);
	}
	
	/** Success **/
	sp_romif_buf->dat = mbus_readb( MBUS_ADDR(params, ZION_MBUS_ROMIF_RDAT) + 1 );
	
	PDEBUG( "Read status dat: 0x%02X\n", sp_romif_buf->dat );

	/* Success */
	return (0);
}


/********** SPI (Renesas) ROM status write **********/
int zion_romif_spi_status_write( zion_params_t *params,
								 zion_romif_params_t *romif_prms,
								 struct zion_romif_spi_stts *sp_romif_buf )
{
	unsigned long irq_flags = 0;
	unsigned short usClkDiv = 0;
	unsigned short usWriteBack = 0;
	
	/* for DEBUG */
	PDEBUG( "ROM I/F SPI Write(dat: 0x%02X)\n", sp_romif_buf->dat );

	/* Read ROM I/F control register for ClockDiv */
	usClkDiv = mbus_readw( MBUS_ADDR(params, ZION_MBUS_ROMIF_CTRL) );
	usClkDiv &= 0x3F00; /* Get ClockDiv */
	
	/* for DEBUG */
	PDEBUG( "usClockDiv: 0x%04X\n", usClkDiv );
	
	/* Clear access address */
	mbus_writew( 0, MBUS_ADDR(params, ZION_MBUS_ROMIF_SPI_ADR) );
	
	/*  Write back -- access address */
	usWriteBack = mbus_readw( MBUS_ADDR(params, ZION_MBUS_ROMIF_SPI_ADR) );
		
	mbus_writew( (ZION_ROMIF_SPI_CMD_WRITE | sp_romif_buf->dat),
				 MBUS_ADDR(params, ZION_MBUS_ROMIF_CMD) );
	
	/* Write back -- command */
	usWriteBack = mbus_readw( MBUS_ADDR(params, ZION_MBUS_ROMIF_CMD) );
	PDEBUG( "StatusWrite cmd: 0x%04X\n", usWriteBack );
	
	/* Set condition */
	spin_lock_irqsave( &(romif_prms->params_lock), irq_flags ); /* Spin lock */
	romif_prms->condition = ZION_ROMIF_DISPATCH_PENDING;
	
	/* Set timed out */
	init_timer( &(romif_prms->timer) );
	romif_prms->timer.function = zion_romif_timeout;
	romif_prms->timer.data = (unsigned long)params;
	romif_prms->timer.expires = jiffies + ZION_ROMIF_TIMEOUT;
	add_timer( &(romif_prms->timer) );
	
	spin_unlock_irqrestore( &(romif_prms->params_lock), irq_flags ); /* Unlock spin lock */
	
	/* Execute */
	mbus_writew( (usClkDiv | 0x4024),
				 MBUS_ADDR(params, ZION_MBUS_ROMIF_CTRL) );
	
	/* Sleep */
	wait_event( romif_prms->zion_romif_wait_queue,
				(romif_prms->condition != ZION_ROMIF_DISPATCH_PENDING) );
	
	/* Status write End -- check timedout */
	if ( ZION_ROMIF_DISPATCH_TIMEOUT == romif_prms->condition ) {
		PERROR( "ZION ROM I/F SCI write timedout!\n" );
		return (-ETIME);
	}
	
	/* Success */
	return (0);
}


/********** SPI (Renesas) ROM write enable **********/
int zion_romif_spi_writeenb( zion_params_t *params,
							 zion_romif_params_t *romif_prms )
{
	unsigned long irq_flags = 0;
	unsigned short usClkDiv = 0;
	unsigned short usWriteBack = 0;
		
	/* for DEBUG */
	PDEBUG( "ROM I/F SPI WriteEnb\n" );

	/* Read ROM I/F control register for ClockDiv */
	usClkDiv = mbus_readw( MBUS_ADDR(params, ZION_MBUS_ROMIF_CTRL) );
	usClkDiv &= 0x3F00; /* Get ClockDiv */
	
	/* for DEBUG */
	PDEBUG( "ClockDiv: 0x%04X\n", usClkDiv );

	/* Set write enb command */
	mbus_writew( ZION_ROMIF_SPI_CMD_WENB, MBUS_ADDR(params, ZION_MBUS_ROMIF_CMD) );
	
	/*  Write back -- command */
	usWriteBack = mbus_readw( MBUS_ADDR(params, ZION_MBUS_ROMIF_CMD) );
	
	/* Clear access address register */
	mbus_writew( 0, MBUS_ADDR(params, ZION_MBUS_ROMIF_SPI_ADR) );
	
	/*  Write back -- access address */
	usWriteBack = mbus_readw( MBUS_ADDR(params, ZION_MBUS_ROMIF_SPI_ADR) );

	/* Set condition */
	spin_lock_irqsave( &(romif_prms->params_lock), irq_flags ); /* Spin lock */
	romif_prms->condition = ZION_ROMIF_DISPATCH_PENDING;
	
	/* Set timed out */
	init_timer( &(romif_prms->timer) );
	romif_prms->timer.function = zion_romif_timeout;
	romif_prms->timer.data = (unsigned long)params;
	romif_prms->timer.expires = jiffies + ZION_ROMIF_TIMEOUT;
	add_timer( &(romif_prms->timer) );
	
	spin_unlock_irqrestore( &(romif_prms->params_lock), irq_flags ); /* Unlock spin lock */
		
	/* Execute */
	mbus_writew( (usClkDiv | 0x4021),
				 MBUS_ADDR(params, ZION_MBUS_ROMIF_CTRL) );
	
	/* Sleep */
	wait_event( romif_prms->zion_romif_wait_queue,
				(romif_prms->condition != ZION_ROMIF_DISPATCH_PENDING) );
	
	/* Write enable End -- check timedout */
	if ( ZION_ROMIF_DISPATCH_TIMEOUT == romif_prms->condition ) {
		PERROR( "ZION ROM I/F SPI write enb timedout!\n" );
		return (-ETIME);
	}
	
	/* Success */
	return (0);
}


/********** SPI (Renesas) ROM write **********/
int zion_romif_spi_write( zion_params_t *params,
						  zion_romif_params_t *romif_prms,
						  struct zion_romif_spi_buf *sp_romif_buf )
{
	unsigned long irq_flags = 0;
	unsigned short usClkDiv = 0;
	unsigned short usWriteBack = 0;

	struct zion_romif_spi_stts s_romif_stts;
	
	/* for DEBUG */
	PDEBUG( "ROM I/F SPI Write(adr: 0x%04X, dat: 0x%04X)\n",
			sp_romif_buf->adr, sp_romif_buf->dat );
	
	/* Read ROM I/F control register for ClockDiv */
	usClkDiv = mbus_readw( MBUS_ADDR(params, ZION_MBUS_ROMIF_CTRL) );
	usClkDiv &= 0x3F00; /* Get ClockDiv */
	
	/* for DEBUG */
	PDEBUG( "usClockDiv: 0x%04X\n", usClkDiv );
	
	/* Set access address */
	mbus_writew( sp_romif_buf->adr, MBUS_ADDR(params, ZION_MBUS_ROMIF_SPI_ADR) );
		
	/*  Write back -- access address */
	usWriteBack = mbus_readw( MBUS_ADDR(params, ZION_MBUS_ROMIF_SPI_ADR) );
	
	/* Set write data */
	iowrite16( sp_romif_buf->dat,
			(void*)MBUS_ADDR(params, ZION_MBUS_ROMIF_WDAT) );

	/* Write back -- write data */
	usWriteBack = mbus_readw( MBUS_ADDR(params, ZION_MBUS_ROMIF_WDAT) );
	
	/* Set write command */
	mbus_writew( ZION_ROMIF_SPI_CMD_WRITE,
				 MBUS_ADDR(params, ZION_MBUS_ROMIF_CMD) );
	
	/* Write back -- command */
	usWriteBack = mbus_readw( MBUS_ADDR(params, ZION_MBUS_ROMIF_CMD) );

	/* Set condition */
	spin_lock_irqsave( &(romif_prms->params_lock), irq_flags ); /* Spin lock */
	romif_prms->condition = ZION_ROMIF_DISPATCH_PENDING;
	
	/* Set timed out */
	init_timer( &(romif_prms->timer) );
	romif_prms->timer.function = zion_romif_timeout;
	romif_prms->timer.data = (unsigned long)params;
	romif_prms->timer.expires = jiffies + ZION_ROMIF_TIMEOUT;
	add_timer( &(romif_prms->timer) );
		
	spin_unlock_irqrestore( &(romif_prms->params_lock), irq_flags ); /* Unlock spin lock */
	
	/* Execute */
	mbus_writew( (usClkDiv | 0x4021),
				 MBUS_ADDR(params, ZION_MBUS_ROMIF_CTRL) );
	
	/* Sleep */
	wait_event( romif_prms->zion_romif_wait_queue,
				(romif_prms->condition != ZION_ROMIF_DISPATCH_PENDING) );
	
	/* Write End -- check timedout */
	if ( ZION_ROMIF_DISPATCH_TIMEOUT == romif_prms->condition ) {
		PERROR( "ZION ROM I/F SPI write timedout!\n" );
		return (-ETIME);
	}


	/* Check WIP bit -- Read ROM status */
	do {
		static unsigned long ulCnt = 0;
		int iRetVal = -1;

		/* Read ROM status */
		iRetVal = zion_romif_spi_status_read( params,
											  romif_prms,
											  &s_romif_stts );

		/* Check return value */
		if ( 0 != iRetVal ) {
			PDEBUG( "Reading ROM status is failed at SPI write!\n" );
			return (iRetVal);
		}

		/* Check counter -- wait 2sec */
		if ( 2000 < ulCnt ) {
			PERROR( "Checking WIP bit time out!\n" );
			return (-ETIME);
		}
		mdelay( 1 );
		
	} while ( s_romif_stts.dat & 0x01 );
	
	/* Success */
	return (0);
}


/********** SPI (Renesas) ROM read to MBUS **********/
int zion_romif_spi_read2mbus( zion_params_t *params,
							  zion_romif_params_t *romif_prms,
							  struct zion_romif_trns *sp_romif_trns )
{
	unsigned long irq_flags = 0;
	unsigned short usClkDiv = 0;
	unsigned short usWriteBack = 0;
	
	/* for DEBUG */
	PDEBUG( "ROM I/F SPI Read to MBUS(src: 0x%04X, adr: 0x%04X, size: 0x%04X, inc: 0x%02X)\n", sp_romif_trns->src, sp_romif_trns->dest, sp_romif_trns->size, sp_romif_trns->inc );
	
	/* Read ROM I/F control register for ClockDiv */
	usClkDiv = mbus_readw( MBUS_ADDR(params, ZION_MBUS_ROMIF_CTRL) );
	usClkDiv &= 0x3F00; /* Get ClockDiv */
	
	/* for DEBUG */
	PDEBUG( "usClockDiv: 0x%04X\n", usClkDiv );
	
	/* Set RomIF Mode */
	mbus_writew( (usClkDiv | 0x20), MBUS_ADDR(params, ZION_MBUS_ROMIF_CTRL) );

	/* Write back -- RomIF Mode */
	usWriteBack = mbus_readw( MBUS_ADDR(params, ZION_MBUS_ROMIF_CTRL) );
	PDEBUG( "RomIF Mode: 0x%04X\n", usWriteBack );
	
	/* Set destination address */
	mbus_writew( sp_romif_trns->dest,
				 MBUS_ADDR(params, ZION_MBUS_ROMIF_DSTADR) );
	
	/*  Write back -- destination address */
	usWriteBack = mbus_readw( MBUS_ADDR(params, ZION_MBUS_ROMIF_DSTADR) );
	
	/* Set RomIncValue */
	mbus_writew( ((sp_romif_trns->inc) & 0x0F),
				 MBUS_ADDR(params, ZION_MBUS_ROMIF_INCVAL) );
	
	/*  Write back -- RomIncValue */
	usWriteBack = mbus_readw( MBUS_ADDR(params, ZION_MBUS_ROMIF_INCVAL) );
	
	/* Set RomDmaLength */
	mbus_writew( sp_romif_trns->size,
				 MBUS_ADDR(params, ZION_MBUS_ROMIF_LENGTH) );
	
	/*  Write back -- RomDmaLength */
	usWriteBack = mbus_readw( MBUS_ADDR(params, ZION_MBUS_ROMIF_LENGTH) );
	
	/* Set access address */
	mbus_writew( sp_romif_trns->src, MBUS_ADDR(params, ZION_MBUS_ROMIF_SPI_ADR) );
	/*  Write back -- access address */
	usWriteBack = mbus_readw( MBUS_ADDR(params, ZION_MBUS_ROMIF_SPI_ADR) );
	
	/* Set read command */
	mbus_writew( ZION_ROMIF_SPI_CMD_READ, MBUS_ADDR(params, ZION_MBUS_ROMIF_CMD) );
	
	/*  Write back -- read command */
	usWriteBack = mbus_readw( MBUS_ADDR(params, ZION_MBUS_ROMIF_CMD) );
	
	/* Set condition */
	spin_lock_irqsave( &(romif_prms->params_lock), irq_flags ); /* Spin lock */
	romif_prms->condition = ZION_ROMIF_DISPATCH_PENDING;
	
	/* Set timed out */
	init_timer( &(romif_prms->timer) );
	romif_prms->timer.function = zion_romif_timeout;
	romif_prms->timer.data = (unsigned long)params;
	romif_prms->timer.expires = jiffies + ZION_ROMIF_TIMEOUT;
	add_timer( &(romif_prms->timer) );
	
	spin_unlock_irqrestore( &(romif_prms->params_lock), irq_flags ); /* Unlock spin lock */
	
	/* Execute */
	mbus_writew( (usClkDiv | 0x0030),
				 MBUS_ADDR(params, ZION_MBUS_ROMIF_CTRL) );

	/* Sleep */
	wait_event( romif_prms->zion_romif_wait_queue,
				(romif_prms->condition != ZION_ROMIF_DISPATCH_PENDING) );
	
	/* Read to MBUS End -- check timedout */
	if ( ZION_ROMIF_DISPATCH_TIMEOUT == romif_prms->condition ) {
		PERROR( "ZION ROM I/F read to MBUS timedout!\n" );
		return (-ETIME);
	}

	/* Success */
	return (0);
}



/***************************************************************************
 * zion_romif_ioctl
 **************************************************************************/
int
zion_romif_ioctl( zion_params_t * params,
				  struct inode *inode, struct file *file,
				  unsigned int function, unsigned long arg )
{
	/* Get ROM I/F params */
	zion_romif_params_t *romif_prms = (zion_romif_params_t *)file->private_data;

	/* Check private data */
	if ( NULL == romif_prms ) {
		PERROR( "Private data is NULL!\n" );
		return (-ENODEV);
	}
	
	/*** Main ***/
	switch ( function )
    {

/*-------- Commom comamnds --------*/
		
	case ZION_ROMIF_IOC_RESET:
	{
		zion_romif_reset( params );

		/* Success */
		break;
	}
	
	case ZION_ROMIF_IOC_SET_CLKDIV:
	{
		unsigned char ucClkDiv = 0;
		
		/* Get clock div */
		if ( copy_from_user((void *)&ucClkDiv,
							(void *)arg,
							sizeof(unsigned char)) )
			return (-EFAULT);
		
		return zion_romif_set_clkdiv( params, ucClkDiv );
	}
/*-------- Commom comamnds END --------*/
	
/*-------- SCI (Asahi Kasei) ROM commands --------*/
	
	case ZION_ROMIF_IOC_SCI_READ:
	{
		int iRetVal = -1;
		struct zion_romif_sci_buf s_romif_buf;
		
		/* Get clock div */
		if ( copy_from_user((void *)&s_romif_buf,
							(void *)arg,
							sizeof(struct zion_romif_sci_buf)) )
			return (-EFAULT);

		/* Execute */
		iRetVal = zion_romif_sci_read( params, romif_prms, &s_romif_buf );
		
		/* Copy reading data */
		if ( copy_to_user((void *)arg,
						  (void *)&s_romif_buf,
						  sizeof(struct zion_romif_sci_buf)) )
			return (-EFAULT);

		return (iRetVal);
	}


	case ZION_ROMIF_IOC_SCI_WRITEENB:
	{
		return zion_romif_sci_writeenb( params, romif_prms );
	}
	
	case ZION_ROMIF_IOC_SCI_WRITE:
	{
		struct zion_romif_sci_buf s_romif_buf;
		int iRetVal = -1;
		
		/* Get ROM I/F SCI buffer */
		if ( copy_from_user((void *)&s_romif_buf,
							(void *)arg,
							sizeof(struct zion_romif_sci_buf)) )
			return (-EFAULT);

		/* Execute write enable */
		iRetVal = zion_romif_sci_writeenb( params, romif_prms );
		if ( 0 != iRetVal ) {
			return iRetVal;
		}
		
		/* Execute */
		return zion_romif_sci_write( params, romif_prms, &s_romif_buf );
	}
	
	
	case ZION_ROMIF_IOC_SCI_READ2MBUS:
	{
		struct zion_romif_trns s_romif_trns;
		
		/* Get ROM I/F Read2MBUS buffer */
		if ( copy_from_user((void *)&s_romif_trns,
							(void *)arg,
							sizeof(struct zion_romif_trns)) )
			return (-EFAULT);

		/* Execute */
		return zion_romif_sci_read2mbus( params, romif_prms, &s_romif_trns );
	}

/*-------- SCI (Asahi Kasei) ROM commands end --------*/

/*-------- SPI (Renesas) ROM commands --------*/
	
	case ZION_ROMIF_IOC_SPI_READ:
	{
		int iRetVal = -1;
		struct zion_romif_spi_buf s_romif_buf;

		/* Get ROM I/F SCI buffer */
		if ( copy_from_user((void *)&s_romif_buf,
							(void *)arg,
							sizeof(struct zion_romif_spi_buf)) )
			return (-EFAULT);

		/* Execute */
		iRetVal = zion_romif_spi_read( params, romif_prms, &s_romif_buf );

		/* Copy reading data */
		if ( copy_to_user((void *)arg,
						  (void *)&s_romif_buf,
						  sizeof(struct zion_romif_spi_buf)) )
			return (-EFAULT);

		/* Exit */
		return (iRetVal);
	}

	
	case ZION_ROMIF_IOC_SPI_WRITEENB:
	{
		return zion_romif_spi_writeenb( params, romif_prms );
	}

	
	case ZION_ROMIF_IOC_SPI_WRITE:
	{
		struct zion_romif_spi_buf s_romif_buf;
		int iRetVal = -1;
		
		/* Get ROM I/F SPI buffer */
		if ( copy_from_user((void *)&s_romif_buf,
							(void *)arg,
							sizeof(struct zion_romif_spi_buf)) )
			return (-EFAULT);

		/* Execute write enable */
		iRetVal = zion_romif_spi_writeenb( params, romif_prms );
		if ( 0 != iRetVal ) {
			return iRetVal;
		}
		
		/* Execute */
		return zion_romif_spi_write( params, romif_prms, &s_romif_buf );
	}

	
	case ZION_ROMIF_IOC_SPI_SREAD:
	{
		int iRetVal = -1;
		struct zion_romif_spi_stts s_romif_buf;

		/* Get ROM I/F SCI buffer */
		if ( copy_from_user((void *)&s_romif_buf,
							(void *)arg,
							sizeof(struct zion_romif_spi_stts)) )
			return (-EFAULT);

		/* Execute */
		iRetVal = zion_romif_spi_status_read( params,
											  romif_prms,
											  &s_romif_buf );

		/* Copy reading data */
		if ( copy_to_user((void *)arg,
						  (void *)&s_romif_buf,
						  sizeof(struct zion_romif_spi_stts)) )
			return (-EFAULT);

		/* Exit */
		return (iRetVal);
	}

	
	case ZION_ROMIF_IOC_SPI_SWRITE:
	{
		struct zion_romif_spi_stts s_romif_buf;

		/* Get ROM I/F SPI buffer */
		if ( copy_from_user((void *)&s_romif_buf,
							(void *)arg,
							sizeof(struct zion_romif_spi_buf)) )
			return (-EFAULT);
	
		/* Execute */
		return zion_romif_spi_status_write( params, romif_prms, &s_romif_buf );
	}


	case ZION_ROMIF_IOC_SPI_READ2MBUS:
	{
		struct zion_romif_trns s_romif_trns;

		/* Get ROM I/F Read2MBUS buffer */
		if ( copy_from_user((void *)&s_romif_trns,
							(void *)arg,
							sizeof(struct zion_romif_trns)) )
			return (-EFAULT);

		/* Execute */
		return zion_romif_spi_read2mbus( params, romif_prms, &s_romif_trns );
	} /* The end of CASE(SPI_READ2MBUS) */

/*-------- SPI (Renesas) ROM commands --------*/
	
	default:
		PERROR( "No such Ioctl command!\n" );
		return (-EINVAL);
		
	}	/* The end of SWITCH(function) */

	
	/* Success */
	return (0);
}


struct zion_file_operations zion_romif_fops = {
	ioctl: zion_romif_ioctl,
	open : zion_romif_open,
	release : zion_romif_release,
};


/***************************************************************************
 * zion_romif_timeout
 * @func
 *		Timedout function of ZION-NEO ROM I/F
 * @args
 *		unsigned long	*prms	[in/out]:	parameters of ZION driver
 * @return
 * 		void
 * @comment
 * 		static function
 * @author
 * 		H. Hoshino
 **************************************************************************/
static void
zion_romif_timeout( unsigned long prms )
{
	zion_params_t *params = (zion_params_t *)prms;
	zion_romif_params_t *romif_prms = NULL;
	
	/* Check argument */
	if ( NULL == params ) {
		PERROR( "Invalid ZION parameters!\n" );
		return;
    }
		
	/* Get and check ROM I/F params */
	romif_prms = ROMIF_PARAM(params, ZION_ROMIF);
	if ( NULL == romif_prms ) {
		PERROR( "Invalid ZION ROM I/F parameters!\n" );
		return;
	}
	
	/* Print error message */
	PERROR( "Timedout occured!\n" );
		
	/* Wake up */
	spin_lock( &(romif_prms->params_lock) ); /* Spin lock */
	romif_prms->condition = ZION_ROMIF_DISPATCH_TIMEOUT;
	spin_unlock( &(romif_prms->params_lock) ); /* Unlock spin lock */
	
	wake_up( &(romif_prms->zion_romif_wait_queue) );
}


/***************************************************************************
 * zion_romif_event
 * @func
 *		Interruption handler for ROM I/F
 * @args
 *		zion_params_t	*params	[in/out]:	parameters of ZION driver
 *		int				irq		[in]	:	IRQ number
 *		void			*dev_id	[in]	:	Device ID
 *		u16				pci_status[in]	:	PCI config interrupt status
 * @return
 * 		void
 * @comment
 * 		
 * @author
 * 		H. Hoshino
 **************************************************************************/
void
zion_romif_event( zion_params_t * params, int bit,
		  int irq, void *dev_id, u16 pci_status )
{
	/* Get ROM I/F params */
	zion_romif_params_t *romif_prms = ROMIF_PARAM(params, ZION_ROMIF);
	
	/* for DEBUG */
	PDEBUG( "Enter ROM I/F interrupt handler\n" );

	/* Check ROM I/F params */
	if ( NULL == romif_prms ) {
		PERROR( "ROM I/F params is NULL!!\n" );
		
		/* Clear interrupt status INT B reg */
		zion_mbus_int_clear( params, Romif_Int );
		return;
	}

	spin_lock( &(romif_prms->params_lock) ); /* Spin lock */
	
	/* Cancel timer */
	del_timer_sync(&(romif_prms->timer));

	/* Wake up*/
	romif_prms->condition = ZION_ROMIF_DISPATCH_DONE;
	
	spin_unlock( &(romif_prms->params_lock) ); /* Unlock spin lock */

	
	wake_up( &(romif_prms->zion_romif_wait_queue) );

	/* Clear interrupt status INT B reg */
	zion_mbus_int_clear( params, Romif_Int );

	/* for DEBUG */
	PDEBUG( "Exit ROM I/F interrupt handler\n" );
	
	return;
}


/***************************************************************************
 * initialize_zion_romif_private_space
 **************************************************************************/
static void initialize_zion_romif_private_space( zion_romif_params_t *romif_params )
{

  memset( (void *)romif_params, 0, sizeof(zion_romif_params_t) );
  init_waitqueue_head( &(romif_params->zion_romif_wait_queue) );
  spin_lock_init( &(romif_params->params_lock) );

  return;
}


/***************************************************************************
 * free_zion_romif_private_space
 **************************************************************************/
static void free_zion_romif_private_space( zion_romif_params_t *romif_params )
{
  if ( romif_params == NULL )
    {
      return;
    }

  wake_up( &(romif_params->zion_romif_wait_queue) );

  kfree( (void *)romif_params );

  /* wait until all threads awake ?? */

  return;
}


/***************************************************************************
 * init_zion_romif
 **************************************************************************/
int init_zion_romif( void )
{
  zion_params_t *zion_params = NULL;
  int i = 0;
  int ret = 0;

  /* get ZION parameters */
  zion_params = find_zion(0);
  if( zion_params == NULL )
    {
      return (-ENODEV);
    }

  /* Check Fireball */
  if((zion_params->revision & 0xF000) == 0xF000)
    {
      PINFO("This is fireball! ZION RomIF cannot be used!!\n");
      return 0;
    }

  /* register fuctions for operation */
  for( i = 0; i < ZION_ROMIF_PORTS; i++ )
    {
      zion_params->zion_operations[ZION_ROMIF+i] = &zion_romif_fops; 
    }
  
  /* Set Private Data Area */
  for( i = 0; i < ZION_ROMIF_PORTS; i++ )
  {
//      ROMIF_PARAM( zion_params, ZION_ROMIF+i ) = kmalloc( sizeof(zion_romif_params_t), GFP_KERNEL );
      zion_params->zion_private[ZION_ROMIF+i] = kmalloc( sizeof(zion_romif_params_t), GFP_KERNEL );
	  
      if ( ROMIF_PARAM(zion_params, ZION_ROMIF+i) == NULL )
	  {
		  PERROR( "Can't get enough space for private data.\n" );
		  return (-ENOMEM);
	  }
	  
      initialize_zion_romif_private_space( ROMIF_PARAM(zion_params, ZION_ROMIF+i) );
  }
  
#ifndef NEO_DEBUG
  PINFO( "ZION ROM I/F module ver. %d.%d Installed.\n",
		  ROMIF_MAJORVER, ROMIF_MINORVER );
#else /* NEO_DEBUG */
  PINFO( "ZION ROM I/F module ver. %d.%d-DEBUG Installed.\n",
		  ROMIF_MAJORVER, ROMIF_MINORVER );
#endif /* !NEO_DEBUG */

  /* enable interruption */
  ret = zion_enable_mbus_interrupt( zion_params, Romif_Int, zion_romif_event );
  if ( ret )
  {
	  PERROR ( "Registering interruption failed.\n" );
	  return (-EINVAL);
  }
  
  /* Reset ROMIF */
  zion_romif_reset( zion_params );
  
  return (0);
}


/***************************************************************************
 * exit_zion_romif
 **************************************************************************/
void exit_zion_romif( void )
{
	zion_params_t *zion_params = NULL;
	int i = 0;
	
	PINFO( "Cleanup ZION ROM I/F module ..." );
	
	/* Get ZION parameters */
	zion_params = find_zion(0);
	if ( zion_params == NULL )
	{
		return;
    }

  /* Check Fireball */
  if((zion_params->revision & 0xF000) == 0xF000)
    {
      return;
    }
	
	for( i = 0; i < ZION_ROMIF_PORTS; i++ )
    {
		free_zion_romif_private_space( ROMIF_PARAM(zion_params, ZION_ROMIF+i) );
    }
	
	PINFO( "Done.\n" );
	
	return;
}
