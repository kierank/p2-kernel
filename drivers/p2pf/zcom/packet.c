/* $Id: packet.c 13117 2011-03-10 04:38:15Z Noguchi Isao $ */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>

#include <linux/ringbuff.h>

#include "zcom.h"

struct zcom_free_list {
    struct list_head list_head;
    spinlock_t lock;
    unsigned int used_count;
    unsigned int max_used_count;
    u16 packet_id;
};
static struct zcom_free_list rx_free_list;
static struct zcom_free_list tx_free_list;

static spinlock_t log_lock;
static struct ringbuff *log_buff=NULL;
static int log_no=0;

static int __free_list_init(struct zcom_free_list *free_list, unsigned int nr_list)
{
    int retval=0;
	unsigned int i;
	zcom_packet_entry_t *entry;

	PRINT_FUNC;

    if (!free_list) {
        PERROR("invalid parameters");
        retval= -EINVAL;
        goto failed;
    }

	INIT_LIST_HEAD(&free_list->list_head);
	spin_lock_init(&free_list->lock);

	free_list->used_count = 0;
	free_list->max_used_count = 0;
	free_list->packet_id = 0;

	for(i = 0; i < nr_list; i++){
		entry = (zcom_packet_entry_t *)kmalloc(sizeof(zcom_packet_entry_t), GFP_KERNEL);
		if(!entry){
			PERROR("kmalloc() failed");
            retval = -ENOMEM;
			goto failed;
		}
		list_add_tail(&entry->list, &free_list->list_head);
	}

failed:

    if (retval<0) {
        while(!list_empty(&free_list->list_head)){
            entry = list_entry(free_list->list_head.next, zcom_packet_entry_t, list);
            list_del(&entry->list);
            kfree(entry);
        }
    }

	return retval;
}

static int __free_list_cleanup(struct zcom_free_list *free_list)
{
    int retval = 0;
	struct list_head *head = &free_list->list_head;
	unsigned long flag;

	PRINT_FUNC;

	spin_lock_irqsave(&free_list->lock, flag);

	while(!list_empty(head)){
		zcom_packet_entry_t *entry = list_entry(head->next, zcom_packet_entry_t, list);

		list_del(&entry->list);
		kfree(entry);

/* 		spin_unlock_irqrestore(&free_list->lock, flag); */
/* 		spin_lock_irqsave(&free_list->lock, flag); */
	}

	spin_unlock_irqrestore(&free_list->lock, flag);

	return retval;
}



int packet_buffer_init(void)
{
    int retval=0;

	PRINT_FUNC;

    retval=__free_list_init(&rx_free_list,ZCOM_N_RX_PACKET_BUFFER);
    if(retval)
        goto fail_rx;

    retval=__free_list_init(&tx_free_list,ZCOM_N_TX_PACKET_BUFFER);
    if(retval)
        goto fail_tx;


	spin_lock_init(&log_lock);
	log_no = 0;
    log_buff = ringbuff_create(ZCOM_N_PACKET_LOG, sizeof(zcom_packet_entry_t));
    if(IS_ERR(log_buff)){
        retval = PTR_ERR(log_buff);
        PERROR("ringbuff_create() is failed: %d", retval);
        goto fail_log;
    }

 fail_log:
    if(retval<0)
        __free_list_cleanup(&tx_free_list);
 fail_tx:
    if(retval<0)
        __free_list_cleanup(&rx_free_list);
 fail_rx:
	return retval;
}


int packet_buffer_exit(void)
{
	unsigned long flag;

	PRINT_FUNC;

	PNOTICE("TX: packet buffer max use count=%d", tx_free_list.max_used_count);
    __free_list_cleanup(&tx_free_list);

	PNOTICE("EX: packet buffer max use count=%d", rx_free_list.max_used_count);
    __free_list_cleanup(&rx_free_list);

	spin_lock_irqsave(&log_lock, flag);
    ringbuff_destroy(log_buff);
	spin_unlock_irqrestore(&log_lock, flag);

	return 0;
}


static zcom_packet_entry_t *__alloc_packet_entry(struct zcom_free_list *free_list)
{
    struct list_head *head = &free_list->list_head;
	zcom_packet_entry_t *entry = NULL;
	unsigned long flag;

	PRINT_FUNC;

	spin_lock_irqsave(&free_list->lock, flag);

	if(list_empty(head))
        goto exit;

	entry = list_entry(head->next, zcom_packet_entry_t, list);
	list_del(&entry->list);

	free_list->used_count++;

	free_list->max_used_count = max(free_list->used_count, free_list->max_used_count);

 exit:

	spin_unlock_irqrestore(&free_list->lock, flag);

    if(entry) {
        memset(entry, 0, sizeof(zcom_packet_entry_t));
        entry->packet.id = free_list->packet_id++;
    }

	return entry;
}

