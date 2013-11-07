#ifndef __ZION_DMAIF_H__
#define __ZION_DMAIF_H__

/*** Structure definition ***/
struct zion_edma_region
{
  int dma_ch;
  int num;
  unsigned long lower;
  unsigned long upper;
};

/*** Ioctl comamnds of EDMA ***/
#define ZION_EDMA_IOC_SET_REGION _IOW(ZION_MAGIC,0, struct zion_edma_region)
#define ZION_EDMA_IOC_GET_REGION _IOR(ZION_MAGIC,1, struct zion_edma_region)

/* for DEBUG */
#define ZION_EDMA_IOC_CHKSTT_INTSTAT _IO(ZION_MAGIC,2)
#define ZION_EDMA_IOC_CHKEND_INTSTAT _IO(ZION_MAGIC,3)

/* for poll-select */
#define ZION_EDMA_IOC_GET_INTSTTS _IOR(ZION_MAGIC,4,unsigned short)

/* EDMA read/write */
#define ZION_EDMA_IOC_OPEN 		_IOW(ZION_MAGIC,5,int)
#define ZION_EDMA_IOC_CLOSE		_IO (ZION_MAGIC,6)
#define ZION_EDMA_IOC_RUN		_IO (ZION_MAGIC,7)
#define ZION_EDMA_IOC_STOP		_IO (ZION_MAGIC,8)
#define ZION_EDMA_IOC_READ		_IO (ZION_MAGIC,9)
#define ZION_EDMA_IOC_WRITE		_IO (ZION_MAGIC,10)

#ifdef __KERNEL__

#define ZION_EDMA_CH 3
#define ZION_EDMA_CH_PORT 4
#define ZION_EDMA_PORT ( ZION_EDMA_CH * ZION_EDMA_CH_PORT )

#define ZION_EDMA_NR_CH		(ZION_EDMA_CH)
#define ZION_EDMA_NR_PORT	(ZION_EDMA_PORT)

#define ZION_EDMA_READ	(0)
#define ZION_EDMA_WRITE	(1)

#define ZION_DMAIF_DISPATCH_DONE     0
#define ZION_DMAIF_DISPATCH_PENDING  1
#define ZION_DMAIF_DISPATCH_TIMEOUT  2

#define ZION_DMAIF_TIMEOUT (2*HZ)

typedef struct _ZION_EDMA_PARAM
{
  int ch_no;
  struct timer_list timer;
  wait_queue_head_t zion_dmaif_wait_queue;
  int condition;
  struct semaphore dmaif_sem;
  spinlock_t params_lock;
  unsigned short int_status;	/* interrupt status */
} zion_edma_params_t;

typedef struct _ZION_DMAIF_PARAM
{
  zion_edma_params_t port_params[ZION_EDMA_PORT];
} zion_dmaif_params_t;

#define ZION_DMAIF_PARAM(param) ((zion_dmaif_params_t *)((param)->zion_private[ZION_DMAIF]))

/* inline macro function */
static inline int is_edma_port( int minor )
{
	if (  ZION_DMAIF <= minor && minor <  (ZION_DMAIF+ZION_NR_PORTS) )
		return (1);
	else
		return (0);
}

/* init & exit module */
int init_zion_dmaif(void);
void exit_zion_dmaif(void);

#endif /* __KERNEL__ */

#endif  /* __ZION_DMAIF_H__ */
