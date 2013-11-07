/* $Id: scsi_ioctl.h 13963 2011-04-19 02:42:59Z Noguchi Isao $ */

#ifndef _SCSI_IOCTL_H
#define _SCSI_IOCTL_H 

#define SCSI_IOCTL_SEND_COMMAND 1
#define SCSI_IOCTL_TEST_UNIT_READY 2
#define SCSI_IOCTL_BENCHMARK_COMMAND 3
#define SCSI_IOCTL_SYNC 4			/* Request synchronous parameters */
#define SCSI_IOCTL_START_UNIT 5
#define SCSI_IOCTL_STOP_UNIT 6
/* The door lock/unlock constants are compatible with Sun constants for
   the cdrom */
#define SCSI_IOCTL_DOORLOCK 0x5380		/* lock the eject mechanism */
#define SCSI_IOCTL_DOORUNLOCK 0x5381		/* unlock the mechanism	  */

/* 2010/3/23, added by Panasonic >>>> */
#define SCSI_IOCTL_GET_HOSTTYPE 16 /* for identify host type (low-level device type) */
#define SCSI_HOSTTYPE_USB       0x01
#define SCSI_HOSTTYPE_SATA      0x02
/* <<<< 2010/3/23, added by Panasonic */

/* 2010/4/8/13, added by Panasonic >>>> */
#define SCSI_IOCTL_CHK_BLK_ERR  17
#define SCSI_IOCTL_CLR_BLK_ERR  18

struct scsi_ioctl_blk_err {
    unsigned int result;
    int sense_valid;
    int sense_deferred; 
    unsigned char response_code;        /* permit: 0x0, 0x70, 0x71, 0x72, 0x73 */
    unsigned char sense_key;
    unsigned char asc;
    unsigned char ascq; 
};

/* <<<< 2010/4/8/13, added by Panasonic */

#define SCSI_IOCTL_GET_DEVINFO  19

struct scsi_ioctl_usb_info {
/*     int stor_no; */
/*     int host_no; */
    unsigned char max_lun;
    unsigned short vendor_id;
    unsigned short product_id;
#define SCSI_IOCTL_USB_STRING_LEN 32
    char    vendor[SCSI_IOCTL_USB_STRING_LEN];
    char    product[SCSI_IOCTL_USB_STRING_LEN];
    char    serial[SCSI_IOCTL_USB_STRING_LEN];
    unsigned char subclass;
    unsigned char protocol;
/*     int    connect; */
/*     unsigned int    nr_count; */
    int t_bus;
    int	t_level;
    int	t_port;
    int t_rootport;
    unsigned int t_route;
    int	t_devno;
};

struct scsi_ioctl_ata_info {
    int print_id;               /* port number = 1,2,3, ... (base 1) */
    int devno;                  /* device number = 0, 1, 2, ... (base 0) */
};


union scsi_ioctl_devinfo {
    struct scsi_ioctl_usb_info u;
    struct scsi_ioctl_ata_info a;
};

/* 2010/10/6, added by Panasonic */
#define SCSI_IOCTL_SET_MAX_SECTORS  20
#define SCSI_IOCTL_GET_MAX_SECTORS  21


/* 2011/4/11, added by Panasonic (PAVBU),
   20011/4/19, modified by Panasonic (PAVBU) ---> */
#define SCSI_IOCTL_SET_US_FFLAGS    22
#define SCSI_IOCTL_CLR_US_FFLAGS    23
#define SCSI_IOCTL_GET_US_FFLAGS    24
#define SCSI_IOCTL_FFLAG_FAST_RECOVERY      (1<<22)
#define SCSI_IOCTL_FFLAG_FORCE_BULK_RESET   (1<<23)
/* <--- 2011/4/11, added by Panasonic (PAVBU),
   20011/4/19, modified by Panasonic (PAVBU) */


#define	SCSI_REMOVAL_PREVENT	1
#define	SCSI_REMOVAL_ALLOW	0

#ifdef __KERNEL__

struct scsi_device;

/*
 * Structures used for scsi_ioctl et al.
 */

typedef struct scsi_ioctl_command {
	unsigned int inlen;
	unsigned int outlen;
	unsigned char data[0];
} Scsi_Ioctl_Command;

typedef struct scsi_idlun {
	__u32 dev_id;
	__u32 host_unique_id;
} Scsi_Idlun;

/* Fibre Channel WWN, port_id struct */
typedef struct scsi_fctargaddress {
	__u32 host_port_id;
	unsigned char host_wwn[8]; // include NULL term.
} Scsi_FCTargAddress;

extern int scsi_ioctl(struct scsi_device *, int, void __user *);
extern int scsi_nonblockable_ioctl(struct scsi_device *sdev, int cmd,
				   void __user *arg, struct file *filp);

/* Panasonic Original */
extern int ioctl_external_command(struct scsi_device *sdev, char *cmd,
		void *buffer, unsigned bufflen, int timeout, int retries);
/*--------------------*/

#endif /* __KERNEL__ */
#endif /* _SCSI_IOCTL_H */
