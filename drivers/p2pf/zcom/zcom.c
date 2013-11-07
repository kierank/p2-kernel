/* $Id: zcom.c 13117 2011-03-10 04:38:15Z Noguchi Isao $ */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>

#include "zcom.h"

#define ZCOM_TIMEOUT	(HZ*3)

static zcom_dev_t zcom_dev[ZCOM_N_DEV];
static zion_dev_t *zion;

static int dump_size = 0;
static u8 dump_buffer[ZCOM_PACKET_DUMP_SIZE];

static int zcom_major = ZCOM_MAJOR;
static struct proc_dir_entry *dir_entry = NULL;

static char *status_msg[] = {"close", "open", "disabled"};

static u32 zcom_time_base;

int zcom_time_reset()
{
	zcom_time_base = jiffies;
	return 0;
}

u32 zcom_time()
{
	return (jiffies - zcom_time_base);
}

static int clear_recv_queue(zcom_dev_t *dev)
{
	unsigned long flag;
	zcom_packet_entry_t *entry;

	spin_lock_irqsave(&dev->lock, flag);

	while(!list_empty(&dev->rx_q)){
		entry = list_entry(dev->rx_q.next, zcom_packet_entry_t, list);

		list_del(&entry->list);
		dev->rx_count--;

		spin_unlock_irqrestore(&dev->lock, flag);

		entry->flags |= ZCOM_DRP_FLAG;

		add_packet_log(entry);
        /* 2011/3/9, Modified by Panasonic (SAV) */
/* 		free_packet_entry(entry); */
		free_rx_packet_entry(entry);

		spin_lock_irqsave(&dev->lock, flag);
	}

	if(dev->rx_count != 0){
		PALERT("rx count mismatch");
		dev->rx_count = 0;
	}

	spin_unlock_irqrestore(&dev->lock, flag);

	return 0;
}

static int zcom_open(struct inode *inode, struct file *file)
{
	int port;
	zcom_dev_t *dev;
	unsigned long flag;

	PRINT_FUNC;

	port = MINOR(inode->i_rdev);
	if(port >= ZCOM_N_DEV){
		PERROR("invalid port(%d)", port);
		return -ENODEV;
	}

	dev = &zcom_dev[port];

	spin_lock_irqsave(&dev->lock, flag);

	if(dev->status == ZCOM_OPEN){
		spin_unlock_irqrestore(&dev->lock, flag);
		PERROR("zcom%d is opened already", port);
		return -EBUSY;
	}

	if(dev->status == ZCOM_DISABLED){
		spin_unlock_irqrestore(&dev->lock, flag);
		PERROR("zcom%d is not initialize", port);
		return -ENODEV;
	}

	dev->status = ZCOM_OPEN;

	spin_unlock_irqrestore(&dev->lock, flag);

	clear_recv_queue(dev);

	file->private_data = (void *)dev;

	return 0;
}

static int zcom_release(struct inode *inode, struct file *file)
{
	int port;
	zcom_dev_t *dev;
	unsigned long flag;

	PRINT_FUNC;

	port = MINOR(inode->i_rdev);
	if(port >= ZCOM_N_DEV){
		PERROR("invalid port id(%d)", port);
		return -ENODEV;
	}

	dev = &zcom_dev[port];

	spin_lock_irqsave(&dev->lock, flag);

	if(dev->status != ZCOM_OPEN){
		dev->status = ZCOM_DISABLED;
		spin_unlock_irqrestore(&dev->lock, flag);
		return -ERESTARTSYS;
	}

	dev->status = ZCOM_CLOSE;

	spin_unlock_irqrestore(&dev->lock, flag);

	clear_recv_queue(dev);

	return 0;
}

static unsigned int zcom_poll(struct file *file, struct poll_table_struct *poll_table)
{
	zcom_dev_t *dev = (zcom_dev_t *)file->private_data;
	unsigned int mask = 0;

	PRINT_FUNC;

	PASSERT(dev == NULL);

	poll_wait(file, &dev->rx_wq, poll_table);

	if(!list_empty(&dev->rx_q)){
		mask |= (POLLIN|POLLRDNORM);
	}

	mask |= (POLLOUT|POLLWRNORM);

	return mask;
}

