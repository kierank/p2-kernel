/*
 * xHCI host controller driver
 *
 * Copyright (C) 2008 Intel Corp.
 *
 * Author: Sarah Sharp
 * Some code borrowed from the Linux EHCI driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
/* $Id: xhci-hub.c 12700 2011-02-24 00:39:25Z Noguchi Isao $ */

#include <asm/unaligned.h>

#include "xhci.h"

/* 2010/8/3, added by Panasonic ---> */
/*
 *  Check port speed of xHCI root hub from xHCI Supported Protocol
 *  Capability information
 *
 *  @param  port    port number (1 ~ bNbrPorts)
 *  @return 0: NON-SS, >0: SS, <0: error(not found port infomation)
 */
static int xhci_hub_check_port_speed(struct xhci_hcd *xhci,int port)
{
    struct list_head *ptr;
    list_for_each(ptr,&xhci->ext_cap_protocol_list){
        struct xhci_ext_cap_protocol *entry = list_entry(ptr,struct xhci_ext_cap_protocol, list);
        struct xhci_ext_cap_protocol_info *info = &entry->info;
        if(port>=info->port_offset && port < (info->port_offset+info->port_count))
            return (info->rev_major==0x03)?1:0;
    }
    return -ENODEV;
}
/* <--- 2010/8/3, added by Panasonic */

static void xhci_hub_descriptor(struct xhci_hcd *xhci,
		struct usb_hub_descriptor *desc)
{
	int ports;
	u16 temp;

	ports = HCS_MAX_PORTS(xhci->hcs_params1);

	/* USB 3.0 hubs have a different descriptor, but we fake this for now */
	desc->bDescriptorType = 0x29;
	desc->bPwrOn2PwrGood = 10;	/* xhci section 5.4.9 says 20ms max */
	desc->bHubContrCurrent = 0;

	desc->bNbrPorts = ports;
	temp = 1 + (ports / 8);
	desc->bDescLength = 7 + 2 * temp;

	/* Why does core/hcd.h define bitmap?  It's just confusing. */
	memset(&desc->DeviceRemovable[0], 0, temp);
	memset(&desc->DeviceRemovable[temp], 0xff, temp);

	/* Ugh, these should be #defines, FIXME */
	/* Using table 11-13 in USB 2.0 spec. */
	temp = 0;
	/* Bits 1:0 - support port power switching, or power always on */
	if (HCC_PPC(xhci->hcc_params))
		temp |= 0x0001;
	else
		temp |= 0x0002;
	/* Bit  2 - root hubs are not part of a compound device */
	/* Bits 4:3 - individual port over current protection */
	temp |= 0x0008;
	/* Bits 6:5 - no TTs in root ports */
	/* Bit  7 - no port indicators */
	desc->wHubCharacteristics = (__force __u16) cpu_to_le16(temp);
}

/* 2010/8/3, modifiedd by Panasonic */
/* static unsigned int xhci_port_speed(unsigned int port_status) */
static unsigned int xhci_port_speed(unsigned int port_status, int ss_port)
{
/* 2010/8/3, modifiedd by Panasonic ---> */
/* 	if (DEV_LOWSPEED(port_status)) */
/* 		return 1 << USB_PORT_FEAT_LOWSPEED; */
/* 	if (DEV_HIGHSPEED(port_status)) */
/* 		return 1 << USB_PORT_FEAT_HIGHSPEED; */
/* 	if (DEV_SUPERSPEED(port_status)) */
/* 		return 1 << USB_PORT_FEAT_SUPERSPEED; */
/* 	/\* */
/* 	 * FIXME: Yes, we should check for full speed, but the core uses that as */
/* 	 * a default in portspeed() in usb/core/hub.c (which is the only place */
/* 	 * USB_PORT_FEAT_*SPEED is used). */
/* 	 *\/ */
    if(ss_port){                /* SuperSpeed port */
        if (DEV_SUPERSPEED(port_status))
            return 1 << USB_PORT_FEAT_SUPERSPEED;
    } else {                    /* NON-SuperSpeed port */
        if (DEV_LOWSPEED(port_status))
            return 1 << USB_PORT_FEAT_LOWSPEED;
        if (DEV_HIGHSPEED(port_status))
            return 1 << USB_PORT_FEAT_HIGHSPEED;
	/*
	 * FIXME: Yes, we should check for full speed, but the core uses that as
	 * a default in portspeed() in usb/core/hub.c (which is the only place
	 * USB_PORT_FEAT_*SPEED is used).
	 */
    }
/* <--- 2010/8/3, modifiedd by Panasonic */

	return 0;
}

/*
 * These bits are Read Only (RO) and should be saved and written to the
 * registers: 0, 3, 10:13, 30
 * connect status, over-current status, port speed, and device removable.
 * connect status and port speed are also sticky - meaning they're in
 * the AUX well and they aren't changed by a hot, warm, or cold reset.
 */
