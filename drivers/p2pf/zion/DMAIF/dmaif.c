/************************************************************
 *
 * dmaif.c : ZION DMAIF Driver
 *
 * $Id: dmaif.c,v 1.1.1.1 2006/02/27 09:20:56 nishikawa Exp $
 *
 ************************************************************/

#define NEO_DEBUG
#define NEO_ERROR
/* #define NEO_EDMASTTS_DEBUG */
#define NEO_WRITEBACK

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
#include <linux/zion_dmaif.h>
#include "zion_dmaif_regs.h"

#define DMAIF_MAJORVER (0)
#define DMAIF_MINORVER (11)

#ifdef CONFIG_ZION_DVCIF
#warning You can NOT compile ZION driver including both DMAIF and DVCIF !
#error You must choose DMAIF or DVCIF
#endif


/********* function for DEBUG XXX *********/
#ifdef NEO_DEBUG
void debug_print_stts( zion_params_t *params, int ch, char *str )
{
	unsigned short edma_stts
		= (unsigned short)mbus_readw( MBUS_ADDR(params, ZION_MBUS_EDMA_EDMA_STATE(ch)) );
	unsigned char sbus_stts
		= mbus_readb( MBUS_ADDR(params, (ZION_MBUS_EDMA_INT_SBUS_STATE(ch)+1)) );
/* 	unsigned short sbus_stts */
/* 		= mbus_readw( MBUS_ADDR(params, (ZION_MBUS_EDMA_INT_SBUS_STATE(ch))) ); */
	PDEBUG( "%sCH%d EDMA state: 0x%04X, SBUS state: 0x%02X\n",
			str, ch ,edma_stts, sbus_stts );
}

void debug_print_debugstts( zion_params_t *params, int ch, char *str )
{
	unsigned short extmode = 0;
	int itr = 0;

	for ( itr = 0; itr < 3; ++itr ) {
		/* Disable TestEn all channel */
		mbus_writew( 0, MBUS_ADDR(params, ZION_MBUS_EDMA_EXT_MODE(itr)) );
	}

	/* Test mode 2 */
	mbus_writeb( 0x21, MBUS_ADDR(params, ZION_MBUS_EDMA_EXT_MODE(ch)+1) );
	extmode = (unsigned short)mbus_readw( MBUS_ADDR(params, ZION_MBUS_EDMA_EXT_MODE(ch)) );
	PDEBUG( "%sCH%d ExtMode mode 2: 0x%04X\n", str, ch, extmode );

	/* Test mode 5 */
	mbus_writeb( 0x51, MBUS_ADDR(params, ZION_MBUS_EDMA_EXT_MODE(ch)+1) );
	extmode = (unsigned short)mbus_readw( MBUS_ADDR(params, ZION_MBUS_EDMA_EXT_MODE(ch)) );
	PDEBUG( "%sCH%d ExtMode mode 5: 0x%04X\n", str, ch, extmode );

	/* Test mode 6 */
	mbus_writeb( 0x61, MBUS_ADDR(params, ZION_MBUS_EDMA_EXT_MODE(ch)+1) );
	extmode = (unsigned short)mbus_readw( MBUS_ADDR(params, ZION_MBUS_EDMA_EXT_MODE(ch)) );
	PDEBUG( "%sCH%d ExtMode mode 6: 0x%04X\n", str, ch, extmode );
}
#else
# define debug_print_stts( params, ch, str )
# define debug_print_debugstts( params, ch, str )
#endif /* NEO_DEBUG */




/***************************************************************************
 * zion_edma_timeout
 * @func
 *		Timedout function of ZION-NEO EDMA
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
zion_edma_timeout(unsigned long prms)
{
	zion_params_t *params = (zion_params_t *)prms;
	int ch = 0;
	zion_edma_params_t *edma_prms = NULL;
	
	/* Check argument */
	if (NULL == params)
    {
		PERROR("Invalid ZION parameters!\n");
		return;
    }
	
	/* Print error message */
	PERROR("Timedout occured!\n");
	
	for (ch = 0; ch < ZION_EDMA_NR_CH; ++ch)
    {
		/* Get EDMA parameters */
		edma_prms =
			&(ZION_DMAIF_PARAM(params)->port_params[ZION_EDMA_DMA_PORTNO(ch)]);
		
		/* Clear DMA command */
      /*  NOTE: Need not write 0 to DmaRun the case of SyncMode = 0 */
		mbus_writew(0, MBUS_ADDR(params, ZION_MBUS_EDMA_DMACMD(ch)));
		
		/* Clear interrupt status: DMA done */
		mbus_writew(ZION_MBUS_EDMA_DMADONEINT,
					MBUS_ADDR(params, ZION_MBUS_EDMA_INTCLR(ch)));
		
		/* Wake up */
		edma_prms->condition = ZION_DMAIF_DISPATCH_TIMEOUT;
		wake_up(&(edma_prms->zion_dmaif_wait_queue));
    }


	/* for DEBUG XXX */
	debug_print_debugstts( params, 0, "" );
	debug_print_debugstts( params, 2, "" );
	
	debug_print_stts( params, 0, "" );
	debug_print_stts( params, 1, "" );
	debug_print_stts( params, 2, "" );
}


