#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>

#include "zcom.h"

#define INTA_CLR_REG	0x10
#define INTA_STS_REG	0x12
#define INTA_MSK_REG	0x14
#define INTA_GEN_REG	0x16

#define INTB_CLR_REG	0x18
#define INTB_STS_REG	0x1a
#define INTB_MSK_REG	0x1c
#define INTB_GEN_REG	0x1e

#define WRAM_SIZE	(16*1024)

#define ZCOM_RX_OFFSET	0x2c00
#define ZCOM_RX_SIZE	2048

#define ZCOM_TX_OFFSET	0x3400
#define ZCOM_TX_SIZE	2048

#define ZMEM_OFFSET	0x0000
#define ZMEM_SIZE	WRAM_SIZE

#define UPINT1		(1 << 4)
#define PCIINT1		(1 << 2)

static zion_dev_t zion_dev;

static inline void mbus_write16(unsigned short b, unsigned long offset)
{
	iowrite16be(b, (void *)(zion_dev.regs_base + offset));
}

static inline unsigned short mbus_read16(unsigned long offset)
{
	return ioread16be((void *)(zion_dev.regs_base + offset));
}

#ifdef ZCOM_LOOPBACK

void loopback_handler(unsigned long arg)
{
	volatile u32 write, read;
	u32 next;
	unsigned long flag;
	zion_packet_t packet;
	zion_dev_t *dev=&zion_dev;
	zion_packet_t *ptr;

	PRINT_FUNC;

	spin_lock_irqsave(&dev->lock, flag);
	for(;;){
		//read from TX queue 
		read  = dev->tx_q.header->read_ptr;
		write = dev->tx_q.header->write_ptr;

		PDEBUG("TX read=%d, write=%d", read, write);

		if(read == write){
			break;
		}
		ptr = dev->tx_q.buffer;
		ptr += read;
		memcpy(&packet, ptr, sizeof(zion_packet_t));

		next = read + 1;
		if(next >= dev->tx_q.depth){
			next = 0;
		}
		dev->tx_q.header->read_ptr = next;

		// write to RX queue
		write = dev->rx_q.header->write_ptr;
		read  = dev->rx_q.header->read_ptr;

		PDEBUG("RX read=%d, write=%d", read, write);

		ptr = dev->rx_q.buffer;
		ptr += write;
		memcpy(ptr, &packet, sizeof(zion_packet_t));

		next = write + 1;
		if(next >= dev->rx_q.depth){
			next = 0;
		}
		dev->rx_q.header->write_ptr = next;
	}
	spin_unlock_irqrestore(&dev->lock, flag);

	tasklet_schedule(&zcom_recv_tasklet);

}

DECLARE_TASKLET(loopback_tasklet, loopback_handler, 0);
#endif // ZCOM_LOOPBACK

zion_dev_t *zion_init(void)
{
	struct pci_dev *pci;
	zion_dev_t *dev = &zion_dev;
	u16 reg;

	PRINT_FUNC;

	spin_lock_init(&dev->lock);

	pci = pci_get_device(ZION_PCI_VENDOR_ID, ZION_PCI_DEVICE_ID, NULL);
	if(!pci){
		PERROR("pci_get_device() failed");
		return NULL;
	}

	dev->pci = pci;
	dev->regs_base = (u8 *)ioremap(
				pci_resource_start(pci,0),
				pci_resource_len(pci,0)
			);

	PINFO("zion regs base=%08x", (u32)dev->regs_base);

	dev->wram_base = (u8 *)ioremap(
				pci_resource_start(pci,1),
				pci_resource_len(pci,1)
			);
	dev->wram_size = (u32)pci_resource_len(pci, 1);

	PINFO("zion work ram base=%08x size=%04x", (u32)dev->wram_base, (u32)dev->wram_size);

//	dev->wram_base = base;
//	dev->wram_size = WRAM_SIZE;

	dev->zcom_tx_base = dev->wram_base + ZCOM_TX_OFFSET;
	dev->zcom_tx_size = ZCOM_TX_SIZE;

	dev->tx_q.header = (zion_queue_header_t*)dev->zcom_tx_base;
	dev->tx_q.buffer = (zion_packet_t *)(dev->zcom_tx_base + sizeof(zion_queue_header_t));
	dev->tx_q.depth  = (ZCOM_TX_SIZE - sizeof(zion_queue_header_t))/sizeof(zion_packet_t);

	dev->zcom_rx_base = dev->wram_base + ZCOM_RX_OFFSET;
	dev->zcom_rx_size = ZCOM_RX_SIZE;

	dev->rx_q.header = (zion_queue_header_t*)dev->zcom_rx_base;
	dev->rx_q.buffer = (zion_packet_t *)(dev->zcom_rx_base + sizeof(zion_queue_header_t));
	dev->rx_q.depth  = (ZCOM_RX_SIZE - sizeof(zion_queue_header_t))/sizeof(zion_packet_t);

	dev->zmem_base = dev->wram_base + ZMEM_OFFSET;
	dev->zmem_size = ZMEM_SIZE;

	// Set INTB Mask
	reg = mbus_read16(INTB_MSK_REG);
	reg |= PCIINT1;
	mbus_write16(reg, INTB_MSK_REG);
	reg = mbus_read16(INTB_MSK_REG);

	return dev;
}

