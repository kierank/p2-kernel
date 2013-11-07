/*
 * Copyright (C) 2007-2008 Freescale Semiconductor, Inc.
 *
 * Author: Vivek Mahajan <vivek.mahajan@freescale.com>
 *
 * Description:
 * IPIC MSI routine implementations
 *
 * Changlelog:
 * Fri Aug 31 2007 Tony Li <tony.li@freescale.com>
 * - Add multi msi irq support and status tracking
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2 of the
 * License.
 */
/* $Id: mpc83xx_msi.c 9139 2010-09-14 00:43:11Z Noguchi Isao $ */

#if defined(CONFIG_PPC_MPC83XX_PCIE)
#include <linux/irq.h>
#include <linux/bootmem.h>
#include <linux/msi.h>
#include <linux/pci.h>
/* #include "mpc83xx.h" */
#include "platforms/83xx/mpc83xx.h" /* 2010/6/8, modified by Panasonic */
#include <asm/ipic.h>
#include <asm/prom.h>
#include <asm/hw_irq.h>

#include "ipic.h"

static struct ipic *msi_ipic;
static u8 msi_used[MSI_REGS];
u8 vec_83xx[MSI_REGS];

irq_hw_number_t ipic_msi_alloc_hwirqs(struct ipic *msi_ipic, int num)
{
	ulong flags;
	int offset, order = get_count_order(num);

	spin_lock_irqsave(&msi_ipic->bitmap_lock, flags);
	offset = bitmap_find_free_region(msi_ipic->hwirq_bitmap,
							MSI_REGS, order);
	spin_unlock_irqrestore(&msi_ipic->bitmap_lock, flags);

	pr_debug("ipic_msi_alloc_hwirqs: allocated \
		0x%x (2^%d) at offset 0x%x\n", num, order, offset);

	return offset;
}

void ipic_msi_free_hwirqs(struct ipic *msi_ipic, int offset, int num)
{
	ulong flags;
	int i;
	int order = get_count_order(num);

	pr_debug("ipic_msi_free_hwirqs: freeing 0x%x (2^%d) at offset 0x%x\n",
		 num, order, offset);

	for (i = 0; i < MSI_REGS; i++) {
		if (vec_83xx[i] == offset)
			msi_used[i] = 0;
	}

	spin_lock_irqsave(&msi_ipic->bitmap_lock, flags);
	bitmap_release_region(msi_ipic->hwirq_bitmap, offset, order);
	spin_unlock_irqrestore(&msi_ipic->bitmap_lock, flags);
}

static int ipic_msi_reserve_hwirqs(struct ipic *msi_ipic)
{
	int i, len, has_address = 0;
	const u32 *p;
	struct device_node *dn;
	struct resource rsrc;

	for (dn = NULL; (dn = of_find_node_by_type(dn, "pci")) != NULL;) {
		has_address = (of_address_to_resource(dn, 0, &rsrc) == 0);
		if (((rsrc.start & 0xFFFFF) == 0x9000) ||
			((rsrc.start & 0xFFFFF) == 0xA000))
			break;
	}

	p = of_get_property(dn, "msi-available-ranges", &len);
	if (!p) {
		printk(KERN_ERR"ipic_msi_reserve_hwirqs: no \
			msi-available-ranges property found on %s\n",
				dn->full_name);
		return -ENODEV;
	}

	if (len % 8) {
		printk(KERN_WARNING"ipic_msi_reserve_hwirqs: \
			Malformed msi-available-ranges property on %s\n",
				dn->full_name);
		return -EINVAL;
	}

	for (i = 0; i < MSI_REGS; i++) {
		vec_83xx[i] = *p++;
		msi_used[i] = 0;
	}

	i = bitmap_allocate_region(msi_ipic->hwirq_bitmap,
					0, get_count_order(MSI_REGS));
	if (i) {
		printk("ipic_msi_reserve_hwirqs: \
			bimap_allocate_region() FAILS ** \n");
		return -EBUSY;
	}

	return 0;
}

int ipic_msi_init_allocator(struct ipic *msi_ipic)
{
	int rc, size;

	BUG_ON(msi_ipic->hwirq_bitmap);
	spin_lock_init(&msi_ipic->bitmap_lock);

	size = 8 * BITS_TO_LONGS(MSI_REGS) * sizeof(long);
	pr_debug("ipic_msi_init_allocator: allocator bitmap \
			size is 0x%x bytes\n", size);

	if (mem_init_done)
		msi_ipic->hwirq_bitmap = kmalloc(size, GFP_KERNEL);
	else
		msi_ipic->hwirq_bitmap = alloc_bootmem(size);

	if (!msi_ipic->hwirq_bitmap) {
		printk("ipic_msi_init_allocator: ENOMEM allocator bitmap !\n");
		return -ENOMEM;
	}

	memset(msi_ipic->hwirq_bitmap, 0, size);

	rc = ipic_msi_reserve_hwirqs(msi_ipic);
	if (rc) {
		printk(KERN_ERR"ipic_msi_init_allocator: \
			ipic_msi_reserve_hwirqs() FAILS ** rc = %x\n", rc);
		goto out_free1;
	}

	return 0;

out_free1:
	if (mem_init_done)
		kfree(msi_ipic->hwirq_bitmap);

	msi_ipic->hwirq_bitmap = NULL;
	return rc;
}

