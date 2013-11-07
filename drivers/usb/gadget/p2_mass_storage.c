/*  p2_mass_storage.c: 
    This mass storage class driver is heavily based on NetChip Technology
	USB Mass Storage Device Driver for Linux2.4, and detail of the NetChip
    driver is described in below.
    Additionally, optional modification for P2 Equipments are implemented
	by Panasonic.
	(c) 2004-2008, Matsushita Electric industrial Co.,Ltd.(Panasonic)
*/

/*  
 * mass_storage.c: USB Mass Storage Device Driver
 *
 * (c) 2003, NetChip Technology, Inc. (http://www.netchip.com/)
 *
 * This driver implements a mass storage RAM disk on top of the
 * USB Gadget driver.
 *
 * This driver has only been tested on the NetChip NET2280.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <asm/system.h>
#include <asm/uaccess.h>

#include <linux/bitops.h>
#include <linux/blkdev.h>
#include <linux/compiler.h>
#include <linux/completion.h>
#include <linux/dcache.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fcntl.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/kthread.h>
#include <linux/limits.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pagemap.h>
#include <linux/rwsem.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/freezer.h>
#include <linux/utsname.h>
#include <linux/poll.h>

#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/cdev.h>

#include <linux/p2_mass_storage.h>

/* Internal macros		*/
/* Driver Information */
#define DRIVER_DESC             CONFIG_P2USB_DRIVER_DESC
#define DRIVER_VERSION          "0000"
#define DRIVER_SHORT_NAME       "ms"
#define DEVICE_MANUFACTURER     "Matsushita"

/* Device Information */
#define DEVICE_VENDOR_NUM       0x04DA  /* Panasonic */
#define DEVICE_PRODUCT_NUM      CONFIG_P2USB_DEVICE_PRODUCT_NUM
#define DEVICE_REVISION         0x0001  /* 0.0.0.1 */
#if defined(CONFIG_P2USB_SELF_POWER)
# define MAX_USB_POWER           1
#else
# define MAX_USB_POWER           CONFIG_P2USB_MAX_USB_POWER
#endif /* CONFIG_P2USB_SELF_POWER */

/* Endpoint Information */
#define EP0_MAXPACKET           64
#define USB_BUFSIZ              512

/*  Kernel Logging Macros */
/*#define P2USB_DEBUG   */
#define P2USB_ERROR

