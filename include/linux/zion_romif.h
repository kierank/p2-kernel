/* $Id: zion_romif.h,v 1.1.1.1 2006/02/27 09:21:05 nishikawa Exp $ */

#ifndef __ZION_ROMIF_H__
#define __ZION_ROMIF_H__

/** Structure definition **/
/* ROM I/F read/write for SCI */
struct zion_romif_sci_buf
{
	unsigned short adr;
	unsigned short dat;
};

/* ROM I/F read/write for SPI */
struct zion_romif_spi_buf
{
	unsigned short adr;
	unsigned short dat;
};


/* ROM I/F status read/write for SPI */
struct zion_romif_spi_stts
{
	unsigned char dat;
};


/* ROM I/F read to MBUS */
struct zion_romif_trns
{
	unsigned short src;
	unsigned short dest;
	unsigned short size;
	unsigned char inc;
};


/** Ioctl commands of ROM I/F **/
#define ZION_ROMIF_IOC_RESET		_IO ( ZION_MAGIC,  0 )
#define ZION_ROMIF_IOC_SET_CLKDIV	_IOW( ZION_MAGIC,  1, unsigned char )
#define ZION_ROMIF_IOC_SCI_READ		_IOR( ZION_MAGIC,  2, struct zion_romif_sci_buf )
#define ZION_ROMIF_IOC_SCI_WRITE	_IOW( ZION_MAGIC,  3, struct zion_romif_sci_buf )
#define ZION_ROMIF_IOC_SCI_WRITEENB	_IO ( ZION_MAGIC,  4 )
#define ZION_ROMIF_IOC_SCI_READ2MBUS	_IOW( ZION_MAGIC,  5, struct zion_romif_trns )

#define ZION_ROMIF_IOC_SPI_READ		_IOR( ZION_MAGIC,  6, struct zion_romif_spi_buf )
#define ZION_ROMIF_IOC_SPI_WRITE	_IOW( ZION_MAGIC,  7, struct zion_romif_spi_buf )
#define ZION_ROMIF_IOC_SPI_WRITEENB	_IO ( ZION_MAGIC,  8 )
#define ZION_ROMIF_IOC_SPI_SREAD	_IOR( ZION_MAGIC,  9, struct zion_romif_spi_stts )
#define ZION_ROMIF_IOC_SPI_SWRITE	_IOW( ZION_MAGIC, 10, struct zion_romif_spi_stts )
#define ZION_ROMIF_IOC_SPI_READ2MBUS	_IOW( ZION_MAGIC, 11, struct zion_romif_trns )

#ifdef __KERNEL__

/* Minor Ports */
#define ZION_ROMIF_PORTS   1  /* Number of Ports */

#define ZION_ROMIF_DISPATCH_DONE		(0)
#define ZION_ROMIF_DISPATCH_PENDING		(1)
#define ZION_ROMIF_DISPATCH_TIMEOUT		(2)

#define ZION_ROMIF_TIMEOUT (2*HZ)


/* command */
#define ZION_ROMIF_SCI_CMD_READ  (0xA800)
#define ZION_ROMIF_SCI_CMD_WRITE (0xA400)
#define ZION_ROMIF_SCI_CMD_WENB  (0xA300)

#define ZION_ROMIF_SPI_CMD_READ  (0x0300)
#define ZION_ROMIF_SPI_CMD_WRITE (0x0200)
#define ZION_ROMIF_SPI_CMD_WENB  (0x0600)
#define ZION_ROMIF_SPI_CMD_SREAD (0x0500)
#define ZION_ROMIF_SPI_CMD_SWRITE (0x0100)

typedef struct _ZION_ROMIF_PARAM
{
	struct timer_list timer;
	wait_queue_head_t zion_romif_wait_queue;
	int condition;
	spinlock_t params_lock;
	int count;
	
	int where_wait; /* for DEBUG */
	
} zion_romif_params_t;

#define ROMIF_PARAM(param,minor) \
      ((zion_romif_params_t *)((param)->zion_private[minor]))

/* init & exit module */
int init_zion_romif(void);
void exit_zion_romif(void);

#endif /* __KERNEL__ */

#endif  /* __ZION_ROMIF_H__ */
