/*
 * Support for indirect PCI bridges.
 *
 * Copyright (C) 1998 Gabriel Paubert.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
/* $Id: indirect_pci.c 11618 2011-01-12 08:21:34Z Noguchi Isao $ */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <asm/machdep.h>

#if defined(CONFIG_PPC_MPC83XX_PCIE)
#include "mpc83xx_pci.h"
#endif

static int
indirect_read_config(struct pci_bus *bus, unsigned int devfn, int offset,
		     int len, u32 *val)
{
	struct pci_controller *hose = bus->sysdata;
	volatile void __iomem *cfg_data;
	u8 cfg_type = 0;
	u32 bus_no, reg;

	if (hose->indirect_type & PPC_INDIRECT_TYPE_NO_PCIE_LINK) {
		if (bus->number != hose->first_busno)
			return PCIBIOS_DEVICE_NOT_FOUND;
		if (devfn != 0)
			return PCIBIOS_DEVICE_NOT_FOUND;
	}

	if (ppc_md.pci_exclude_device)
		if (ppc_md.pci_exclude_device(hose, bus->number, devfn))
			return PCIBIOS_DEVICE_NOT_FOUND;

	if (hose->indirect_type & PPC_INDIRECT_TYPE_SET_CFG_TYPE)
		if (bus->number != hose->first_busno)
			cfg_type = 1;

	bus_no = (bus->number == hose->first_busno) ?
			hose->self_busno : bus->number;

	if (hose->indirect_type & PPC_INDIRECT_TYPE_EXT_REG)
		reg = ((offset & 0xf00) << 16) | (offset & 0xfc);
	else
		reg = offset & 0xfc;

	if (hose->indirect_type & PPC_INDIRECT_TYPE_BIG_ENDIAN)
		out_be32(hose->cfg_addr, (0x80000000 | (bus_no << 16) |
			 (devfn << 8) | reg | cfg_type));
	else
		out_le32(hose->cfg_addr, (0x80000000 | (bus_no << 16) |
			 (devfn << 8) | reg | cfg_type));

	/*
	 * Note: the caller has already checked that offset is
	 * suitably aligned and that len is 1, 2 or 4.
	 */
	cfg_data = hose->cfg_data + (offset & 3);
	switch (len) {
	case 1:
		*val = in_8(cfg_data);
		break;
	case 2:
		*val = in_le16(cfg_data);
		break;
	default:
		*val = in_le32(cfg_data);
		break;
	}
	return PCIBIOS_SUCCESSFUL;
}