static ssize_t zcom_write(struct file *file, const char *buffer, size_t count, loff_t *offset)
{
	int ret;
	zcom_packet_entry_t *entry;
	int i;
	int num;
	zcom_dev_t *dev;
	const char *ptr;

	PRINT_FUNC;

	dev = (zcom_dev_t *)file->private_data;
	ptr = buffer;
	num = count/ZCOM_PACKET_SIZE;

	for(i = 0; i < num; i++){
        /* 2011/3/9, Modified by Panasonic (SAV) */
/* 		entry = alloc_packet_entry(); */
		entry = alloc_tx_packet_entry();
		if(!entry){
			PERROR("alloc_packet_entry() failed");
			ZCOM_ATOMIC_INC(dev->log.err);
			return -ENOMEM;
		}

		entry->packet.port = dev->id;
		entry->packet.flags = 0;
		entry->flags = ZCOM_SND_FLAG;

		memcpy(entry->packet.data, ptr, ZCOM_PACKET_SIZE);

		ptr += ZCOM_PACKET_SIZE;

		ret = zion_write_packet(dev->zion, &entry->packet);
		if(ret == -EAGAIN){
			PWARNING("zion send queue full");
			entry->flags |= ZCOM_DRP_FLAG;

			ZCOM_ATOMIC_INC(dev->log.drp);

			add_packet_log(entry);
        /* 2011/3/9, Modified by Panasonic (SAV) */
/* 			free_packet_entry(entry); */
			free_tx_packet_entry(entry);

			return i*ZCOM_PACKET_SIZE;
		}
		else if (ret < 0){
			PERROR("zion_write_packet() error(%d)", ret);
			entry->flags |= ZCOM_ERR_FLAG;

			ZCOM_ATOMIC_INC(dev->log.err);

			add_packet_log(entry);
        /* 2011/3/9, Modified by Panasonic (SAV) */
/* 			free_packet_entry(entry); */
			free_tx_packet_entry(entry);

			return ret;
		}

		ZCOM_ATOMIC_INC(dev->log.snd);

		add_packet_log(entry);
        /* 2011/3/9, Modified by Panasonic (SAV) */
/* 		free_packet_entry(entry); */
		free_tx_packet_entry(entry);
	}

	return ZCOM_PACKET_SIZE*i;
}

static ssize_t zcom_read(struct file *file, char *buffer, size_t count, loff_t *offset)
{
	unsigned long flag;
	zcom_packet_entry_t *entry;
	zcom_dev_t *dev;
	int i, num;
	char *ptr;
	unsigned long rx_delay, rd_delay, delay;

	PRINT_FUNC;

	dev = (zcom_dev_t *)file->private_data;

	if(count < ZCOM_PACKET_SIZE){
		return -EINVAL;
	}

	if(list_empty(&dev->rx_q)){
/*
		if((file->f_flags&O_NONBLOCK) == 0){
			return -EAGAIN;
		}
		else {
			wait_event_interruptible(dev->rx_wq, !list_empty(&dev->rx_q));
		}
*/

		wait_event_interruptible(dev->rx_wq, !list_empty(&dev->rx_q));
	}

	if(list_empty(&dev->rx_q)){
		PWARNING("rx queue interrupt signal");
		return -EINTR;
	}

	num = count/ZCOM_PACKET_SIZE;
	ptr = buffer;
	i   = 0;
	while(i < num && !list_empty(&dev->rx_q)){
		spin_lock_irqsave(&dev->lock, flag);

		entry = list_entry(dev->rx_q.next, zcom_packet_entry_t, list);
		list_del(&entry->list);

		dev->rx_count--;

		spin_unlock_irqrestore(&dev->lock, flag);

		entry->rd_time = zcom_time();
		entry->flags &= ~ZCOM_PND_FLAG;

		memcpy(ptr, entry->packet.data, ZCOM_PACKET_SIZE);

		delay = entry->rd_time - entry->packet.tx_time;
		rx_delay    = entry->rx_time - entry->packet.tx_time;
		rd_delay    = entry->rd_time - entry->rx_time;

		PDEBUG("rx_delay=%lu, rd_delay=%lu, delay=%lu", rx_delay, rd_delay, delay);

		dev->log.rx_delay_max = max(dev->log.rx_delay_max, rx_delay);
		dev->log.rd_delay_max = max(dev->log.rd_delay_max, rd_delay);
		dev->log.delay_max    = max(dev->log.delay_max,    delay);

		dev->log.rx_delay = (dev->log.rx_delay + rx_delay)/2;
		dev->log.rd_delay = (dev->log.rd_delay + rd_delay)/2;
		dev->log.delay    = (dev->log.delay    + delay)/2;

		ZCOM_ATOMIC_INC(dev->log.rd);

		add_packet_log(entry);
        /* 2011/3/9, Modified by Panasonic (SAV) */
/* 		free_packet_entry(entry); */
		free_rx_packet_entry(entry);

		i++;
		ptr+=ZCOM_PACKET_SIZE;
	}
	return ZCOM_PACKET_SIZE*i;
}

