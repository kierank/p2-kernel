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
/* $Id: xhci-mem.c 10436 2010-11-16 03:21:50Z Noguchi Isao $ */

#include <linux/usb.h>
#include <linux/pci.h>
#include <linux/dmapool.h>

#include "xhci.h"

/*
 * Allocates a generic ring segment from the ring pool, sets the dma address,
 * initializes the segment to zero, and sets the private next pointer to NULL.
 *
 * Section 4.11.1.1:
 * "All components of all Command and Transfer TRBs shall be initialized to '0'"
 */
static struct xhci_segment *xhci_segment_alloc(struct xhci_hcd *xhci, gfp_t flags)
{
	struct xhci_segment *seg;
	dma_addr_t	dma;

	seg = kzalloc(sizeof *seg, flags);
	if (!seg)
		return 0;
	xhci_dbg(xhci, "Allocating priv segment structure at %p\n", seg);

	seg->trbs = dma_pool_alloc(xhci->segment_pool, flags, &dma);
	if (!seg->trbs) {
		kfree(seg);
		return 0;
	}
	xhci_dbg(xhci, "// Allocating segment at %p (virtual) 0x%llx (DMA)\n",
			seg->trbs, (unsigned long long)dma);

	memset(seg->trbs, 0, SEGMENT_SIZE);
    /* 2010/7/2, added by Panasonic 
       write back from cache */
    xhci_dma_cache_sync(xhci, seg->trbs, SEGMENT_SIZE, DMA_TO_DEVICE);
	seg->dma = dma;
	seg->next = NULL;

	return seg;
}

static void xhci_segment_free(struct xhci_hcd *xhci, struct xhci_segment *seg)
{
	if (!seg)
		return;
	if (seg->trbs) {
		xhci_dbg(xhci, "Freeing DMA segment at %p (virtual) 0x%llx (DMA)\n",
				seg->trbs, (unsigned long long)seg->dma);
		dma_pool_free(xhci->segment_pool, seg->trbs, seg->dma);
		seg->trbs = NULL;
	}
	xhci_dbg(xhci, "Freeing priv segment structure at %p\n", seg);
	kfree(seg);
}

/*
 * Make the prev segment point to the next segment.
 *
 * Change the last TRB in the prev segment to be a Link TRB which points to the
 * DMA address of the next segment.  The caller needs to set any Link TRB
 * related flags, such as End TRB, Toggle Cycle, and no snoop.
 */
static void xhci_link_segments(struct xhci_hcd *xhci, struct xhci_segment *prev,
		struct xhci_segment *next, bool link_trbs)
{
	u32 val;

	if (!prev || !next)
		return;
	prev->next = next;
	if (link_trbs) {
#if 0 /* original code */
		prev->trbs[TRBS_PER_SEGMENT-1].link.segment_ptr = next->dma;

		/* Set the last TRB in the segment to have a TRB type ID of Link TRB */
		val = prev->trbs[TRBS_PER_SEGMENT-1].link.control;
		val &= ~TRB_TYPE_BITMASK;
		val |= TRB_TYPE(TRB_LINK);
		/* Always set the chain bit with 0.95 hardware */
		if (xhci_link_trb_quirk(xhci))
			val |= TRB_CHAIN;
		prev->trbs[TRBS_PER_SEGMENT-1].link.control = val;
#else /* 2010/6/28,2010/7/16 modified by Panasonic for little-endian
         access to the data structures in host memory */
		xhci_desc_write_64(xhci, next->dma, &prev->trbs[TRBS_PER_SEGMENT-1].link.segment_ptr);

		/* Set the last TRB in the segment to have a TRB type ID of Link TRB */
		val = xhci_desc_readl(xhci, &prev->trbs[TRBS_PER_SEGMENT-1].link.control);
		val &= ~TRB_TYPE_BITMASK;
		val |= TRB_TYPE(TRB_LINK);
		/* Always set the chain bit with 0.95 hardware */
		if (xhci_link_trb_quirk(xhci))
			val |= TRB_CHAIN;
		xhci_desc_writel(xhci, val, &prev->trbs[TRBS_PER_SEGMENT-1].link.control);
#endif
        /* 2010/7/2, added by Panasonic 
           write back from cache */
        xhci_dma_cache_sync(xhci, &prev->trbs[TRBS_PER_SEGMENT-1], sizeof(struct xhci_link_trb), 
                            DMA_TO_DEVICE);
	}
	xhci_dbg(xhci, "Linking segment 0x%llx to segment 0x%llx (DMA)\n",
			(unsigned long long)prev->dma,
			(unsigned long long)next->dma);
}

/* XXX: Do we need the hcd structure in all these functions? */
void xhci_ring_free(struct xhci_hcd *xhci, struct xhci_ring *ring)
{
	struct xhci_segment *seg;
	struct xhci_segment *first_seg;

	if (!ring || !ring->first_seg)
		return;
	first_seg = ring->first_seg;
	seg = first_seg->next;
	xhci_dbg(xhci, "Freeing ring at %p\n", ring);
	while (seg != first_seg) {
		struct xhci_segment *next = seg->next;
		xhci_segment_free(xhci, seg);
		seg = next;
	}
	xhci_segment_free(xhci, first_seg);
	ring->first_seg = NULL;
	kfree(ring);
}

/**
 * Create a new ring with zero or more segments.
 *
 * Link each segment together into a ring.
 * Set the end flag and the cycle toggle bit on the last segment.
 * See section 4.9.1 and figures 15 and 16.
 */
static struct xhci_ring *xhci_ring_alloc(struct xhci_hcd *xhci,
		unsigned int num_segs, bool link_trbs, gfp_t flags)
{
	struct xhci_ring	*ring;
	struct xhci_segment	*prev;

	ring = kzalloc(sizeof *(ring), flags);
	xhci_dbg(xhci, "Allocating ring at %p\n", ring);
	if (!ring)
		return 0;

	INIT_LIST_HEAD(&ring->td_list);
	INIT_LIST_HEAD(&ring->cancelled_td_list);
	if (num_segs == 0)
		return ring;

	ring->first_seg = xhci_segment_alloc(xhci, flags);
	if (!ring->first_seg)
		goto fail;
	num_segs--;

	prev = ring->first_seg;
	while (num_segs > 0) {
		struct xhci_segment	*next;

		next = xhci_segment_alloc(xhci, flags);
		if (!next)
			goto fail;
		xhci_link_segments(xhci, prev, next, link_trbs);

		prev = next;
		num_segs--;
	}
	xhci_link_segments(xhci, prev, ring->first_seg, link_trbs);

	if (link_trbs) {
		/* See section 4.9.2.1 and 6.4.4.1 */
#if 0 /* original code */
		prev->trbs[TRBS_PER_SEGMENT-1].link.control |= (LINK_TOGGLE);
#else /* 2010/6/28,2010/7/16 modified by Panasonic for little-endian
         access to the data structures in host memory */
        xhci_desc_writel(xhci,
                        xhci_desc_readl(xhci, &prev->trbs[TRBS_PER_SEGMENT-1].link.control) | (LINK_TOGGLE),
                        &prev->trbs[TRBS_PER_SEGMENT-1].link.control);
#endif
        /* 2010/7/2, added by Panasonic 
           write back from cache */
        xhci_dma_cache_sync(xhci, &prev->trbs[TRBS_PER_SEGMENT-1], sizeof(struct xhci_link_trb), 
                            DMA_TO_DEVICE);
		xhci_dbg(xhci, "Wrote link toggle flag to"
				" segment %p (virtual), 0x%llx (DMA)\n",
				prev, (unsigned long long)prev->dma);
	}
	/* The ring is empty, so the enqueue pointer == dequeue pointer */
	ring->enqueue = ring->first_seg->trbs;
	ring->enq_seg = ring->first_seg;
	ring->dequeue = ring->enqueue;
	ring->deq_seg = ring->first_seg;
	/* The ring is initialized to 0. The producer must write 1 to the cycle
	 * bit to handover ownership of the TRB, so PCS = 1.  The consumer must
	 * compare CCS to the cycle bit to check ownership, so CCS = 1.
	 */
	ring->cycle_state = 1;

	return ring;

fail:
	xhci_ring_free(xhci, ring);
	return 0;
}