zcom_packet_entry_t *alloc_tx_packet_entry(void)
{
    return __alloc_packet_entry(&tx_free_list);
}

zcom_packet_entry_t *alloc_rx_packet_entry(void)
{
    return __alloc_packet_entry(&rx_free_list);
}

static int __free_packet_entry(struct zcom_free_list *free_list, zcom_packet_entry_t *entry)
{
    int retval=0;
    struct list_head *head = &free_list->list_head;
	unsigned long flag;

	PRINT_FUNC;

	if(unlikely(!free_list||!entry)){
		PALERT("null pointer");
		retval = -EFAULT;
        goto fail;
	}

	spin_lock_irqsave(&free_list->lock, flag);

	free_list->used_count--;
    list_add(&entry->list, head);

	spin_unlock_irqrestore(&free_list->lock, flag);

 fail:

	return retval;
}

int free_tx_packet_entry(zcom_packet_entry_t *entry)
{
    return __free_packet_entry(&tx_free_list, entry);
}

int free_rx_packet_entry(zcom_packet_entry_t *entry)
{
    return __free_packet_entry(&rx_free_list, entry);
}


void add_packet_log(zcom_packet_entry_t *entry)
{
    int retval;
	unsigned long flag;

	PRINT_FUNC;

    if (!entry) {
        PERROR("null pointer");
        return;
    }

    spin_lock_irqsave(&log_lock, flag);

	entry->log_no = log_no++;

    retval=ringbuff_put(log_buff, entry);
    if ( unlikely(retval<0) )
        PERROR("ringbuff_put() is failed: %d", retval);
    
    spin_unlock_irqrestore(&log_lock, flag);

}

struct dump_log {
    zcom_packet_entry_t *entry;
    u8 *buf;
    int count;
    int len;
};

static int __dump_log(struct ringbuff * rb,  struct ringbuff_data *data)
{
    zcom_packet_entry_t *entry = (zcom_packet_entry_t *)data->entry;
    struct dump_log *dl = (struct dump_log*)data->private;
    u8 *buf = dl->buf;
    int count = dl->count;
    int len = dl->len;

    int retval=0;
	int i;
	int port;
	int dir;

    port = entry->packet.port;
    PDEBUG("port=%d", port);

    dir = ((entry->flags & ZCOM_SND_FLAG) ? 'T' :'R');

    len += snprintf(buf + len, count - len,
                    "%c%d:No.%d\n", dir, port, entry->log_no);

    len += snprintf(buf + len, count - len,
                    "%c%d:%c%c...%c%c%c\n", dir, port,
                    ((entry->flags & ZCOM_SND_FLAG) ? 'T' : '.'),
                    ((entry->flags & ZCOM_RCV_FLAG) ? 'R' : '.'),
                    ((entry->flags & ZCOM_PND_FLAG) ? 'P' : '.'),
                    ((entry->flags & ZCOM_DRP_FLAG) ? 'D' : '.'),
                    ((entry->flags & ZCOM_ERR_FLAG) ? 'E' : '.'));

    len += snprintf(buf + len, count - len,
                    "%c%d:id 0x%04x  flags 0x%02x\n",
                    dir, port,
                    entry->packet.id,
                    entry->packet.flags);
    
    len += snprintf(buf + len, count - len,
                    "%c%d:timestamp(send/recv/read) %08x/%08x/%08x\n",
                    dir, port,
                    entry->packet.tx_time,
                    entry->rx_time,
                    entry->rd_time);

    for(i = 0; i < ZCOM_PACKET_SIZE / 8; i++){
        len += snprintf(buf + len, count - len,
                        "%c%d:%02x %02x %02x %02x-%02x %02x %02x %02x\n",
                        dir, port,
                        entry->packet.data[i*8+0],
                        entry->packet.data[i*8+1],
                        entry->packet.data[i*8+2],
                        entry->packet.data[i*8+3],
                        entry->packet.data[i*8+4],
                        entry->packet.data[i*8+5],
                        entry->packet.data[i*8+6],
                        entry->packet.data[i*8+7]);
    }

    len += snprintf(buf + len, count - len, "\n");		      

    dl->len = len;

    return retval;
}

int dump_packet_log(u8 *buf, int count)
{
    int retval=0;
	unsigned long flag;
    struct dump_log dl = {
        .buf = buf,
        .count = count,
        .len = 0,
    };

	PRINT_FUNC;

	spin_lock_irqsave(&log_lock, flag);

    retval=ringbuff_foreach(log_buff, __dump_log, &dl, 1);
    if(retval<0){
        PERROR("ringbuff_foreach() is failed: %d", retval);
        spin_unlock_irqrestore(&log_lock, flag);
        return 0;
    }

	spin_unlock_irqrestore(&log_lock, flag);

	return dl.len;
}