int zion_exit(void)
{
	zion_dev_t *dev = &zion_dev;
	u16 reg;

	PRINT_FUNC;

	// Clear INTB Mask
	reg = mbus_read16(INTB_MSK_REG);
	reg &= ~PCIINT1;
	mbus_write16(reg, INTB_MSK_REG);
	mbus_write16(PCIINT1, INTB_CLR_REG);
	reg = mbus_read16(INTB_STS_REG);

	iounmap(dev->regs_base);
	iounmap(dev->wram_base);

	return 0;
}

int zion_write_packet(zion_dev_t *dev, zion_packet_t *packet)
{
	volatile u16 write, read;
	volatile u16 d16;
	volatile u32 d32;

	u16 next;
	unsigned long flag;
	zion_packet_t *ptr;

	PRINT_FUNC;

	PASSERT(dev == NULL);
	spin_lock_irqsave(&dev->lock, flag);

	d16 = dev->tx_q.header->read_ptr;
	SWAP16(&read,  &d16);

	d16 = dev->tx_q.header->write_ptr;
	SWAP16(&write, &d16);

//	PINFO("read=%d, write=%d", read, write);

	ptr = dev->tx_q.buffer;
	ptr += write;
	packet->tx_time = zcom_time();
	packet->flags |= ZION_OWNER_SH_FLAG;

	ptr->port  = packet->port;
	ptr->flags = packet->flags;

	SWAP16(&d16, &packet->id);
	ptr->id = d16;

	SWAP32(&d32, &packet->tx_time);
	ptr->tx_time = d32;

	memcpy(ptr->data, packet->data, ZCOM_PACKET_SIZE);

	next  = write + 1;
	if(next >= dev->tx_q.depth){
		next = 0;
	}
	if(read == next){
		PWARNING("TX queue warning");
	}
	SWAP16(&d16, &next);
	dev->tx_q.header->write_ptr=d16;

	spin_unlock_irqrestore(&dev->lock, flag);

#ifdef ZCOM_LOOPBACK
	tasklet_schedule(&loopback_tasklet);
#endif // ZCOM_LOOPBACK

	zion_irq_send(dev);

	return 0;
}

int zion_read_packet(zion_dev_t *dev, zion_packet_t *packet)
{
	volatile u16 read, write;
	u16 next;
	u16 d16;
	u32 d32;

	unsigned long flag;
	zion_packet_t *ptr;

	PRINT_FUNC;

	spin_lock_irqsave(&dev->lock, flag);

	d16 = dev->rx_q.header->read_ptr;
	SWAP16(&read,  &d16);

	d16 = dev->rx_q.header->write_ptr;
	SWAP16(&write, &d16);

	if(read == write){
		PDEBUG("packet empty");
		spin_unlock_irqrestore(&dev->lock, flag);
		return 0;
	}

	ptr = dev->rx_q.buffer;
	ptr += read;

	packet->port = ptr->port;
	packet->flags = ptr->flags;

	d16 = ptr->id;
	SWAP16(&packet->id, &d16);

	d32 = ptr->tx_time;
	SWAP32(&packet->tx_time, &d32);

	memcpy(packet->data, ptr->data, ZCOM_PACKET_SIZE);

	next = read + 1;
	if(next >= dev->rx_q.depth){
		next = 0;
	}
	SWAP16(&d16, &next);
	dev->rx_q.header->read_ptr = d16;

	spin_unlock_irqrestore(&dev->lock, flag);

	return sizeof(zion_packet_t);
}

int zion_irq_clear(zion_dev_t *dev)
{
	u16 v;
	PRINT_FUNC;
	mbus_write16(PCIINT1, INTB_CLR_REG);
	v = mbus_read16(INTB_CLR_REG);

	return 0;
}

int zion_irq_enable(zion_dev_t *dev)
{
	PRINT_FUNC;

	return 0;
}

int zion_irq_disable(zion_dev_t *dev)
{
	PRINT_FUNC;

	return 0;
}

int zion_irq_send(zion_dev_t *dev)
{
	volatile u16 v;

	PRINT_FUNC;

	mbus_write16(UPINT1, INTB_GEN_REG);
	v = mbus_read16(INTA_STS_REG);

	return 0;
}

int zion_is_irq(zion_dev_t *dev)
{
	volatile u16 v;

//	PRINT_FUNC;

	v = mbus_read16(INTB_STS_REG);
	return ((v & PCIINT1) == PCIINT1);
}