/***************************************************************************
 * zion_edma_run
 * @func
 *		Run ZION-NEO EDMA
 * @args
 *		zion_params_t	*params	[in/out]:	parameters of ZION driver
 *		struct inode	*inode	[in]	:	inode
 *		struct file		*file	[in]	:	file structure
 *		int				rw		[in]	:	command
 * @return
 * 		int
 * 		   0	:	success
 * 		   < 0	:	error
 * @comment
 * 
 * @author
 * 		H. Hoshino
 **************************************************************************/
static int
zion_edma_run(zion_params_t * params,
			  struct inode *inode, struct file *file, int rw)
{
	unsigned char ucDmaWnR = 0;
#ifdef NEO_WRITEBACK
	unsigned short usCmdReg = 0;
#endif /* NEO_WRITEBACK */
	
	/* Get minor number */
	int zion_minor = MINOR(inode->i_rdev);
	
	/* Get EDMA parameters */
	zion_edma_params_t *edma_prms = (zion_edma_params_t *)file->private_data;
	
	/* Check EDMA parameters */
	if (NULL == edma_prms)
    {
		PERROR("Private data is NULL!(minor:%d)\n", zion_minor);
		return (-ENODEV);
    }
	
	/* Check argument: rw and set DMA I/O direction */
	switch (rw)
    {
		
    case ZION_EDMA_READ:
		
		/* for DEBUG */
/* 		PDEBUG("[CH%d minor%d]EDMA read\n", edma_prms->ch_no, zion_minor); */
		
		ucDmaWnR = ZION_MBUS_EDMA_READ;
		break;
		
    case ZION_EDMA_WRITE:
		
		/* for DEBUG */
/* 		PDEBUG("[CH%d minor%d]EDMA write\n", edma_prms->ch_no, zion_minor); */
		
		ucDmaWnR = ZION_MBUS_EDMA_WRITE;
		break;
		
    default:
		PERROR("[CH%d minor%d]Invalid I/O direction!(%d)\n",
			   edma_prms->ch_no, zion_minor, rw);
		return (-EINVAL);
    }				/* end of switch (rw) */
	
	/* Set condition */
	edma_prms->condition = ZION_DMAIF_DISPATCH_PENDING;
	
	/* Set timed out */
	init_timer(&(edma_prms->timer));
	edma_prms->timer.function = zion_edma_timeout;
	edma_prms->timer.data = (unsigned long)params;
	edma_prms->timer.expires = jiffies + ZION_DMAIF_TIMEOUT;
	add_timer(&(edma_prms->timer));
	
	/* DMA Run */
	mbus_writeb( ((ucDmaWnR | ZION_MBUS_EDMA_RUN | ZION_MBUS_EDMA_OPEN) & 0x0F),
				MBUS_ADDR(params, ZION_MBUS_EDMA_DMACMD(edma_prms->ch_no) + 1));

#ifdef NEO_WRITEBACK
	/* for DEBUG -- write back XXX */
	usCmdReg = (unsigned short)mbus_readw( MBUS_ADDR(params, ZION_MBUS_EDMA_DMACMD(edma_prms->ch_no)) );
/* 	PDEBUG( "[CH%d minor%d] Write back DMA RUN: 0x%04X\n", */
/* 			edma_prms->ch_no, zion_minor, usCmdReg ); */
#endif /* NEO_WRITEBACK */
	
	/* Sleep */
	wait_event(edma_prms->zion_dmaif_wait_queue,
			   (edma_prms->condition != ZION_DMAIF_DISPATCH_PENDING));
	
	/* DMA End -- check timedout */
	if (ZION_DMAIF_DISPATCH_TIMEOUT == edma_prms->condition)
    {
		PERROR("[CH%d minor%d]ZION EDMA Write Timedout!\n",
			   edma_prms->ch_no, zion_minor);

		/* for DEBUG XXX */
		debug_print_debugstts( params, 0, "" );
		debug_print_debugstts( params, 2, "" );
		
		debug_print_stts( params, 0, "" );
		debug_print_stts( params, 1, "" );
		debug_print_stts( params, 2, "" );
		return (-ETIME);
    }
	
	/* Success */
	return (0);
}


/***************************************************************************
 * zion_edma_set_region
 * @func
 *		set EDMA buffer addresses
 * @args
 *		zion_params_t	*params	[in]:	parameters of ZION driver
 *		int				dma_ch	[in]:	channel
 *		int				num		[in]:	buffer number
 *		unsigned long	lower	[in]:	lower address
 *		unsigned long	upper	[in]:	upper address
 * @return
 * 		int
 * 		   0		:	success
 * 		   -EINVAL	:	invalid argument
 * @comment
 * 		static function
 * @author
 * 		H. Hoshino
 **************************************************************************/
static int
zion_edma_set_region(zion_params_t * params, int dma_ch,
					 int num, unsigned long lower, unsigned long upper)
{
	unsigned short tlower = (unsigned short)( (lower&0x07FF0000L) >> 16 );
	
	/* Check channel number */
	if (dma_ch >= ZION_EDMA_NR_CH)
    {
		PERROR("Invalid EDMA Channel.\n");
		return (-EINVAL);
    }

	/* for DEBUG */
	PDEBUG( "[CH%d Buf%d] lower: 0x%08lX, tlower: 0x%04X, upper: 0x%08lX\n",
			dma_ch, num, lower, tlower, upper );
	
	/* Set addresses */
	mbus_writew(tlower, MBUS_ADDR(params, ZION_MBUS_EDMA_BUFF_LWR_ADRS(dma_ch, num)));
	mbus_writel(upper,  MBUS_ADDR(params, ZION_MBUS_EDMA_BUFF_UPR_ADRS(dma_ch, num)));
	
	return (0);
}


