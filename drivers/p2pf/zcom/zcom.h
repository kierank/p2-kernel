/* $Id: zcom.h 13117 2011-03-10 04:38:15Z Noguchi Isao $ */

#ifndef _ZCOM_H
#define _ZCOM_H

#define ZCOM_VERSION	"zcom version 1.0.2"

#define ZCOM_MAJOR	122
#define ZCOM_MINOR	0
#define ZCOM_DEV_NAME	"zcom"
#define ZCOM_N_DEV	16

#define ZMEM_MAJOR	123
#define ZMEM_MINOR	0
#define ZMEM_DEV_NAME	"zmem"

#define ZCOM_PACKET_SIZE	32

#ifdef CONFIG_ZCOM_TX_BUFFER_NUM
#define ZCOM_N_TX_PACKET_BUFFER	CONFIG_ZCOM_TX_BUFFER_NUM
#else  /* ! CONFIG_ZCOM_TX_BUFFER_NUM */
#define ZCOM_N_TX_PACKET_BUFFER	64
#endif  /* CONFIG_ZCOM_TX_BUFFER_NUM */

#ifdef CONFIG_ZCOM_RX_BUFFER_NUM
#define ZCOM_N_RX_PACKET_BUFFER	CONFIG_ZCOM_RX_BUFFER_NUM
#else  /* ! CONFIG_ZCOM_RX_BUFFER_NUM */
#define ZCOM_N_RX_PACKET_BUFFER	64
#endif  /* CONFIG_ZCOM_RX_BUFFER_NUM */

#ifdef CONFIG_ZCOM_LOG_BUFFER_NUM
#define ZCOM_N_PACKET_LOG	CONFIG_ZCOM_LOG_BUFFER_NUM
#else  /* ! CONFIG_ZCOM_LOG_BUFFER_NUM */
#define ZCOM_N_PACKET_LOG	64
#endif  /* CONFIG_ZCOM_LOG_BUFFER_NUM */

#define ZCOM_PACKET_DUMP_SIZE	(ZCOM_N_PACKET_LOG*1024)

#define ZION_PCI_VENDOR_ID	0x10f7
#define ZION_PCI_DEVICE_ID	0x820a

typedef struct {
	u8 port;
	u8 flags;
#define ZION_OWNER_SH_FLAG	0x80
#define ZION_OWNER_TX_FLAG	0x00
	u16 id;
	u32 tx_time;
	u8 data[ZCOM_PACKET_SIZE];
}zion_packet_t;

#define ZCOM_CMD_INIT	0x3344
#define ZCOM_ACK_INIT	0x1122

typedef struct {
	volatile u16 command;
	volatile u16 status;
	volatile u16 read_ptr;
	volatile u16 write_ptr;
}zion_queue_header_t;

typedef struct {
	zion_queue_header_t *header;
	zion_packet_t *buffer;
	int depth;
}zion_queue_t;

typedef struct {
	spinlock_t lock;

	u8 *regs_base;
	u8 *wram_base;
	u32 wram_size;

	u8 *zmem_base;
	u32 zmem_size;

	u8 *zcom_tx_base;
	u32 zcom_tx_size;
	zion_queue_t tx_q;

	u8 *zcom_rx_base;
	u32 zcom_rx_size;
	zion_queue_t rx_q;

	struct pci_dev *pci;
}zion_dev_t;

typedef struct {
	struct list_head list;
	u32 rx_time;
	u32 rd_time;
	zion_packet_t packet;
	u16 log_no;
	u8 flags;
#define ZCOM_SND_FLAG	0x80
#define ZCOM_RCV_FLAG	0x40
#define ZCOM_ERR_FLAG	0x01
#define ZCOM_DRP_FLAG	0x02
#define ZCOM_PND_FLAG	0x04

	u8 padd[1];
} zcom_packet_entry_t;

struct port_log{
	u32 snd;
	u32 rcv;
	u32 rd;
	u32 drp;
	u32 err;

