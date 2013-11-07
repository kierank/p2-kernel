/* Driver for USB Mass Storage compliant devices
 * SCSI layer glue code
 *
 * Current development and maintenance by:
 *   (c) 1999-2002 Matthew Dharm (mdharm-usb@one-eyed-alien.net)
 *
 * Developed with the assistance of:
 *   (c) 2000 David L. Brown, Jr. (usb-storage@davidb.org)
 *   (c) 2000 Stephen J. Gowdy (SGowdy@lbl.gov)
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
/* $Id: scsiglue.c 13963 2011-04-19 02:42:59Z Noguchi Isao $ */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/mutex.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_devinfo.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_eh.h>

/* 2010/5/18, modified by Panasonic ==> */
#ifdef CONFIG_P2PF_SCSI_DISK_FUNC
#include <scsi/scsi_ioctl.h>
#include <scsi/sdc.h>
#endif  /* CONFIG_P2PF_SCSI_DISK_FUNC */
/* <== 2010/5/18, modified by Panasonic */

#include "usb.h"
#include "scsiglue.h"
#include "debug.h"
#include "transport.h"
#include "protocol.h"

/***********************************************************************
 * Host functions 
 ***********************************************************************/

static const char* host_info(struct Scsi_Host *host)
{
	return "SCSI emulation for USB Mass Storage devices";
}

static int slave_alloc (struct scsi_device *sdev)
{
	struct us_data *us = host_to_us(sdev->host);

	/*
	 * Set the INQUIRY transfer length to 36.  We don't use any of
	 * the extra data and many devices choke if asked for more or
	 * less than 36 bytes.
	 */
	sdev->inquiry_len = 36;

	/* USB has unusual DMA-alignment requirements: Although the
	 * starting address of each scatter-gather element doesn't matter,
	 * the length of each element except the last must be divisible
	 * by the Bulk maxpacket value.  There's currently no way to
	 * express this by block-layer constraints, so we'll cop out
	 * and simply require addresses to be aligned at 512-byte
	 * boundaries.  This is okay since most block I/O involves
	 * hardware sectors that are multiples of 512 bytes in length,
	 * and since host controllers up through USB 2.0 have maxpacket
	 * values no larger than 512.
	 *
	 * But it doesn't suffice for Wireless USB, where Bulk maxpacket
	 * values can be as large as 2048.  To make that work properly
	 * will require changes to the block layer.
	 */
	blk_queue_update_dma_alignment(sdev->request_queue, (512 - 1));

	/*
	 * The UFI spec treates the Peripheral Qualifier bits in an
	 * INQUIRY result as reserved and requires devices to set them
	 * to 0.  However the SCSI spec requires these bits to be set
	 * to 3 to indicate when a LUN is not present.
	 *
	 * Let the scanning code know if this target merely sets
	 * Peripheral Device Type to 0x1f to indicate no LUN.
	 */
	if (us->subclass == US_SC_UFI)
		sdev->sdev_target->pdt_1f_for_no_lun = 1;

	return 0;
}