#define CTX_SIZE(_hcc) (HCC_64BYTE_CONTEXT(_hcc) ? 64 : 32)

struct xhci_container_ctx *xhci_alloc_container_ctx(struct xhci_hcd *xhci,
						    int type, gfp_t flags)
{
	struct xhci_container_ctx *ctx = kzalloc(sizeof(*ctx), flags);
	if (!ctx)
		return NULL;

	BUG_ON((type != XHCI_CTX_TYPE_DEVICE) && (type != XHCI_CTX_TYPE_INPUT));
	ctx->type = type;
	ctx->size = HCC_64BYTE_CONTEXT(xhci->hcc_params) ? 2048 : 1024;
	if (type == XHCI_CTX_TYPE_INPUT)
		ctx->size += CTX_SIZE(xhci->hcc_params);

	ctx->bytes = dma_pool_alloc(xhci->device_pool, flags, &ctx->dma);
	memset(ctx->bytes, 0, ctx->size);
	return ctx;
}

void xhci_free_container_ctx(struct xhci_hcd *xhci,
			     struct xhci_container_ctx *ctx)
{
	dma_pool_free(xhci->device_pool, ctx->bytes, ctx->dma);
	kfree(ctx);
}

struct xhci_input_control_ctx *xhci_get_input_control_ctx(struct xhci_hcd *xhci,
					      struct xhci_container_ctx *ctx)
{
	BUG_ON(ctx->type != XHCI_CTX_TYPE_INPUT);
	return (struct xhci_input_control_ctx *)ctx->bytes;
}

struct xhci_slot_ctx *xhci_get_slot_ctx(struct xhci_hcd *xhci,
					struct xhci_container_ctx *ctx)
{
	if (ctx->type == XHCI_CTX_TYPE_DEVICE)
		return (struct xhci_slot_ctx *)ctx->bytes;

	return (struct xhci_slot_ctx *)
		(ctx->bytes + CTX_SIZE(xhci->hcc_params));
}

struct xhci_ep_ctx *xhci_get_ep_ctx(struct xhci_hcd *xhci,
				    struct xhci_container_ctx *ctx,
				    unsigned int ep_index)
{
	/* increment ep index by offset of start of ep ctx array */
	ep_index++;
	if (ctx->type == XHCI_CTX_TYPE_INPUT)
		ep_index++;

	return (struct xhci_ep_ctx *)
		(ctx->bytes + (ep_index * CTX_SIZE(xhci->hcc_params)));
}

/* All the xhci_tds in the ring's TD list should be freed at this point */
void xhci_free_virt_device(struct xhci_hcd *xhci, int slot_id)
{
	struct xhci_virt_device *dev;
	int i;

	/* Slot ID 0 is reserved */
	if (slot_id == 0 || !xhci->devs[slot_id])
		return;

	dev = xhci->devs[slot_id];
#if 0 /* original code */
    xhci->dcbaa->dev_context_ptrs[slot_id] = 0;
#else /* 2010/6/28,2010/7/16 modified by Panasonic for little-endian
         access to the data structures in host memory */
	xhci_desc_write_64(xhci, 0, &xhci->dcbaa->dev_context_ptrs[slot_id]);
#endif
    /* 2010/7/2, added by Panasonic
       write back from cache */
    xhci_dma_cache_sync(xhci, &xhci->dcbaa->dev_context_ptrs[slot_id], sizeof(u64), DMA_TO_DEVICE);
	if (!dev)
		return;

	for (i = 0; i < 31; ++i)
		if (dev->ep_rings[i])
			xhci_ring_free(xhci, dev->ep_rings[i]);

	if (dev->in_ctx)
		xhci_free_container_ctx(xhci, dev->in_ctx);
	if (dev->out_ctx)
		xhci_free_container_ctx(xhci, dev->out_ctx);

	kfree(xhci->devs[slot_id]);
	xhci->devs[slot_id] = 0;
}

int xhci_alloc_virt_device(struct xhci_hcd *xhci, int slot_id,
		struct usb_device *udev, gfp_t flags)
{
	struct xhci_virt_device *dev;

	/* Slot ID 0 is reserved */
	if (slot_id == 0 || xhci->devs[slot_id]) {
		xhci_warn(xhci, "Bad Slot ID %d\n", slot_id);
		return 0;
	}

	xhci->devs[slot_id] = kzalloc(sizeof(*xhci->devs[slot_id]), flags);
	if (!xhci->devs[slot_id])
		return 0;
	dev = xhci->devs[slot_id];

	/* Allocate the (output) device context that will be used in the HC. */
	dev->out_ctx = xhci_alloc_container_ctx(xhci, XHCI_CTX_TYPE_DEVICE, flags);
	if (!dev->out_ctx)
		goto fail;

	xhci_dbg(xhci, "Slot %d output ctx = 0x%llx (dma)\n", slot_id,
			(unsigned long long)dev->out_ctx->dma);

	/* Allocate the (input) device context for address device command */
	dev->in_ctx = xhci_alloc_container_ctx(xhci, XHCI_CTX_TYPE_INPUT, flags);
	if (!dev->in_ctx)
		goto fail;

	xhci_dbg(xhci, "Slot %d input ctx = 0x%llx (dma)\n", slot_id,
			(unsigned long long)dev->in_ctx->dma);

	/* Allocate endpoint 0 ring */
	dev->ep_rings[0] = xhci_ring_alloc(xhci, 1, true, flags);
	if (!dev->ep_rings[0])
		goto fail;

	init_completion(&dev->cmd_completion);

	/* Point to output device context in dcbaa. */
#if 0 /* original code */
	xhci->dcbaa->dev_context_ptrs[slot_id] = dev->out_ctx->dma;
	xhci_dbg(xhci, "Set slot id %d dcbaa entry %p to 0x%llx\n",
			slot_id,
			&xhci->dcbaa->dev_context_ptrs[slot_id],
			(unsigned long long) xhci->dcbaa->dev_context_ptrs[slot_id]);
#else /* 2010/6/28,2010/7/16 modified by Panasonic for little-endian
         access to the data structures in host memory */
	xhci_desc_write_64(xhci, dev->out_ctx->dma, &xhci->dcbaa->dev_context_ptrs[slot_id]);
	xhci_dbg(xhci, "Set slot id %d dcbaa entry %p to 0x%llx\n",
             slot_id,
             &xhci->dcbaa->dev_context_ptrs[slot_id],
             (unsigned long long) xhci_desc_read_64(xhci, &xhci->dcbaa->dev_context_ptrs[slot_id]));
#endif
    /* 2010/7/2, added by Panasonic
       write back from cache */
    xhci_dma_cache_sync(xhci, &xhci->dcbaa->dev_context_ptrs[slot_id], sizeof(u64), DMA_TO_DEVICE);

	return 1;
fail:
	xhci_free_virt_device(xhci, slot_id);
	return 0;
}

