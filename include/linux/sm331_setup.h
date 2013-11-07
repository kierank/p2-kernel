#ifndef __SM331_SETUP_H__
#define __SM331_SETUP_H__

#define SM331_TRANS_TIMEOUT	4	/* ref) transport.h */
#define SM331_SDOP_FLAG		0xFF
#define SM331_SDOP_FSERR	0xFE

#define SM331_NO_ERROR		0
#define SM331_MEDIA_CHANGED	1
#define SM331_NO_MEDIA		2
#define SM331_OTHER_MEDIA	3
#define SM331_OTHER_ERROR	4

#ifdef __KERNEL__
typedef int (*alloc_card_info_t)(
	void *, void **, struct usb_device *, struct Scsi_Host *, struct mutex *,
	unsigned long *, int, int, unsigned int);
typedef void (*free_card_info_t)(void *, int);
typedef int (*read_transport_t)(struct scsi_cmnd *, void *, int);
typedef int (*write_transport_t)(struct scsi_cmnd *, void *, int);
typedef int (*other_transport_t)(struct scsi_cmnd *, void *);
typedef int (*alignment_write_t)(struct scsi_cmnd *, void *);
typedef int (*retry_write_t)(struct scsi_cmnd *, void *);
typedef void (*notify_error_t)(void *, unsigned long, int);
typedef int (*reset_device_t)(struct scsi_cmnd *, void *);
typedef int (*request_sense_t)(struct scsi_cmnd *, void *, int *);
typedef void (*notify_mount_t)(void *);
typedef void (*notify_umount_t)(void *);

extern void sm331_set_alloc_card_info(alloc_card_info_t);
extern void sm331_set_free_card_info(free_card_info_t);
extern void sm331_set_read_transport(read_transport_t);
extern void sm331_set_write_transport(write_transport_t);
extern void sm331_set_other_transport(other_transport_t);
extern void sm331_set_alignment_write(alignment_write_t);
extern void sm331_set_retry_write(retry_write_t);
extern void sm331_set_notify_error(notify_error_t);
extern void sm331_set_reset_device(reset_device_t);
extern void sm331_set_request_sense(request_sense_t);
extern void sm331_set_notify_mount(notify_mount_t);
extern void sm331_set_notify_umount(notify_umount_t);
extern void sm331_set_func_status(int);

extern int sm331_is_last_retry_reset(void);
extern int sm331_is_write_reset_on(void);
extern int sm331_is_read_reset_on(void);

extern int sm331_internal_cmd(
	struct scsi_cmnd *, void *, __u8 *, __u8, void *, unsigned int, int);
extern int sm331_internal_cmd_with_status(
	struct scsi_cmnd *, void *, __u8 *, __u8, void *, unsigned int, int, int*);

#endif// __KERNEL__

#endif //__SM331_SETUP_H__
