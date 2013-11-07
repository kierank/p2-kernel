/* $Id: sdc.h 13969 2011-04-19 03:45:56Z Noguchi Isao $ */

#ifndef _SCSI_DISK_CHAR_H
#define _SCSI_DISK_CHAR_H

#ifdef __KERNEL__

/* The major number of SCSI character driver    */
#define SDC_MAJOR   127
/* USB/SATA device list, jusy for SCSI disk charactoer driver   */
#define SDC_MAX_DEVICE_NUM  32

/* USB/SATA Insertion & Removal detection		*/
#define SDC_PLUG_NO_EVENT		0
#define SDC_PLUG_EVENT_USB_IN	1
#define SDC_PLUG_EVENT_USB_OUT	2
#define SDC_PLUG_EVENT_SATA_IN	3
#define SDC_PLUG_EVENT_SATA_OUT	4

/* Maximum transfer size in sector                              */
/* Max transfer size per SCSI command 
    = sector_size(512) * MAX_TRANSFER_LIMITS(240) = 122880 bytes*/
/* --> Refer ./Documentation/block/biodoc.txt                   */
#define MAX_TRANSFER_LIMITS 200

#else   /* ! __KERNEL__ */

/* 2011/2/1, added by Panasonic (SAV) */
#ifndef __user
#define __user
#endif  /* __user */

#endif  /* __KERNEL__ */

#define SDC_STRING_LEN  32

struct sdc_dev_info {
    int host_no;
    int max_lun;
    unsigned short  vendor_id;
    unsigned short  product_id;
    char            vendor[SDC_STRING_LEN];
    char            product[SDC_STRING_LEN];
    char            serial[SDC_STRING_LEN];
    unsigned char   subclass;
    int t_bus;
    int t_level;
    int t_port;
    int t_devno;
};

#ifdef __KERNEL__

struct sdc_topology_info {
    int level;
    int port;
};

struct sdc_usbp_info {
	int topology_bus;
	int	topology_level;
	int topology_port;
	int topology_devnum;
	unsigned char	usb_max_lun;
	unsigned char	subclass;
	unsigned short	usb_vendor_id;
	unsigned short	usb_product_id;
};

extern int get_usb_device_info( struct Scsi_Host *host, struct sdc_usbp_info *usbp_info );
int sdc_func_queue_direct_transfer( struct file *filp, struct gendisk *disk, struct scsi_device *sdp , void __user *arg);

#endif	/* __KERNEL__	*/

struct sdc_ioc_dev_info {
    int no;
    int flag;
    struct sdc_dev_info dev_info;
};

struct sdc_ioc_status {
    int sdc_num;    /* Number of SCSI disk(s) which the system currently attached   */
    int sdc_total_num;   /* Total number of SCSI disk(s) which was attached by the system    */
};

/* Modified by Panasonic (SAV), 2011/01/31 ---> */

struct sdcdev_ioc_req_direct_transfer {
    int dir;                    /* 0: device to memory(READ), non-0: memory to device(WRITE) */
    unsigned long address;      /* start address on bus memory */
    unsigned int len;           /* byte size (equal to sectors * sector_size))*/
    unsigned long start;        /* start sector address on device */
    unsigned short sectors;     /* count in sector */
    int nonblock;               /* if nonblock!=0 then NON-BLOCKING*/
};

struct sdcdev_ioc_conf_direct_transfer{
	int status;                  /* [o] result of SCSI exexution	*/
	unsigned char __user *sense; /* [o] sense data */
	unsigned int senselen;       /* [i/o]  */
	int nonblock;                /* [i] if nonblock!=0 then NON-BLOCKING*/
};

/* <--- Modified by Panasonic (SAV), 2011/01/31 */

struct sdcdev_ioc_debug_direct_transfer {
	unsigned long	addr;
	char *buff;
};

#define DIRECT_TRANS_IDLE   0
#define DIRECT_TRANS_BUSY   1
#define DIRECT_TRANS_DONE   2
#define DIRECT_TRANS_ERROR  3

#define SDC_IOCTL_MAGIC 0xF7
#define SDCDEV_IOCTL_MAGIC 0xF8

#define SDC_IOCTL_USB_FIND_SDDEV _IOW(SDC_IOCTL_MAGIC, 0, unsigned int [2])
#define SDC_IOCTL_USB_STATUS _IOR(SDC_IOCTL_MAGIC, 1, struct sdc_ioc_status)
#define SDC_IOCTL_ATA_STATUS _IOR(SDC_IOCTL_MAGIC, 2, struct sdc_ioc_status)
#define SDC_IOCTL_USB_DEV_INFO _IOWR(SDC_IOCTL_MAGIC, 3, struct sdc_ioc_dev_info)
#define SDC_IOCTL_ATA_DEV_INFO _IOWR(SDC_IOCTL_MAGIC, 4, struct sdc_ioc_dev_info)
#define SDC_IOCTL_USB_FIND_DEV_CONNECTED _IOWR(SDC_IOCTL_MAGIC, 5, struct sdc_ioc_dev_info)
#define SDC_IOCTL_ATA_FIND_DEV_CONNECTED _IOWR(SDC_IOCTL_MAGIC, 6, struct sdc_ioc_dev_info)
#define SDC_IOCTL_USB_CHECK_HOTPLUG_EVENT _IO(SDC_IOCTL_MAGIC, 7)
#define SDC_IOCTL_ATA_CHECK_HOTPLUG_EVENT _IO(SDC_IOCTL_MAGIC, 8)

#define SDCDEV_IOCTL_REQ_DIRECT_TRANSFER    _IOR(SDCDEV_IOCTL_MAGIC, 0, struct sdcdev_ioc_req_direct_transfer)
#define SDCDEV_IOCTL_CONF_DIRECT_TRANSFER   _IOWR(SDCDEV_IOCTL_MAGIC, 1, struct sdcdev_ioc_conf_direct_transfer)
#define SDCDEV_IOCTL_CHK_DIRECT_TRANSFER    _IO(SDCDEV_IOCTL_MAGIC, 2)
/* 2011/4/19, added by Panasonic(SAV) ---> */
#define SDCDEV_IOCTL_SET_TMOUT              _IO(SDCDEV_IOCTL_MAGIC, 3)
#define SDCDEV_IOCTL_GET_TMOUT              _IOR(SDCDEV_IOCTL_MAGIC, 4, unsigned int)
/* <--- 2011/4/19, added by Panasonic(SAV) */

#endif /* _SCSI_DISK_CHAR_H */