/* Setup an xHCI virtual device for a Set Address command */
int xhci_setup_addressable_virt_dev(struct xhci_hcd *xhci, struct usb_device *udev)
{
	struct xhci_virt_device *dev;
	struct xhci_ep_ctx	*ep0_ctx;
	struct usb_device	*top_dev;
	struct xhci_slot_ctx    *slot_ctx;
	struct xhci_input_control_ctx *ctrl_ctx;

	dev = xhci->devs[udev->slot_id];
	/* Slot ID 0 is reserved */
	if (udev->slot_id == 0 || !dev) {
		xhci_warn(xhci, "Slot ID %d is not assigned to this device\n",
				udev->slot_id);
		return -EINVAL;
	}
	ep0_ctx = xhci_get_ep_ctx(xhci, dev->in_ctx, 0);
	ctrl_ctx = xhci_get_input_control_ctx(xhci, dev->in_ctx);
	slot_ctx = xhci_get_slot_ctx(xhci, dev->in_ctx);

	/* 2) New slot context and endpoint 0 context are valid*/
#if 0 /* original code */
	ctrl_ctx->add_flags = SLOT_FLAG | EP0_FLAG;
#else /* 2010/6/28,2010/7/16 modified by Panasonics for little-endian
         access to event TRB */
	xhci_desc_writel(xhci, SLOT_FLAG | EP0_FLAG, &ctrl_ctx->add_flags);
#endif

    /* 2010/7/5, added by Panasonic
       write back from cache */
    xhci_dma_cache_sync(xhci,ctrl_ctx,sizeof(struct xhci_input_control_ctx),DMA_TO_DEVICE);

	/* 3) Only the control endpoint is valid - one endpoint context */
#if 0 /* original code */
	slot_ctx->dev_info |= LAST_CTX(1);

    slot_ctx->dev_info |= (u32) udev->route; /*  2010/7/12, back-porting from 2.6.33 by Panasonic */
	switch (udev->speed) {
	case USB_SPEED_SUPER:
		slot_ctx->dev_info |= (u32) SLOT_SPEED_SS;
		break;
	case USB_SPEED_HIGH:
		slot_ctx->dev_info |= (u32) SLOT_SPEED_HS;
		break;
	case USB_SPEED_FULL:
		slot_ctx->dev_info |= (u32) SLOT_SPEED_FS;
		break;
	case USB_SPEED_LOW:
		slot_ctx->dev_info |= (u32) SLOT_SPEED_LS;
		break;
	case USB_SPEED_VARIABLE:
		xhci_dbg(xhci, "FIXME xHCI doesn't support wireless speeds\n");
		return -EINVAL;
		break;
	default:
		/* Speed was set earlier, this shouldn't happen. */
		BUG();
	}
#else /* 2010/6/28,2010/7/16 modified by Panasonic for little-endian
         access to the data structures in host memory */
	xhci_desc_writel(xhci, 
                    (xhci_desc_readl(xhci, &slot_ctx->dev_info) & ~LAST_CTX_MASK) | LAST_CTX(1),
                    &slot_ctx->dev_info);

/*  2010/7/12, back-porting from 2.6.33 by Panasonic */
    xhci_desc_writel(xhci, 
                    (xhci_desc_readl(xhci, &slot_ctx->dev_info) & ~ROUTE_STRING_MASK)
                    | (u32) udev->route,
                    &slot_ctx->dev_info);

	switch (udev->speed) {
	case USB_SPEED_SUPER:
		xhci_desc_writel(xhci, 
                        (xhci_desc_readl(xhci, &slot_ctx->dev_info) & ~DEV_SPEED)
                        | (u32) SLOT_SPEED_SS,
                        &slot_ctx->dev_info);
		break;
	case USB_SPEED_HIGH:
		xhci_desc_writel(xhci, 
                        (xhci_desc_readl(xhci, &slot_ctx->dev_info)  & ~DEV_SPEED)
                        | (u32) SLOT_SPEED_HS,
                        &slot_ctx->dev_info);
		break;
	case USB_SPEED_FULL:
		xhci_desc_writel(xhci, 
                        (xhci_desc_readl(xhci, &slot_ctx->dev_info)  & ~DEV_SPEED)
                        | (u32) SLOT_SPEED_FS,
                        &slot_ctx->dev_info);
		break;
	case USB_SPEED_LOW:
		xhci_desc_writel(xhci, 
                        (xhci_desc_readl(xhci, &slot_ctx->dev_info)  & ~DEV_SPEED)
                        | (u32) SLOT_SPEED_LS,
                        &slot_ctx->dev_info);
		break;
	case USB_SPEED_VARIABLE:
		xhci_dbg(xhci, "FIXME xHCI doesn't support wireless speeds\n");
		return -EINVAL;
		break;
	default:
		/* Speed was set earlier, this shouldn't happen. */
		BUG();
	}
#endif
	/* Find the root hub port this device is under */
	for (top_dev = udev; top_dev->parent && top_dev->parent->parent;
         top_dev = top_dev->parent){
		/* Found device below root hub */;
    }
#if 0 /* original code */
	slot_ctx->dev_info2 |= (u32) ROOT_HUB_PORT(top_dev->portnum);
#else /* 2010/6/28, modified by Panasonic for little-endian access to
         the data structures in host memory */
	xhci_desc_writel(xhci, 
                    (xhci_desc_readl(xhci, &slot_ctx->dev_info2) & ~ROOT_HUB_PORT_MASK)
                    | (u32) ROOT_HUB_PORT(top_dev->portnum),
                    &slot_ctx->dev_info2);
#endif
	xhci_dbg(xhci, "Set root hub portnum to %d\n", top_dev->portnum);

	/* Is this a LS/FS device under a HS hub? */
	if ((udev->speed == USB_SPEED_LOW || udev->speed == USB_SPEED_FULL) &&
			udev->tt) {
#if 0   /* original code */
		slot_ctx->tt_info = udev->tt->hub->slot_id;
		slot_ctx->tt_info |= udev->ttport << 8;
        /* 2010/7/12, back-porting from 2.6.33 by Panasonic */
        if (udev->tt->multi)
            slot_ctx->dev_info |= DEV_MTT;
#else /* 2010/6/28,2010/7/16 modified by Panasonic for little-endian
         access to the data structures in host memory */
        u32 tt_info = xhci_desc_readl(xhci, &slot_ctx->tt_info);
        tt_info = (tt_info & ~TT_SLOT_MASK) | udev->tt->hub->slot_id;
		tt_info = (tt_info & ~TT_PORT_MASK) | (udev->ttport << 8);
        xhci_desc_writel(xhci, tt_info, &slot_ctx->tt_info);
        /* 2010/7/12, back-porting from 2.6.33 by Panasonic */
        if (udev->tt->multi)
            xhci_desc_writel(xhci,
                            xhci_desc_readl(xhci, &slot_ctx->dev_info) | DEV_MTT,
                            &slot_ctx->dev_info);
#endif
	}
	xhci_dbg(xhci, "udev->tt = %p\n", udev->tt);
	xhci_dbg(xhci, "udev->ttport = 0x%x\n", udev->ttport);

    /* 2010/7/5, added by Panasonic
       write back from cache */
    xhci_dma_cache_sync(xhci,slot_ctx,sizeof(struct xhci_slot_ctx),DMA_TO_DEVICE);


	/* Step 4 - ring already allocated */
	/* Step 5 */
#if 0   /* original code */
	ep0_ctx->ep_info2 = EP_TYPE(CTRL_EP);
	/*
	 * XXX: Not sure about wireless USB devices.
	 */
	switch (udev->speed) {
	case USB_SPEED_SUPER:
		ep0_ctx->ep_info2 &= ~MAX_PACKET_MASK; /* 2010/7/7 added by Panasonic */
		ep0_ctx->ep_info2 |= MAX_PACKET(512);
		break;
	case USB_SPEED_HIGH:
	/* USB core guesses at a 64-byte max packet first for FS devices */
	case USB_SPEED_FULL:
		ep0_ctx->ep_info2 &= ~MAX_PACKET_MASK; /* 2010/7/7 added by Panasonic */
		ep0_ctx->ep_info2 |= MAX_PACKET(64);
		break;
	case USB_SPEED_LOW:
		ep0_ctx->ep_info2 &= ~MAX_PACKET_MASK; /* 2010/7/7 added by Panasonic */
		ep0_ctx->ep_info2 |= MAX_PACKET(8);
		break;
	case USB_SPEED_VARIABLE:
		xhci_dbg(xhci, "FIXME xHCI doesn't support wireless speeds\n");
		return -EINVAL;
		break;
	default:
		/* New speed? */
		BUG();
	}
	/* EP 0 can handle "burst" sizes of 1, so Max Burst Size field is 0 */
	ep0_ctx->ep_info2 &= ~MAX_BURST_MASK; /* 2010/7/7 added by Panasonic */
	ep0_ctx->ep_info2 |= MAX_BURST(0);
	ep0_ctx->ep_info2 |= ERROR_COUNT(3);

	ep0_ctx->deq =
		dev->ep_rings[0]->first_seg->dma;
	ep0_ctx->deq |= dev->ep_rings[0]->cycle_state;
#else /* 2010/6/28,2010/7/7,2010/7/16 modified by Panasonic for
         little-endian access to to the data structures in host
         memory */
	xhci_desc_writel(xhci,  EP_TYPE(CTRL_EP), &ep0_ctx->ep_info2);
	/*
	 * XXX: Not sure about wireless USB devices.
	 */
	switch (udev->speed) {
	case USB_SPEED_SUPER:
        xhci_desc_writel(xhci,
                        (xhci_desc_readl(xhci, &ep0_ctx->ep_info2) & ~MAX_PACKET_MASK) | MAX_PACKET(512),
                        &ep0_ctx->ep_info2);
		break;
	case USB_SPEED_HIGH:
	/* USB core guesses at a 64-byte max packet first for FS devices */
	case USB_SPEED_FULL:
        xhci_desc_writel(xhci,
                        (xhci_desc_readl(xhci, &ep0_ctx->ep_info2) & ~MAX_PACKET_MASK) | MAX_PACKET(64),
                        &ep0_ctx->ep_info2);
		break;
	case USB_SPEED_LOW:
        xhci_desc_writel(xhci,
                        (xhci_desc_readl(xhci, &ep0_ctx->ep_info2) & ~MAX_PACKET_MASK) | MAX_PACKET(8),
                        &ep0_ctx->ep_info2);
		break;
	case USB_SPEED_VARIABLE:
		xhci_dbg(xhci, "FIXME xHCI doesn't support wireless speeds\n");
		return -EINVAL;
		break;
	default:
		/* New speed? */
		BUG();
	}
	/* EP 0 can handle "burst" sizes of 1, so Max Burst Size field is 0 */
    xhci_desc_writel(xhci,
                    (xhci_desc_readl(xhci, &ep0_ctx->ep_info2) & ~MAX_BURST_MASK) | MAX_BURST(0),
                    &ep0_ctx->ep_info2);
    xhci_desc_writel(xhci,
                    (xhci_desc_readl(xhci, &ep0_ctx->ep_info2) & ~ERROR_COUNT_MASK)
                    | ERROR_COUNT(3),
                    &ep0_ctx->ep_info2);

    xhci_desc_write_64(xhci, dev->ep_rings[0]->first_seg->dma, &ep0_ctx->deq);
	xhci_desc_write_64(xhci, 
                      xhci_desc_read_64(xhci, &ep0_ctx->deq) | dev->ep_rings[0]->cycle_state,
                      &ep0_ctx->deq);
#endif

    /* 2010/7/5, added by Panasonic
       write back from cache */
    xhci_dma_cache_sync(xhci,ep0_ctx,sizeof(struct xhci_ep_ctx),DMA_TO_DEVICE);

	/* Steps 7 and 8 were done in xhci_alloc_virt_device() */

	return 0;
}