#define xprintk(level,fmt,args...) \
	printk(level "%s: " fmt, shortname, ## args)

#ifdef P2USB_DEBUG
#define P2DEBUG(fmt,args...)    xprintk(KERN_ERR, fmt, ## args)
#else
#define P2DEBUG(fmt,args...) while(0) {;}
#endif
#ifdef P2USB_ERROR
#define P2ERROR(fmt,args...)    xprintk(KERN_ERR, fmt, ## args)
#else
#define P2ERROR(fmt,args...) while(0) {;}
#endif
/*  
 * These constants describe the driver's name and description used
 * throughout the system.
 */
static struct cdev p2_ms_dev;

static const char longname[] = DRIVER_DESC;
static const char shortname[] = DRIVER_SHORT_NAME;
static const char manufacturer[] = DEVICE_MANUFACTURER;
static const char driver_desc[] = DRIVER_DESC;

/* Name of Endpoints for BBB	*/
static const char EP_OUT_NAME[] = "ep1out";		/* name of out ep will be created in the driver to bind	*/
static const char EP_IN_NAME[] = "ep1in";		/* name of in ep will be created in the driver to bind */

/*
 * These are the USB descriptors used for enumeration.  They should function
 * correctly on both full-speed and high-speed hosts.  Details on the meanings  
 * of the values in these structures can be seen in the USB and Mass Storage 
 * Class specs.
 */
#define STRING_MANUFACTURER 1
#define STRING_PRODUCT      2
#define STRING_SERIAL       3
#define STRING_CONFIG       4
#define STRING_INTERFACE    5

/* Bulk-only class specific requests */
#define USB_BULK_RESET_REQUEST			0xFF
#define USB_BULK_GET_MAX_LUN_REQUEST	0xFE

/* USB Peripheral definition    */
#define USB_PR_BULK 0x50        /* Bulk-only        */
#define USB_SC_SCSI 0x06        /* Transparent SCSI */

/* USB Device Descriptor	*/
static struct usb_device_descriptor device_desc = {
	.bLength = sizeof device_desc,
	.bDescriptorType = USB_DT_DEVICE,

	.bcdUSB = __constant_cpu_to_le16(0x0200),
	.bDeviceClass = USB_CLASS_PER_INTERFACE,
	.bDeviceSubClass = 0,
	.bDeviceProtocol = 0,
	.bMaxPacketSize0 = EP0_MAXPACKET,

	.idVendor = __constant_cpu_to_le16(DEVICE_VENDOR_NUM),
	.idProduct = __constant_cpu_to_le16(DEVICE_PRODUCT_NUM),
	.bcdDevice = __constant_cpu_to_le16(DEVICE_REVISION),
	.iManufacturer = STRING_MANUFACTURER,
	.iProduct = STRING_PRODUCT,
	.iSerialNumber = STRING_SERIAL,

	.bNumConfigurations = 1,
};

/* Configuration Descriptor	*/
static struct usb_config_descriptor config_desc = {
	.bLength = sizeof config_desc,
	.bDescriptorType = USB_DT_CONFIG,
	.bNumInterfaces = 1,
	.bConfigurationValue = 1,
	.iConfiguration = 0,
	.bmAttributes = USB_CONFIG_ATT_ONE | USB_CONFIG_ATT_SELFPOWER,
	.bMaxPower = MAX_USB_POWER,
};

/* Interface Descriptor		*/
static struct usb_interface_descriptor intf_desc = {
	.bLength = sizeof intf_desc,
	.bDescriptorType = USB_DT_INTERFACE,

	.bNumEndpoints = 2, 		/* Only Bulk IN and Bulk OUT (BBB)	*/
	.bInterfaceClass = USB_CLASS_MASS_STORAGE,
	.bInterfaceSubClass = USB_SC_SCSI, /* SCSI		*//* Modified [SBG-SAV]    */
	.bInterfaceProtocol = USB_PR_BULK, /* BULK ONLY	*//* Modified [SBG-SAV]    */
	.iInterface = STRING_INTERFACE,
};

/* Full Speed Bulk IN Endpoint Descriptor   */
static struct usb_endpoint_descriptor fs_bulk_in_desc = {
    .bLength = USB_DT_ENDPOINT_SIZE,
    .bDescriptorType = USB_DT_ENDPOINT,

	.bEndpointAddress = USB_DIR_IN,
    .bmAttributes = USB_ENDPOINT_XFER_BULK,
    .wMaxPacketSize = __constant_cpu_to_le16(64),
    .bInterval = 1,
};

/* Full Speed Bulk OUT Endpoint Descriptor  */
static struct usb_endpoint_descriptor fs_bulk_out_desc = {
    .bLength = USB_DT_ENDPOINT_SIZE,
    .bDescriptorType = USB_DT_ENDPOINT,

	.bEndpointAddress = USB_DIR_OUT,
    .bmAttributes = USB_ENDPOINT_XFER_BULK,
    .wMaxPacketSize = __constant_cpu_to_le16(64),
    .bInterval = 1,
};

/* High Speed Bulk IN Endpoint Descriptor   */
static struct usb_endpoint_descriptor hs_bulk_in_desc = {
    .bLength = USB_DT_ENDPOINT_SIZE,
    .bDescriptorType = USB_DT_ENDPOINT,

	.bEndpointAddress = USB_DIR_IN,
    .bmAttributes = USB_ENDPOINT_XFER_BULK,
    .wMaxPacketSize = __constant_cpu_to_le16(512),
    .bInterval = 1,
};

/* High Speed Bulk OUT Endpoint Descriptor  */
static struct usb_endpoint_descriptor hs_bulk_out_desc = {
    .bLength = USB_DT_ENDPOINT_SIZE,
    .bDescriptorType = USB_DT_ENDPOINT,

	.bEndpointAddress = USB_DIR_OUT,
    .bmAttributes = USB_ENDPOINT_XFER_BULK,
    .wMaxPacketSize = __constant_cpu_to_le16(512),
    .bInterval = 1,
};

/* Device Qualifier Descriptor	*/
static struct usb_qualifier_descriptor dev_qualifier = {
	.bLength = sizeof dev_qualifier,
	.bDescriptorType  = USB_DT_DEVICE_QUALIFIER,
	.bcdUSB = __constant_cpu_to_le16(0x0200),
	.bDeviceClass = USB_CLASS_PER_INTERFACE,
	.bMaxPacketSize0 = EP0_MAXPACKET,
	.bNumConfigurations = 1
};

static const struct usb_descriptor_header *fs_function[] = {
	(struct usb_descriptor_header *) &intf_desc,
	(struct usb_descriptor_header *) &fs_bulk_in_desc,
	(struct usb_descriptor_header *) &fs_bulk_out_desc,
	NULL,
};

static const struct usb_descriptor_header *hs_function[] = {
	(struct usb_descriptor_header *) &intf_desc,
	(struct usb_descriptor_header *) &hs_bulk_in_desc,
	(struct usb_descriptor_header *) &hs_bulk_out_desc,
	NULL,
};

/* Endpoint Extraction */
#define ep_desc(g,hs,fs) (((g)->speed==USB_SPEED_HIGH)?(hs):(fs))

/* Big enough to hold our biggest descriptor */
#define EP0_BUFSIZE 256

/* current state */
#define POW_ON           0
#define INIT             1
#define CBW_READY        2
#define CBW_DONE         3
#define CMD_READY        4
#define CMD_DONE         5
#define SET_READY_DATA   6
#define SET_READY_CSW    7
#define WAIT_NEXT        8

#define MS_DRV_NAME "g_ms"
#define MS_MAJOR 125

/* Bulk-Only Command Constants */
#define CBW_VALID_SIGNATURE     0x43425355
#define CSW_VALID_SIGNATURE     0x53425355
#define CBW_SIZE                0x1F
#define CSW_SIZE                0x0D

/* Data Status Constants */
#define DATA_OK     0   /* Sent/Recieved some data normally */
#define NO_DATA_REQD    1   /* No data was required - must manually send CSW */
#define DATA_ERR    2   /* Command failed/unimplemented */

/* Gadget Device Structure */
struct ms_dev {
    spinlock_t lock;
    struct usb_gadget *gadget;
    struct usb_request *req;
    u8 config;
    struct usb_ep *in_ep;
    struct usb_ep *out_ep;
    struct semaphore mutex_ms_dev;
};

/* Mass Storage state machine definitions */
typedef enum {
    IDLE,           /* Waiting for CBW */
    RESPONSE,       /* Sending/Receiveing command data */
    SEND_CSW        /* Sending CSW */
} ms_state;

/* Status of an executed SCSI command */
typedef enum {
    CMD_OK,         /* Responded correctly to SCSI command */
    CMD_UNIMPLEMENTED,  /* SCSI command not handled */
    CMD_ERROR,      /* Error processing SCSI command */
} ms_command_status;

struct p2_ms_req {
    struct list_head list_ms_dev;
    u16 WhichReturn;
    u16 ConnectStatus;
    u32 ReturnStatus;
    u32 TransferedLength;
};

struct transfer_context {
    ms_state state;
    struct p2usb_cbw cmd;
    int status;
    int free_buffer;
    void *extra;
};

/* Device Serial Number	*/
static char serial[P2USB_MAX_SERIAL];

/* String Descriptor				*/
/* Static strings, in UTF-8			*/

#if defined(CONFIG_P2USB_BUS_SELF_POWER)
# define P2USB_POWERMODE "Bus-powered, Self-powered"
#elif defined(CONFIG_P2USB_BUS_POWER)
# define P2USB_POWERMODE "Bus-powered"
#elif defined(CONFIG_P2USB_SELF_POWER)
# define P2USB_POWERMODE "Self-powered"
#else
# define P2USB_POWERMODE "Undefined" /* XXX */
#endif /* CONFIG_P2USB_*_POWER */

static struct usb_string strings[] = {
	{STRING_MANUFACTURER, manufacturer},
	{STRING_PRODUCT, longname},
	{STRING_SERIAL, serial},
	{STRING_CONFIG,     P2USB_POWERMODE},
	{STRING_INTERFACE,  "Mass Storage"},
	{}
};

/* Sets language and string descriptor to use	*/
static struct usb_gadget_strings stringtab = {
	.language = 0x0409, 	/* en-us			*/
	.strings = strings,
};

/* Function Prototypes */
extern void ep_set_state(struct p2_ms_req *ms_req, u16 whichReturn, u16 connectStatus, u32 returnStatus, u32 transferedLength);
extern int config_buf(enum usb_device_speed speed, u8 * buf, u8 type, unsigned index);
extern inline int ms_send_data(void *buf, int buflen, struct usb_ep *ep, struct p2usb_cbw cbw, ms_state state, void *extra);
extern struct usb_request *alloc_ep_req(struct usb_ep *ep, unsigned length, void *buf_adr);
extern void ms_setup_complete(struct usb_ep *ep, struct usb_request *req);
extern int enable_ms_eps(struct ms_dev *dev, int gfp_flags);
extern void disable_ms_eps(struct ms_dev *dev);
extern int ms_set_config(struct ms_dev *dev, unsigned number, int gfp_flags);
extern void ms_ep_complete(struct usb_ep *ep, struct usb_request *req);
extern struct usb_request *ms_in_ep_start(struct usb_ep *ep, int gfp_flags);
extern struct usb_request *ms_out_ep_start(struct usb_ep *ep, int gfp_flags);
extern void free_ep_req(struct usb_ep *ep, struct usb_request *req);

static u8 CurrentState;
static u8 PreviousState;
static u32 CurrentSerialNumber = 0;

static P2USB_GET_DATA_STRUCT StoreGetData;
static P2USB_SET_DATA_STRUCT StoreSetData;
static P2USB_COMMAND_STATUS_STRUCT StoreCommandStatus;
static DECLARE_WAIT_QUEUE_HEAD (WaitQueue);

static struct ms_dev *StoreDev;

/* used list_entry	*/
static struct list_head todo_list;

static u16 UsbChangeStatus = 0;
static u16 StatusQueueError = 0;

static void *ReceiveCbwBuf;
static void *SendCswBuf;
static int g_ms_major = MS_MAJOR;

inline void clear_queue_info(void)
{
	/* clear all queue	*/
	while(!list_empty(todo_list.next)) {
		struct p2_ms_req *req = NULL;

		req = list_entry(todo_list.next, struct p2_ms_req, list_ms_dev);
		list_del_init(&req->list_ms_dev);
		kfree(req);
	}
}

/*
 * Copies configuration descriptor to buf.
 * Device has 1 configuration, 1 interace, and 2 endpoints
 */
int config_buf(enum usb_device_speed speed, u8 *buf, u8 type, unsigned index)
{
	const struct usb_descriptor_header  **function;

	int len;

	if (index > 0)
		return -EINVAL;

	if (type == USB_DT_OTHER_SPEED_CONFIG)
		speed = (USB_SPEED_FULL + USB_SPEED_HIGH) - speed;
	if (speed == USB_SPEED_HIGH)
		function = hs_function;
	else
		function = fs_function;

    len = usb_gadget_config_buf(&config_desc, buf, EP0_BUFSIZE, function);
    ((struct usb_config_descriptor *) buf)->bDescriptorType = type;	

	return len;
}

/* Creates a request object and I/O buffer for ep */
struct usb_request *alloc_ep_req(struct usb_ep *ep, unsigned length, void *buf_adr)
{
	struct usb_request *req;    /* Request to return */

	/* Allocate a request object to use with this endpoint */
	req = usb_ep_alloc_request(ep, GFP_ATOMIC);
	if(!req) {
		P2ERROR("Could not allocate request!\n");
		return 0;
	}
	req->length = length;

	if (buf_adr) {
		req->buf = buf_adr;
	}else {
		P2ERROR("Invalid buffer pointer\n");
		usb_ep_free_request(ep, req);
		return 0;
	}

	/* Set default parameters for bulk-only transport */
	req->zero = 0;
	req->short_not_ok = 0;
	req->no_interrupt = 0;
    
	/*  
	 * This is what we usually want.  It will be different for
	 * disk accesses, since they need to perform additional processing.
	 */
	req->complete = ms_ep_complete;

	/* Request and buffer created - return */
	return req;
}

/*
 * Finish responding to setup packet
 * Called when setup request completes
 */
void ms_setup_complete(struct usb_ep *ep, struct usb_request *req)
{
	/*
	 * Everything was already handled.  Show status on
	 * error or if actual bytes transferred != requested bytes  
	 */
	if (req->status || req->actual != req->length)
		P2DEBUG("setup complete --> %d, %d/%d\n", req->status, req->actual, req->length);

}

/*
 * Finish setting the configuration to mass storage
 * Enable endpoints to start processing commands
 */
int enable_ms_eps(struct ms_dev *dev, int gfp_flags)
{
	int result = 0;
	struct usb_ep *ep;
	struct usb_gadget *gadget = dev->gadget;

	StoreDev = dev;

	gadget_for_each_ep(ep, gadget) {
		/* Loop thru all device endpoints */
		const struct usb_endpoint_descriptor *d;
		P2DEBUG("Configuring %s\n", ep->name);
		if (strcmp(ep->name, EP_IN_NAME) == 0) {
			/* IN Endpoint */
			d = ep_desc(gadget, &hs_bulk_in_desc, &fs_bulk_in_desc);
			ep->driver_data = dev;
			dev->in_ep = ep;

			/* Enable IN Endpoint */
			P2DEBUG("ENABLING IN ENDPOINT\n");
	
			result = usb_ep_enable(ep, d);
			if (result == 0) {
				P2DEBUG("%s enabled!\n", ep->name);
				continue;
			}
		}else
		if (strcmp(ep->name, EP_OUT_NAME) == 0) {
			/* OUT Endpoint */
			d = ep_desc(gadget, &hs_bulk_out_desc, &fs_bulk_out_desc);
			ep->driver_data = dev;
			dev->out_ep = ep;

			/* Enable OUT Endpoint */
			P2DEBUG("ENABLING OUT ENDPOINT\n");

			result = usb_ep_enable(ep, d);
			P2DEBUG("%s enabled!\n", ep->name);
			if (result == 0) {
                /* Start waiting for commands */
				if (ms_out_ep_start(ep, gfp_flags)) {
					P2DEBUG("%s endpoint enabled\n", ep->name);
					continue;
				}else {
					P2ERROR("ms_out_ep_start() failed\n");
					result = -EFAULT;
					break;
				}
			}
		}else {      /* Some other endpoint, don't need it */
			continue;
		}
		P2ERROR("Couldn't enabling %s, result %d\n", ep->name, result);
		break;
	}

	/* Endpoints enabled */
	return result;
}

/*
 * Reset configuration.  Disable all endpoints.
 */
void disable_ms_eps(struct ms_dev *dev)
{
	if (dev->config == 0)
		return;
	
	if (dev->in_ep) {
		/* IN was configured - disable it */
		usb_ep_disable(dev->in_ep);
		dev->in_ep = 0;
	}
	if (dev->out_ep) {
		/* OUT was configured - disable it */
		usb_ep_disable(dev->out_ep);
		dev->out_ep = 0;
	}
	/* Device now unconfigured */
	dev->config = 0;

	CurrentState = INIT;
}

/*
 * Enable configuration "number"
 * This function resets the current configuration and calls enable_ms_eps
 * to switch to the Mass Storage configuration.
 *
 * We only use configuration 1.
 */
int ms_set_config(struct ms_dev *dev, unsigned number, int gfp_flags)
{
	int result = 0;
	struct usb_gadget *gadget = dev->gadget;
	struct p2_ms_req *ms_req = NULL;

	P2DEBUG("enter ms_set_config(%d, %d->%d)\n", CurrentState, dev->config, number);
	if (CurrentState == POW_ON) {
		P2ERROR("Could not run ms_set_config in POW_ON\n");
		return -EFAULT;
	}
	if (number == dev->config)
		/* We're already set to that configuration - do nothing. */
		return 0;

	P2DEBUG("in ms_set_config: number=%d\n", number);
	switch (number) {
	/* See what configuration is requested.  We only accept #1 */
	case 1:
    	P2DEBUG("in ms_set_config: number=1\n");
		if (CurrentState == INIT) {
			/* Reset configuration just in case */
			disable_ms_eps(dev);

			/* Main configuration number */
			result = enable_ms_eps(dev, gfp_flags);
			if (result == 0) {
				P2DEBUG("Set config success\n");
				/* clear all queue	*/
				clear_queue_info();
				P2DEBUG("DACT set:ms_set_config(1)\n");
				UsbChangeStatus = P2USB_DACT_BIT;
				StatusQueueError = 0;
				CurrentState = CBW_READY;
			}else {
				P2ERROR("Set config failed(%d)\n", result);
			}
		}
		break;

	default:
		/* Reset configuration just in case */
		disable_ms_eps(dev);

		/* Bad number */
		P2ERROR("in ms_set_config() : default state\n");
		result = -EINVAL;

	case 0:
    	P2DEBUG("in ms_set_config() : number=0\n");
		if (CurrentState != INIT) {
			/* Reset configuration just in case */
			disable_ms_eps(dev);

			/* clear all queue	*/
			clear_queue_info();
			ms_req = kzalloc(sizeof *ms_req, GFP_KERNEL);
			if( ms_req ){
				memset(ms_req, 0, sizeof *ms_req);
				list_add_tail(&ms_req->list_ms_dev, &todo_list);
				P2DEBUG("list_add_tail(MassStorageReset)\n");
			}else {
				P2ERROR("Couldn't allocate StatusQueue(MassStorageReset)\n");
				result = -ENOMEM;
			}
			UsbChangeStatus = P2USB_DACT_BIT;
			CurrentSerialNumber++;
			CurrentState = INIT;
		}
		wake_up_interruptible(&WaitQueue);
		P2DEBUG("exit ms_set_config 0(%d)\n", CurrentState);
		return result;
	}

	if (!result && (!dev->in_ep || !dev->out_ep)){
	/*
	 * Either enable_ms_eps failed or   
	 * it didn't create the endpoints
	 */
		P2ERROR("Couldn't not create the endpoints\n");
		result = -ENODEV;
	}

	if (result) {
		/* Setting configuration failed, make sure it's reset */
		disable_ms_eps(dev);
	} else {
		/* Configuration has been set.  Show status. */
		char *speed;
		switch (gadget->speed) {
			case USB_SPEED_LOW:
				speed = "low";
				break;
			case USB_SPEED_FULL:
				speed = "full";
				break;
			case USB_SPEED_HIGH:
				speed = "high";
				break;
			default:
				speed = "?";
				break;
		}

		dev->config = number;
		P2DEBUG("%s speed config #%d\n", speed, number);
	}
	P2DEBUG("Exit ms_set_config 1(%d)  return = %d\n", CurrentState, result);
	return result;
}

static int
standard_setup_req(struct usb_gadget *gadget, struct usb_request *req, const struct usb_ctrlrequest *ctrl)
{
	struct ms_dev *dev = get_gadget_data(gadget);
	int value = -EOPNOTSUPP;
	
	/* [SBG-SAV] --> [ENDIAN]   */
	u16 wValue = le16_to_cpu(ctrl->wValue);
	u16 wIndex = le16_to_cpu(ctrl->wIndex);
	u16 wLength = le16_to_cpu(ctrl->wLength);

	unsigned long flags;

	/* Ep0 standard request handlers.  These always run in_irq. */
	switch (ctrl->bRequest) {
    case USB_REQ_GET_DESCRIPTOR:
		/*
		 * GET DESCRIPTOR request
		 * Check descriptor type and copy it to req->buf.
		*/
		P2DEBUG("ms_setup : USB_REQ_GET_DESCRIPTOR\n");
		if (ctrl->bRequestType != USB_DIR_IN)
			break;

		switch (wValue >> 8) {
		/* What kind of descriptor? */
		case USB_DT_DEVICE:
			P2DEBUG("\tUSB_DT_DEVICE\n");
			value = min(wLength, (u16) sizeof device_desc);
			memcpy(req->buf, &device_desc, value);
			break;

		case USB_DT_DEVICE_QUALIFIER:
			P2DEBUG("\tUSB_DT_QUALIFIER\n");
			value = min(wLength, (u16) sizeof dev_qualifier);
			memcpy(req->buf, &dev_qualifier, value);
			break;

		case USB_DT_OTHER_SPEED_CONFIG:
		case USB_DT_CONFIG:
			/*
			 * These both do the same thing 
			 * because config_buf will figure out
			 * which config to copy.
			 */
			P2DEBUG("\tUSB_DT_CONFIG/OTHER_SPEED_CONFIG\n");
			value = config_buf(gadget->speed, req->buf, wValue >> 8, wValue & 0xff);
			if (value >= 0){
				value = min(wLength, (u16)value);
			}
			break;

		case USB_DT_STRING:
			P2DEBUG("\tUSB_DT_STRING\n");
			value = usb_gadget_get_string(&stringtab, wValue & 0xff, req->buf);
			if (value >= 0){
				value = min(wLength, (u16)value);
			}
			break;
		default:
			P2ERROR("ms_setup : What kind of descriptor? : Unknown descriptor = %02x\n", (wValue >> 8));

		}
		P2DEBUG("ms_setup : USB_REQ_GET_DESCRIPTOR : End : descriptor = %02x\n", (wValue >> 8));
		break;

	case USB_REQ_SET_CONFIGURATION:
		/* SET CONFIGURATION request */
		P2DEBUG("USB_REQ_SET_CONFIGURATION\n");
		if (ctrl->bRequestType != 0)
			break;

		spin_lock_irqsave(&dev->lock, flags);
		value = ms_set_config(dev, wValue, GFP_ATOMIC);
		spin_unlock_irqrestore(&dev->lock, flags);

		P2DEBUG("USB_REQ_SET_CONFIGURATION : BREAK : %d\n", value);
		break;

	case USB_REQ_GET_CONFIGURATION:
		/* GET CONFIGURATION request */
		P2DEBUG("USB_REQ_GET_CONFIGURATION\n");
		if (ctrl->bRequestType != USB_DIR_IN)
			break;

		*(u8 *) req->buf = dev->config;
		value = min(wLength, (u16) 1);
		break;

	case USB_REQ_SET_INTERFACE:
		/* SET INTERFACE request */
		P2DEBUG("USB_REQ_SET_INTERFACE\n");
		if (ctrl->bRequestType != USB_RECIP_INTERFACE)
			break;

		spin_lock_irqsave(&dev->lock, flags);
		if (dev->config && wIndex == 0 && wValue == 0) {
			/* Command is valid */
			u8 config = dev->config;
			P2DEBUG("USB_REQ_SET_INTERFACE \n");
			disable_ms_eps(dev);
			ms_set_config(dev, config, GFP_ATOMIC);
			value = 0;
		}
		spin_unlock_irqrestore(&dev->lock, flags);
		break;

	case USB_REQ_GET_INTERFACE:
		/* GET INTERFACE command */
		P2DEBUG("USB_REQ_GET_INTERFACE\n");
		if (ctrl->bRequestType != (USB_DIR_IN | USB_RECIP_INTERFACE)) {
			break;
		}
		if (!dev->config) {
			break;
		}
		if (wIndex != 0) {
			value = -EDOM;
			break;
		}
		/* Only one interface, so it must be 0 */
		*(u8 *) req->buf = 0;
		value = min(wLength, (u16) 1);
		break;

	case USB_REQ_SET_FEATURE:
	case USB_REQ_CLEAR_FEATURE:
		/* SET_FEATURE/CLEAR_FEATURE command	*/
		P2DEBUG("USB_REQ_USB_REQ_SET_FEATURE/USB_REQ_CLEAR_FEATURE\n");
		value = 0;
		break;
	}

	if (value == -EOPNOTSUPP)
		P2DEBUG("Unknown standard setup request %02x.%02x v%04x i%04x l%u\n",
			ctrl->bRequestType, ctrl->bRequest, wValue, wIndex, wLength);
	return value;
}

static int
class_setup_req(struct usb_gadget *gadget, struct usb_request *req, const struct usb_ctrlrequest *ctrl)
{
	struct ms_dev *dev = get_gadget_data(gadget);
	struct p2_ms_req *ms_req = NULL;
	int	value = -EOPNOTSUPP;
	u16	wLength = le16_to_cpu(ctrl->wLength);
   
	unsigned long flags;
 
	/* Handle Bulk-only class-specific requests */
	switch (ctrl->bRequest) {
	case USB_BULK_GET_MAX_LUN_REQUEST:
		/* GET_MAX_LUN  */
		P2DEBUG("GET_MAX_LUN\n");
		*(u8 *) req->buf = P2USB_LUN;
		value = min(wLength, (u16) 1);
		break;

	case USB_BULK_RESET_REQUEST:
		P2DEBUG("BulkOnly MassStorageReset\n");
		/* BulkOnly MassStorageReset    */
       	if( (CurrentState == CBW_READY) ||
			(CurrentState == CBW_DONE) ||
			(CurrentState == CMD_READY) ||
			(CurrentState == CMD_DONE) ||
			(CurrentState == SET_READY_DATA) ||
			(CurrentState == SET_READY_CSW) ||
			(CurrentState == WAIT_NEXT) ) {
			P2DEBUG("BulkOnly MassStorageReset (%d)\n", CurrentState);

			spin_lock_irqsave(&dev->lock, flags);
			
			/* clear all queue  */
			clear_queue_info();
			P2DEBUG("DACT set : BulkOnly MassStorageReset\n");
			UsbChangeStatus = P2USB_DACT_BIT;

			ms_req = kzalloc(sizeof *ms_req, GFP_KERNEL);
			if( ms_req ){
				memset(ms_req, 0, sizeof *ms_req);
				ep_set_state(ms_req, P2USB_MRT_BIT, 0, 0, 0);
				list_add_tail(&ms_req->list_ms_dev, &todo_list);

				/* ready to get CBW data    */
				CurrentState = CBW_READY;
				ms_out_ep_start(dev->out_ep, GFP_ATOMIC);
				value = 0;
			}else {
				P2ERROR("Couldn't allocate StatusQueue(MassStorageReset)\n");
				StatusQueueError = P2USB_MRT_BIT | P2USB_ERR_BIT;
				value = -EFAULT;
			}

			spin_unlock_irqrestore(&dev->lock, flags);
			wake_up_interruptible(&WaitQueue);

		}else {
			P2DEBUG("BulkOnly MassStorageReset (%d), Wrong statement\n", CurrentState);

			ms_req = kzalloc(sizeof *ms_req, GFP_KERNEL);
			if( ms_req ){
				memset(ms_req, 0, sizeof *ms_req);
				ep_set_state(ms_req, (P2USB_MRT_BIT | P2USB_ERR_BIT), 0, INVALID_STATE_ERROR, 0);
				list_add_tail(&ms_req->list_ms_dev, &todo_list);
				P2DEBUG("list_add_tail(MassStorageReset)\n");
			}else {
				P2ERROR("Couldn't allocate StatusQueue(MassStorageReset)\n");
				StatusQueueError = P2USB_MRT_BIT | P2USB_ERR_BIT;
			}
		}
		break;
	}

	if (value == -EOPNOTSUPP)
		P2DEBUG("Unknown class-specific setup request %02x.%02x v%04x i%04x l%u\n",
				ctrl->bRequestType, ctrl->bRequest,
				le16_to_cpu(ctrl->wValue), le16_to_cpu(ctrl->wIndex), wLength);
	return value;
}

/*
 * Main setup packet handler.
 * This function is called for every control packet received.  It
 * checks the request type and performs the necessary operations.
 *
 */
static int ms_setup(struct usb_gadget *gadget, const struct usb_ctrlrequest *ctrl)
{
	struct ms_dev *dev = get_gadget_data(gadget);
	struct usb_request *req = dev->req;
	int value = -EOPNOTSUPP;

	if ((ctrl->bRequestType & USB_TYPE_MASK) == USB_TYPE_CLASS){
		value = class_setup_req(gadget, req, ctrl);
	}else {
		value = standard_setup_req(gadget, req, ctrl);
	}

	P2DEBUG("ms_setup : done [%d]\n", value);
	if (value >= 0) {
		/* We're sending data */
		req->length = value;
		/* Queue up response */
		value = usb_ep_queue(gadget->ep0, req, GFP_ATOMIC);
		if (value < 0) {
			/* usb_ep_queue failed */
			P2ERROR("ep_queue --> %d\n", value);
			req->status = 0;
			ms_setup_complete(gadget->ep0, req);
		}
	}
	return value;
}

/*
 * Free an endpoint request
 */
void free_ep_req(struct usb_ep *ep, struct usb_request *req)
{
	struct transfer_context  *context = (struct transfer_context *)req->context;

	if (!req) {
		P2ERROR("free_ep_req with no reqest : error\n");
		return;
	}
	if (!ep) {
		P2ERROR("free_ep_req with no endpoint available : error\n");
		return;
	}
	/* Free context */
	if (context) {
		kfree(req->context);
	}
	/* Free request */
	/* Fix bug: Modified 2009/09/29 */
	if( ep && req )
		usb_ep_free_request(ep, req);
}

void ep_normal_completion(struct usb_ep *ep, struct usb_request *req)
{
    struct ms_dev *dev = ep->driver_data;
    int status = req->status;
	struct transfer_context *_context;
    struct usb_request *_req;
    int retStatus;
    struct p2_ms_req *ms_req = NULL;
    static u32 cmdTransferLength = 0;
    static u32 setTransferLength = 0;
	u32 t_val;

	switch (CurrentState) {
	case CBW_READY:
		P2DEBUG("CBW_READY\n");
		CurrentState = CBW_DONE;
		CurrentSerialNumber++;

		ms_req = kzalloc(sizeof *ms_req, GFP_KERNEL);
		if( ms_req ){
			memset(ms_req, 0, sizeof *ms_req);
			ep_set_state(ms_req, P2USB_CBW_BIT, 0, 0, req->actual);
			list_add_tail(&ms_req->list_ms_dev, &todo_list);
			P2DEBUG("Endpoint request completed (CBW_READY)\n");

			memcpy(&StoreGetData.CbwData, (struct cbw *)req->buf, req->actual);

			/* Swapping endian	*/
			t_val = le32_to_cpu(StoreGetData.CbwData.signature);
			StoreGetData.CbwData.signature = t_val;
			t_val = le32_to_cpu(StoreGetData.CbwData.data_transfer_length);
			StoreGetData.CbwData.data_transfer_length = t_val;
		}else {
			P2ERROR("Couldn't not allocate StatusQueue(ms_ep_complete:CBW_READY)\n");
			StatusQueueError = P2USB_CBW_BIT | P2USB_ERR_BIT;
		}
		wake_up_interruptible(&WaitQueue);
		break;

	case CMD_READY:
		P2DEBUG("CMD_READY\n");
		cmdTransferLength = req->actual;
		CurrentState = CMD_DONE;

		ms_req = kzalloc(sizeof *ms_req, GFP_KERNEL);
		if( ms_req ){
			memset(ms_req, 0, sizeof *ms_req);
			ep_set_state(ms_req, P2USB_CMD_BIT, 0, 0, cmdTransferLength);
			list_add_tail(&ms_req->list_ms_dev, &todo_list);
			P2DEBUG("Endpoint request completed (CMD_READY)\n");
		}else {
			P2ERROR("Couldn't not allocate StatusQueue(ms_ep_complete:CMD_READY\n");
			StatusQueueError = P2USB_CMD_BIT | P2USB_ERR_BIT;
		}
		wake_up_interruptible(&WaitQueue);
		break;

	case SET_READY_DATA:
		P2DEBUG("SET_READY_DATA : data length :%d\n", req->actual);
		setTransferLength = req->actual;

		if ( (StoreSetData.SetDataTransferInfo & P2USB_DO_TRANSFER) &&
						(StoreSetData.SetDataTransferInfo & P2USB_STALL) ) {
			P2DEBUG("Set STALL after data transfer\n");
			usb_ep_set_halt(dev->in_ep);
		}

		t_val = cpu_to_le32(StoreSetData.CswData.signature);
		StoreSetData.CswData.signature = t_val;
		t_val = cpu_to_le32(StoreSetData.CswData.data_residue);
		StoreSetData.CswData.data_residue = t_val;
		memcpy(SendCswBuf, &StoreSetData.CswData, CSW_SIZE);
		_req = alloc_ep_req(dev->in_ep, CSW_SIZE, (void *)SendCswBuf);
		if (_req) {
			P2DEBUG("SET_READY_DATA : Going to kmalloc context\n");
			_context = kmalloc(sizeof(struct transfer_context), GFP_ATOMIC);
			if(!_context) {
				ms_req = kzalloc(sizeof *ms_req, GFP_KERNEL);
				if( ms_req ){
					memset(ms_req, 0, sizeof *ms_req);
					ep_set_state(ms_req, (P2USB_SET_BIT | P2USB_ERR_BIT), 0,
										 ALLOC_CONTEXT_ERROR, setTransferLength);
					list_add_tail(&ms_req->list_ms_dev, &todo_list);
					P2ERROR("Couldn't allocate context (SET_READY_DATA)\n");
					usb_ep_free_request(dev->in_ep, _req);
				}else {
					P2ERROR("Couldn't allocate StatusQueue (SET_READY_DATA)\n");
					StatusQueueError = P2USB_SET_BIT | P2USB_ERR_BIT;
				}
				wake_up_interruptible(&WaitQueue);
			}else {
				_context->free_buffer = 1;
				_req->context = _context;
				PreviousState = CurrentState;
				CurrentState = SET_READY_CSW;

				retStatus = usb_ep_queue(dev->in_ep, _req, GFP_ATOMIC);
				if (retStatus == 0) {
					P2DEBUG("start %s --> %d\n", dev->in_ep->name, status);
				} else {
					CurrentState = PreviousState;
					ms_req = kzalloc(sizeof *ms_req, GFP_KERNEL);
					if( ms_req ){
						memset(ms_req, 0, sizeof *ms_req);
						ep_set_state(ms_req, (P2USB_SET_BIT | P2USB_ERR_BIT), 0, 
                                         QUEUE_REQUEST_ERROR, setTransferLength);
						list_add_tail(&ms_req->list_ms_dev, &todo_list);
						P2ERROR("usb_ep_queue failed with status=%d (SET_READY_DATA)\n", status);
						usb_ep_free_request(dev->in_ep, _req);
					}else {
						P2ERROR("Couldn't not allocate StatusQueue (SET_READY_DATA)\n");
						StatusQueueError = P2USB_SET_BIT | P2USB_ERR_BIT;
					}
				}
			}
		}else {
			ms_req = kzalloc(sizeof *ms_req, GFP_KERNEL);
			if( ms_req ){
				memset(ms_req, 0, sizeof *ms_req);
				ep_set_state(ms_req, (P2USB_SET_BIT | P2USB_ERR_BIT), 0,
                                         ALLOC_REQUEST_ERROR, setTransferLength);
				list_add_tail(&ms_req->list_ms_dev, &todo_list);
				P2DEBUG("Endpoint request completed (SET_READY_DATA))\n");
			}else {
				P2ERROR("Couldn't not allocate StatusQueue (SET_READY_DATA)\n");
				StatusQueueError = P2USB_SET_BIT | P2USB_ERR_BIT;
			}
			wake_up_interruptible(&WaitQueue);
		}
		break;

	case SET_READY_CSW:
		P2DEBUG("SET_READY_CSW\n");
		/* calculate transfered data length("DATA + CSW" or "CSW")  */
		if (StoreSetData.SetDataTransferInfo & P2USB_DO_TRANSFER) {
			P2DEBUG("data length:%d\n", setTransferLength);
			P2DEBUG("csw length :%d\n", req->actual);
			setTransferLength += req->actual;
		}else {
			P2DEBUG("csw length :%d\n", req->actual);
			setTransferLength = req->actual;
		}

		ms_req = kzalloc(sizeof *ms_req, GFP_KERNEL);
		if( ms_req ){
			memset(ms_req, 0, sizeof *ms_req);
			ep_set_state(ms_req, P2USB_SET_BIT, 0, 0, setTransferLength);
			list_add_tail(&ms_req->list_ms_dev, &todo_list);
			P2DEBUG("Endpoint request completed (SET_READY_CSW)\n");

			PreviousState = CurrentState;
			CurrentState = CBW_READY;

			if (!ms_out_ep_start(dev->out_ep, GFP_ATOMIC)) {
				P2ERROR("ms_out_ep_start() : Failed\n");
				CurrentState = PreviousState;
				ms_req = kzalloc(sizeof *ms_req, GFP_KERNEL);
				if( ms_req ){
					memset(ms_req, 0, sizeof *ms_req);
					ep_set_state(ms_req, (P2USB_SET_BIT | P2USB_ERR_BIT), 0,
                                         CBW_REQUEST_ERROR, setTransferLength);
					list_add_tail(&ms_req->list_ms_dev, &todo_list);
					P2DEBUG("Couldn't not start CBW request\n");
				}else {
					P2ERROR("Couldn't not allocate StatusQueue (SET_READY_CSW)\n");
					StatusQueueError = P2USB_SET_BIT | P2USB_ERR_BIT;
				}
			}
		}else {
			P2ERROR("Couldn't not allocate StatusQueue (SET_READY_CSW)\n");
			StatusQueueError = P2USB_SET_BIT | P2USB_ERR_BIT;
		}
		wake_up_interruptible(&WaitQueue);
		break;

	default:
		P2ERROR("Endpoint : Invalid status(%d)\n", CurrentState);
		break;
	}
}

unsigned short ep_reset_state(void)
{
	u16 whichReturn;

	switch (CurrentState) {
	case CBW_READY:
		whichReturn = P2USB_CBW_BIT;
		break;

	case CMD_READY:
		whichReturn = P2USB_CMD_BIT;
		break;

	case SET_READY_DATA:
	case SET_READY_CSW :
		whichReturn = P2USB_SET_BIT;
		break;

	default:
		whichReturn = 0;
		P2ERROR("Endpoint completion error : Invalid state (%d)\n", CurrentState);
		break;
	}
	
	return (u16)whichReturn;
	
} 

void ep_set_state(
	struct p2_ms_req *ms_req,
	u16 whichReturn,
	u16 connectStatus,
	u32 returnStatus,
	u32 transferedLength)
{
	ms_req->WhichReturn = (u16)whichReturn;
	ms_req->ConnectStatus =(u16)connectStatus;
	ms_req->ReturnStatus = (u32)returnStatus;
	ms_req->TransferedLength = (u32)transferedLength;
}

/*
 * This function is called by the gadget driver after a transfer completes.
 * If the request completes with a successful status, it checks the current 
 * state  of the mass storage driver.
 */
void ms_ep_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct ms_dev *dev = ep->driver_data;
	int status = req->status;
	struct transfer_context *context = (struct transfer_context *)req->context;
	struct p2_ms_req *ms_req = NULL;
	unsigned long flags; 
	u16 whichReturn;

	spin_lock_irqsave(&dev->lock, flags);

	P2DEBUG("enter ms_ep_complete current status =%d / request status = %d\n", CurrentState, status);
	switch (status) {
	case 0:
		/* Normal Completion */
		ep_normal_completion(ep, req);
		break;

	case -ENODATA:
		/* DMA transfer fails	*/
		P2ERROR("DMA fail:%s gone (%d), %d/%d.  Either the host is gone "
				"or you got the endpoints backwards(%d)\n",
				ep->name, status, req->actual, req->length, CurrentState);
		whichReturn = ep_reset_state();
		if (whichReturn) {
			ms_req = kzalloc(sizeof *ms_req, GFP_KERNEL);
			if( ms_req ){
				memset(ms_req, 0, sizeof *ms_req);
				ep_set_state(ms_req, (whichReturn | P2USB_ERR_BIT), 0, DMA_TRANSFER_ERROR, req->actual);
				list_add_tail(&ms_req->list_ms_dev, &todo_list);
				P2ERROR("Endpoint request completion error (ERR2)\n");
			}else {
				P2ERROR("Couldn't not allocate StatusQueue (ERR2)\n");
				StatusQueueError = whichReturn | P2USB_ERR_BIT; 
			}
			wake_up_interruptible(&WaitQueue);
		}
		break;

	case -ECONNRESET:
	case -ESHUTDOWN:
		/*
		 * This can happen either if the device disappeared or if
		 * an IN transfer was requested when the host was expecting 
		 * an OUT.
		 */
		P2ERROR("%s gone (%d), %d/%d.  Either the host is gone "
			"or you got the endpoints backwards(%d)\n", 
			ep->name, status, req->actual, req->length, CurrentState);

		whichReturn = ep_reset_state();
		if (whichReturn) {
			ms_req = kzalloc(sizeof *ms_req, GFP_KERNEL);
			if( ms_req ){
				memset(ms_req, 0, sizeof *ms_req);
				ep_set_state(ms_req, (whichReturn | P2USB_ERR_BIT), 0, OTHER_TRANSFER_ERROR1, req->actual);
				list_add_tail(&ms_req->list_ms_dev, &todo_list);
				P2ERROR("Endpoint in shutdown or reconnect-reset state \n");
			}else {
				P2ERROR("Couldn't not allocate StatusQueue (ERR1)\n");
				StatusQueueError = P2USB_ERR_BIT;
			}
			wake_up_interruptible(&WaitQueue);
		}
		break;

	case -EOVERFLOW:
	default:
		/*
		 * Unknown status, but it probably means something's wrong.
		 */
		P2ERROR("%s complete (Unknown status) --> %d, %d/%d(%d)\n", ep->name, status,
			req->actual, req->length, CurrentState);

	case -EREMOTEIO:
		whichReturn = ep_reset_state();
		if (whichReturn) {
			ms_req = kzalloc(sizeof *ms_req, GFP_KERNEL);
			if( ms_req ){
				memset(ms_req, 0, sizeof *ms_req);
				ep_set_state(ms_req, (whichReturn | P2USB_ERR_BIT), 0, OTHER_TRANSFER_ERROR2, req->actual);
				list_add_tail(&ms_req->list_ms_dev, &todo_list);
				P2ERROR("Endpoint completion error (ERR2)\n");
			}else {
				P2ERROR("Couldn't not allocate StatusQueue (ERR2)\n");
				StatusQueueError = P2USB_ERR_BIT;
			}
			wake_up_interruptible(&WaitQueue);
		}
		break;
	}
	P2DEBUG("ms_ep_complete() : Going to free %d byte request\n", req->actual);

	if(ep && req && context->free_buffer){
		free_ep_req(ep, req);
	}

	spin_unlock_irqrestore(&dev->lock, flags);
	P2DEBUG("ms_ep_complete() : completed (%d)\n", CurrentState);
}

