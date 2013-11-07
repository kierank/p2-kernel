/*
 * drivers/mmc/host/esdhc.h
 *
 * Copyright (C) 2007 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Author: Xiaobo Xie <X.Xie@freescale.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

/*
 * Controller registers (Big Endian)
 */
/* DMA System Address Register */
#define ESDHC_DMA_ADDRESS	0x00

/* Block Attributes Register */
#define ESDHC_BLOCK_ATTR	0x04
#define ESDHC_BLOCK_SIZE_MASK	0x00000fff
#define ESDHC_BLCOK_CNT_MASK	0xffff0000
#define ESDHC_MAKE_BLKSZ(dma, blksz) (((dma & 0x7) << 12) | (blksz & 0xFFF))

/* Command Argument */
#define ESDHC_ARGUMENT		0x08

/* Transfer Type Register */
#define ESDHC_COMMAND		0x0C

#define ESDHC_TRNS_DMA		0x00000001
#define ESDHC_TRNS_BLK_CNT_EN	0x00000002
#define ESDHC_TRNS_ACMD12	0x00000004
#define ESDHC_TRNS_READ		0x00000010
#define ESDHC_TRNS_MULTI	0x00000020

#define ESDHC_CMD_RESP_MASK	0x00030000
#define ESDHC_CMD_CRC_EN	0x00080000
#define ESDHC_CMD_INDEX_EN	0x00100000
#define ESDHC_CMD_DATA		0x00200000
#define ESDHC_CMD_TYPE_MASK	0x00c00000
#define ESDHC_CMD_INDEX		0x3f000000

#define ESDHC_CMD_RESP_NONE	0x00000000
#define ESDHC_CMD_RESP_LONG	0x00010000
#define ESDHC_CMD_RESP_SHORT	0x00020000
#define ESDHC_CMD_RESP_SHORT_BUSY 0x00030000

#define ESDHC_MAKE_CMD(c, f) (((c & 0xff) << 24) | (f & 0xfb0037))

/* Response Register */
#define ESDHC_RESPONSE		0x10

/* Buffer Data Port Register */
#define ESDHC_BUFFER		0x20

/* Present State Register */
#define ESDHC_PRESENT_STATE	0x24
#define  ESDHC_CMD_INHIBIT	0x00000001
#define  ESDHC_DATA_INHIBIT	0x00000002
#define  ESDHC_DOING_WRITE	0x00000100
#define  ESDHC_DOING_READ	0x00000200
#define  ESDHC_SPACE_AVAILABLE	0x00000400
#define  ESDHC_DATA_AVAILABLE	0x00000800
#define  ESDHC_CARD_PRESENT	0x00010000
#define  ESDHC_WRITE_PROTECT	0x00080000

/* Protocol control Register */
#define ESDHC_PROTOCOL_CONTROL 	0x28

#define ESDHC_CTRL_BUS_MASK	0x00000006
#define ESDHC_CTRL_4BITBUS	0x00000002
#define ESDHC_CTRL_D3_DETEC	0x00000008
#define ESDHC_CTRL_DTCT_EN	0x00000080
#define ESDHC_CTRL_DTCT_STATUS	0x00000040
#define ESDHC_CTRL_WU_CRM	0x04000000
#define ESDHC_CTRL_WU_CINS	0x02000000
#define ESDHC_CTRL_WU_CINT	0x01000000

/* System Control Register */
#define ESDHC_SYSTEM_CONTROL	0x2C

#define ESDHC_CLOCK_MASK	0x0000fff0
#define ESDHC_CLOCK_DEFAULT	0x00008000
#define ESDHC_PREDIV_SHIFT	8
#define ESDHC_DIVIDER_SHIFT	4
#define ESDHC_CLOCK_CARD_EN	0x00000004
#define ESDHC_CLOCK_INT_STABLE	0x00000002
#define ESDHC_CLOCK_INT_EN	0x00000001

#define ESDHC_TIMEOUT_MASK	0x000f0000
#define ESDHC_TIMEOUT_SHIFT	16

#define ESDHC_RESET_SHIFT	24
#define ESDHC_RESET_ALL		0x01
#define ESDHC_RESET_CMD		0x02
#define ESDHC_RESET_DATA	0x04
#define ESDHC_INIT_CARD		0x08

/* Interrupt Register */
#define ESDHC_INT_STATUS	0x30
#define ESDHC_INT_ENABLE	0x34
#define ESDHC_SIGNAL_ENABLE	0x38

#define ESDHC_INT_RESPONSE	0x00000001
#define ESDHC_INT_DATA_END	0x00000002
#define ESDHC_INT_DMA_END	0x00000008
#define ESDHC_INT_SPACE_AVAIL	0x00000010
#define ESDHC_INT_DATA_AVAIL	0x00000020
#define ESDHC_INT_CARD_INSERT	0x00000040
#define ESDHC_INT_CARD_REMOVE	0x00000080
#define ESDHC_INT_CARD_INT	0x00000100

#define ESDHC_INT_TIMEOUT	0x00010000
#define ESDHC_INT_CRC		0x00020000
#define ESDHC_INT_END_BIT	0x00040000
#define ESDHC_INT_INDEX		0x00080000
#define ESDHC_INT_DATA_TIMEOUT	0x00100000
#define ESDHC_INT_DATA_CRC	0x00200000
#define ESDHC_INT_DATA_END_BIT	0x00400000
#define ESDHC_INT_ACMD12ERR	0x01000000
#define ESDHC_INT_DMAERR	0x10000000

#define ESDHC_INT_NORMAL_MASK	0x00007FFF
#define ESDHC_INT_ERROR_MASK	0xFFFF8000