/* Return the polling or NAK interval.
 *
 * The polling interval is expressed in "microframes".  If xHCI's Interval field
 * is set to N, it will service the endpoint every 2^(Interval)*125us.
 *
 * The NAK interval is one NAK per 1 to 255 microframes, or no NAKs if interval
 * is set to 0.
 */
static inline unsigned int xhci_get_endpoint_interval(struct usb_device *udev,
		struct usb_host_endpoint *ep)
{
	unsigned int interval = 0;

	switch (udev->speed) {
	case USB_SPEED_HIGH:
		/* Max NAK rate */
		if (usb_endpoint_xfer_control(&ep->desc) ||
				usb_endpoint_xfer_bulk(&ep->desc))
			interval = ep->desc.bInterval;
		/* Fall through - SS and HS isoc/int have same decoding */
	case USB_SPEED_SUPER:
		if (usb_endpoint_xfer_int(&ep->desc) ||
				usb_endpoint_xfer_isoc(&ep->desc)) {
			if (ep->desc.bInterval == 0)
				interval = 0;
			else
				interval = ep->desc.bInterval - 1;
			if (interval > 15)
				interval = 15;
			if (interval != ep->desc.bInterval + 1)
				dev_warn(&udev->dev, "ep %#x - rounding interval to %d microframes\n",
						ep->desc.bEndpointAddress, 1 << interval);
		}
		break;
	/* Convert bInterval (in 1-255 frames) to microframes and round down to
	 * nearest power of 2.
	 */
	case USB_SPEED_FULL:
	case USB_SPEED_LOW:
		if (usb_endpoint_xfer_int(&ep->desc) ||
				usb_endpoint_xfer_isoc(&ep->desc)) {
			interval = fls(8*ep->desc.bInterval) - 1;
			if (interval > 10)
				interval = 10;
			if (interval < 3)
				interval = 3;
			if ((1 << interval) != 8*ep->desc.bInterval)
				dev_warn(&udev->dev, "ep %#x - rounding interval to %d microframes\n",
						ep->desc.bEndpointAddress, 1 << interval);
		}
		break;
	default:
		BUG();
	}
	return EP_INTERVAL(interval);
}

/* 2010/7/12, back-porting from 2.6.33 by Panasonic ---> */

/* The "Mult" field in the endpoint context is only set for SuperSpeed devices.
 * High speed endpoint descriptors can define "the number of additional
 * transaction opportunities per microframe", but that goes in the Max Burst
 * endpoint context field.
 */
static inline u32 xhci_get_endpoint_mult(struct usb_device *udev,
		struct usb_host_endpoint *ep)
{
	if (udev->speed != USB_SPEED_SUPER || !ep->ss_ep_comp)
		return 0;
	return ep->ss_ep_comp->desc.bmAttributes;
}

/* <--- 2010/7/12, back-porting from 2.6.33 by Panasonic */

static inline u32 xhci_get_endpoint_type(struct usb_device *udev,
		struct usb_host_endpoint *ep)
{
	int in;
	u32 type;

	in = usb_endpoint_dir_in(&ep->desc);
	if (usb_endpoint_xfer_control(&ep->desc)) {
		type = EP_TYPE(CTRL_EP);
	} else if (usb_endpoint_xfer_bulk(&ep->desc)) {
		if (in)
			type = EP_TYPE(BULK_IN_EP);
		else
			type = EP_TYPE(BULK_OUT_EP);
	} else if (usb_endpoint_xfer_isoc(&ep->desc)) {
		if (in)
			type = EP_TYPE(ISOC_IN_EP);
		else
			type = EP_TYPE(ISOC_OUT_EP);
	} else if (usb_endpoint_xfer_int(&ep->desc)) {
		if (in)
			type = EP_TYPE(INT_IN_EP);
		else
			type = EP_TYPE(INT_OUT_EP);
	} else {
		BUG();
	}
	return type;
}

/* 2010/7/12, back-porting from 2.6.33 by Panasonic ---> */

/* Return the maximum endpoint service interval time (ESIT) payload.
 * Basically, this is the maxpacket size, multiplied by the burst size
 * and mult size.
 */
static inline u32 xhci_get_max_esit_payload(struct xhci_hcd *xhci,
		struct usb_device *udev,
		struct usb_host_endpoint *ep)
{
	int max_burst;
	int max_packet;

	/* Only applies for interrupt or isochronous endpoints */
	if (usb_endpoint_xfer_control(&ep->desc) ||
			usb_endpoint_xfer_bulk(&ep->desc))
		return 0;