/*
 * This function is the entry to begin processing CBWs.  It sets up
 * a request (of length CBW_SIZE) and sets the initial state to IDLE.
 * The request is then queued and the function ms_out_ep_complete
 * (pointed to by req->complete) will be called once the data is available.
 */
struct usb_request *ms_out_ep_start(struct usb_ep *ep, int gfp_flags)
{
	struct usb_request *req;
	struct transfer_context *context;
	int status;

	/* Allocate only enough to get one CBW */
	req = alloc_ep_req(ep, CBW_SIZE, (void *)ReceiveCbwBuf);
	if (!req) {
		P2ERROR("Couldn't allocate EP REQ\n");
		return 0;
	}
	P2DEBUG("Going to kmalloc context\n");
	context = kmalloc(sizeof(struct transfer_context), GFP_ATOMIC);
	if(!context) {
		P2ERROR("Couldn't allocate context\n");
		usb_ep_free_request(ep, req);
		return 0;
	}
	context->free_buffer = 1;
	req->context = context;

	status = usb_ep_queue(ep, req, gfp_flags);
	if (status == 0) {
		P2DEBUG("Start %s --> %d\n", ep->name, status);
	} else {
		P2ERROR("ms_out_ep_start : Failed with status = %d\n", status);
		usb_ep_free_request(ep, req);
		return 0;
	}
	return req;
}   