static int slave_configure(struct scsi_device *sdev)
{
	struct us_data *us = host_to_us(sdev->host);
    struct usb_device *udev = us->pusb_dev; /* 2010/10/06, modified by Panasonic (SAV) */

	/* Many devices have trouble transfering more than 32KB at a time,
	 * while others have trouble with more than 64K. At this time we
	 * are limiting both to 32K (64 sectores).
	 */
	if (us->fflags & (US_FL_MAX_SECTORS_64 | US_FL_MAX_SECTORS_MIN)) {
		unsigned int max_sectors = 64;

		if (us->fflags & US_FL_MAX_SECTORS_MIN)
			max_sectors = PAGE_CACHE_SIZE >> 9;
		if (sdev->request_queue->max_sectors > max_sectors)
			blk_queue_max_sectors(sdev->request_queue,
					      max_sectors);
/* 2010/10/06, modified by Panasonic (SAV) ---> */
    } else if (udev->bus->max_sectors) {
        blk_queue_max_sectors(sdev->request_queue,
                              udev->bus->max_sectors);
/* <--- 2010/10/06, modified by Panasonic (SAV) */
	}

	/* We can't put these settings in slave_alloc() because that gets
	 * called before the device type is known.  Consequently these
	 * settings can't be overridden via the scsi devinfo mechanism. */
	if (sdev->type == TYPE_DISK) {

		/* Disk-type devices use MODE SENSE(6) if the protocol
		 * (SubClass) is Transparent SCSI, otherwise they use
		 * MODE SENSE(10). */
		if (us->subclass != US_SC_SCSI && us->subclass != US_SC_CYP_ATACB)
			sdev->use_10_for_ms = 1;

		/* Many disks only accept MODE SENSE transfer lengths of
		 * 192 bytes (that's what Windows uses). */
		sdev->use_192_bytes_for_3f = 1;

		/* Some devices don't like MODE SENSE with page=0x3f,
		 * which is the command used for checking if a device
		 * is write-protected.  Now that we tell the sd driver
		 * to do a 192-byte transfer with this command the
		 * majority of devices work fine, but a few still can't
		 * handle it.  The sd driver will simply assume those
		 * devices are write-enabled. */
		if (us->fflags & US_FL_NO_WP_DETECT)
			sdev->skip_ms_page_3f = 1;

		/* A number of devices have problems with MODE SENSE for
		 * page x08, so we will skip it. */
		sdev->skip_ms_page_8 = 1;

		/* Some disks return the total number of blocks in response
		 * to READ CAPACITY rather than the highest block number.
		 * If this device makes that mistake, tell the sd driver. */
		if (us->fflags & US_FL_FIX_CAPACITY)
			sdev->fix_capacity = 1;

		/* A few disks have two indistinguishable version, one of
		 * which reports the correct capacity and the other does not.
		 * The sd driver has to guess which is the case. */
		if (us->fflags & US_FL_CAPACITY_HEURISTICS)
			sdev->guess_capacity = 1;

		/* Some devices report a SCSI revision level above 2 but are
		 * unable to handle the REPORT LUNS command (for which
		 * support is mandatory at level 3).  Since we already have
		 * a Get-Max-LUN request, we won't lose much by setting the
		 * revision level down to 2.  The only devices that would be
		 * affected are those with sparse LUNs. */
		if (sdev->scsi_level > SCSI_2)
			sdev->sdev_target->scsi_level =
					sdev->scsi_level = SCSI_2;

		/* USB-IDE bridges tend to report SK = 0x04 (Non-recoverable
		 * Hardware Error) when any low-level error occurs,
		 * recoverable or not.  Setting this flag tells the SCSI
		 * midlayer to retry such commands, which frequently will
		 * succeed and fix the error.  The worst this can lead to
		 * is an occasional series of retries that will all fail. */
		sdev->retry_hwerror = 1;

		/* USB disks should allow restart.  Some drives spin down
		 * automatically, requiring a START-STOP UNIT command. */
		sdev->allow_restart = 1;

		/* Some USB cardreaders have trouble reading an sdcard's last
		 * sector in a larger then 1 sector read, since the performance
		 * impact is negible we set this flag for all USB disks */
		sdev->last_sector_bug = 1;
	} else {

		/* Non-disk-type devices don't need to blacklist any pages
		 * or to force 192-byte transfer lengths for MODE SENSE.
		 * But they do need to use MODE SENSE(10). */
		sdev->use_10_for_ms = 1;
	}

	/* The CB and CBI transports have no way to pass LUN values
	 * other than the bits in the second byte of a CDB.  But those
	 * bits don't get set to the LUN value if the device reports
	 * scsi_level == 0 (UNKNOWN).  Hence such devices must necessarily
	 * be single-LUN.
	 */
	if ((us->protocol == US_PR_CB || us->protocol == US_PR_CBI) &&
			sdev->scsi_level == SCSI_UNKNOWN)
		us->max_lun = 0;

	/* Some devices choke when they receive a PREVENT-ALLOW MEDIUM
	 * REMOVAL command, so suppress those commands. */
	if (us->fflags & US_FL_NOT_LOCKABLE)
		sdev->lockable = 0;

	/* this is to satisfy the compiler, tho I don't think the 
	 * return code is ever checked anywhere. */
	return 0;
}