	if (udev->speed == USB_SPEED_SUPER) {
#if 0 /* original code */
		if (ep->ss_ep_comp)
			return ep->ss_ep_comp->desc.wBytesPerInterval;
		xhci_warn(xhci, "WARN no SS endpoint companion descriptor.\n");
		/* Assume no bursts, no multiple opportunities to send. */
		return ep->desc.wMaxPacketSize;
#else /* 2010/7/12 modified by Panasonic for little-endian access to
         the descriptors */
		if (ep->ss_ep_comp)
			return __le16_to_cpu(ep->ss_ep_comp->desc.wBytesPerInterval);
		xhci_warn(xhci, "WARN no SS endpoint companion descriptor.\n");
		/* Assume no bursts, no multiple opportunities to send. */
		return __le16_to_cpu(ep->desc.wMaxPacketSize);
#endif
	}

#if 0 /* original code */
	max_packet = ep->desc.wMaxPacketSize & 0x3ff;
	max_burst = (ep->desc.wMaxPacketSize & 0x1800) >> 11;
#else /* 2010/7/12 modified by Panasonic for little-endian access to
         the descriptors */
	max_packet = __le16_to_cpu(ep->desc.wMaxPacketSize) & 0x3ff;
	max_burst = (__le16_to_cpu(ep->desc.wMaxPacketSize) & 0x1800) >> 11;
#endif
	/* A 0 in max burst means 1 transfer per ESIT */
	return max_packet * (max_burst + 1);
}

/* <--- 2010/7/12, back-porting from 2.6.33 by Panasonic */

int xhci_endpoint_init(struct xhci_hcd *xhci,
		struct xhci_virt_device *virt_dev,
		struct usb_device *udev,
		struct usb_host_endpoint *ep,
		gfp_t mem_flags)
{
	unsigned int ep_index;
	struct xhci_ep_ctx *ep_ctx;
	struct xhci_ring *ep_ring;
	unsigned int max_packet;
	unsigned int max_burst;
	u32 max_esit_payload;    /* 2010/7/12, back-porting from 2.6.33 by
                                Panasonic */

	ep_index = xhci_get_endpoint_index(&ep->desc);
	ep_ctx = xhci_get_ep_ctx(xhci, virt_dev->in_ctx, ep_index);

	/* Set up the endpoint ring */
    /* 2010/10/06, added by Panasonic (SAV) ---> */
/* 	virt_dev->new_ep_rings[ep_index] = xhci_ring_alloc(xhci, 1, true, mem_flags); */
	virt_dev->new_ep_rings[ep_index] = xhci_ring_alloc(xhci, EP_RING_SEGS, true, mem_flags);
    /* <--- 2010/10/06, added by Panasonic (SAV) */
	if (!virt_dev->new_ep_rings[ep_index])
		return -ENOMEM;
	ep_ring = virt_dev->new_ep_rings[ep_index];
#if 0   /* original code */
	ep_ctx->deq = ep_ring->first_seg->dma | ep_ring->cycle_state;

	ep_ctx->ep_info = xhci_get_endpoint_interval(udev, ep);

	/* 2010/7/12, back-porting from 2.6.33 by Panasonic */
    ep_ctx->ep_info |= EP_MULT(xhci_get_endpoint_mult(udev, ep));

#else /* 2010/6/28,2010/7/12,2010/7/16 modified by Panasonic for
         little-endian access to the data structures in host memory */
	xhci_desc_write_64(xhci,
                      ep_ring->first_seg->dma | ep_ring->cycle_state,
                      &ep_ctx->deq);

	xhci_desc_writel(xhci, xhci_get_endpoint_interval(udev, ep), &ep_ctx->ep_info);

	/* 2010/7/12, back-porting from 2.6.33 by Panasonic */
	xhci_desc_writel(xhci, 
                    (xhci_desc_readl(xhci, &ep_ctx->ep_info) & ~EP_MULT_MASK)
                    | EP_MULT(xhci_get_endpoint_mult(udev, ep)),
                    &ep_ctx->ep_info);

#endif

	/* FIXME dig Mult and streams info out of ep companion desc */

	/* Allow 3 retries for everything but isoc;
	 * error count = 0 means infinite retries.
	 */
#if 0   /* original code */
	if (!usb_endpoint_xfer_isoc(&ep->desc))
		ep_ctx->ep_info2 = ERROR_COUNT(3);
	else
		ep_ctx->ep_info2 = ERROR_COUNT(1);

	ep_ctx->ep_info2 |= xhci_get_endpoint_type(udev, ep);
#else /* 2010/6/28,2010/7/16 modified by Panasonic for little-endian
         access to the data structures in host memory */
	if (!usb_endpoint_xfer_isoc(&ep->desc))
		xhci_desc_writel(xhci, ERROR_COUNT(3), &ep_ctx->ep_info2);
	else
		xhci_desc_writel(xhci, ERROR_COUNT(1), &ep_ctx->ep_info2);

	xhci_desc_writel(xhci,
                    (xhci_desc_readl(xhci, &ep_ctx->ep_info2) & ~EP_TYPE_MASK)
                    | xhci_get_endpoint_type(udev, ep),
                    &ep_ctx->ep_info2);
#endif
	/* Set the max packet size and max burst */
	switch (udev->speed) {
	case USB_SPEED_SUPER:
		max_packet = __le16_to_cpu(ep->desc.wMaxPacketSize);
#if 0   /* original code */
		ep_ctx->ep_info2 &= ~MAX_PACKET_MASK; /* 2010/7/7 added by Panasonic */
		ep_ctx->ep_info2 |= MAX_PACKET(max_packet);
#else /* 2010/6/28,2010/7/7,2010/7/16 modified by Panasonic for
         little-endian access to the data structures in host memory */
		xhci_desc_writel(xhci,
                        (xhci_desc_readl(xhci, &ep_ctx->ep_info2) & ~MAX_PACKET_MASK) | MAX_PACKET(max_packet),
                        &ep_ctx->ep_info2);
#endif
		/* dig out max burst from ep companion desc */
		if (!ep->ss_ep_comp) {
			xhci_warn(xhci, "WARN no SS endpoint companion descriptor.\n");
			max_packet = 0;
		} else {
			max_packet = ep->ss_ep_comp->desc.bMaxBurst;
		}
#if 0   /* original code */
		ep_ctx->ep_info2 &= ~MAX_BURST_MASK; /* 2010/7/7, added by Panasonic */
		ep_ctx->ep_info2 |= MAX_BURST(max_packet);
#else /* 2010/6/28,2010/7/7,2010/7/16 modified by Panasonic for little-endian
         access to to the data structures in host memory */
		xhci_desc_writel(xhci,
                        (xhci_desc_readl(xhci, &ep_ctx->ep_info2) & ~MAX_BURST_MASK) | MAX_BURST(max_packet),
                        &ep_ctx->ep_info2);
#endif
		break;
	case USB_SPEED_HIGH:
		/* bits 11:12 specify the number of additional transaction
		 * opportunities per microframe (USB 2.0, section 9.6.6)
		 */
		if (usb_endpoint_xfer_isoc(&ep->desc) ||
            usb_endpoint_xfer_int(&ep->desc)) {
			max_burst = (__le16_to_cpu(ep->desc.wMaxPacketSize) & 0x1800) >> 11;
#if 0   /* original code */
			ep_ctx->ep_info2 &= ~MAX_BURST_MASK; /* 2010/7/7, added by Panasonic */
			ep_ctx->ep_info2 |= MAX_BURST(max_burst);
#else /* 2010/6/28,2010/7/16 modified by Panasonic for little-endian
         access to the data structures in host memory */
            xhci_desc_writel(xhci,
                            (xhci_desc_readl(xhci, &ep_ctx->ep_info2) & ~MAX_BURST_MASK) | MAX_BURST(max_burst),
                            &ep_ctx->ep_info2);
#endif
		}
		/* Fall through */
	case USB_SPEED_FULL:
	case USB_SPEED_LOW:
		max_packet = __le16_to_cpu(ep->desc.wMaxPacketSize) & 0x3ff;
#if 0   /* original code */
		ep_ctx->ep_info2 &= ~MAX_PACKET_MASK; /* 2010/7/7 added by Panasonic */
		ep_ctx->ep_info2 |= MAX_PACKET(max_packet);
#else /* 2010/6/28,2010/7/7,2010/7/16 modified by Panasonic for
         little-endian access to the data structures in host memory */
		xhci_desc_writel(xhci,
                        (xhci_desc_readl(xhci, &ep_ctx->ep_info2) & ~MAX_PACKET_MASK) | MAX_PACKET(max_packet),
                        &ep_ctx->ep_info2);
#endif
		break;
	default:
		BUG();
	}

/* 2010/7/12, back-porting from 2.6.33 by Panasonic ---> */

	max_esit_payload = xhci_get_max_esit_payload(xhci, udev, ep);
#if 0   /* original code */
	ep_ctx->tx_info = MAX_ESIT_PAYLOAD_FOR_EP(max_esit_payload);
#else /* 2010/7/12,2010/7/16 modified by Panasonic for little-endian
         access to the data structures in host memory */
	xhci_desc_writel(xhci, MAX_ESIT_PAYLOAD_FOR_EP(max_esit_payload), &ep_ctx->tx_info);
#endif

	/*
	 * XXX no idea how to calculate the average TRB buffer length for bulk
	 * endpoints, as the driver gives us no clue how big each scatter gather
	 * list entry (or buffer) is going to be.
	 *
	 * For isochronous and interrupt endpoints, we set it to the max
	 * available, until we have new API in the USB core to allow drivers to
	 * declare how much bandwidth they actually need.
	 *
	 * Normally, it would be calculated by taking the total of the buffer
	 * lengths in the TD and then dividing by the number of TRBs in a TD,
	 * including link TRBs, No-op TRBs, and Event data TRBs.  Since we don't
	 * use Event Data TRBs, and we don't chain in a link TRB on short
	 * transfers, we're basically dividing by 1.
	 */
#if 0   /* original code */
	ep_ctx->tx_info |= AVG_TRB_LENGTH_FOR_EP(max_esit_payload);
#else /* 2010/7/12,2010/7/16 modified by Panasonic for little-endian
         access to the data structures in host memory */
	xhci_desc_writel(xhci, 
                    (xhci_desc_readl(xhci, &ep_ctx->tx_info) & ~AVG_TRB_LENGTH_FOR_EP_MASK)
                    | AVG_TRB_LENGTH_FOR_EP(max_esit_payload),
                    &ep_ctx->tx_info);
#endif

/* <--- 2010/7/12, back-porting from 2.6.33 by Panasonic */

    /* 2010/7/5, added by Panasonic
       write back from cache */
    xhci_dma_cache_sync(xhci,ep_ctx,sizeof(struct xhci_ep_ctx),DMA_TO_DEVICE);

	/* FIXME Debug endpoint context */
	return 0;
}