/*
 * This function is called when the device is suspended.
 * It simply changes the state only. (It similar to ms_disconnect)
 */
static void ms_suspend(struct usb_gadget *gadget)
{
	struct ms_dev *dev = get_gadget_data(gadget);
	unsigned long flags;
	struct p2_ms_req *ms_req = NULL;

	spin_lock_irqsave(&dev->lock, flags);
#if 0	/* Modify 2009/03/25	*/
	if (CurrentState == POW_ON) {
		P2ERROR("ms_suspend : Invalid status (POW_ON)\n");
		spin_unlock_irqrestore(&dev->lock, flags);
		return;
	}
#endif
	if (CurrentState == INIT) {
		P2DEBUG("ms_suspend : CurrentState == INIT : return\n");
		spin_unlock_irqrestore(&dev->lock, flags);
		return;
	}

	disable_ms_eps(dev);

	/* clear all queue  */
	clear_queue_info();
	P2DEBUG("ms_suspend : DACT set\n");		
	UsbChangeStatus = P2USB_DACT_BIT;

	ms_req = kzalloc(sizeof *ms_req, GFP_KERNEL);
	if( ms_req ){
		memset(ms_req, 0, sizeof *ms_req);
		list_add_tail(&ms_req->list_ms_dev, &todo_list);
	}else {
		P2ERROR("Couldn't not allocate StatusQueue (ms_disconnect)\n");
		StatusQueueError = P2USB_ERR_BIT;
	}
	/* CurrentState does go INIT, cause disconnects PC  */
	CurrentState = INIT;

	wake_up_interruptible(&WaitQueue);	
	spin_unlock_irqrestore(&dev->lock, flags);
}