#define	XHCI_PORT_RO	((1<<0) | (1<<3) | (0xf<<10) | (1<<30))
/*
 * These bits are RW; writing a 0 clears the bit, writing a 1 sets the bit:
 * bits 5:8, 9, 14:15, 25:27
 * link state, port power, port indicator state, "wake on" enable state
 */
#define XHCI_PORT_RWS	((0xf<<5) | (1<<9) | (0x3<<14) | (0x7<<25))
/*
 * These bits are RW; writing a 1 sets the bit, writing a 0 has no effect:
 * bit 4 (port reset)
 */
#define	XHCI_PORT_RW1S	((1<<4))
/*
 * These bits are RW; writing a 1 clears the bit, writing a 0 has no effect:
 * bits 1, 17, 18, 19, 20, 21, 22, 23
 * port enable/disable, and
 * change bits: connect, PED, warm port reset changed (reserved zero for USB 2.0 ports),
 * over-current, reset, link state, and L1 change
 */
#define XHCI_PORT_RW1CS	((1<<1) | (0x7f<<17))
/*
 * Bit 16 is RW, and writing a '1' to it causes the link state control to be
 * latched in
 */
#define	XHCI_PORT_RW	((1<<16))
/*
 * These bits are Reserved Zero (RsvdZ) and zero should be written to them:
 * bits 2, 24, 28:31
 */
#define	XHCI_PORT_RZ	((1<<2) | (1<<24) | (0xf<<28))

/*
 * Given a port state, this function returns a value that would result in the
 * port being in the same state, if the value was written to the port status
 * control register.
 * Save Read Only (RO) bits and save read/write bits where
 * writing a 0 clears the bit and writing a 1 sets the bit (RWS).
 * For all other types (RW1S, RW1CS, RW, and RZ), writing a '0' has no effect.
 */
static u32 xhci_port_state_to_neutral(u32 state)
{
	/* Save read-only status and port state */
	return (state & XHCI_PORT_RO) | (state & XHCI_PORT_RWS);
}