void xhci_endpoint_zero(struct xhci_hcd *xhci,
		struct xhci_virt_device *virt_dev,
		struct usb_host_endpoint *ep)
{
	unsigned int ep_index;
	struct xhci_ep_ctx *ep_ctx;

	ep_index = xhci_get_endpoint_index(&ep->desc);
	ep_ctx = xhci_get_ep_ctx(xhci, virt_dev->in_ctx, ep_index);

#if 0   /* original code */
	ep_ctx->ep_info = 0;
	ep_ctx->ep_info2 = 0;
	ep_ctx->deq = 0;
	ep_ctx->tx_info = 0;
#else /* 2010/6/28,2010/7/16 modified by Panasonic for little-endian
         access to the data structures in host memory */
	xhci_desc_writel(xhci, 0, &ep_ctx->ep_info);
	xhci_desc_writel(xhci, 0, &ep_ctx->ep_info2);
	xhci_desc_write_64(xhci, 0, &ep_ctx->deq);
	xhci_desc_writel(xhci, 0, &ep_ctx->tx_info);
#endif

    /* 2010/7/5, added by Panasonic
       write back from cache */
    xhci_dma_cache_sync(xhci,ep_ctx,sizeof(struct xhci_ep_ctx),DMA_TO_DEVICE);

	/* Don't free the endpoint ring until the set interface or configuration
	 * request succeeds.
	 */
}

/* Copy output xhci_ep_ctx to the input xhci_ep_ctx copy.
 * Useful when you want to change one particular aspect of the endpoint and then
 * issue a configure endpoint command.
 */
void xhci_endpoint_copy(struct xhci_hcd *xhci,
		struct xhci_virt_device *vdev, unsigned int ep_index)
{
	struct xhci_ep_ctx *out_ep_ctx;
	struct xhci_ep_ctx *in_ep_ctx;

	out_ep_ctx = xhci_get_ep_ctx(xhci, vdev->out_ctx, ep_index);
	in_ep_ctx = xhci_get_ep_ctx(xhci, vdev->in_ctx, ep_index);

    /* 2010/7/5, added by Panasonic
       invalidate cache */
    xhci_dma_cache_sync(xhci,out_ep_ctx,sizeof(struct xhci_ep_ctx),DMA_FROM_DEVICE);

#if 0   /* original code */
	in_ep_ctx->ep_info = out_ep_ctx->ep_info;
	in_ep_ctx->ep_info2 = out_ep_ctx->ep_info2;
	in_ep_ctx->deq = out_ep_ctx->deq;
	in_ep_ctx->tx_info = out_ep_ctx->tx_info;
#else /* 2010/6/28,2010/7/16 modified by Panasonic for little-endian
         access to the data structures in host memory */
	xhci_desc_writel(xhci, xhci_desc_readl(xhci, &out_ep_ctx->ep_info), &in_ep_ctx->ep_info);
	xhci_desc_writel(xhci, xhci_desc_readl(xhci, &out_ep_ctx->ep_info2), &in_ep_ctx->ep_info2);
	xhci_desc_write_64(xhci, xhci_desc_read_64(xhci, &out_ep_ctx->deq), &in_ep_ctx->deq);
	xhci_desc_writel(xhci, xhci_desc_readl(xhci, &out_ep_ctx->tx_info), &in_ep_ctx->tx_info);
#endif

    /* 2010/7/5, added by Panasonic
       write back from cache */
    xhci_dma_cache_sync(xhci,in_ep_ctx,sizeof(struct xhci_ep_ctx),DMA_TO_DEVICE);

}

/* Copy output xhci_slot_ctx to the input xhci_slot_ctx.
 * Useful when you want to change one particular aspect of the endpoint and then
 * issue a configure endpoint command.  Only the context entries field matters,
 * but we'll copy the whole thing anyway.
 */
void xhci_slot_copy(struct xhci_hcd *xhci, struct xhci_virt_device *vdev)
{
	struct xhci_slot_ctx *in_slot_ctx;
	struct xhci_slot_ctx *out_slot_ctx;

	in_slot_ctx = xhci_get_slot_ctx(xhci, vdev->in_ctx);
	out_slot_ctx = xhci_get_slot_ctx(xhci, vdev->out_ctx);

    /* 2010/7/5, added by Panasonic
       invalidate cache */
    xhci_dma_cache_sync(xhci,out_slot_ctx,sizeof(struct xhci_slot_ctx),DMA_FROM_DEVICE);

#if 0 /* original code */
	in_slot_ctx->dev_info = out_slot_ctx->dev_info;
	in_slot_ctx->dev_info2 = out_slot_ctx->dev_info2;
	in_slot_ctx->tt_info = out_slot_ctx->tt_info;
	in_slot_ctx->dev_state = out_slot_ctx->dev_state;
#else /* 2010/6/28,2010/7/16 modified by Panasonic for little-endian
         access to the data structures in host memory */
	xhci_desc_writel(xhci,
                    xhci_desc_readl(xhci, &out_slot_ctx->dev_info),
                    &in_slot_ctx->dev_info);
	xhci_desc_writel(xhci,
                    xhci_desc_readl(xhci, &out_slot_ctx->dev_info2),
                    &in_slot_ctx->dev_info2);
    xhci_desc_writel(xhci,
                    xhci_desc_readl(xhci, &out_slot_ctx->tt_info),
                    &in_slot_ctx->tt_info);
    xhci_desc_writel(xhci,
                    xhci_desc_readl(xhci, &out_slot_ctx->dev_state),
                    &in_slot_ctx->dev_state);
#endif

    /* 2010/7/5, added by Panasonic
       write-back cache */
    xhci_dma_cache_sync(xhci,in_slot_ctx,sizeof(struct xhci_slot_ctx),DMA_TO_DEVICE);

}