/*
 * This function is called when the device is disconnected from the USB.
 * It simply resets the configuration, disabling both endpoints.
 */
static void ms_disconnect(struct usb_gadget *gadget)
{
	struct ms_dev *dev = get_gadget_data(gadget);
	unsigned long flags;
	struct p2_ms_req *ms_req = NULL;

	spin_lock_irqsave(&dev->lock, flags);
#if 0	/* Modify 2009/03/25    */
	if (CurrentState == POW_ON) {
		P2ERROR("ms_disconnect : Invalid status (POW_ON)\n");
		spin_unlock_irqrestore(&dev->lock, flags);
		return;
	}
#endif
	if (CurrentState == INIT) {
		spin_unlock_irqrestore(&dev->lock, flags);
		P2DEBUG("ms_disconnect : CurrentState == INIT : return\n");
		return;
	}

	disable_ms_eps(dev);

	/* clear all queue	*/
	clear_queue_info();
	P2DEBUG("ms_disconnect : DACT set\n");
	UsbChangeStatus = P2USB_DACT_BIT;

	ms_req = kzalloc(sizeof *ms_req, GFP_KERNEL);
	if( ms_req ){
		memset(ms_req, 0, sizeof *ms_req);
		list_add_tail(&ms_req->list_ms_dev, &todo_list);
	}else {
		P2ERROR("Couldn't not allocate StatusQueue (ms_disconnect)\n");
		StatusQueueError = P2USB_ERR_BIT;
	}
	/* CurrentState does go INIT, cause disconnects PC	*/
	CurrentState = INIT;

	CurrentSerialNumber++;
	wake_up_interruptible(&WaitQueue);

	spin_unlock_irqrestore(&dev->lock, flags);

}

