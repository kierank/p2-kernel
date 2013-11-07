#ifndef __PROXYDRV_H__
#define __PROXYDRV_H__

#define	PROXY_REVISION	"2.0"

#define PROXY_DEVICE_NAME	"proxy"
#define PROXY_MAX_DEV		6
#define PROXY_MAJOR		126

//
#define PROXY_FIRST_DETECT	(1 << 0)
#define PROXY_RICOH_ZV_ENA	(1 << 1)
#define PROXY_JUST_INSERTED	(1 << 2)
#define PROXY_JUST_REMOVED	(1 << 3)

typedef struct proxy_dev_t{
	struct pcmcia_device *link;
	unsigned char intr_status;
	wait_queue_head_t intr_wq;

spinlock_t dev_lock;

	int dev_status;
	u_char *common_base;
}proxy_dev_t;

typedef struct proxy_info_t{
	struct pcmcia_device *link;
	dev_node_t node;
	proxy_dev_t *dev;
} proxy_info_t;

#endif // __PROXYDRV_H__