/* Set up the scratchpad buffer array and scratchpad buffers, if needed. */
static int scratchpad_alloc(struct xhci_hcd *xhci, gfp_t flags)
{
	int i;
	struct device *dev = xhci_to_hcd(xhci)->self.controller;
	int num_sp = HCS_MAX_SCRATCHPAD(xhci->hcs_params2);

	xhci_dbg(xhci, "Allocating %d scratchpad buffers\n", num_sp);

	if (!num_sp)
		return 0;

	xhci->scratchpad = kzalloc(sizeof(*xhci->scratchpad), flags);
	if (!xhci->scratchpad)
		goto fail_sp;

	xhci->scratchpad->sp_array =
		pci_alloc_consistent(to_pci_dev(dev),
				     num_sp * sizeof(u64),
				     &xhci->scratchpad->sp_dma);
	if (!xhci->scratchpad->sp_array)
		goto fail_sp2;

	xhci->scratchpad->sp_buffers = kzalloc(sizeof(void *) * num_sp, flags);
	if (!xhci->scratchpad->sp_buffers)
		goto fail_sp3;

	xhci->scratchpad->sp_dma_buffers =
		kzalloc(sizeof(dma_addr_t) * num_sp, flags);

	if (!xhci->scratchpad->sp_dma_buffers)
		goto fail_sp4;

#if 0 /* original code */
	xhci->dcbaa->dev_context_ptrs[0] = xhci->scratchpad->sp_dma;
#else /* 2010/6/28,2010/7/16 modified by Panasonic for little-endian
         access to the data structures in host memory */
	xhci_desc_write_64(xhci, xhci->scratchpad->sp_dma, &xhci->dcbaa->dev_context_ptrs[0]);
#endif
    /* 2010/7/2, added by Panasonic
       write back from cache */
    xhci_dma_cache_sync(xhci, &xhci->dcbaa->dev_context_ptrs[0], sizeof(u64), DMA_TO_DEVICE);
	for (i = 0; i < num_sp; i++) {
		dma_addr_t dma;
		void *buf = pci_alloc_consistent(to_pci_dev(dev),
						 xhci->page_size, &dma);
		if (!buf)
			goto fail_sp5;

#if 0 /* original code */
		xhci->scratchpad->sp_array[i] = dma;
#else /* 2010/6/28,2010/7/16 modified by Panasonic for little-endian
         access to the data structures in host memory */
		xhci_desc_write_64(xhci, dma, &xhci->scratchpad->sp_array[i]);
#endif
		xhci->scratchpad->sp_buffers[i] = buf;
		xhci->scratchpad->sp_dma_buffers[i] = dma;
	}
    /* 2010/7/1, added by Panasonic
       write back from cache */
    xhci_dma_cache_sync(xhci, xhci->scratchpad->sp_array, sizeof(u64)*num_sp, DMA_TO_DEVICE);

	return 0;

 fail_sp5:
	for (i = i - 1; i >= 0; i--) {
		pci_free_consistent(to_pci_dev(dev), xhci->page_size,
				    xhci->scratchpad->sp_buffers[i],
				    xhci->scratchpad->sp_dma_buffers[i]);
	}
	kfree(xhci->scratchpad->sp_dma_buffers);

 fail_sp4:
	kfree(xhci->scratchpad->sp_buffers);

 fail_sp3:
	pci_free_consistent(to_pci_dev(dev), num_sp * sizeof(u64),
			    xhci->scratchpad->sp_array,
			    xhci->scratchpad->sp_dma);

 fail_sp2:
	kfree(xhci->scratchpad);
	xhci->scratchpad = NULL;

 fail_sp:
	return -ENOMEM;
}

static void scratchpad_free(struct xhci_hcd *xhci)
{
	int num_sp;
	int i;
	struct pci_dev	*pdev = to_pci_dev(xhci_to_hcd(xhci)->self.controller);

	if (!xhci->scratchpad)
		return;

	num_sp = HCS_MAX_SCRATCHPAD(xhci->hcs_params2);

	for (i = 0; i < num_sp; i++) {
		pci_free_consistent(pdev, xhci->page_size,
				    xhci->scratchpad->sp_buffers[i],
				    xhci->scratchpad->sp_dma_buffers[i]);
	}
	kfree(xhci->scratchpad->sp_dma_buffers);
	kfree(xhci->scratchpad->sp_buffers);
	pci_free_consistent(pdev, num_sp * sizeof(u64),
			    xhci->scratchpad->sp_array,
			    xhci->scratchpad->sp_dma);
	kfree(xhci->scratchpad);
	xhci->scratchpad = NULL;
}

void xhci_mem_cleanup(struct xhci_hcd *xhci)
{
	struct pci_dev	*pdev = to_pci_dev(xhci_to_hcd(xhci)->self.controller);
	int size;
	int i;

	/* Free the Event Ring Segment Table and the actual Event Ring */
	if (xhci->ir_set) {
		xhci_writel(xhci, 0, &xhci->ir_set->erst_size);
		xhci_write_64(xhci, 0, &xhci->ir_set->erst_base);
		xhci_write_64(xhci, 0, &xhci->ir_set->erst_dequeue);
	}
	size = sizeof(struct xhci_erst_entry)*(xhci->erst.num_entries);
	if (xhci->erst.entries)
		pci_free_consistent(pdev, size,
				xhci->erst.entries, xhci->erst.erst_dma_addr);
	xhci->erst.entries = NULL;
	xhci_dbg(xhci, "Freed ERST\n");
	if (xhci->event_ring)
		xhci_ring_free(xhci, xhci->event_ring);
	xhci->event_ring = NULL;
	xhci_dbg(xhci, "Freed event ring\n");

	xhci_write_64(xhci, 0, &xhci->op_regs->cmd_ring);
	if (xhci->cmd_ring)
		xhci_ring_free(xhci, xhci->cmd_ring);
	xhci->cmd_ring = NULL;
	xhci_dbg(xhci, "Freed command ring\n");

	for (i = 1; i < MAX_HC_SLOTS; ++i)
		xhci_free_virt_device(xhci, i);

	if (xhci->segment_pool)
		dma_pool_destroy(xhci->segment_pool);
	xhci->segment_pool = NULL;
	xhci_dbg(xhci, "Freed segment pool\n");

	if (xhci->device_pool)
		dma_pool_destroy(xhci->device_pool);
	xhci->device_pool = NULL;
	xhci_dbg(xhci, "Freed device context pool\n");

	xhci_write_64(xhci, 0, &xhci->op_regs->dcbaa_ptr);
	if (xhci->dcbaa)
		pci_free_consistent(pdev, sizeof(*xhci->dcbaa),
				xhci->dcbaa, xhci->dcbaa->dma);
	xhci->dcbaa = NULL;

	scratchpad_free(xhci);
	xhci->page_size = 0;
	xhci->page_shift = 0;
}