#define ESDHC_INT_CMD_MASK	(ESDHC_INT_RESPONSE | ESDHC_INT_TIMEOUT | \
		ESDHC_INT_CRC | ESDHC_INT_END_BIT | ESDHC_INT_INDEX)
#define ESDHC_INT_DATA_MASK	(ESDHC_INT_DATA_END | ESDHC_INT_DMA_END | \
		ESDHC_INT_DATA_AVAIL | ESDHC_INT_SPACE_AVAIL | \
		ESDHC_INT_DATA_TIMEOUT | ESDHC_INT_DATA_CRC | \
		ESDHC_INT_DATA_END_BIT)

#define ESDHC_INT_INSERT_MASK (ESDHC_INT_DATA_END_BIT | ESDHC_INT_DATA_CRC | \
		ESDHC_INT_DATA_TIMEOUT | ESDHC_INT_INDEX | \
		ESDHC_INT_END_BIT | ESDHC_INT_CRC | ESDHC_INT_TIMEOUT | \
		ESDHC_INT_DATA_AVAIL | ESDHC_INT_SPACE_AVAIL | \
		ESDHC_INT_DMA_END | ESDHC_INT_DATA_END | \
		ESDHC_INT_RESPONSE | ESDHC_INT_CARD_REMOVE)

#define ESDHC_INT_REMOVE_MASK (ESDHC_INT_DATA_END_BIT | ESDHC_INT_DATA_CRC | \
		ESDHC_INT_DATA_TIMEOUT | ESDHC_INT_INDEX | \
		ESDHC_INT_END_BIT | ESDHC_INT_CRC | ESDHC_INT_TIMEOUT | \
		ESDHC_INT_DATA_AVAIL | ESDHC_INT_SPACE_AVAIL | \
		ESDHC_INT_DMA_END | ESDHC_INT_DATA_END | \
		ESDHC_INT_RESPONSE | ESDHC_INT_CARD_INSERT)

/* Auto CMD12 Error Status Register */
#define ESDHC_ACMD12_ERR	0x3C

/* 3E-3F reserved */
/* Host Controller Capabilities */
#define ESDHC_CAPABILITIES	0x40

#define ESDHC_MAX_BLOCK_MASK	0x00070000
#define ESDHC_MAX_BLOCK_SHIFT	16
#define ESDHC_CAN_DO_HISPD	0x00200000
#define ESDHC_CAN_DO_DMA	0x00400000
#define ESDHC_CAN_DO_SUSPEND	0x00800000
#define ESDHC_CAN_VDD_330	0x01000000
#define ESDHC_CAN_VDD_300	0x02000000
#define ESDHC_CAN_VDD_180	0x04000000

/* Watermark Level Register */
#define ESDHC_WML		0x44
#define ESDHC_WML_MASK		0xff
#define ESDHC_WML_READ_SHIFT	0
#define ESDHC_WML_WRITE_SHIFT	16

/* 45-4F reserved for more caps and max curren*/

/* Force Event Register */
#define ESDHC_FORCE_EVENT	0x50

/* 54-FB reserved */

/* Host Controller Version Register */
#define ESDHC_HOST_VERSION	0xFC

#define ESDHC_VENDOR_VER_MASK	0xFF00
#define ESDHC_VENDOR_VER_SHIFT	8
#define ESDHC_SPEC_VER_MASK	0x00FF
#define ESDHC_SPEC_VER_SHIFT	0

#define ESDHC_DMA_SYSCTL	0x40C
#define ESDHC_DMA_SNOOP		0x00000040

#define ESDHC_SLOTS_NUMBER	1

/* The SCCR[SDHCCM] Register */
#define MPC837X_SCCR_OFFS	0xA08
#define MPC837X_SDHCCM_MASK	0x0c000000
#define MPC837X_SDHCCM_SHIFT	26

static inline u32 fsl_readl(unsigned __iomem *addr)
{
	u32 val;
	val = in_be32(addr);
	return val;
}

static inline void fsl_writel(unsigned __iomem *addr, u32 val)
{
	out_be32(addr, val);
}

struct esdhc_chip;

struct esdhc_host {
	struct esdhc_chip	*chip;
	struct mmc_host		*mmc;		/* MMC structure */

	spinlock_t		lock;		/* Mutex */

	int			flags;		/* Host attributes */
#define ESDHC_USE_DMA		(1<<0)

	unsigned int		max_clk;	/* Max possible freq (MHz) */
	unsigned int		timeout_clk;	/* Timeout freq (KHz) */

	unsigned int		clock;		/* Current clock (MHz) */
	unsigned short		power;		/* Current voltage */
	unsigned short		bus_width;	/* current bus width */

	struct mmc_request	*mrq;		/* Current request */
	struct mmc_command	*cmd;		/* Current command */
	struct mmc_data		*data;		/* Current data request */

	struct scatterlist	*cur_sg;	/* We're working on this */
	int			num_sg;		/* Entries left */
	int			offset;		/* Offset into current sg */
	int			remain;		/* Bytes left in current */

	char			slot_descr[20];	/* Name for reservations */

	int			card_insert;

	int			irq;		/* Device IRQ */
	unsigned long		addr;		/* Bus address */
	unsigned int		size;		/* IO size */
	void __iomem		*ioaddr;	/* Mapped address */

	struct tasklet_struct	card_tasklet;	/* Tasklet structures */
	struct tasklet_struct	finish_tasklet;

	struct timer_list	timer;		/* Timer for timeouts */
};

struct esdhc_chip {
	struct of_device	*ofdev;

	unsigned long		quirks;

	int			num_slots;	/* Slots on controller */
	struct esdhc_host	*hosts[0];	/* Pointers to hosts */
};