/*
 * This function is called when this module is unloaded.
 * It frees the control endpoint's buffer and requests as
 * well as the device structure.
 */
static void ms_unbind(struct usb_gadget *gadget)
{
	struct ms_dev *dev = get_gadget_data(gadget);
	if (dev->req) {
		kfree(dev->req->buf);
		usb_ep_free_request(gadget->ep0, dev->req);
	}
	/* Free memory from device struct	*/
	kfree(dev);
	set_gadget_data(gadget, 0);

	/* clear all queue	*/
	clear_queue_info();
}

/*
 * This function is called when the driver is loaded.
 * It initializes the device structure and allocates a buffer 
 * and request for the control endpoint
 */
static int __init ms_bind(struct usb_gadget *gadget)
{
	struct ms_dev *dev;
    struct usb_ep       *ep;

	dev = kzalloc(sizeof *dev, GFP_KERNEL);
	if (!dev)
		return -ENOMEM;
	memset(dev, 0, sizeof *dev);

	spin_lock_init(&dev->lock);

	dev->gadget = gadget;
	set_gadget_data(gadget, dev);

    /* Find all the endpoints we will use */
	usb_ep_autoconfig_reset(gadget);
	ep = usb_ep_autoconfig(gadget, &fs_bulk_in_desc);
	if (!ep)
		goto autoconf_fail;
	ep->driver_data = dev;
	dev->in_ep = ep;

	ep = usb_ep_autoconfig(gadget, &fs_bulk_out_desc);
	if (!ep)
		goto autoconf_fail;
	ep->driver_data = dev;
	dev->out_ep = ep;

    /* Assume that all endpoint addresses are the same for both speeds */
    hs_bulk_in_desc.bEndpointAddress = fs_bulk_in_desc.bEndpointAddress;
    hs_bulk_out_desc.bEndpointAddress = fs_bulk_out_desc.bEndpointAddress;

	/* Allocate request struct for control transfers */
	dev->req = usb_ep_alloc_request(gadget->ep0, GFP_KERNEL);
	if (!dev->req)
		goto enomem;

	/* Allocate I/O buffer for control transfers */
	dev->req->buf = kmalloc(USB_BUFSIZ, GFP_KERNEL);
	if (!dev->req->buf)
		goto enomem;

	dev->req->complete = ms_setup_complete;
	gadget->ep0->driver_data = dev;

	printk("%s, version: " DRIVER_VERSION "\n", longname);
	return 0;

autoconf_fail:
	P2ERROR("ms_bind : Failed to autoconfigure all endpoints\n");
	ms_unbind(gadget);
	return -ENOTSUPP;

enomem:
	P2ERROR("ms_bind : ENOMEM\n");
	ms_unbind(gadget);
	return -ENOMEM;
}

/*
 * This is the main driver structure.
 * It points to the basic function used by the
 * gadget driver to access this module.
 */
static struct usb_gadget_driver ms_driver = {
	.speed = USB_SPEED_HIGH,
	.function = (char *) longname,
	.bind =			ms_bind,
	.unbind =		ms_unbind,
	.setup =		ms_setup,
	.disconnect =	ms_disconnect,
	.suspend = 		ms_suspend,
	.driver = {
	.name = (char *) shortname,
	.owner      = THIS_MODULE,
		//  release =
		//  suspend =
		//  resume =
	},
};

/*	[SBG-SAV]
	Revised by Panasonic, 2008-02-28
	Following funstions were implemented by NEC-AT 2003-2004
*/
void ms_set_state(
    u16 whichReturn,
    u16 connectStatus,
    u32 returnStatus,
    u32 transferedLength)
{
    StoreGetData.WhichReturn = whichReturn;
    StoreGetData.ConnectStatus = connectStatus;
    StoreGetData.ReturnStatus = returnStatus;
    StoreGetData.TransferedLength = transferedLength;
}

int	p2usb_get_data(struct p2_ms_req *ms_req_current, struct p2_ms_req *ms_req)
{
	int retVal = 0;

	switch (CurrentState) {
	case INIT:
		if (!list_empty(todo_list.next)) {
			ms_req_current = list_entry(todo_list.next, struct p2_ms_req, list_ms_dev);
			ms_set_state(ms_req_current->WhichReturn, UsbChangeStatus,
				ms_req_current->ReturnStatus, ms_req_current->TransferedLength);
			list_del_init(&ms_req_current->list_ms_dev);
			kfree(ms_req_current);
		}else {
			ms_set_state(StatusQueueError, UsbChangeStatus, QUEUE_STATE_ERROR, 0);
			StatusQueueError = 0;
		}
		UsbChangeStatus = 0;
		break;

	case CBW_READY:
		if (!list_empty(todo_list.next)) {
			ms_req_current = list_entry(todo_list.next, struct p2_ms_req, list_ms_dev);
			ms_set_state(ms_req_current->WhichReturn, (P2USB_ACT_BIT | UsbChangeStatus),
				ms_req_current->ReturnStatus, ms_req_current->TransferedLength);
			list_del_init(&ms_req_current->list_ms_dev);
			kfree(ms_req_current);
		}else {
			ms_set_state(StatusQueueError, (P2USB_ACT_BIT | UsbChangeStatus), QUEUE_STATE_ERROR, 0);
			StatusQueueError = 0;
		}
		UsbChangeStatus = 0;
		break;

	case CBW_DONE:
		if (!list_empty(todo_list.next)) {
			ms_req_current = list_entry(todo_list.next, struct p2_ms_req, list_ms_dev);
			ms_set_state(ms_req_current->WhichReturn, (P2USB_ACT_BIT | UsbChangeStatus),
				ms_req_current->ReturnStatus, ms_req_current->TransferedLength);
			UsbChangeStatus = 0;
			if (StoreGetData.WhichReturn & P2USB_CBW_BIT) {
				CurrentState = WAIT_NEXT;
				StoreGetData.SerialNumber = CurrentSerialNumber;
			}

			list_del_init(&ms_req_current->list_ms_dev);
			kfree(ms_req_current);
		}else {
			P2ERROR("No queue found\n");
			retVal = -EFAULT;
		}
		break;

	case CMD_READY:
	case SET_READY_DATA:
	case SET_READY_CSW:
	case WAIT_NEXT:
		if (!list_empty(todo_list.next)) {
			P2ERROR("Fault in queue\n");
			retVal = -EFAULT;
		}else {
			ms_set_state(StatusQueueError, (P2USB_ACT_BIT | UsbChangeStatus), QUEUE_STATE_ERROR, 0);
			UsbChangeStatus = 0;
			StatusQueueError = 0;
		}
		break;

	case CMD_DONE:
		if (!list_empty(todo_list.next)) {
			ms_req_current = list_entry(todo_list.next, struct p2_ms_req, list_ms_dev);
			ms_set_state(ms_req_current->WhichReturn, (P2USB_ACT_BIT | UsbChangeStatus),
				ms_req_current->ReturnStatus, ms_req_current->TransferedLength);
			UsbChangeStatus = 0;
			CurrentState = WAIT_NEXT;

			list_del_init(&ms_req_current->list_ms_dev);
			kfree(ms_req_current);
		}else {
			P2ERROR("No queue found (CMD_DONE)\n");
			retVal = -EFAULT;
		}
		break;

	case POW_ON:
		P2ERROR("Not suppoted : POW_ON\n");
		retVal = -EPERM;
		break;

	default:
		P2ERROR("Invalid state(ioctl)\n");
		retVal = -EPERM;
		break;
	}
	return retVal;
}

