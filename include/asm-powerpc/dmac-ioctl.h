/******************************************************************************
 *** FILENAME	: dmac-ioctl.h
 *** MODULE	: DMA Control Driver
 *** CONTENT	: Ioctl include header file
 *****************************************************************************/
/* Driver's magic number : 'd' */
#define DMAC_IOC_MAGIC 'd'

#define IOCTL_DMAC_DIRECT_DMA   _IO(DMAC_IOC_MAGIC, 1)

#define DMAC_READ	0
#define DMAC_WRITE	1

typedef struct _DmaStruct {
	unsigned int src_addr;  /* source address */
	unsigned int dest_addr; /* destination address */
	unsigned int size;
	unsigned int command;
} DmaStruct;