/* queue a command */
/* This is always called with scsi_lock(host) held */
static int queuecommand(struct scsi_cmnd *srb,
			void (*done)(struct scsi_cmnd *))
{
	struct us_data *us = host_to_us(srb->device->host);

	US_DEBUGP("%s called\n", __func__);

	/* check for state-transition errors */
	if (us->srb != NULL) {
		printk(KERN_ERR USB_STORAGE "Error in %s: us->srb = %p\n",
			__func__, us->srb);
		return SCSI_MLQUEUE_HOST_BUSY;
	}

	/* fail the command if we are disconnecting */
	if (test_bit(US_FLIDX_DISCONNECTING, &us->dflags)) {
		US_DEBUGP("Fail command during disconnect\n");
		srb->result = DID_NO_CONNECT << 16;
		done(srb);
		return 0;
	}

	/* enqueue the command and wake up the control thread */
	srb->scsi_done = done;
	us->srb = srb;
	complete(&us->cmnd_ready);

	return 0;
}



/* 2010/5/18, modified by Panasonic (SAV)
   2010/10/18, modified by Panasonic (SAV) ---> */
#ifdef CONFIG_P2PF_SCSI_DISK_FUNC

static int get_devinfo(struct scsi_device *dev, void *devinfo)
{
    int retval = 0;
    struct sdc_usbp_info info;
    struct us_data *us=NULL;
    struct scsi_ioctl_usb_info *uinfo
        = (struct scsi_ioctl_usb_info *)devinfo;

    if(!uinfo||!dev)
        return -EINVAL;

    us = host_to_us(dev->host);
    if(!us)
        return -ENXIO;

    retval = get_usb_device_info(dev->host, &info);
    if(retval<0){
        retval = -ENXIO;
        goto exit;
    }

    uinfo->max_lun = info.usb_max_lun;
    uinfo->vendor_id = info.usb_vendor_id;
    uinfo->product_id = info.usb_product_id;
    uinfo->subclass = info.subclass;
    uinfo->protocol = us->protocol;
    uinfo->t_bus = info.topology_bus;
    uinfo->t_level = info.topology_level;
    uinfo->t_port = info.topology_port;
    {
        struct usb_device *top_dev=NULL;
        for (top_dev = us->pusb_dev;
             top_dev->parent && top_dev->parent->parent; top_dev = top_dev->parent){
            /* Found device below root hub */
        }
        uinfo->t_rootport = top_dev->vportnum - 1;;
    }
    uinfo->t_route = us->pusb_dev->route;
    uinfo->t_devno = info.topology_devnum;
    memset(uinfo->vendor,0,SCSI_IOCTL_USB_STRING_LEN);
    if(us->pusb_dev->product)
        strncpy(uinfo->vendor,us->pusb_dev->product,SCSI_IOCTL_USB_STRING_LEN);
    memset(uinfo->product,0,SCSI_IOCTL_USB_STRING_LEN);
    if(us->pusb_dev->manufacturer)
        strncpy(uinfo->product,us->pusb_dev->manufacturer,SCSI_IOCTL_USB_STRING_LEN);
    memset(uinfo->serial,0,SCSI_IOCTL_USB_STRING_LEN);
    if(us->pusb_dev->serial)
        strncpy(uinfo->serial,us->pusb_dev->serial,SCSI_IOCTL_USB_STRING_LEN);

 exit:
    return retval;
}



