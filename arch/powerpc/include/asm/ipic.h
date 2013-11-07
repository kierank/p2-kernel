/*
 * IPIC external definitions and structure.
 *
 * Maintainer: Kumar Gala <galak@kernel.crashing.org>
 *
 * Copyright 2005 Freescale Semiconductor, Inc
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#ifdef __KERNEL__
#ifndef __ASM_IPIC_H__
#define __ASM_IPIC_H__

#include <linux/irq.h>

/* Flags when we init the IPIC */
#define IPIC_SPREADMODE_GRP_A	0x00000001
#define IPIC_SPREADMODE_GRP_B	0x00000002
#define IPIC_SPREADMODE_GRP_C	0x00000004
#define IPIC_SPREADMODE_GRP_D	0x00000008
#define IPIC_SPREADMODE_MIX_A	0x00000010
#define IPIC_SPREADMODE_MIX_B	0x00000020
#define IPIC_DISABLE_MCP_OUT	0x00000040
#define IPIC_IRQ0_MCP		0x00000080

/* IPIC registers offsets */
#define IPIC_SICFR	0x00	/* System Global Interrupt Configuration Register */
#define IPIC_SIVCR	0x04	/* System Global Interrupt Vector Register */
#define IPIC_SIPNR_H	0x08	/* System Internal Interrupt Pending Register (HIGH) */
#define IPIC_SIPNR_L	0x0C	/* System Internal Interrupt Pending Register (LOW) */
#define IPIC_SIPRR_A	0x10	/* System Internal Interrupt group A Priority Register */
#define IPIC_SIPRR_B	0x14	/* System Internal Interrupt group B Priority Register */
#define IPIC_SIPRR_C	0x18	/* System Internal Interrupt group C Priority Register */
#define IPIC_SIPRR_D	0x1C	/* System Internal Interrupt group D Priority Register */
#define IPIC_SIMSR_H	0x20	/* System Internal Interrupt Mask Register (HIGH) */
#define IPIC_SIMSR_L	0x24	/* System Internal Interrupt Mask Register (LOW) */
#define IPIC_SICNR	0x28	/* System Internal Interrupt Control Register */
#define IPIC_SEPNR	0x2C	/* System External Interrupt Pending Register */
#define IPIC_SMPRR_A	0x30	/* System Mixed Interrupt group A Priority Register */
#define IPIC_SMPRR_B	0x34	/* System Mixed Interrupt group B Priority Register */
#define IPIC_SEMSR	0x38	/* System External Interrupt Mask Register */
#define IPIC_SECNR	0x3C	/* System External Interrupt Control Register */
#define IPIC_SERSR	0x40	/* System Error Status Register */
#define IPIC_SERMR	0x44	/* System Error Mask Register */
#define IPIC_SERCR	0x48	/* System Error Control Register */
#define IPIC_SIFCR_H	0x50	/* System Internal Interrupt Force Register (HIGH) */
#define IPIC_SIFCR_L	0x54	/* System Internal Interrupt Force Register (LOW) */
#define IPIC_SEFCR	0x58	/* System External Interrupt Force Register */
#define IPIC_SERFR	0x5C	/* System Error Force Register */
#define IPIC_SCVCR	0x60	/* System Critical Interrupt Vector Register */
#define IPIC_SMVCR	0x64	/* System Management Interrupt Vector Register */

/* IPIC MSI register (for 837x and 8315) */
#define IPIC_MSIR0	0xC0	/* Msg Shared Interrupt Reg 0 */
#define IPIC_MSIR1	0xC4	/* Msg Shared Interrupt Reg 1 */
#define IPIC_MSIR2	0xC8	/* Msg Shared Interrupt Reg 2 */
#define IPIC_MSIR3	0xCC	/* Msg Shared Interrupt Reg 3 */
#define IPIC_MSIR4	0xD0	/* Msg Shared Interrupt Reg 4 */
#define IPIC_MSIR5	0xD4	/* Msg Shared Interrupt Reg 5 */
#define IPIC_MSIR6	0xD8	/* Msg Shared Interrupt Reg 6 */
#define IPIC_MSIR7	0xDC	/* Msg Shared Interrupt Reg 7 */
#define IPIC_MSIMR	0xF0	/* Msg Shared Interrupt Mask Reg */
#define IPIC_MSISR	0xF4	/* Msg Shared Interrupt Status Reg */
#define IPIC_MSIIR	0xF8	/* Msg Shared Interrupt Index Reg */

enum ipic_prio_grp {
	IPIC_INT_GRP_A = IPIC_SIPRR_A,
	IPIC_INT_GRP_D = IPIC_SIPRR_D,
	IPIC_MIX_GRP_A = IPIC_SMPRR_A,
	IPIC_MIX_GRP_B = IPIC_SMPRR_B,
};

enum ipic_mcp_irq {
	IPIC_MCP_IRQ0 = 0,
	IPIC_MCP_WDT  = 1,
	IPIC_MCP_SBA  = 2,
	IPIC_MCP_PCI1 = 5,
	IPIC_MCP_PCI2 = 6,
	IPIC_MCP_MU   = 7,
};

struct ipic_83xx_s {
	u32 sicfr;
	u32 sivcr;
	u32 sipnr_h;
	u32 sipnr_l;
	u32 siprr_a;
	u32 siprr_b;
	u32 siprr_c;
	u32 siprr_d;
	u32 simsr_h;
	u32 simsr_l;
	u32 sicnr;
	u32 sepnr;
	u32 smprr_a;
	u32 smprr_b;
	u32 semsr;
	u32 secnr;
	u32 sersr;
	u32 sermr;
	u32 sercr;
	u32 sepcr;
 	u32 sifcr_h;
 	u32 sifcr_l;
 	u32 sefcr;
 	u32 serfr;
 	u32 scvcr;
 	u32 smvcr;
 	u8 res0[0x58];
 	u32 msir[8];
 	u8 res1[0x10];
 	u32 msimr;
 	u32 msisr;
 	u32 msiir;
 };

extern int ipic_set_priority(unsigned int irq, unsigned int priority);
extern void ipic_set_highest_priority(unsigned int irq);
extern void ipic_set_default_priority(void);
extern void ipic_enable_mcp(enum ipic_mcp_irq mcp_irq);
extern void ipic_disable_mcp(enum ipic_mcp_irq mcp_irq);
extern u32 ipic_get_mcp_status(void);
extern void ipic_clear_mcp_status(u32 mask);

extern struct ipic * ipic_init(struct device_node *node, unsigned int flags);
extern unsigned int ipic_get_irq(void);

#endif /* __ASM_IPIC_H__ */
#endif /* __KERNEL__ */