static int zcom_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg)
{
	PRINT_FUNC;
	return 0;
}

static struct file_operations fops = {
	.open		= zcom_open,
	.release	= zcom_release,
	.read		= zcom_read,
	.write		= zcom_write,
	.poll		= zcom_poll,
	.ioctl		= zcom_ioctl
};

void zcom_recv_handler(unsigned long arg)
{
	int ret;
	unsigned long flag;
	zcom_packet_entry_t *entry;
	zcom_dev_t *dev = NULL;
	int port;

	PRINT_FUNC;
	PASSERT(zion_recv_empty(zion));

	while(!zion_recv_empty(zion)){
        /* 2011/3/9, Modified by Panasonic (SAV) */
/* 		entry = alloc_packet_entry(); */
		entry = alloc_rx_packet_entry();
		if(entry == NULL){
			PALERT("alloc_packet_entry() failed");
			return;
		}
		ret = zion_read_packet(zion, &entry->packet);
		entry->rx_time = zcom_time();
		entry->flags = ZCOM_RCV_FLAG;

		if (ret < 0){
			entry->flags |= ZCOM_ERR_FLAG;
			add_packet_log(entry);
        /* 2011/3/9, Modified by Panasonic (SAV) */
/* 			free_packet_entry(entry); */
			free_rx_packet_entry(entry);
			PERROR("zion_read_packet() failed(%d)", ret);
			return;
		}
		if (ret == 0){
        /* 2011/3/9, Modified by Panasonic (SAV) */
/* 			free_packet_entry(entry); */
			free_rx_packet_entry(entry);
			break;
		}

		port = entry->packet.port;
		dev = &zcom_dev[port];
		if(dev->status == ZCOM_OPEN){
			entry->flags |= ZCOM_PND_FLAG;
			spin_lock_irqsave(&dev->lock, flag);
			list_add_tail(&entry->list, &dev->rx_q);
			dev->rx_count++;
			dev->log.rcv++;
			spin_unlock_irqrestore(&dev->lock, flag);
			wake_up_interruptible(&dev->rx_wq);
		}
		else {
			entry->flags |= ZCOM_DRP_FLAG;
			add_packet_log(entry);
        /* 2011/3/9, Modified by Panasonic (SAV) */
/* 			free_packet_entry(entry); */
			free_rx_packet_entry(entry);
		}
	}
}

DECLARE_TASKLET(zcom_recv_tasklet, zcom_recv_handler, 0);

irqreturn_t zcom_event(int irq, void *arg)
{
	if(zion_is_irq(zion)){
		tasklet_schedule(&zcom_recv_tasklet);
		zion_irq_clear(zion);
	}
	return IRQ_HANDLED;
}

static int read_procfs_dump(char *buf, char **start, off_t offset, int count, int *eof, void *data)
{
	int len = 0;

	PRINT_FUNC;

	PDEBUG("offset=%ud, count=%d", (unsigned int)offset, count);

	if(offset == 0){
		dump_size = dump_packet_log(dump_buffer, ZCOM_PACKET_DUMP_SIZE);
		PDEBUG("dump_size = %d", dump_size);
	}

	if(offset >= dump_size){
		*eof = 1;
		return 0;
	}

	len = dump_size - offset;
	PDEBUG("len=%d", len);
	len = ((len < count) ? len : count);

	*start = buf;
	memcpy(buf, dump_buffer + offset, len);

	PDEBUG("len=%d", len);

	return len;
}

static int read_procfs_port(char *buf, char **start, off_t offset, int count, int *eof, void *data)
{
	int len = 0;
	zcom_dev_t *dev;

	PRINT_FUNC;

	dev = (zcom_dev_t *)data;

	len += snprintf(buf + len, count - len,
			"zcom%d:port status %s\n",
			dev->id,
			status_msg[dev->status]);

	len += snprintf(buf + len, count - len,
			"zcom%d:recv queue %d\n",
			dev->id,
			dev->rx_count);

	len += snprintf(buf + len, count - len,
			"zcom%d:packet count(send/recv/read/drop/error)"
			" %d/%d/%d/%d/%d\n",
			dev->id,
			dev->log.snd,
			dev->log.rcv,
			dev->log.rd,
			dev->log.drp,
			dev->log.err);

	len += snprintf(buf + len, count - len,
			"zcom%d:recv  delay time(max/ave) %lu/%lu\n",
			dev->id,
			dev->log.rx_delay_max,
			dev->log.rx_delay);

	len += snprintf(buf + len, count - len,
			"zcom%d:read  delay time(max/ave) %lu/%lu\n",
			dev->id,
			dev->log.rd_delay_max,
			dev->log.rd_delay);

	len += snprintf(buf + len, count - len,
			"zcom%d:total delay time(max/ave) %lu/%lu\n",
			dev->id,
			dev->log.delay_max,
			dev->log.delay);

	*eof = 1;

	return len;
}