int p2usb_set_data(struct p2_ms_req *ms_req_current, struct p2_ms_req *ms_req)
{
	struct usb_request *req;
	int retVal = 0;
    int status;
    struct transfer_context *context;
	u32 t_val;

	if ( !(StoreSetData.SetDataTransferInfo & P2USB_DO_TRANSFER) &&
		!(StoreSetData.SetDataTransferInfo & P2USB_STALL) ) {
		P2DEBUG("SET_DATA : send only CSW \n");

		t_val = cpu_to_le32(StoreSetData.CswData.signature);
		StoreSetData.CswData.signature = t_val;
		t_val = cpu_to_le32(StoreSetData.CswData.data_residue);
		StoreSetData.CswData.data_residue = t_val;

		memcpy(SendCswBuf, &StoreSetData.CswData, CSW_SIZE);
		req = alloc_ep_req(StoreDev->in_ep, CSW_SIZE, (void *)SendCswBuf);
		if (req) {
			P2DEBUG("SEND_CSW : Going to kmalloc context\n");
			context = kmalloc(sizeof(struct transfer_context), GFP_ATOMIC);
			if(!context) {
				P2ERROR("SEND_CSW failed : Couldn't allocate context\n");
				usb_ep_free_request(StoreDev->in_ep, req);
				retVal = -ENOMEM;
			}else {
				context->free_buffer = 1;
				req->context = context;
				PreviousState = CurrentState;
				CurrentState = SET_READY_CSW;
				status = usb_ep_queue(StoreDev->in_ep, req, GFP_ATOMIC);
				if (status == 0) {
					P2DEBUG("SEND_CSW : Start %s --> %d\n", StoreDev->in_ep->name, status);
				} else {
					CurrentState = PreviousState;
					P2ERROR("SEND_CSW failed : usb_ep_queue() failed with status = %d\n", status);
					usb_ep_free_request(StoreDev->in_ep, req);
					retVal = -EFAULT;
				}
			}
		}else {
			P2ERROR("SEND_CSW failed : alloc_ep_req failed\n");
			retVal = -ENOMEM;
		}

	}else if (  (StoreSetData.SetDataTransferInfo & P2USB_DO_TRANSFER) &&
				!(StoreSetData.SetDataTransferInfo & P2USB_STALL) ) {
		P2DEBUG("SET_DATA : send DATA & CSW \n");
		req = alloc_ep_req(StoreDev->in_ep, StoreSetData.TransferLength,
									(void *)StoreSetData.TransferAddress);
		if (req) {
			P2DEBUG("SEND_DATA_CSW : Going to kmalloc context\n");
			context = kmalloc(sizeof(struct transfer_context), GFP_ATOMIC);
			if(!context) {
				P2ERROR("SEND_DATA_CSW : Couldn't allocate context\n");
				usb_ep_free_request(StoreDev->in_ep, req);
				retVal = -ENOMEM;
			}else {
				context->free_buffer = 1;
				req->context = context;
				PreviousState = CurrentState;
				CurrentState = SET_READY_DATA;
				status = usb_ep_queue(StoreDev->in_ep, req, GFP_ATOMIC);
				if (status == 0) {
					P2DEBUG("SEND_DATA_CSW : Start %s --> %d\n", StoreDev->in_ep->name, status);
				} else {
					CurrentState = PreviousState;
					P2ERROR("SEND_DATA_CSW : usb_ep_queue() failed with status = %d\n", status);
					usb_ep_free_request(StoreDev->in_ep, req);
					retVal = -EFAULT;
				}
			}
		}else {
			P2ERROR("SEND_DATA_CSW : Couldn't allocate req\n");
			retVal = -ENOMEM;
		}

	}else if ( !(StoreSetData.SetDataTransferInfo & P2USB_DO_TRANSFER) &&
				(StoreSetData.SetDataTransferInfo & P2USB_STALL) ) {
		P2DEBUG("SEND_STALL_CSW : Send STALL & CSW\n");
		usb_ep_set_halt(StoreDev->in_ep);

		t_val = cpu_to_le32(StoreSetData.CswData.signature);
		StoreSetData.CswData.signature = t_val;
		t_val = cpu_to_le32(StoreSetData.CswData.data_residue);
		StoreSetData.CswData.data_residue = t_val;

		memcpy(SendCswBuf, &StoreSetData.CswData, CSW_SIZE);
		req = alloc_ep_req(StoreDev->in_ep, CSW_SIZE, (void *)SendCswBuf);
		if (req) {
			P2DEBUG("SEND_STALL_CSW : Going to kmalloc context\n");
			context = kmalloc(sizeof(struct transfer_context), GFP_ATOMIC);
			if(!context) {
				P2ERROR("SEND_STALL_CSW : Couldn't allocate context\n");
				usb_ep_free_request(StoreDev->in_ep, req);
				retVal = -ENOMEM;
			}else {
				context->free_buffer = 1;
				req->context = context;
				PreviousState = CurrentState;
				CurrentState = SET_READY_CSW;
				status = usb_ep_queue(StoreDev->in_ep, req, GFP_ATOMIC);
				if (status == 0) {
					P2DEBUG("SEND_STALL_CSW : Start %s --> %d\n", StoreDev->in_ep->name, status);
				} else {
					CurrentState = PreviousState;
					P2ERROR("SEND_STALL_CSW : usb_ep_queue() failed with status=%d\n", status);
					usb_ep_free_request(StoreDev->in_ep, req);
					retVal = -EFAULT;
				}
			}
		}else {
			P2ERROR("SEND_STALL_CSW : Couldn't allocate req\n");
			retVal = -ENOMEM;
		}

	}else if (  (StoreSetData.SetDataTransferInfo & P2USB_DO_TRANSFER) &&
				(StoreSetData.SetDataTransferInfo & P2USB_STALL) ) {
		P2DEBUG("SEND_STALL_CSW : Send DATA & STALL & CSW = (ame as send DATA and CSW)\n");
		req = alloc_ep_req(StoreDev->in_ep, StoreSetData.TransferLength,
						(void *)StoreSetData.TransferAddress);
		if (req) {
			P2DEBUG("SEND_DATA_STALL_CSW : Going to kmalloc context\n");
			context = kmalloc(sizeof(struct transfer_context), GFP_ATOMIC);
			if(!context) {
				P2ERROR("SEND_DATA_STALL_CSW : Couldn't allocate context\n");
				usb_ep_free_request(StoreDev->in_ep, req);
				retVal = -ENOMEM;
			}else {
				context->free_buffer = 1;
				req->context = context;
				PreviousState = CurrentState;
				CurrentState = SET_READY_DATA;
				status = usb_ep_queue(StoreDev->in_ep, req, GFP_ATOMIC);
				if (status == 0) {
					P2DEBUG("SEND_DATA_STALL_CSW : Start %s --> %d\n", StoreDev->in_ep->name, status);
				} else {
					CurrentState = PreviousState;
					P2ERROR("SEND_DATA_STALL_CSW : usb_ep_queue() failed with status=%d\n", status);
					usb_ep_free_request(StoreDev->in_ep, req);
					retVal = -EFAULT;
				}
			}
		}else {
			P2ERROR("SEND_DATA_STALL_CSW : Couldn't allocate req\n");
			retVal = -ENOMEM;
		}
	}else {
		P2ERROR("SET_DATA : Unknown state : May be never happen\n");
		retVal = -EACCES;
	}
	return retVal;
}

static int ms_open(struct inode *inode, struct file *file)
{
	/* Initialize CurrentState	*/
	CurrentState = POW_ON;
	return 0;
}

static int ms_release(struct inode *inode, struct file *file)
{
	return 0;
}