static int ioctl(struct scsi_device *dev, int cmd, void __user *arg)
{
    int retval=0;
    struct us_data *us=NULL;

	if (!dev)
		return -ENXIO;

    us = host_to_us(dev->host);
    if(!us)
        return -ENXIO;

    /* main proccess */
    switch(cmd){
    case SCSI_IOCTL_GET_HOSTTYPE:
        retval = SCSI_HOSTTYPE_USB;
        break;

    case SCSI_IOCTL_GET_DEVINFO:
        if(arg==NULL){
            pr_err(USB_STORAGE "ERROR: \"arg\" is NULL pointer\n");
            retval = -EINVAL;
            break;
        }
        {
            struct scsi_ioctl_usb_info info;
            retval = get_devinfo(dev, (void *)&info);
            if(retval){
                pr_err(USB_STORAGE "ERROR: fail at get_devinfo()\n");
                break;
            }
            if( copy_to_user((struct scsi_ioctl_usb_info *)arg, &info,
                             sizeof(struct scsi_ioctl_usb_info)) ){
                retval = -EFAULT;
                pr_err(USB_STORAGE "ERROR: Can't accsess the argument..\n");
                break;
            }
        }
        break;

/* 2011/4/11, added by Panasonic (PAVBU),
   20011/4/19, modified by Panasonic (PAVBU) ---> */

    case SCSI_IOCTL_SET_US_FFLAGS:
        {
            unsigned long fflags = 0;
            unsigned long val = (unsigned long)arg;

            if(val & SCSI_IOCTL_FFLAG_FAST_RECOVERY)
                fflags |= US_FL_FAST_RECOVERY; 

            if(val & SCSI_IOCTL_FFLAG_FORCE_BULK_RESET)
                fflags |= US_FL_FORCE_BULK_RESET; 

            us->fflags |= fflags;
        }
        break;

    case SCSI_IOCTL_CLR_US_FFLAGS:
        {
            unsigned long fflags = 0;
            unsigned long val = (unsigned long)arg;

            if(val & SCSI_IOCTL_FFLAG_FAST_RECOVERY)
                fflags |= US_FL_FAST_RECOVERY; 

            if(val & SCSI_IOCTL_FFLAG_FORCE_BULK_RESET)
                fflags |= US_FL_FORCE_BULK_RESET; 

            us->fflags &= ~fflags;
        }
        break;

    case SCSI_IOCTL_GET_US_FFLAGS:
        if (arg==NULL){
            usdev_err(us, "ERROR: \"arg\" is NULL pointer\n");
            retval = -EINVAL;
            break;
        } else {
            if (copy_to_user(arg, (void*)&us->fflags, sizeof(unsigned long))) {
                retval = -EFAULT;
                usdev_err(us, "ERROR: cannot copy to user space\n");
                break;
            }
        }
        break;

/* <-- 2011/4/11, added by Panasonic (PAVBU),
   20011/4/19, modified by Panasonic (PAVBU) */

    default:
        retval = -ENOTTY;
    }

    /* complete */
    return retval;

}

#endif  /* CONFIG_P2PF_SCSI_DISK_FUNC */
/* <--- 2010/5/18, modified by Panasonic (SAV)
   2010/10/18, modified by Panasonic (SAV) */


/***********************************************************************
 * Error handling functions
 ***********************************************************************/

/* Command timeout and abort */
static int command_abort(struct scsi_cmnd *srb)
{
	struct us_data *us = host_to_us(srb->device->host);

/* 2011/4/11, modified by Panasonic (PAVBU) */
	// US_DEBUGP("%s called\n", __func__);
	usdev_dbg(us, "%s called\n", __func__);

	/* us->srb together with the TIMED_OUT, RESETTING, and ABORTING
	 * bits are protected by the host lock. */
	scsi_lock(us_to_host(us));

	/* Is this command still active? */
	if (us->srb != srb) {
		scsi_unlock(us_to_host(us));
		US_DEBUGP ("-- nothing to abort\n");
		return FAILED;
	}

	/* Set the TIMED_OUT bit.  Also set the ABORTING bit, but only if
	 * a device reset isn't already in progress (to avoid interfering
	 * with the reset).  Note that we must retain the host lock while
	 * calling usb_stor_stop_transport(); otherwise it might interfere
	 * with an auto-reset that begins as soon as we release the lock. */
	set_bit(US_FLIDX_TIMED_OUT, &us->dflags);
	if (!test_bit(US_FLIDX_RESETTING, &us->dflags)) {
		set_bit(US_FLIDX_ABORTING, &us->dflags);
        usdev_info(us, "Command is timeout and abort\n"); /* 2011/4/11, modified by Panasonic (PAVBU) */
		usb_stor_stop_transport(us);
	}
	scsi_unlock(us_to_host(us));

	/* Wait for the aborted command to finish */
	wait_for_completion(&us->notify);
	return SUCCESS;
}

/* This invokes the transport reset mechanism to reset the state of the
 * device */
static int device_reset(struct scsi_cmnd *srb)
{
	struct us_data *us = host_to_us(srb->device->host);
	int result;

/* 2011/4/11, modified by Panasonic (PAVBU) */
	//US_DEBUGP("%s called\n", __func__);
	usdev_dbg(us, "%s called\n", __func__);

	/* lock the device pointers and do the reset */
    usdev_info(us, "transport reset @ %s(%d).\n", __FUNCTION__, __LINE__); /* 2011/4/11, modified by Panasonic (PAVBU) */
	mutex_lock(&(us->dev_mutex));
	result = us->transport_reset(us);
	mutex_unlock(&us->dev_mutex);

/* 2011/4/11, modified by Panasonic (PAVBU) --> */
/* 	return result < 0 ? FAILED : SUCCESS; */
    if (result<0) {
        usdev_err(us, "ERROR: the transport reset is failed (%d) @ %s(%d)\n",
                  result,  __FUNCTION__, __LINE__);
        return FAILED;
    }
    return SUCCESS;
/* <-- 2011/4/11, modified by Panasonic (PAVBU) */
}