/***************************************************************************
 * zion_edma_get_region
 * @func
 *		Get EDMA buffer addresses
 * @args
 *		zion_params_t	*params	[in]:	parameters of ZION driver
 *		int				dma_ch	[in]:	channel
 *		int				num		[in]:	buffer number
 *		unsigned long	*lower	[out]:	lower address
 *		unsigned long	*upper	[out]:	upper address
 * @return
 * 		int
 * 		   0		:	success
 * 		   -EINVAL	:	invalid argument
 * @comment
 * 		static function
 * @author
 * 		H. Hoshino
 **************************************************************************/
static int
zion_edma_get_region(zion_params_t * params, int dma_ch,
					 int num, unsigned long *lower, unsigned long *upper)
{
	unsigned short tlower = 0;
	
	/* Check channel number */
	if (dma_ch >= ZION_EDMA_NR_CH)
    {
		PERROR("Invalid EDMA Channel.\n");
		return (-EINVAL);
    }
	
	/* Get addresses */
	tlower = mbus_readw(MBUS_ADDR(params, ZION_MBUS_EDMA_BUFF_LWR_ADRS(dma_ch, num)));
	*upper = mbus_readl(MBUS_ADDR(params, ZION_MBUS_EDMA_BUFF_UPR_ADRS(dma_ch, num)));

	*lower = (unsigned long)( (tlower << 16) & 0x07FF0000L );

	/* for DEBUG */
	PDEBUG( "[CH%d Buf%d] lower: 0x%08lX, tlower: 0x%04X, upper: 0x%08lX\n",
			dma_ch, num, *lower, tlower, *upper );
	
	return (0);
}


/*********************** functions for system calls. ***********************/

/***************************************************************************
 * zion_dmaif_port_open
 **************************************************************************/
int
zion_dmaif_port_open(zion_params_t * zion_params, struct inode *inode,
					 struct file *filp)
{
	int zion_minor = MINOR(inode->i_rdev);
	
	/* The case of EDMA port opened */
	if (is_edma_port(zion_minor))
    {
		int zion_edma_port = zion_minor - ZION_DMAIF;
		zion_edma_params_t *edma_prms =
			&(ZION_DMAIF_PARAM(zion_params)->port_params[zion_edma_port]);
		unsigned long irq_flags = 0;

		/* Spin lock */
		spin_lock_irqsave( &(edma_prms->params_lock), irq_flags );
		
/* 		PDEBUG("EDMA port%d opened\n", zion_edma_port); */
		
		/* Initialize EDMA parameters */
		edma_prms->ch_no = (int)(zion_edma_port / ZION_EDMA_CH_PORT);
		edma_prms->condition = ZION_DMAIF_DISPATCH_PENDING;
		edma_prms->int_status = 0x0000;
		
/* 		PDEBUG("Initialize EDMA CH%d\n", edma_prms->ch_no); */

		/* Use private data of file structure */
		filp->private_data = (void *)edma_prms;

		/* Clear DMA command register */
		mbus_writeb( 0, MBUS_ADDR(zion_params, ZION_MBUS_EDMA_DMACMD(edma_prms->ch_no)+1) );
		
		/** Success **/
		/* Unlock spin lock */
		spin_unlock_irqrestore( &(edma_prms->params_lock), irq_flags );
		
//		MOD_INC_USE_COUNT;
		return 0;
    }
	
	/** The case of NOT EDMA port opened **/
	PERROR("Minor number is invalid(%d)!\n", zion_minor);
	return (-ENODEV);
}

/***************************************************************************
 * zion_dmaif_port_release
 **************************************************************************/
int
zion_dmaif_port_release(zion_params_t * zion_params, struct inode *inode,
						struct file *filp)
{
	int zion_minor = MINOR(inode->i_rdev);
	
	/* The case of EDMA port released */
	if (is_edma_port(zion_minor))
    {
		int zion_edma_port = zion_minor - ZION_DMAIF;
		zion_edma_params_t *edma_prms =
			&(ZION_DMAIF_PARAM(zion_params)->port_params[zion_edma_port]);
		unsigned long irq_flags = 0;

		/* Spin lock */
		spin_lock_irqsave( &(edma_prms->params_lock), irq_flags );
		
/* 		PDEBUG("EDMA port%d released\n", zion_edma_port); */
		
		/* Clear EDMA parameters */
		edma_prms->ch_no = 0;
		edma_prms->condition = ZION_DMAIF_DISPATCH_PENDING;
		edma_prms->int_status = 0x0000;
		
		filp->private_data = NULL;
		
		/** Success **/
		/* Unlock spin lock */
		spin_unlock_irqrestore( &(edma_prms->params_lock), irq_flags );

//		MOD_DEC_USE_COUNT;
		return 0;
    }
	