int xhci_hub_control(struct usb_hcd *hcd, u16 typeReq, u16 wValue,
		u16 wIndex, char *buf, u16 wLength)
{
	struct xhci_hcd	*xhci = hcd_to_xhci(hcd);
	int ports;
	unsigned long flags;
	u32 portsc, portpmsc, status;
	int retval = 0;
	u32 __iomem *addr;
	char *port_change_bit;
    unsigned        selector;
    int ss_port;

	ports = HCS_MAX_PORTS(xhci->hcs_params1);

	spin_lock_irqsave(&xhci->lock, flags);
	switch (typeReq) {
	case GetHubStatus:
		/* No power source, over-current reported per port */
		memset(buf, 0, 4);
		break;
	case GetHubDescriptor:
		xhci_hub_descriptor(xhci, (struct usb_hub_descriptor *) buf);
		break;
	case GetPortStatus:
		if (!wIndex || wIndex > ports)
			goto error;
/* 2010/8/3, added by Panasonic ---> */
        ss_port = xhci_hub_check_port_speed(xhci,wIndex);
        if(ss_port<0)
            goto error;
/* <--- 2010/8/3, added by Panasonic */
		wIndex--;
		status = 0;
		addr = &xhci->op_regs->port_status_base + NUM_PORT_REGS*(wIndex & 0xff);
		portsc = xhci_readl(xhci, addr);
		xhci_dbg(xhci, "get port status, actual port %d status  = 0x%x\n", wIndex, portsc);

		/* wPortChange bits */
		if (portsc & PORT_CSC)
			status |= 1 << USB_PORT_FEAT_C_CONNECTION;
		if (portsc & PORT_PEC)
			status |= 1 << USB_PORT_FEAT_C_ENABLE;
		if ((portsc & PORT_OCC))
			status |= 1 << USB_PORT_FEAT_C_OVER_CURRENT;
		/*
		 * FIXME ignoring suspend, reset, and USB 2.1/3.0 specific
		 * changes
		 */
		if (portsc & PORT_CONNECT) {
			status |= 1 << USB_PORT_FEAT_CONNECTION;
            /* 2010/8/3, modifiedd by Panasonic  */
/* 			status |= xhci_port_speed(temp); */
			status |= xhci_port_speed(portsc,ss_port);
		}
		if (portsc & PORT_PE)
			status |= 1 << USB_PORT_FEAT_ENABLE;
		if (portsc & PORT_OC)
			status |= 1 << USB_PORT_FEAT_OVER_CURRENT;
		if (portsc & PORT_RESET)
			status |= 1 << USB_PORT_FEAT_RESET;
		if (portsc & PORT_POWER)
			status |= 1 << USB_PORT_FEAT_POWER;
/* 2010/8/3, added by Panasonic ---> */
        if(!ss_port && ((portsc&PORT_PLS_MASK)==PORT_PLS_TEST_MODE))
           status |= 1<< 11;
/* <--- 2010/8/3, added by Panasonic */
		xhci_dbg(xhci, "Get port status returned 0x%x\n", status);
		put_unaligned(cpu_to_le32(status), (__le32 *) buf);
		break;
	case SetPortFeature:
        selector = wIndex >> 8; /* 2010/8/3, added by Panasonic */
		wIndex &= 0xff;
		if (!wIndex || wIndex > ports)
			goto error;
/* 2010/8/3, added by Panasonic ---> */
        ss_port = xhci_hub_check_port_speed(xhci,wIndex);
        if(ss_port<0)
            goto error;
/* <--- 2010/8/3, added by Panasonic */
		wIndex--;
		addr = &xhci->op_regs->port_status_base + NUM_PORT_REGS*(wIndex & 0xff);
		portsc = xhci_readl(xhci, addr);
		portsc = xhci_port_state_to_neutral(portsc);
		portpmsc = xhci_readl(xhci, addr+1); /* 2010/8/3, added by Panasonic */
		switch (wValue) {
		case USB_PORT_FEAT_POWER:
			/*
			 * Turn on ports, even if there isn't per-port switching.
			 * HC will report connect events even before this is set.
			 * However, khubd will ignore the roothub events until
			 * the roothub is registered.
			 */
			xhci_writel(xhci, portsc | PORT_POWER, addr);

			portsc = xhci_readl(xhci, addr);
			xhci_dbg(xhci, "set port power, actual port %d status  = 0x%x\n", wIndex, portsc);
			break;
		case USB_PORT_FEAT_RESET:
			portsc = (portsc | PORT_RESET);
			xhci_writel(xhci, portsc, addr);

			portsc = xhci_readl(xhci, addr);
			xhci_dbg(xhci, "set port reset, actual port %d status  = 0x%x\n", wIndex, portsc);
			break;
/* 2010/8/3, added by Panasonic (SAV),
   2010/12/16, modifiedd by Panasonic (SAV) ---> */
        case USB_PORT_FEAT_TEST:
            if (!selector || selector > 5)
                goto error;
            if(ss_port){
                xhci_warn(xhci,
                          "SetPortFeature(PORT_TEST) is NOT support, becouse port%d is Super Speed\n",
                          wIndex+1);
                goto error;
            }
            if (HCC_PPC (xhci->hcc_params))
                portsc &= ~PORT_POWER;
			xhci_writel(xhci, portsc, addr);
            xhci_quiesce(xhci);
            xhci_halt(xhci);
            portpmsc &= ~PORT_TEST_MODE_MASK;
            portpmsc |= PORT_TEST_MODE(selector);
            xhci_writel(xhci,portpmsc,addr+1);
            if(selector==5){    /* TEST_Force_Enable */
                u32 cmd = xhci_readl(xhci, &xhci->op_regs->command);
                cmd &= ~CMD_RUN;
                xhci_writel(xhci, cmd, &xhci->op_regs->command);
            }

			portsc = xhci_readl(xhci, addr);
			xhci_dbg(xhci, "set port test, actual port %d status = 0x%x\n", wIndex, portsc);
			portpmsc = xhci_readl(xhci, addr+1);
			xhci_dbg(xhci, "set port test, actual port %d portpmsc = 0x%x\n", wIndex, portsc);
            break;
/* <--- 2010/8/3, added by Panasonic (SAV),
   2010/12/16, modifiedd by Panasonic (SAV) */
/* 2010/12/16, added by Panasonic ---> */
        case USB_PORT_FEAT_SUSPEND:
            if(ss_port){
                xhci_warn(xhci,
                          "SetPortFeature(PORT_SUSPEND) is NOT support, becouse port%d is Super Speed\n",
                          wIndex+1);
                goto error;
            }
            portsc &= ~PORT_PLS_MASK;
            portsc |= (PORT_PLS_U3|PORT_LINK_STROBE);
			xhci_writel(xhci, portsc, addr);

			portsc = xhci_readl(xhci, addr);
			xhci_dbg(xhci, "set port suspendt, actual port %d status  = 0x%x\n", wIndex, portsc);
            break;
/* <--- 2010/12/16, added by Panasonic */
		default:
			goto error;
		}
		portsc = xhci_readl(xhci, addr); /* unblock any posted writes */
		portpmsc = xhci_readl(xhci, addr+1); /* unblock any posted writes *//* 2010/8/3, added by Panasonic */
		break;
	case ClearPortFeature:
		if (!wIndex || wIndex > ports)
			goto error;
/* 2010/8/3, added by Panasonic ---> */
        ss_port = xhci_hub_check_port_speed(xhci,wIndex);
        if(ss_port<0)
            goto error;
/* <--- 2010/8/3, added by Panasonic */
		wIndex--;
		addr = &xhci->op_regs->port_status_base +
			NUM_PORT_REGS*(wIndex & 0xff);
		portsc = xhci_readl(xhci, addr);
		portsc = xhci_port_state_to_neutral(portsc);
		switch (wValue) {
		case USB_PORT_FEAT_C_RESET:
			portsc |= PORT_RC;
			port_change_bit = "c-reset";
			break;
		case USB_PORT_FEAT_C_CONNECTION:
			portsc |= PORT_CSC;
			port_change_bit = "c-connect";
			break;
		case USB_PORT_FEAT_C_OVER_CURRENT:
			portsc |= PORT_OCC;
			port_change_bit = "c-over-current";
			break;
/* 2010/8/3,2011/2/23, added by Panasonic ---> */
        case USB_PORT_FEAT_C_ENABLE:
            if(!ss_port){
                portsc |= PORT_PEC;
                port_change_bit = "c-enable";
            }
            break;
        case USB_PORT_FEAT_ENABLE:
            if (!ss_port) {
                portsc |= PORT_PE;
                port_change_bit = "enable";
            }
            break;
        case USB_PORT_FEAT_POWER:
            if (HCC_PPC (xhci->hcc_params)) {
                portsc &= ~PORT_POWER;
                port_change_bit = "power";
            }
            break;
/* <--- 2010/8/3,2011/2/23, added by Panasonic */
		default:
			goto error;
		}
		/* Change bits are all write 1 to clear */
		xhci_writel(xhci, portsc, addr);
		portsc = xhci_readl(xhci, addr);
		xhci_dbg(xhci, "clear port %s change, actual port %d status  = 0x%x\n",
				port_change_bit, wIndex, portsc);
		portsc = xhci_readl(xhci, addr); /* unblock any posted writes */
		break;
	default:
error:
		/* "stall" on error */
		retval = -EPIPE;
	}
	spin_unlock_irqrestore(&xhci->lock, flags);
	return retval;
}

