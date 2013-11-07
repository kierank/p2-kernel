#ifndef _USB_STORAGE_SM331_H
#define _USB_STORAGE_SM331_H

#define SD_USB_CMD_TIMEOUT	8
#define SD_USB_WRITE_RETRY	3
#define SD_USB_READ_RETRY	3

#define SD_MINIMUM_TIMEOUT	(60*HZ)

extern int sm331_transport(struct scsi_cmnd *srb, struct us_data *us);
extern int usb_stor_sm331_reset(struct us_data *us);
extern int usb_stor_sm331_init (struct us_data *us);

#endif //_USB_STORAGE_SM331_H
