#ifndef _LINUX_P2HOTPLUG_H
#define _LINUX_P2HOTPLUG_H

#include <linux/device.h>	/* for struct device */

struct plug_info{
  struct plug_info *next;
  int  event;
  char bus_id[13];
  char vendor[9];
  char model[17];
  char driveletter[5];
  unsigned int type;
  unsigned long long sectors_max;
  unsigned int sector_size;
};


#define USB_UNPLUG_EVENT 0
#define USB_PLUG_EVENT 1
#define SCSI_UNPLUG_EVENT 0
#define SCSI_PLUG_EVENT 1

struct plug_info *get_tailof_usb_plug_info(struct device *dev);
void add_usb_plug_info(struct device *dev,struct plug_info new_info);
struct plug_info *get_headof_usb_plug_info(struct device *dev);
int find_usb_same_bus_id_event(struct device *dev,char *bus_id);
void remove_usb_same_bus_id_event(struct device *dev,char *bus_id);

struct plug_info *get_tailof_scsi_plug_info(struct class_device *cdev);
void add_scsi_plug_info(struct class_device *cdev,struct plug_info new_info);
struct plug_info *get_headof_scsi_plug_info(struct class_device *cdev);
int find_scsi_same_bus_id_event(struct class_device *cdev,char *bus_id);
void remove_scsi_same_bus_id_event(struct class_device *cdev,char *bus_id);
#endif /* _LINUX_P2HOTPLUG_H */