static int
indirect_write_config(struct pci_bus *bus, unsigned int devfn, int offset,
		      int len, u32 val)
{
	struct pci_controller *hose = bus->sysdata;
	volatile void __iomem *cfg_data;
	u8 cfg_type = 0;
	u32 bus_no, reg;

	if (hose->indirect_type & PPC_INDIRECT_TYPE_NO_PCIE_LINK) {
		if (bus->number != hose->first_busno)
			return PCIBIOS_DEVICE_NOT_FOUND;
		if (devfn != 0)
			return PCIBIOS_DEVICE_NOT_FOUND;
	}

	if (ppc_md.pci_exclude_device)
		if (ppc_md.pci_exclude_device(hose, bus->number, devfn))
			return PCIBIOS_DEVICE_NOT_FOUND;

	if (hose->indirect_type & PPC_INDIRECT_TYPE_SET_CFG_TYPE)
		if (bus->number != hose->first_busno)
			cfg_type = 1;

	bus_no = (bus->number == hose->first_busno) ?
			hose->self_busno : bus->number;

	if (hose->indirect_type & PPC_INDIRECT_TYPE_EXT_REG)
		reg = ((offset & 0xf00) << 16) | (offset & 0xfc);
	else
		reg = offset & 0xfc;

	if (hose->indirect_type & PPC_INDIRECT_TYPE_BIG_ENDIAN)
		out_be32(hose->cfg_addr, (0x80000000 | (bus_no << 16) |
			 (devfn << 8) | reg | cfg_type));
	else
		out_le32(hose->cfg_addr, (0x80000000 | (bus_no << 16) |
			 (devfn << 8) | reg | cfg_type));

	/* surpress setting of PCI_PRIMARY_BUS */
	if (hose->indirect_type & PPC_INDIRECT_TYPE_SURPRESS_PRIMARY_BUS)
		if ((offset == PCI_PRIMARY_BUS) &&
			(bus->number == hose->first_busno))
		val &= 0xffffff00;

	/* Workaround for PCI_28 Errata in 440EPx/GRx */
	if ((hose->indirect_type & PPC_INDIRECT_TYPE_BROKEN_MRM) &&
			offset == PCI_CACHE_LINE_SIZE) {
		val = 0;
	}

	/*
	 * Note: the caller has already checked that offset is
	 * suitably aligned and that len is 1, 2 or 4.
	 */
	cfg_data = hose->cfg_data + (offset & 3);
	switch (len) {
	case 1:
		out_8(cfg_data, val);
		break;
	case 2:
		out_le16(cfg_data, val);
		break;
	default:
		out_le32(cfg_data, val);
		break;
	}
	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops indirect_pci_ops =
{
	.read = indirect_read_config,
	.write = indirect_write_config,
};

void __init
setup_indirect_pci(struct pci_controller* hose,
		   resource_size_t cfg_addr,
		   resource_size_t cfg_data, u32 flags)
{
	resource_size_t base = cfg_addr & PAGE_MASK;
	void __iomem *mbase;

	mbase = ioremap(base, PAGE_SIZE);
	hose->cfg_addr = mbase + (cfg_addr & ~PAGE_MASK);
	if ((cfg_data & PAGE_MASK) != base)
		mbase = ioremap(cfg_data & PAGE_MASK, PAGE_SIZE);
	hose->cfg_data = mbase + (cfg_data & ~PAGE_MASK);
	hose->ops = &indirect_pci_ops;
	hose->indirect_type = flags;
}

#if defined(CONFIG_PPC_MPC83XX_PCIE)

/* PCIe R/W config space routines */
static int
indirect_read_config_pcie(struct pci_bus *bus, uint devfn, int offset,
			int len, u32 *val)
{
	struct pci_controller *hose = bus->sysdata;
	void __iomem *cfg_addr;
	u32 bus_no;

	if (hose->indirect_type & PPC_INDIRECT_TYPE_NO_PCIE_LINK)
		return PCIBIOS_DEVICE_NOT_FOUND;

	switch (len) {
	case 2:
		if (offset & 1)
			return -EINVAL;
		break;
	case 4:
		if (offset & 3)
			return -EINVAL;
		break;
	}

	pr_debug("_read_cfg_pcie: bus=%d devfn=%x off=%x len=%x\n",
		bus->number, devfn, offset, len);

	if (devfn)
		return PCIBIOS_DEVICE_NOT_FOUND;

	bus_no = (bus->number == hose->first_busno) ?
			hose->self_busno : bus->number;

/* 2011/1/12, modified by Panasonic (SAV) */
/* 	cfg_addr = (void __iomem *)((ulong) hose->cfg_addr + */
/* 			((bus_no << 20) | (devfn << 12) | (offset & 0xfff))); */
	cfg_addr = (void __iomem *)((ulong) hose->cfg_addr +
			((bus_no << 24) | (devfn << 16) | (offset & 0xfff)));

	switch (len) {
	case 1:
		*val = in_8(cfg_addr);
		break;
	case 2:
		*val = in_le16(cfg_addr);
		break;
	default:
		*val = in_le32(cfg_addr);
		break;
	}
	pr_debug("_read_cfg_pcie: val=%x cfg_addr=%p\n", *val, cfg_addr);

	return PCIBIOS_SUCCESSFUL;
}

static int
indirect_write_config_pcie(struct pci_bus *bus, uint devfn, int offset,
			   int len, u32 val)
{
	struct pci_controller *hose = bus->sysdata;
	void __iomem *cfg_addr;
	u32 bus_no;

	if (hose->indirect_type & PPC_INDIRECT_TYPE_NO_PCIE_LINK)
		return PCIBIOS_DEVICE_NOT_FOUND;

	switch (len) {
	case 2:
		if (offset & 1)
			return -EINVAL;
		break;
	case 4:
		if (offset & 3)
			return -EINVAL;
		break;
	}

	if (devfn)
		return PCIBIOS_DEVICE_NOT_FOUND;

	bus_no = (bus->number == hose->first_busno) ?
			hose->self_busno : bus->number;

/* 2011/1/12, modified by Panasonic (SAV) */
/* 	cfg_addr = (void __iomem *)((ulong) hose->cfg_addr + */
/* 			((bus_no << 20) | (devfn << 12) | (offset & 0xfff))); */
	cfg_addr = (void __iomem *)((ulong) hose->cfg_addr +
			((bus_no << 24) | (devfn << 16) | (offset & 0xfff)));

	switch (len) {
	case 1:
		out_8(cfg_addr, val);
		break;
	case 2:
		out_le16(cfg_addr, val);
		break;
	default:
		out_le32(cfg_addr, val);
		break;
	}

	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops indirect_pcie_ops = {
	indirect_read_config_pcie,
	indirect_write_config_pcie
};

void __init
setup_indirect_pcie_nomap(struct pci_controller *hose, void __iomem *cfg_addr)
{
	hose->cfg_addr = cfg_addr;
	hose->ops = &indirect_pcie_ops;
}

/* 2011/1/12, modified by Panasonic (SAV) */
/* void __init setup_indirect_pcie(struct pci_controller *hose, u32 cfg_addr) */
void __init setup_indirect_pcie(struct pci_controller *hose, u32 cfg_addr, u32 cfg_size)
{
	ulong base = cfg_addr & PAGE_MASK;
	void __iomem *mbase, *addr;

/* 2011/1/12, modified by Panasonic (SAV) ---> */
/* 	mbase = ioremap(base, 0x1000); /\* CFG-space of PCIe is 4KB: 2010/7/24, modified by Panasonic (SAV) *\/ */
	mbase = ioremap(base, cfg_size);
/* <--- 2011/1/12, modified by Panasonic (SAV) */
	addr = mbase + (cfg_addr & ~PAGE_MASK);
	setup_indirect_pcie_nomap(hose, addr);
}
#endif /* CONFIG_PPC_MPC83XX_PCIE */