	/** The case of NOT EDMA port released **/
	PERROR("Minor number is invalid(%d)!\n", zion_minor);
	return (-ENODEV);
	

}

/***************************************************************************
 * zion_dmaif_port_poll
 **************************************************************************/
unsigned int
zion_dmaif_port_poll(zion_params_t * params,
					 struct file *filp, struct poll_table_struct *pts)
{
	zion_edma_params_t *edma_prms = NULL;
	unsigned long irq_flags = 0;
	
	/* Get minor number */
	int zion_minor = MINOR(filp->f_dentry->d_inode->i_rdev);

	/* Check the case of EDMA port opened */
	if (!is_edma_port(zion_minor))
    {
		/** NOT EDMA port **/
		PERROR("Minor number is invalid(%d)!\n", zion_minor);
		return (POLLERR);
    }
	
	/* Get EDMA parameters */
	edma_prms = (zion_edma_params_t *)filp->private_data;
	
	/* Check private data */
	if (NULL == edma_prms)
    {
		PERROR("Private data is NULL!(minor:%d)\n", zion_minor);
		return (POLLERR);
    }

	/* Spin lock */
	spin_lock_irqsave( &(edma_prms->params_lock), irq_flags );
	
	if (ZION_DMAIF_DISPATCH_DONE == edma_prms->condition)	/* XXX */
    {
		/* Wake up */
		edma_prms->condition = ZION_DMAIF_DISPATCH_PENDING;	/* XXX */

		/* Unlock Spin lock */
		spin_unlock_irqrestore( &(edma_prms->params_lock), irq_flags );
		
		return (POLLIN | POLLRDNORM);
    }
	
	/* Sleep */
	poll_wait(filp, &(edma_prms->zion_dmaif_wait_queue), pts);
	edma_prms->condition = ZION_DMAIF_DISPATCH_PENDING;
	edma_prms->int_status = 0x0000;
/* 	PDEBUG( "EDMA CH%d minor%d sleeping...\n", */
/* 			edma_prms->ch_no, zion_minor ); */
	
	/* Unlock Spin lock */
	spin_unlock_irqrestore( &(edma_prms->params_lock), irq_flags );
		
	return (0);
}


/***************************************************************************
 * zion_dmaif_port_ioctl
 **************************************************************************/