int xhci_mem_init(struct xhci_hcd *xhci, gfp_t flags)
{
	dma_addr_t	dma;
	struct device	*dev = xhci_to_hcd(xhci)->self.controller;
	unsigned int	val, val2;
	u64		val_64;
	struct xhci_segment	*seg;
	u32 page_size;
	int i;

	page_size = xhci_readl(xhci, &xhci->op_regs->page_size);
	xhci_dbg(xhci, "Supported page size register = 0x%x\n", page_size);
	for (i = 0; i < 16; i++) {
		if ((0x1 & page_size) != 0)
			break;
		page_size = page_size >> 1;
	}
	if (i < 16)
		xhci_dbg(xhci, "Supported page size of %iK\n", (1 << (i+12)) / 1024);
	else
		xhci_warn(xhci, "WARN: no supported page size\n");
	/* Use 4K pages, since that's common and the minimum the HC supports */
	xhci->page_shift = 12;
	xhci->page_size = 1 << xhci->page_shift;
	xhci_dbg(xhci, "HCD page size set to %iK\n", xhci->page_size / 1024);

	/*
	 * Program the Number of Device Slots Enabled field in the CONFIG
	 * register with the max value of slots the HC can handle.
	 */
	val = HCS_MAX_SLOTS(xhci_readl(xhci, &xhci->cap_regs->hcs_params1));
	xhci_dbg(xhci, "// xHC can handle at most %d device slots.\n",
			(unsigned int) val);
	val2 = xhci_readl(xhci, &xhci->op_regs->config_reg);
	val |= (val2 & ~HCS_SLOTS_MASK);
	xhci_dbg(xhci, "// Setting Max device slots reg = 0x%x.\n",
			(unsigned int) val);
	xhci_writel(xhci, val, &xhci->op_regs->config_reg);

	/*
	 * Section 5.4.8 - doorbell array must be
	 * "physically contiguous and 64-byte (cache line) aligned".
	 */
	xhci->dcbaa = pci_alloc_consistent(to_pci_dev(dev),
			sizeof(*xhci->dcbaa), &dma);
	if (!xhci->dcbaa)
		goto fail;
	memset(xhci->dcbaa, 0, sizeof *(xhci->dcbaa));
	xhci->dcbaa->dma = dma;
	xhci_dbg(xhci, "// Device context base array address = 0x%llx (DMA), %p (virt)\n",
			(unsigned long long)xhci->dcbaa->dma, xhci->dcbaa);
	xhci_write_64(xhci, dma, &xhci->op_regs->dcbaa_ptr);

	/*
	 * Initialize the ring segment pool.  The ring must be a contiguous
	 * structure comprised of TRBs.  The TRBs must be 16 byte aligned,
	 * however, the command ring segment needs 64-byte aligned segments,
	 * so we pick the greater alignment need.
	 */
	xhci->segment_pool = dma_pool_create("xHCI ring segments", dev,
			SEGMENT_SIZE, 64, xhci->page_size);

	/* See Table 46 and Note on Figure 55 */
	xhci->device_pool = dma_pool_create("xHCI input/output contexts", dev,
			2112, 64, xhci->page_size);
	if (!xhci->segment_pool || !xhci->device_pool)
		goto fail;

	/* Set up the command ring to have one segments for now. */
	xhci->cmd_ring = xhci_ring_alloc(xhci, 1, true, flags);
	if (!xhci->cmd_ring)
		goto fail;
	xhci_dbg(xhci, "Allocated command ring at %p\n", xhci->cmd_ring);
	xhci_dbg(xhci, "First segment DMA is 0x%llx\n",
			(unsigned long long)xhci->cmd_ring->first_seg->dma);

	/* Set the address in the Command Ring Control register */
	val_64 = xhci_read_64(xhci, &xhci->op_regs->cmd_ring);
	val_64 = (val_64 & (u64) CMD_RING_RSVD_BITS) |
		(xhci->cmd_ring->first_seg->dma & (u64) ~CMD_RING_RSVD_BITS) |
		xhci->cmd_ring->cycle_state;
	xhci_dbg(xhci, "// Setting command ring address to 0x%x\n", val);
	xhci_write_64(xhci, val_64, &xhci->op_regs->cmd_ring);
	xhci_dbg_cmd_ptrs(xhci);

	val = xhci_readl(xhci, &xhci->cap_regs->db_off);
	val &= DBOFF_MASK;
	xhci_dbg(xhci, "// Doorbell array is located at offset 0x%x"
			" from cap regs base addr\n", val);
	xhci->dba = (void *) xhci->cap_regs + val;
	xhci_dbg_regs(xhci);
	xhci_print_run_regs(xhci);
	/* Set ir_set to interrupt register set 0 */
	xhci->ir_set = (void *) xhci->run_regs->ir_set;

	/*
	 * Event ring setup: Allocate a normal ring, but also setup
	 * the event ring segment table (ERST).  Section 4.9.3.
	 */
	xhci_dbg(xhci, "// Allocating event ring\n");
	xhci->event_ring = xhci_ring_alloc(xhci, ERST_NUM_SEGS, false, flags);
	if (!xhci->event_ring)
		goto fail;

	xhci->erst.entries = pci_alloc_consistent(to_pci_dev(dev),
			sizeof(struct xhci_erst_entry)*ERST_NUM_SEGS, &dma);
	if (!xhci->erst.entries)
		goto fail;
	xhci_dbg(xhci, "// Allocated event ring segment table at 0x%llx\n",
			(unsigned long long)dma);

	memset(xhci->erst.entries, 0, sizeof(struct xhci_erst_entry)*ERST_NUM_SEGS);
    /* 2010/6/28, modified by Panasonic
       write-back from cache */
    xhci_dma_cache_sync(xhci,xhci->erst.entries,sizeof(struct xhci_erst_entry)*ERST_NUM_SEGS,DMA_TO_DEVICE);

	xhci->erst.num_entries = ERST_NUM_SEGS;
	xhci->erst.erst_dma_addr = dma;
	xhci_dbg(xhci, "Set ERST to 0; private num segs = %i, virt addr = %p, dma addr = 0x%llx\n",
			xhci->erst.num_entries,
			xhci->erst.entries,
			(unsigned long long)xhci->erst.erst_dma_addr);

	/* set ring base address and size for each segment table entry */
	for (val = 0, seg = xhci->event_ring->first_seg; val < ERST_NUM_SEGS; val++) {
		struct xhci_erst_entry *entry = &xhci->erst.entries[val];
#if 0 /* original code */
		entry->seg_addr = seg->dma;
		entry->seg_size = TRBS_PER_SEGMENT;
		entry->rsvd = 0;
#else /* 2010/6/28,2010/7/16 modified by Panasonic for little-endian
         access to the data structures in host memory */
		xhci_desc_write_64(xhci, seg->dma, &entry->seg_addr);
		xhci_desc_writel(xhci, TRBS_PER_SEGMENT, &entry->seg_size);
		xhci_desc_writel(xhci, 0, &entry->rsvd);
#endif
        /* 2010/6/28, modified by Panasonic
           write-back from cache */
        xhci_dma_cache_sync(xhci,entry,sizeof(struct xhci_erst_entry),DMA_TO_DEVICE);

		seg = seg->next;
	}

	/* set ERST count with the number of entries in the segment table */
	val = xhci_readl(xhci, &xhci->ir_set->erst_size);
	val &= ERST_SIZE_MASK;
	val |= ERST_NUM_SEGS;
	xhci_dbg(xhci, "// Write ERST size = %i to ir_set 0 (some bits preserved)\n",
			val);
	xhci_writel(xhci, val, &xhci->ir_set->erst_size);

	xhci_dbg(xhci, "// Set ERST entries to point to event ring.\n");
	/* set the segment table base address */
	xhci_dbg(xhci, "// Set ERST base address for ir_set 0 = 0x%llx\n",
			(unsigned long long)xhci->erst.erst_dma_addr);
	val_64 = xhci_read_64(xhci, &xhci->ir_set->erst_base);
	val_64 &= ERST_PTR_MASK;
	val_64 |= (xhci->erst.erst_dma_addr & (u64) ~ERST_PTR_MASK);
	xhci_write_64(xhci, val_64, &xhci->ir_set->erst_base);

	/* Set the event ring dequeue address */
	xhci_set_hc_event_deq(xhci);
	xhci_dbg(xhci, "Wrote ERST address to ir_set 0.\n");
	xhci_print_ir_set(xhci, xhci->ir_set, 0);

	/*
	 * XXX: Might need to set the Interrupter Moderation Register to
	 * something other than the default (~1ms minimum between interrupts).
	 * See section 5.5.1.2.
	 */
	init_completion(&xhci->addr_dev);
	for (i = 0; i < MAX_HC_SLOTS; ++i)
		xhci->devs[i] = 0;

	if (scratchpad_alloc(xhci, flags))
		goto fail;

	return 0;

fail:
	xhci_warn(xhci, "Couldn't initialize memory\n");
	xhci_mem_cleanup(xhci);
	return -ENOMEM;
}