/* Simulate a SCSI bus reset by resetting the device's USB port. */
static int bus_reset(struct scsi_cmnd *srb)
{
	struct us_data *us = host_to_us(srb->device->host);
	int result;

 /* 2011/4/11, modified by Panasonic (PAVBU) ---> */
	//US_DEBUGP("%s called\n", __func__);
	usdev_dbg(us, "%s called\n", __func__);

    if (us->fflags & US_FL_FORCE_BULK_RESET) {
        usdev_info(us, "Port reset is ignored @ %s(%d).\n",
                   __FUNCTION__, __LINE__);
        return SUCCESS;
    }

    usdev_info(us, "Simulate a SCSI bus reset by reseting the device's USB port @ %s(%d).\n",
               __FUNCTION__, __LINE__);
	result = usb_stor_port_reset(us);
    if (result<0) {
        usdev_err(us, "ERROR: the port reset is failed (%d) @ %s(%d)\n",
                  result, __FUNCTION__, __LINE__);
        return FAILED;
    }
    return SUCCESS;
/* 	result = usb_stor_port_reset(us); */
/* 	return result < 0 ? FAILED : SUCCESS; */
/* <--- 2011/4/11, modified by Panasonic (PAVBU) */
}


/* 2011/4/19, added by Panasonic (PAVBU) ---> */

/* unjaming handler for hostt */
static int timed_out(struct scsi_cmnd *srb)
{
	struct us_data *us = host_to_us(srb->device->host);
    int retval;

	usdev_dbg(us, "%s called\n", __func__);

    if (!(us->fflags & US_FL_FAST_RECOVERY))
        return SCSI_RETURN_NOT_HANDLED;

    if (srb->serial_number == 0)
        return SUCCESS;

    retval=command_abort(srb);
    if(retval!=SUCCESS)
        return retval;

    if (us->fflags & US_FL_FORCE_BULK_RESET)
        retval=device_reset(srb);
    else
        retval=bus_reset(srb);
    if (retval!=SUCCESS)
            return retval;

    return SUCCESS;
}

/* <--- 2011/4/19, added by Panasonic (PAVBU) */


/* Report a driver-initiated device reset to the SCSI layer.
 * Calling this for a SCSI-initiated reset is unnecessary but harmless.
 * The caller must own the SCSI host lock. */
void usb_stor_report_device_reset(struct us_data *us)
{
	int i;
	struct Scsi_Host *host = us_to_host(us);

	scsi_report_device_reset(host, 0, 0);
	if (us->fflags & US_FL_SCM_MULT_TARG) {
		for (i = 1; i < host->max_id; ++i)
			scsi_report_device_reset(host, 0, i);
	}
}

/* Report a driver-initiated bus reset to the SCSI layer.
 * Calling this for a SCSI-initiated reset is unnecessary but harmless.
 * The caller must not own the SCSI host lock. */
void usb_stor_report_bus_reset(struct us_data *us)
{
	struct Scsi_Host *host = us_to_host(us);

	scsi_lock(host);
	scsi_report_bus_reset(host, 0);
	scsi_unlock(host);
}

/***********************************************************************
 * /proc/scsi/ functions
 ***********************************************************************/