	unsigned long rx_delay_max;
	unsigned long rx_delay;

	unsigned long rd_delay_max;
	unsigned long rd_delay;

	unsigned long delay_max;
	unsigned long delay;
};

typedef struct{
	spinlock_t lock;
	int id;
	u32 status;
#define ZCOM_CLOSE	0
#define ZCOM_OPEN	1
#define ZCOM_DISABLED	2

	struct list_head rx_q;
	wait_queue_head_t rx_wq;
	int rx_count;

	zion_dev_t *zion;
	struct port_log log;
} zcom_dev_t;

typedef struct{
	zion_dev_t *zion;
} zmem_dev_t;

// zion.c
extern zion_dev_t *zion_init(void);
extern int zion_exit(void);
extern int zion_irq_enable(zion_dev_t *dev);
extern int zion_irq_disable(zion_dev_t *dev);
extern int zion_irq_send(zion_dev_t *dev);
extern int zion_irq_clear(zion_dev_t *dev);
extern int zion_is_irq(zion_dev_t *dev);
extern int zion_write_packet(zion_dev_t *dev, zion_packet_t *packet);
extern int zion_read_packet(zion_dev_t *dev, zion_packet_t *packet);

static inline int zion_recv_empty(zion_dev_t *dev)
{
	return (dev->rx_q.header->read_ptr == dev->rx_q.header->write_ptr);
}

// packet.c
extern int packet_buffer_init(void);
extern int packet_buffer_exit(void);
/* 2011/3/9, Modified by Panasonic (SAV) ---> */
/* extern zcom_packet_entry_t *alloc_packet_entry(void); */
/* extern int free_packet_entry(zcom_packet_entry_t *entry); */
extern zcom_packet_entry_t *alloc_tx_packet_entry(void);
extern zcom_packet_entry_t *alloc_rx_packet_entry(void);
extern int free_tx_packet_entry(zcom_packet_entry_t *entry);
extern int free_rx_packet_entry(zcom_packet_entry_t *entry);
/* <--- 2011/3/9, Modified by Panasonic (SAV) */
extern void add_packet_log(zcom_packet_entry_t *entry);
extern int dump_packet_log(u8 *buf, int count);

// zcom.c
extern struct tasklet_struct zcom_recv_tasklet;
extern int zcom_time_reset(void);
extern u32 zcom_time(void);

// zmem.c 
extern int zmem_init(zion_dev_t *zion);
extern void zmem_exit(void);

/*
#ifdef __SH4__
 #include <asm/preem_latency.h>
 #define zcom_time()	(unsigned long)ctrl_inl(TMU1_TCNT)
 #define zcom_timer_reset() 
 #define OPEN_CLEAR_QUEUE 1
#else
 #error "SH4 Only" 
#endif
*/

#include <asm/atomic.h>
#define ZCOM_ATOMIC_INC(v) atomic_inc((atomic_t *)&v)

#define SWAP32(d, s)	(*(u32*)(d) = __be32_to_cpu(*(u32*)(s)))
#define SWAP16(d, s)	(*(u16*)(d) = __be16_to_cpu(*(u16*)(s)))
#define SWAP8(d, s)	(*(u8*)(d) = *(u8*)(s))

/*
#define SWAP32(d, s)	((char *)(d))[0] = ((char *)(s))[3],\
			((char *)(d))[1] = ((char *)(s))[2],\
			((char *)(d))[2] = ((char *)(s))[1],\
			((char *)(d))[3] = ((char *)(s))[0]

#define SWAP16(d, s)	((char *)(d))[0] = ((char *)(s))[1],\
			((char *)(d))[1] = ((char *)(s))[0]

#define SWAP8(d, s)	((char *)(d))[0] = ((char *)(s))[0]
*/

#define DBG_SYS_NAME	"[zcom]"

#include "debug.h"

#endif /* _ZCOM_H */