/*
 * Returns 0 if the status hasn't changed, or the number of bytes in buf.
 * Ports are 0-indexed from the HCD point of view,
 * and 1-indexed from the USB core pointer of view.
 * xHCI instances can have up to 127 ports, so FIXME if you see more than 15.
 *
 * Note that the status change bits will be cleared as soon as a port status
 * change event is generated, so we use the saved status from that event.
 */
int xhci_hub_status_data(struct usb_hcd *hcd, char *buf)
{
	unsigned long flags;
	u32 temp, status;
	int i, retval;
	struct xhci_hcd	*xhci = hcd_to_xhci(hcd);
	int ports;
	u32 __iomem *addr;

	ports = HCS_MAX_PORTS(xhci->hcs_params1);

	/* Initial status is no changes */
	buf[0] = 0;
	status = 0;
	if (ports > 7) {
		buf[1] = 0;
		retval = 2;
	} else {
		retval = 1;
	}

	spin_lock_irqsave(&xhci->lock, flags);
	/* For each port, did anything change?  If so, set that bit in buf. */
	for (i = 0; i < ports; i++) {
		addr = &xhci->op_regs->port_status_base +
			NUM_PORT_REGS*i;
		temp = xhci_readl(xhci, addr);
		if (temp & (PORT_CSC | PORT_PEC | PORT_OCC)) {
			if (i < 7)
				buf[0] |= 1 << (i + 1);
			else
				buf[1] |= 1 << (i - 7);
			status = 1;
		}
	}
	spin_unlock_irqrestore(&xhci->lock, flags);
	return status ? retval : 0;
}


/* 2010/10/18, added by Panasonic (SAV) ---> */
int xhci_hub_virtual_portnum(struct usb_hcd *hcd, unsigned portnum)
{
	struct xhci_hcd	*xhci = hcd_to_xhci(hcd);
    int ports = HCS_MAX_PORTS(xhci->hcs_params1);
    int i, vportnum_ss=0, vportnum=0;

    if(!portnum||portnum>ports)
        return -EINVAL;

    for(i=1; i<=ports; i++){
        int retval = xhci_hub_check_port_speed(xhci,i);

        if(retval<0)
            return retval;
        if(retval)
            vportnum_ss++;
        else
            vportnum++;

/*         pr_info("-------- i=%d, ss_port=%s, vportnum(%d,%d)\n", */
/*                 i, retval?"SS":"NON-SS", vportnum_ss, vportnum); */

        if(i==portnum)
            return (retval?vportnum_ss:vportnum);
    }
    return -ENODEV;
}
/* <--- 2010/10/18, added by Panasonic (SAV) */