static int ms_ioctl(struct inode *inode, struct file *file,
                        unsigned int cmd, unsigned long arg)
{
	int retVal = 0;
	struct usb_request *req;
	int status;
	struct transfer_context *context;
	unsigned long flags;

	struct p2_ms_req *ms_req_current = NULL;
	struct p2_ms_req *ms_req = NULL;

	spin_lock_irqsave(&StoreDev->lock, flags);

	P2DEBUG("Enter ms_ioctl : Command = %d\n", cmd);
	switch(cmd) {
	case P2USB_GET_DATA:
		P2DEBUG("GET_DATA(%d)\n", CurrentState);
		if (access_ok(VERIFY_WRITE, (void *)arg, sizeof(P2USB_GET_DATA_STRUCT))) {
			/* Return StatusQueue & CBW data	*/
			retVal = p2usb_get_data(ms_req_current, ms_req);
			if (retVal == 0 && copy_to_user((void *)arg, &StoreGetData, sizeof(P2USB_GET_DATA_STRUCT))) {
				P2ERROR("Failed getting CBW data ()\n");
				retVal = -EFAULT;
			}
		}else {
			P2ERROR("Unknown error : Failed getting data\n");
			retVal = -EACCES;
		}
		break;

	case P2USB_SET_DATA:
		P2DEBUG("SET_DATA(%d)\n", CurrentState);
		if (CurrentState == WAIT_NEXT) {
			if (access_ok(VERIFY_READ, (void *)arg, sizeof(P2USB_SET_DATA_STRUCT))) {
				if (copy_from_user(&StoreSetData, (void *)arg, sizeof(P2USB_SET_DATA_STRUCT))) {
					P2ERROR("SET_DATA failed : copy_from_user()\n");
					retVal = -EFAULT;
					break;
				}
				if (StoreSetData.SerialNumber != CurrentSerialNumber) {
					P2ERROR("SET_DATA failed : SerialNumber erro (%08X, %08X)\n", StoreSetData.SerialNumber, CurrentSerialNumber);
					retVal = -EFAULT;
					break;
				}

				retVal = p2usb_set_data(ms_req_current, ms_req);
				if(retVal != 0){
					P2ERROR("SET_DATA failed : p2usb_set_data() error %d\n", retVal);
					retVal = -EFAULT;
				}	
			}else {
				P2ERROR("SET_DATA : Failed SET_DATA\n");
				retVal = -EACCES;
			}
		}else {
			P2ERROR("SET_DATA : Failed SET_DATA : CurrentState != WAIT_NEXT  [CurrentState = %d]\n", CurrentState);
			retVal = -EPERM;
		}
		break;

	case P2USB_COMMAND_STATUS:

		P2DEBUG("COMMAND_STATUS(%d)\n", CurrentState);
		if (CurrentState == WAIT_NEXT) {
			if (access_ok(VERIFY_READ, (void *)arg, sizeof(P2USB_COMMAND_STATUS_STRUCT))) {
				if (copy_from_user(&StoreCommandStatus, (void *)arg, sizeof(P2USB_COMMAND_STATUS_STRUCT))) {
					P2ERROR("COMMAND_STATUS : Failed COMMAND_STATUS\n");
					retVal = -EFAULT;
					goto P2USB_COMMAND_STATUS_ERROR;
				}
				if (StoreCommandStatus.SerialNumber != CurrentSerialNumber) {
					P2ERROR("COMMAND_STATUS failed : Error in SerialNumber(%08X, %08X)\n",
					StoreCommandStatus.SerialNumber,
					CurrentSerialNumber);
					retVal = -EFAULT;
					goto P2USB_COMMAND_STATUS_ERROR;
				}

				if (StoreCommandStatus.CommandStatusTransferInfo & P2USB_STALL) {
					P2ERROR("COMMAND_STATUS failed : Set STALL\n");
					PreviousState = CurrentState;
					CurrentState = CMD_DONE;
					usb_ep_set_halt(StoreDev->out_ep);
					ms_req = kzalloc(sizeof *ms_req, GFP_KERNEL);
					if( ms_req ){
						memset(ms_req, 0, sizeof *ms_req);
						ep_set_state(ms_req, P2USB_CMD_BIT, 0, 0, 0);
						list_add_tail(&ms_req->list_ms_dev, &todo_list);
						P2ERROR("COMMAND_STATUS failed : (COMMAND_STATUS STALL)\n");
					}else {
						CurrentState = PreviousState;
						P2ERROR("Couldn't not allocate StatusQueue (P2USB_COMMAND_STATUS)\n");
						retVal = -ENOMEM;
					}
					wake_up_interruptible(&WaitQueue);
				}else {
					if ( (StoreCommandStatus.DmaInfo.Boundary == 0x00) ||
						(StoreCommandStatus.DmaInfo.Boundary == 0x01) ) {
						req = alloc_ep_req(StoreDev->out_ep, StoreCommandStatus.DmaInfo.TransferLength0,
								(void *)StoreCommandStatus.DmaInfo.TransferAddress0);
					}else {
						P2ERROR("COMMAND_STATUS failed : Boundary invalid\n");
						retVal = -EINVAL;
						goto P2USB_COMMAND_STATUS_ERROR;
					}
					if (req) {
						P2DEBUG("COMMAND_STATUS : Going to kmalloc context\n");
						context = kmalloc(sizeof(struct transfer_context), GFP_ATOMIC);
						if(!context) {
							P2ERROR("COMMAND_STATUS failed : Couldn't allocate context\n");
							usb_ep_free_request(StoreDev->out_ep, req);
							retVal = -ENOMEM;
						}else {
							context->free_buffer = 1;
							req->context = context;
							PreviousState = CurrentState;
							CurrentState = CMD_READY;
							status = usb_ep_queue(StoreDev->out_ep, req, GFP_ATOMIC);
							if (status == 0) {
								P2DEBUG("COMMAND_STATUS : Start %s --> %d\n", StoreDev->out_ep->name, status);
							} else {
								CurrentState = PreviousState;
								P2ERROR("COMMAND_STATUS failed : usb_ep_queue() failed with status=%d\n", status);
								usb_ep_free_request(StoreDev->out_ep, req);
								retVal = -EFAULT;
							}
						}
					}else {
						P2ERROR("COMMAND_STATUS failed : alloc_ep_req fail\n");
						retVal = -ENOMEM;
					}
				}
			}else {
				P2ERROR("COMMAND_STATUS failed :  Failed COMMAND_STATUS\n");
				retVal = -EACCES;
			}

		}else {
			P2ERROR("COMMAND_STATUS failed : Failed COMMAND_STATUS\n");
			retVal = -EPERM;
		}
P2USB_COMMAND_STATUS_ERROR:
		break;

	case P2USB_SET_STATE:
		P2DEBUG("SET_STATE(%d)\n", CurrentState);
		if (arg == P2USB_DISABLE_USB_MODE) {
			if (CurrentState != POW_ON) {
				retVal = usb_gadget_unregister_driver(&ms_driver);
				if (retVal == 0) {
					P2DEBUG("Set P2USB_DISABLE_USB_MODE (%d)\n", CurrentState);
					/* clear all queue	*/
					clear_queue_info();
					if (CurrentState != INIT){
						CurrentSerialNumber++;
					}
					CurrentState = POW_ON;
				}else {
					P2ERROR("usb_gadget_unregister_driver failed (%d)\n", retVal);
					retVal = -ENODEV;
				}
			}else {
				P2ERROR("State error was occured (%d) : DISABLE_USB\n", CurrentState);
				retVal = -EPERM;
			}
		}else if (arg == P2USB_ENABLE_USB_MODE) {

			P2DEBUG("Set P2USB_ENABLE_USB_MODE (%d)\n", CurrentState);
			/* clear all queue  */
			clear_queue_info();
			CurrentState = INIT;
#if 0	
/* The usb_gadget_register_driver() call back cause unreliable state.	*/
/* The kernel cound not handlekernel paging request?					*/
/* Then we call usb_gadget_register_driver() from init() function		*/ 
			if (CurrentState == POW_ON) {
				retVal = usb_gadget_register_driver(&ms_driver);
				if (retVal == 0) {
					P2DEBUG("Set P2USB_ENABLE_USB_MODE (%d)\n", CurrentState);
					/* clear all queue	*/
					clear_queue_info();
					CurrentState = INIT;
				}else {
					P2ERROR("usb_gadget_register_driver failed (%d)\n", retVal);
					retVal = -ENODEV;
				}
			}else {
				P2ERROR("State error was occured (%d) : ENABLE_USB\n", CurrentState);
				retVal = -EPERM;
			}
#endif
		}else {
			P2ERROR("Unknown argument : neither set or clear\n");
			retVal = -EINVAL;
		}
		break;

	case P2USB_SET_SERIAL:		/* Support High-end shoulder & mobiles	*/
		P2DEBUG("SET_SERIAL(%d)\n", CurrentState);
		if (CurrentState == POW_ON) {
			int len_serial;
			P2USB_SET_SERIAL_STRUCT SerialData;
			if (copy_from_user(&SerialData, (void *)arg, sizeof(P2USB_SET_SERIAL_STRUCT))) {
				P2ERROR("Copy USB serial ID from user space failed\n");
				retVal = -EFAULT;
				goto P2USB_SET_SERIAL_ERROR;
			}
			len_serial = ( SerialData.len > ( P2USB_MAX_SERIAL - 1 ) ) ? ( P2USB_MAX_SERIAL - 1 ) : SerialData.len;
			memset(serial,0,P2USB_MAX_SERIAL);
			if (copy_from_user(serial, (void *)SerialData.str, len_serial)){
				P2ERROR("Copy USB serial string from user space failed\n");
				retVal = -EFAULT;
				goto P2USB_SET_SERIAL_ERROR;
			}
		}else {
			P2ERROR("Set Serial command failed due to current state is !POW_ON\n");
			retVal = -EACCES;
			goto P2USB_SET_SERIAL_ERROR;
		}
		P2DEBUG("SET_SERIAL Exit(%d/%d)\n", retVal, CurrentState);
P2USB_SET_SERIAL_ERROR:
		break;

	case P2USB_SET_PRODUCT_ID:	/* Support High-end shoulder & mobiles  */
		P2DEBUG("SET_PRODUCT_ID(%d)\n", CurrentState);
		if (CurrentState == POW_ON) {
			device_desc.idProduct = (__u16)arg;
		}else {
			P2ERROR("Set Product ID failed due to current state is !POW_ON\n");
			retVal = -EACCES;
			goto P2USB_SET_PRODUCT_ID_ERROR;
		}
		P2DEBUG("SET_PRODUCT_ID Exit(%d/%d)\n", retVal, CurrentState);
P2USB_SET_PRODUCT_ID_ERROR:
		break;

	default:
		P2ERROR("Invalid ioctl command (%d)\n", CurrentState);
		retVal = -EINVAL;
	}

	P2DEBUG("Exit ms_ioctl : Command = %d completed with status = %d\n", cmd, retVal);
	spin_unlock_irqrestore(&StoreDev->lock, flags);
	return retVal;
}

static unsigned int ms_poll(struct file *flip, poll_table *wait)
{
	unsigned int mask = 0;

	poll_wait(flip, &WaitQueue, wait);

	if (!list_empty(todo_list.next)) {
		mask |= POLLIN | POLLRDNORM;
	}

	return mask;
}

static struct file_operations ms_fops = {
	owner:   THIS_MODULE,
	open:    ms_open,
	release: ms_release,
	ioctl:   ms_ioctl,
	poll:    ms_poll,
};

/*
 * Set up module information
 */
MODULE_AUTHOR("NetChip Technology, Inc (http://www.netchip.com)");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Mass Storage Gadget (P2)");

/*
 * Module entry point.  Called when the
 * module is loaded
 */
static int __init init(void)
{
	int retVal;
	int dev_num = MKDEV( g_ms_major, 0 );

	/* initialize list head		*/
	INIT_LIST_HEAD (&todo_list);

	/* Initialize CurrentState	*/
	CurrentState = POW_ON;

	/* You should change this to match a real serial number */
	strncpy(serial, "001", sizeof serial);
	serial[sizeof serial - 1] = 0;

	/* Register character device */
	retVal = register_chrdev_region( dev_num, 1, "g_ms" );
	if( retVal ){
		P2ERROR("register_chardev_region() error\n");
		return -ENODEV;
	}

	cdev_init( &p2_ms_dev, &ms_fops );
	p2_ms_dev.owner = THIS_MODULE;

	retVal = cdev_add( &p2_ms_dev, dev_num, 1 );
	if (retVal < 0) {
		P2ERROR("cdev_add() error\n");
		unregister_chrdev_region( dev_num, 1 );
		return -ENODEV;
	}

	ReceiveCbwBuf = kmalloc(CBW_SIZE, GFP_ATOMIC);
	if (!ReceiveCbwBuf) {
		P2ERROR("CBW memory could not allocated\n");
		unregister_chrdev_region( dev_num, 1 );
		cdev_del( &p2_ms_dev );
		unregister_chrdev_region( dev_num, 1 );
		return -ENOMEM;
	}

	SendCswBuf = kmalloc(CSW_SIZE, GFP_ATOMIC);
	if (!SendCswBuf) {
		kfree(ReceiveCbwBuf);
		P2ERROR("CSW memory could not allocated\n");
		cdev_del( &p2_ms_dev );
		unregister_chrdev_region( dev_num, 1 );
		return -ENOMEM;
	}

	retVal = usb_gadget_register_driver(&ms_driver);
	if (retVal < 0) {
		P2ERROR("usb_gadget_register_driver() failed\n");
		kfree(ReceiveCbwBuf);
		kfree(SendCswBuf);
		cdev_del( &p2_ms_dev );
		unregister_chrdev_region( dev_num, 1 );
		return -ENODEV;
	}

	return 0;
}
module_init(init);

/*
 * Called when module is unloaded
 */
static void __exit cleanup(void)
{
	/* Remove /proc/drivers/ms */
	int dev_num = MKDEV( g_ms_major, 0 );

	/* Do nothing for proc	*/

	/* Tell driver of link controller that we're going away */
	usb_gadget_unregister_driver(&ms_driver);

	if (ReceiveCbwBuf)
		kfree(ReceiveCbwBuf);

	cdev_del( &p2_ms_dev );
	unregister_chrdev_region( dev_num, 1 );

	/* clear all queue	*/
	clear_queue_info();
}
module_exit(cleanup);