void mpc83xx_mask_msi_irq(uint irq)
{
	ipic_mask_irq(irq);
	mask_msi_irq(irq);
}
EXPORT_SYMBOL(mpc83xx_mask_msi_irq);

void mpc83xx_unmask_msi_irq(uint irq)
{
	ipic_unmask_irq(irq);
	unmask_msi_irq(irq);
}
EXPORT_SYMBOL(mpc83xx_unmask_msi_irq);

void mpc83xx_ack_msi_irq(uint irq)
{
	ipic_ack_irq(irq);
}
EXPORT_SYMBOL(mpc83xx_ack_msi_irq);

static struct irq_chip mpc83xx_msi_chip = {
	.typename = " MSI ",
	.unmask = mpc83xx_unmask_msi_irq,
	.mask = mpc83xx_mask_msi_irq,
	.ack = mpc83xx_ack_msi_irq,
	.shutdown = mpc83xx_mask_msi_irq,
	.set_type = ipic_set_irq_type,
};

static int mpc83xx_msi_check_device(struct pci_dev *pdev, int nvec, int type)
{
	if (type == PCI_CAP_ID_MSIX)
		printk("mpc83xx_msi_check_device: \
			MSI-X untested, trying anyway.\n");

	return 0;
}

static void mpc83xx_teardown_msi_irqs(struct pci_dev *pdev)
{
	struct msi_desc *entry;

	pr_debug("mpc83xx_teardown_msi_irqs: pdev=%p ipic=%p\n", pdev,
		 msi_ipic);
	list_for_each_entry(entry, &pdev->msi_list, list) {
		if (entry->irq == NO_IRQ)
			continue;

		set_irq_msi(entry->irq, NULL);
		ipic_msi_free_hwirqs(msi_ipic, virq_to_hw(entry->irq), 1);
		irq_dispose_mapping(entry->irq);
	}
}

static void mpc83xx_compose_msi_msg(struct pci_dev *pdev,
					uint irq, struct msi_msg *msg)
{
	u32 i, bit = 0;

	pr_debug("mpc83xx_compose_msi_msg: pdev=%p irq=%x msg=%p\n",
			pdev, irq, msg);
	msg->address_hi = MSI_ADDR_BASE_HI;
	msg->address_lo = MSI_ADDR_BASE_LO;
	for (i = 0; i < MSI_REGS; i++) {
		if (irq == vec_83xx[i]) {
			msg->data = (i << 5) | (~bit & 0x1F);
			break;
		}
	}
}

static int mpc83xx_setup_msi_irqs(struct pci_dev *pdev, int nvec, int type)
{
	irq_hw_number_t hwirq;
	int rc;
	int i;
	uint virq;
	struct msi_desc *entry;
	struct msi_msg msg;

	pr_debug("mpc83xx_setup_msi_irqs: pdev=%p nvec=%d ipic=%p\n", pdev,
		 nvec, msi_ipic);
	list_for_each_entry(entry, &pdev->msi_list, list) {
		hwirq = -1;
		for (i = 0; i < MSI_REGS; i++) {
			if (msi_used[i] == 0) {
				hwirq = vec_83xx[i];
				msi_used[i] = 1;
				break;
			}
		}
		if (hwirq < 0) {
			rc = hwirq;
			printk("mpc83xx_setup_msi: failed allocating hwirq\n");
			goto out_free;
		}
		pr_debug("mpc83xx_setup_msi_irqs: entry->irq = 0x%x \
				msi_used = 0x%x hwirq = 0x%lx irqhost = %p\n",
			 entry->irq, i, hwirq, msi_ipic->irqhost);

		virq = irq_create_mapping(msi_ipic->irqhost, hwirq);
		if (virq == NO_IRQ) {
			printk(KERN_ERR"mpc83xx_setup_msi_irqs: \
				failed mapping hwirq 0x%lx\n", hwirq);
			ipic_msi_free_hwirqs(msi_ipic, hwirq, 1);
			rc = -ENOSPC;
			goto out_free;
		}
		pr_debug("mpc83xx_setup_msi_irqs: virq=0x%x entry=%p\n", virq,
			 entry);

		set_irq_msi(virq, entry);
		set_irq_chip(virq, &mpc83xx_msi_chip);
		set_irq_type(virq, IRQ_TYPE_EDGE_FALLING);
		ipic_set_priority(virq, 2);

		mpc83xx_compose_msi_msg(pdev, hwirq, &msg);
		write_msi_msg(virq, &msg);
	}

	return 0;

out_free:
	mpc83xx_teardown_msi_irqs(pdev);
	return rc;
}

int mpc83xx_msi_init(struct ipic *ipic)
{
	int rc;

	rc = ipic_msi_init_allocator(ipic);
	if (rc) {
		printk(KERN_ERR"mpc83xx_msi_init: Error allocating bitmap!\n");
		return rc;
	}

	pr_debug("mpc83xx_msi_init: Registering MSI callbacks.\n");

	BUG_ON(msi_ipic);
	msi_ipic = ipic;

	WARN_ON(ppc_md.setup_msi_irqs);
	ppc_md.setup_msi_irqs = mpc83xx_setup_msi_irqs;
	ppc_md.teardown_msi_irqs = mpc83xx_teardown_msi_irqs;
	ppc_md.msi_check_device = mpc83xx_msi_check_device;

	return 0;
}
#endif