int
zion_dmaif_port_ioctl(zion_params_t * params,
					  struct inode *inode, struct file *file,
					  unsigned int function, unsigned long arg)
{
	switch (function)
    {
		
    case ZION_EDMA_IOC_OPEN:
	{
		unsigned char ucReg = 0;
		zion_edma_params_t *edma_prms = (zion_edma_params_t *)file->private_data;
		int ch = -1;
		int rw = -1;
		unsigned long irq_flags = 0;
		
		/* Check EDMA parameters */
		if (NULL == edma_prms)
		{
			PERROR("Private data is NULL!\n");
			return (-ENODEV);
		}
		
		/* Get and check EDMA channel number */
		ch = edma_prms->ch_no;
		if (ch < 0 || ZION_EDMA_NR_CH <= ch)
		{
			PERROR("Invalid ZION EDMA channel number(%d)!", ch);
			return (-ENODEV);
		}
		
		/* Get argument: I/O direction */
		if (copy_from_user((void *)&rw, (void *)arg, sizeof(int)))
			return (-EFAULT);
		
		/* Check argument: I/O direction */
		if ((ZION_EDMA_READ != rw) && (ZION_EDMA_WRITE != rw))
		{
			PERROR("[CH%d] Invalid I/O direction(%d)!\n", ch, rw);
			return (-EINVAL);
		}
		
		/* Read EDMA command register */
		ucReg = mbus_readb(MBUS_ADDR(params, ZION_MBUS_EDMA_DMACMD(ch) + 1));
		
		/* Write EDMA command register: DmaOpen=1 */
		ucReg |= (ZION_MBUS_EDMA_OPEN | (rw << 2)) & 0x0F;
/* 		PDEBUG("[CH%d] EDMA open(0x%02X)\n", ch, ucReg); */
		mbus_writeb(ucReg, MBUS_ADDR(params, ZION_MBUS_EDMA_DMACMD(ch) + 1));

		/* Clear interrupt status */
		spin_lock_irqsave( &(edma_prms->params_lock), irq_flags );
		edma_prms->int_status = 0x0000;
		spin_unlock_irqrestore( &(edma_prms->params_lock), irq_flags );
		
		break;
	}
	
    case ZION_EDMA_IOC_CLOSE:
	{
		unsigned char ucReg = 0;
		zion_edma_params_t *edma_prms =
			(zion_edma_params_t *)file->private_data;
		int ch = -1;
		
		/* Check EDMA parameters */
		if (NULL == edma_prms)
		{
			PERROR("Private data is NULL!\n");
			return (-ENODEV);
		}
		
		/* Get and check EDMA channel number */
		ch = edma_prms->ch_no;
		if (ch < 0 || ZION_EDMA_NR_CH <= ch)
		{
			PERROR("Invalid ZION EDMA channel number(%d)!", ch);
			return (-ENODEV);
		}
		
		/* Read EDMA command register */
		ucReg = mbus_readb( MBUS_ADDR(params, (ZION_MBUS_EDMA_DMACMD(ch)+1)) );
		
		/* Write EDMA command register: DmaOpen=0 */
		ucReg &= ZION_MBUS_EDMA_CLOSE & 0x0F;
/* 		PDEBUG("[CH%d] EDMA close(0x%02X)\n", ch, ucReg); */
		mbus_writeb( ucReg, MBUS_ADDR(params, (ZION_MBUS_EDMA_DMACMD(ch)+1)) );
		
		/* Success */
		break;
	}
	
    case ZION_EDMA_IOC_RUN:
	{
		unsigned char ucReg = 0;
		zion_edma_params_t *edma_prms = (zion_edma_params_t *)file->private_data;
		int ch = -1;

		unsigned short usCmdReg = 0;
		
		/* Check EDMA parameters */
		if (NULL == edma_prms)
		{
			PERROR("Private data is NULL!\n");
			return (-ENODEV);
		}
		
		/* Get and check EDMA channel number */
		ch = edma_prms->ch_no;
		if (ch < 0 || ZION_EDMA_NR_CH <= ch)
		{
			PERROR("Invalid ZION EDMA channel number(%d)!", ch);
			return (-ENODEV);
		}
		
		/* Read EDMA command register */
		ucReg = mbus_readb(MBUS_ADDR(params, ZION_MBUS_EDMA_DMACMD(ch) + 1));
		
		/* Write EDMA command register: DmaRun=1 */
		ucReg |= ZION_MBUS_EDMA_RUN;
		PDEBUG ("[CH%d] EDMA run(0x%02X)\n", ch, ucReg);
		mbus_writeb(ucReg, MBUS_ADDR(params, ZION_MBUS_EDMA_DMACMD(ch) + 1));

#ifdef NEO_WRITEBACK
		/* for DEBUG -- write back XXX */
		usCmdReg = (unsigned short)mbus_readw( MBUS_ADDR(params, ZION_MBUS_EDMA_DMACMD(edma_prms->ch_no)) );
/* 		PDEBUG( "[CH%d] Write back DMA RUN: 0x%04X\n", */
/* 				edma_prms->ch_no, usCmdReg ); */
#endif /* NEO_WRITEBACK */
		
		/* Success */
		break;
	}
	
    case ZION_EDMA_IOC_STOP:
	{
		unsigned char ucReg = 0;
		zion_edma_params_t *edma_prms = (zion_edma_params_t *)file->private_data;
		int ch = -1;
		
		/* Check EDMA parameters */
		if (NULL == edma_prms)
		{
			PERROR("Private data is NULL!\n");
			return (-ENODEV);
		}
		
		/* Get and check EDMA channel number */
		ch = edma_prms->ch_no;
		if (ch < 0 || ZION_EDMA_NR_CH <= ch)
		{
			PERROR("Invalid ZION EDMA channel number(%d)!", ch);
			return (-ENODEV);
		}
		
		/* Read EDMA command register */
		ucReg = mbus_readb(MBUS_ADDR(params, ZION_MBUS_EDMA_DMACMD(ch) + 1));
		
		/* Write EDMA command register: DmaRun=0 */
		ucReg &= ZION_MBUS_EDMA_STOP & 0x0F;
/* 		PDEBUG("[CH%d] EDMA stop(0x%02X)\n", ch, ucReg); */
		mbus_writeb(ucReg, MBUS_ADDR(params, ZION_MBUS_EDMA_DMACMD(ch) + 1));
		
		/* Success */
		break;
	}
	
    case ZION_EDMA_IOC_READ:
	{
		/* EDMA read */
		return (zion_edma_run(params, inode, file, ZION_EDMA_READ));
	}
	
	
    case ZION_EDMA_IOC_WRITE:
	{
		/* EDMA write */
		return (zion_edma_run(params, inode, file, ZION_EDMA_WRITE));
	}
	
	
    case ZION_EDMA_IOC_SET_REGION:
	{
		struct zion_edma_region s_zion_edma_region;
		
		if (copy_from_user((void *)&s_zion_edma_region,
						   (void *)arg, sizeof(struct zion_edma_region)))
			return (-EFAULT);
		
		return zion_edma_set_region(params,
									s_zion_edma_region.dma_ch,
									s_zion_edma_region.num,
									s_zion_edma_region.lower,
									s_zion_edma_region.upper);
	}
	
	
    case ZION_EDMA_IOC_GET_REGION:
	{
		struct zion_edma_region s_zion_edma_region;
		
		if (copy_from_user((void *)&s_zion_edma_region,
						   (void *)arg, sizeof(struct zion_edma_region)))
			return (-EFAULT);
		
		if (zion_edma_get_region(params,
								 s_zion_edma_region.dma_ch,
								 s_zion_edma_region.num,
								 &s_zion_edma_region.lower,
								 &s_zion_edma_region.upper))
			return -EINVAL;
		
		if (copy_to_user((void *)arg,
						 (void *)&s_zion_edma_region,
						 sizeof(struct zion_edma_region)))
			return -EFAULT;
		
		/* Success */
		break;
	}
	
	/* Get interrupt status for poll-select */
    case ZION_EDMA_IOC_GET_INTSTTS:
	{
		zion_edma_params_t *edma_prms = (zion_edma_params_t *)file->private_data;
		int ch = -1;
		unsigned long irq_flags = 0;
		
		/* Check EDMA parameters */
		if (NULL == edma_prms)
		{
			PERROR("Invalid ZION EDMA parameters!\n");
			return (-ENODEV);
		}
		
		/* Set and check EDMA channel number */
		ch = edma_prms->ch_no;
		if (ch < 0 || ZION_EDMA_NR_CH <= ch)
		{
			PERROR("Invalid ZION EDMA channel number(%d)!", ch);
			return (-ENODEV);
		}
		
		/* Set interrupt status */
		if (copy_to_user((void *)arg, (void *)&(edma_prms->int_status),
						 sizeof(unsigned short)))
			return (-EFAULT);

		/* Clear interrupt status */
		spin_lock_irqsave( &(edma_prms->params_lock), irq_flags );
		edma_prms->int_status = 0x0000;
		spin_unlock_irqrestore( &(edma_prms->params_lock), irq_flags );
		
		/* Success */
		break;
	}
	
    default:
		PERROR("No such Ioctl command!\n");
		return (-EINVAL);
		
    } /* The end of SWITCH(function) */
	
	return 0;
}

