/* Driver for USB Mass Storage compliant devices
 * Debugging Functions Header File
 *
 * Current development and maintenance by:
 *   (c) 1999-2002 Matthew Dharm (mdharm-usb@one-eyed-alien.net)
 *
 * Initial work by:
 *   (c) 1999 Michael Gee (michael@linuxspecific.com)
 *
 * This driver is based on the 'USB Mass Storage Class' document. This
 * describes in detail the protocol used to communicate with such
 * devices.  Clearly, the designers had SCSI and ATAPI commands in
 * mind when they created this document.  The commands are all very
 * similar to commands in the SCSI-II and ATAPI specifications.
 *
 * It is important to note that in a number of cases this class
 * exhibits class-specific exemptions from the USB specification.
 * Notably the usage of NAK, STALL and ACK differs from the norm, in
 * that they are used to communicate wait, failed and OK on commands.
 *
 * Also, for certain devices, the interrupt endpoint is used to convey
 * status of a command.
 *
 * Please see http://www.one-eyed-alien.net/~mdharm/linux-usb for more
 * information about this driver.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */
/* $Id: debug.h 13800 2011-04-12 01:23:14Z Noguchi Isao $ */

#ifndef _DEBUG_H_
#define _DEBUG_H_

#include <linux/kernel.h>
#include <linux/device.h>

#define USB_STORAGE "usb-storage: "

#ifdef CONFIG_USB_STORAGE_DEBUG
void usb_stor_show_command(struct scsi_cmnd *srb);
void usb_stor_show_sense( unsigned char key,
		unsigned char asc, unsigned char ascq );
#define US_DEBUGP(x...) printk( KERN_DEBUG USB_STORAGE x )
#define US_DEBUGPX(x...) printk( x )
#define US_DEBUG(x) x 
#else  /* ! CONFIG_USB_STORAGE_DEBUG */
#define US_DEBUGP(x...)
#define US_DEBUGPX(x...)
#define US_DEBUG(x)
#endif  /* CONFIG_USB_STORAGE_DEBUG */

/* 2011/4/11, added by Panasonic (PAVBU) ---> */
#define __usdev_devp(us)  (&us->pusb_intf->dev)
#ifdef CONFIG_USB_STORAGE_DEBUG
#define usdev_dbg(us, format, arg...)       dev_dbg(__usdev_devp(us), "%s" format, USB_STORAGE, ## arg)
#else  /* ! CONFIG_USB_STORAGE_DEBUG */
#define usdev_dbg(us, format, arg...)
#endif  /* CONFIG_USB_STORAGE_DEBUG */
#define usdev_info(us, format, arg...)      dev_info(__usdev_devp(us), "%s" format, USB_STORAGE, ## arg)
#define usdev_notice(us, format, arg...)    dev_notice(__usdev_devp(us), "%s" format, USB_STORAGE, ## arg)
#define usdev_warn(us, format, arg...)      dev_warn(__usdev_devp(us), "%s" format, USB_STORAGE, ## arg)
#define usdev_err(us, format, arg...)       dev_err(__usdev_devp(us), "%s" format, USB_STORAGE, ## arg)
#define usdev_crit(us, format, arg...)      dev_crit(__usdev_devp(us), "%s" format, USB_STORAGE, ## arg)
#define usdev_alert(us, format, arg...)     dev_alert(__usdev_devp(us), "%s" format, USB_STORAGE, ## arg)
#define usdev_emerg(us, format, arg...)     dev_emerg(__usdev_devp(us), "%s" format, USB_STORAGE, ## arg)
/* <--- 2011/4/11, added by Panasonic (PAVBU) */


#endif  /* _DEBUG_H_ */