static int zcom_procfs_init(void)
{
	int i;
	char name[8];
	zcom_dev_t *dev;

	PRINT_FUNC;

	dir_entry = proc_mkdir("driver/zcom", NULL);
	if(!dir_entry){
		PERROR("proc_mkdir() failed.");
		return -EFAULT;
	}

	create_proc_read_entry("dump", 0, dir_entry, read_procfs_dump, NULL);

	for(i = 0; i < ZCOM_N_DEV; i++){
		dev = &zcom_dev[i];

		snprintf(name, sizeof(name), "%d", i);
		create_proc_read_entry(name, 0, dir_entry, read_procfs_port, (void *)dev);
	}

	return 0;
}

static int zcom_procfs_exit(void)
{
	int i;
	char name[8];

	PRINT_FUNC;

	if(!dir_entry)
		return 0;

	for(i = 0; i < ZCOM_N_DEV; i++){
		snprintf(name, sizeof(name), "%d", i);
		remove_proc_entry(name, dir_entry);
	}

	remove_proc_entry("dump", dir_entry);
	remove_proc_entry("driver/zcom", NULL);

	return 0;
}

static int __init zcom_module_init(void)
{
	int i;
	int ret = 0;
	zcom_dev_t *dev;

	u32 tm, ts;
	volatile u32 sig;

	PRINT_FUNC;

	PNOTICE(ZCOM_VERSION);

	ret = packet_buffer_init();
	if(ret < 0){
		PERROR("packet_buffer_init() faild(%d)", ret);
		return ret;
	}

	zion = zion_init();
	if(!zion){
		PERROR("zion_init() failed");
		ret = -EIO;
		goto out_packet_buffer;
	}

	ret = request_irq(zion->pci->irq, zcom_event, IRQF_SHARED, ZCOM_DEV_NAME, zion);
	if(ret){
		PERROR("request_irq() failed.");
		ret = -EIO;
		goto out_zion;
	}

	printk("IRQ : %d\n", zion->pci->irq);

	ts = jiffies;
	tm = jiffies + ZCOM_TIMEOUT;

	while(tm > jiffies){
		sig = __le16_to_cpu(zion->rx_q.header->command);
		if(sig == ZCOM_CMD_INIT)
			break;
	}

	PINFO("negotiation ticks=%lu", jiffies - ts);
	if(sig != ZCOM_CMD_INIT){
		PERROR("negotiation timeout(signature=%04x)", sig);
		ret = -EIO;
		goto out_free_irq;
	}

	for(i = 0; i < ZCOM_N_DEV; i++){
		dev = &zcom_dev[i];
		memset(dev, 0, sizeof(zcom_dev_t));

		spin_lock_init(&dev->lock);
		dev->id = i;
//		dev->status = ZCOM_DISABLED;
		dev->status = ZCOM_CLOSE;

		INIT_LIST_HEAD(&dev->rx_q);
		init_waitqueue_head(&dev->rx_wq);
		dev->zion = zion;
	}

	ret = register_chrdev(zcom_major, ZCOM_DEV_NAME, &fops);
	if(ret < 0){
		PERROR("register_chrdev() failed(%d)", ret);
		goto out_free_irq;
	}

	if(zcom_major == 0){
		zcom_major = ret;
	}

	zcom_procfs_init();

	ret = zmem_init(zion);
	if(ret < 0){
		PERROR("zmem_init() failed(%d)", ret);
		goto out_unregister;
	}
	zion->rx_q.header->status = __cpu_to_le16(ZCOM_ACK_INIT);
//	zcom_timer_reset();
	zion_irq_send(zion);
	zion_irq_enable(zion);

	return 0;

out_unregister:
	unregister_chrdev(zcom_major, ZCOM_DEV_NAME);
	zcom_procfs_exit();

out_free_irq:
	free_irq(zion->pci->irq, zion);

out_zion:
	zion_exit();

out_packet_buffer:
	packet_buffer_exit();

	return ret;
}

static void __exit zcom_module_exit(void)
{
	PRINT_FUNC;

	free_irq(zion->pci->irq, zion);

	unregister_chrdev(zcom_major, ZCOM_DEV_NAME);

	zmem_exit();
	zcom_procfs_exit();
	zion_exit();
	packet_buffer_exit();
}

module_init(zcom_module_init);
module_exit(zcom_module_exit);