/** DMA I/F operations for each port **/
struct zion_file_operations zion_dmaif_port_fops = {
	ioctl: zion_dmaif_port_ioctl,
	open: zion_dmaif_port_open,
	release: zion_dmaif_port_release,
	poll: zion_dmaif_port_poll,
};

/***************************************************************************
 * zion_dmaif_event
 * @func
 *		Interruption handler for EDMA
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
zion_dmaif_event(zion_params_t * params, int bit,
		 int irq, void *dev_id, u16 pci_status)
{
	unsigned short int_status = 0;
	unsigned short int_mask = 0;
	int ch = 0;
	zion_edma_params_t *edma_prms = NULL;
	
	/*** Check EDMA interruption for each channel ***/
	for (ch = 0; ch < ZION_EDMA_NR_CH; ++ch)
    {
		/* Get interrupt status and mask(enable) */
		int_status =
			(unsigned short)mbus_readw( MBUS_ADDR(params, ZION_MBUS_EDMA_INTSTTS(ch)) );
		int_mask =
			(unsigned short)mbus_readw( MBUS_ADDR(params, ZION_MBUS_EDMA_INTENB(ch)) );
		
		/* Memorize it ! */
		params->interrupt_bits.DMA_INT[ch] |= int_status;

		/* for DEBUG */
/* 		PDEBUG( "CH%d int_status: 0x%04X, mask: 0x%04X\n", */
/* 				ch, int_status, int_mask ); */

		/** Check interruptions (Common port) **/
		if ( int_status & int_mask ) {
			/* Get EDMA params */
			edma_prms = &(ZION_DMAIF_PARAM(params)->port_params[ZION_EDMA_CMN_PORTNO(ch)]);

			/* for DEBUG */
/* 			PDEBUG( "[CH%d port%d] Get interruptions!\n", */
/* 					ch, ZION_EDMA_CMN_PORTNO(ch) ); */

			if ( NULL == edma_prms ) {
				PERROR( "[CH%d port%d] EDMA params is NULL!!\n",
						ch, ZION_EDMA_CMN_PORTNO(ch) );

				mbus_writew( (ZION_MBUS_EDMA_DMADONEINT |
							  ZION_MBUS_EDMA_SYNCFRMINT |
							  ZION_MBUS_EDMA_BUFFINT_MASK |
							  ZION_MBUS_EDMA_ERRINT_MASK),
							 MBUS_ADDR(params, ZION_MBUS_EDMA_INTCLR(ch)));

				/** Clear interrupt status INT B reg **/
				zion_mbus_int_clear(params, Dmaif_Int);
				
				return;
			}
			
			/* Set interrupt status */
			spin_lock( &(edma_prms->params_lock) ); /* Spin lock */
			edma_prms->int_status |= int_status;
/* 			edma_prms->int_status = int_status; */
			spin_unlock( &(edma_prms->params_lock) ); /* Unlock spin lock */

			/* Wake up */
			spin_lock( &(edma_prms->params_lock) ); /* Spin lock */
			edma_prms->condition = ZION_DMAIF_DISPATCH_DONE;
			spin_unlock( &(edma_prms->params_lock) ); /* Unlock spin lock */
			
			wake_up(&(edma_prms->zion_dmaif_wait_queue));
		}
		
		
		/** Check sync frame pulse interruption **/
		if ( int_status & int_mask & ZION_MBUS_EDMA_SYNCFRMINT )
		{
			/* Get EDMA params */
			edma_prms = &(ZION_DMAIF_PARAM(params)->port_params[ZION_EDMA_FRM_PORTNO(ch)]);

			/* for DEBUG */
/* 			PDEBUG( "[CH%d port%d] Sync frame pulse!\n", */
/* 					ch, ZION_EDMA_FRM_PORTNO(ch) ); */

			/* Set interrupt status */
			spin_lock( &(edma_prms->params_lock) ); /* Spin lock */
			if ( 0 == edma_prms->int_status ) {
				edma_prms->int_status = int_status;
			} else {
/* 				PDEBUG( "[CH%d port%d] int_status is already set(0x%04X, 0x%04X)!\n", */
/* 						ch, ZION_EDMA_DMA_PORTNO(ch), edma_prms->int_status, int_status ); */
			}

			spin_unlock( &(edma_prms->params_lock) ); /* Unlock spin lock */
			
			/* Clear interrupt status: sync frame pulse */
			mbus_writew(ZION_MBUS_EDMA_SYNCFRMINT, MBUS_ADDR(params, ZION_MBUS_EDMA_INTCLR(ch)));
			
			/* Wake up */
			spin_lock( &(edma_prms->params_lock) ); /* Spin lock */
			edma_prms->condition = ZION_DMAIF_DISPATCH_DONE;
			spin_unlock( &(edma_prms->params_lock) ); /* Unlock spin lock */
			
			wake_up(&(edma_prms->zion_dmaif_wait_queue));
		}
		
		/** Check DMA done interruption **/
		if ( int_status & int_mask & ZION_MBUS_EDMA_DMADONEINT )
		{
			/* Get EDMA params */
			edma_prms = &(ZION_DMAIF_PARAM(params)->port_params[ZION_EDMA_DMA_PORTNO(ch)]);
			
			/* for DEBUG */
			PDEBUG( "[CH%d port%d] DMA Done!\n",
					ch, ZION_EDMA_DMA_PORTNO(ch) );
/* 			PINFO( "[CH%d port%d] DMA Done!\n", */
/* 					ch, ZION_EDMA_DMA_PORTNO(ch) ); */
			
			/* Stop DMA for G1&G2 transfer mode */
			{
				unsigned char cmd = mbus_readb( MBUS_ADDR(params, (ZION_MBUS_EDMA_DMACMD(ch)+1)) );
				mbus_writeb( (cmd & (~0x02)),
							 MBUS_ADDR(params, (ZION_MBUS_EDMA_DMACMD(ch)+1)) );
			}

			/* for DEBUG XXX*/
/* 			{ */
/* 				unsigned short usPMT = mbus_readw( MBUS_ADDR(params, ZION_MBUS_EDMA_CURRPMT(ch)) ); */
/* 				PDEBUG( "[CH%d port%d] CurrPMT: 0x%04X\n", */
/* 						ch, ZION_EDMA_DMA_PORTNO(ch), usPMT ); */
/* 			} */
			
			
			spin_lock( &(edma_prms->params_lock) ); /* Spin lock */
			
			/* Set interrupt status */
/* 			PDEBUG( "[CH%d] int_status: 0x%04X, old: 0x%04X!\n", */
/* 					ch, int_status, edma_prms->int_status ); */
			if ( 0 == edma_prms->int_status ) {
				edma_prms->int_status = int_status;
			} else {
/* 				PDEBUG( "[CH%d port%d] int_status is already set(0x%04X, 0x%04X)!\n", */
/* 						ch, ZION_EDMA_DMA_PORTNO(ch), edma_prms->int_status, int_status ); */
			}

			
			/* Delete timer */
			del_timer_sync(&(edma_prms->timer));
			spin_unlock( &(edma_prms->params_lock) ); /* Unlock spin lock */
			
			/* Clear interrupt status: DMA done */
			mbus_writew(ZION_MBUS_EDMA_DMADONEINT,
						MBUS_ADDR(params, ZION_MBUS_EDMA_INTCLR(ch)));
			
			/* Wake up */
			spin_lock( &(edma_prms->params_lock) ); /* Spin lock */
			edma_prms->condition = ZION_DMAIF_DISPATCH_DONE;
			spin_unlock( &(edma_prms->params_lock) ); /* Unlock spin lock */
			
			wake_up(&(edma_prms->zion_dmaif_wait_queue));
		}
		
		
		/** Check error and buffer empty/almost empty/almost full/full interruption **/
		if ( int_status & int_mask &
			 (ZION_MBUS_EDMA_BUFFINT_MASK | ZION_MBUS_EDMA_ERRINT_MASK) )
		{
			/* Get EDMA parameters */
			edma_prms = &(ZION_DMAIF_PARAM (params)->port_params[ZION_EDMA_BUF_PORTNO(ch)]);

			/* for DEBUG */
			PDEBUG( "[CH%d port%d] Error or warning!\n",
					ch, ZION_EDMA_BUF_PORTNO(ch) );

			/* Set interrupt status */
			spin_lock( &(edma_prms->params_lock) ); /* Spin lock */
			
/* 			PDEBUG( "[CH%d] int_status: 0x%04X, old: 0x%04X!\n", */
/* 					ch, int_status, edma_prms->int_status ); */
			if ( 0 == edma_prms->int_status ) {
				edma_prms->int_status = int_status;
			} else {
/* 				PDEBUG( "[CH%d port%d] int_status is already set(0x%04X, 0x%04X)!\n", */
/* 						ch, ZION_EDMA_BUF_PORTNO(ch), edma_prms->int_status, int_status ); */
			}
			
			spin_unlock( &(edma_prms->params_lock) ); /* Unlock spin lock */
			
			/* Clear interrupt status */
			mbus_writew((ZION_MBUS_EDMA_BUFFINT_MASK | ZION_MBUS_EDMA_ERRINT_MASK),
						MBUS_ADDR(params, ZION_MBUS_EDMA_INTCLR(ch)));
			
			/* Wake up */
			spin_lock( &(edma_prms->params_lock) ); /* Spin lock */
			edma_prms->condition = ZION_DMAIF_DISPATCH_DONE;
			spin_unlock( &(edma_prms->params_lock) ); /* Unlock spin lock */
			
			wake_up(&(edma_prms->zion_dmaif_wait_queue));
		}
    }	/* The end of FOR(ch) */

	/** Clear interrupt status INT B reg **/
	zion_mbus_int_clear(params, Dmaif_Int);