/* we use this macro to help us write into the buffer */
#undef SPRINTF
#define SPRINTF(args...) \
	do { if (pos < buffer+length) pos += sprintf(pos, ## args); } while (0)

static int proc_info (struct Scsi_Host *host, char *buffer,
		char **start, off_t offset, int length, int inout)
{
	struct us_data *us = host_to_us(host);
	char *pos = buffer;
	const char *string;

	/* if someone is sending us data, just throw it away */
	if (inout)
		return length;

	/* print the controller name */
	SPRINTF("   Host scsi%d: usb-storage\n", host->host_no);

	/* print product, vendor, and serial number strings */
	if (us->pusb_dev->manufacturer)
		string = us->pusb_dev->manufacturer;
	else if (us->unusual_dev->vendorName)
		string = us->unusual_dev->vendorName;
	else
		string = "Unknown";
	SPRINTF("       Vendor: %s\n", string);
	if (us->pusb_dev->product)
		string = us->pusb_dev->product;
	else if (us->unusual_dev->productName)
		string = us->unusual_dev->productName;
	else
		string = "Unknown";
	SPRINTF("      Product: %s\n", string);
	if (us->pusb_dev->serial)
		string = us->pusb_dev->serial;
	else
		string = "None";
	SPRINTF("Serial Number: %s\n", string);

	/* show the protocol and transport */
	SPRINTF("     Protocol: %s\n", us->protocol_name);
	SPRINTF("    Transport: %s\n", us->transport_name);

	/* show the device flags */
	if (pos < buffer + length) {
		pos += sprintf(pos, "       Quirks:");

#define US_FLAG(name, value) \
	if (us->fflags & value) pos += sprintf(pos, " " #name);
US_DO_ALL_FLAGS
#undef US_FLAG

		*(pos++) = '\n';
	}

	/*
	 * Calculate start of next buffer, and return value.
	 */
	*start = buffer + offset;

	if ((pos - buffer) < offset)
		return (0);
	else if ((pos - buffer - offset) < length)
		return (pos - buffer - offset);
	else
		return (length);
}

/***********************************************************************
 * Sysfs interface
 ***********************************************************************/

/* Output routine for the sysfs max_sectors file */
static ssize_t show_max_sectors(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct scsi_device *sdev = to_scsi_device(dev);

	return sprintf(buf, "%u\n", sdev->request_queue->max_sectors);
}

/* Input routine for the sysfs max_sectors file */
static ssize_t store_max_sectors(struct device *dev, struct device_attribute *attr, const char *buf,
		size_t count)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	unsigned short ms;

	if (sscanf(buf, "%hu", &ms) > 0 && ms <= SCSI_DEFAULT_MAX_SECTORS) {
		blk_queue_max_sectors(sdev->request_queue, ms);
		return strlen(buf);
	}
	return -EINVAL;	
}

static DEVICE_ATTR(max_sectors, S_IRUGO | S_IWUSR, show_max_sectors,
		store_max_sectors);

static struct device_attribute *sysfs_device_attr_list[] = {
		&dev_attr_max_sectors,
		NULL,
		};

/*
 * this defines our host template, with which we'll allocate hosts
 */

struct scsi_host_template usb_stor_host_template = {
	/* basic userland interface stuff */
	.name =				"usb-storage",
	.proc_name =			"usb-storage",
	.proc_info =			proc_info,
	.info =				host_info,

	/* command interface -- queued only */
	.queuecommand =			queuecommand,

/* 2010/5/18, modified by Panasonic ==> */
#ifdef CONFIG_P2PF_SCSI_DISK_FUNC
    .get_devinfo = get_devinfo, 
    .ioctl =          ioctl,
#endif  /* CONFIG_P2PF_SCSI_DISK_FUNC */
/* <== 2010/5/18, modified by Panasonic */

	/* error and abort handlers */
	.eh_abort_handler =		command_abort,
	.eh_device_reset_handler =	device_reset,
	.eh_bus_reset_handler =		bus_reset,

    /* 2011/4/19, added by Panasonic (PAVBU) */
    .eh_unjam_host = timed_out,

	/* queue commands only, only one command per LUN */
	.can_queue =			1,
	.cmd_per_lun =			1,

	/* unknown initiator id */
	.this_id =			-1,

	.slave_alloc =			slave_alloc,
	.slave_configure =		slave_configure,

	/* lots of sg segments can be handled */
	.sg_tablesize =			SG_ALL,

	/* limit the total size of a transfer to 120 KB */
	.max_sectors =                  240,

	/* merge commands... this seems to help performance, but
	 * periodically someone should test to see which setting is more
	 * optimal.
	 */
	.use_clustering =		1,

	/* emulated HBA */
	.emulated =			1,

	/* we do our own delay after a device or bus reset */
	.skip_settle_delay =		1,

	/* sysfs device attributes */
	.sdev_attrs =			sysfs_device_attr_list,

	/* module management */
	.module =			THIS_MODULE
};

/* To Report "Illegal Request: Invalid Field in CDB */
unsigned char usb_stor_sense_invalidCDB[18] = {
	[0]	= 0x70,			    /* current error */
	[2]	= ILLEGAL_REQUEST,	    /* Illegal Request = 0x05 */
	[7]	= 0x0a,			    /* additional length */
	[12]	= 0x24			    /* Invalid Field in CDB */
};