/* 	PDEBUG ("exit %s\n", __FUNCTION__); */
	
  return;
}

/***************************************************************************
 * initialize_zion_dmaif_private_space
 **************************************************************************/
static int
initialize_zion_dmaif_private_space(zion_dmaif_params_t * dmaif_params)
{
  int i;
  
  memset((void *)dmaif_params, 0, sizeof(zion_dmaif_params_t));
  
  for (i = 0; i < ZION_EDMA_PORT; ++i)
  {
      init_waitqueue_head(&(dmaif_params->port_params[i].zion_dmaif_wait_queue));
      init_MUTEX(&(dmaif_params->port_params[i].dmaif_sem));
	  spin_lock_init( &(dmaif_params->port_params[i].params_lock) );
  }

  return 0;
}

/***************************************************************************
 * free_zion_dmaif_private_space
 **************************************************************************/
static void
free_zion_dmaif_private_space(zion_dmaif_params_t * dmaif_params)
{
	int i = 0;
	
	if (dmaif_params == NULL)
    {
		return;
    }

	/* Wake up if exist sleeping processes */
	for (i = 0; i < ZION_EDMA_PORT; ++i)
	{
		wake_up( &(dmaif_params->port_params[i].zion_dmaif_wait_queue) );
	}
	
	kfree((void *)dmaif_params);
	dmaif_params = NULL;
	
	return;
}

/***************************************************************************
 * init_zion_dmaif
 **************************************************************************/
int
init_zion_dmaif(void)
{
	zion_params_t *zion_params;
	int ret, i;
	
	/* get ZION parameters */
	zion_params = find_zion(0);
	if (zion_params == NULL)
	{
		return -ENODEV;
    }

  /* Check Fireball */
  if((zion_params->revision & 0xF000) == 0xF000)
    {
      PINFO("This is fireball! ZION DMA IF cannot be used!!\n");
      return 0;
    }
	
	/* register fuctions for operation */
	for (i = 0; i < ZION_EDMA_PORT; i++)
    {
		zion_params->zion_operations[ZION_DMAIF + i] = &zion_dmaif_port_fops;
    }
	
	/* Set Private Data Area */
	zion_params->zion_private[ZION_DMAIF] = kmalloc(sizeof(zion_dmaif_params_t), GFP_KERNEL);
	if (ZION_DMAIF_PARAM(zion_params) == NULL)
    {
		PERROR ("Can't get enough space for private data.\n");
		return -ENOMEM;
    }
	
	initialize_zion_dmaif_private_space(ZION_DMAIF_PARAM (zion_params));

#ifndef NEO_DEBUG
	PINFO("ZION DMAIF(EDMA) module ver. %d.%d Installed.\n",
		   DMAIF_MAJORVER, DMAIF_MINORVER);
#else /* NEO_DEBUG */
	PINFO("ZION DMAIF(EDMA) module ver. %d.%d-DEBUG Installed.\n",
		   DMAIF_MAJORVER, DMAIF_MINORVER);
#endif /* !NEO_DEBUG */

	/* enable interruption */
	mbus_writew(ZION_MBUS_DMASEL_ENB, MBUS_ADDR(zion_params, ZION_MBUS_IOSEL));	/* DMA select */
	ret = zion_enable_mbus_interrupt(zion_params, Dmaif_Int, zion_dmaif_event);
	if (ret)
    {
		PERROR ("registering interruption failed.\n");
		return -EINVAL;
    }
	mbus_writew(ZION_MBUS_INTGB_MPU1ENB, MBUS_ADDR(zion_params, ZION_MBUS_INTGEN_B));	/* INT Gen B Enable */

	return 0;
}

/***************************************************************************
 * exit_zion_dmaif
 **************************************************************************/
void
exit_zion_dmaif(void)
{
	zion_params_t *zion_params;
	
	PINFO("Cleanup ZION DMAIF module ...");
	
	/* get ZION parameters */
	zion_params = find_zion (0);
	if (zion_params == NULL)
    {
		return;
    }

  /* Check Fireball */
  if((zion_params->revision & 0xF000) == 0xF000)
    {
      return;
    }
	/* Disable backend interrupts */
	mbus_writew(0, MBUS_ADDR(zion_params, ZION_MBUS_INTGEN_B));	/* INT Gen B Enable */
	zion_disable_mbus_interrupt(zion_params, Dmaif_Int);
	mbus_writew(0, MBUS_ADDR(zion_params, ZION_MBUS_IOSEL));	/* DMA select */
	
	free_zion_dmaif_private_space(ZION_DMAIF_PARAM (zion_params));
	
	PINFO( "done.\n" );
	
	return;
}
