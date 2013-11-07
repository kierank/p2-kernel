/*
 * drivers/mmc/host/esdhc.c
 *
 * Copyright (C) 2007 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Author: Xiaobo Xie <X.Xie@freescale.com>
 *
 * derived from sdhci.c by Pierre Ossman
 *
 * Description:
 * Freescale Enhanced Secure Digital Host Controller driver.
 * The Controller is used in MPC837xE cpus.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/highmem.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <linux/of_platform.h>
#include <linux/uaccess.h>
#include <linux/irq.h>
#include <linux/io.h>

#include <linux/mmc/host.h>

#include <asm/dma.h>
#include <asm/page.h>
#include <asm/reg.h>
#include <sysdev/fsl_soc.h>

#include "esdhc.h"

#define DRIVER_NAME "esdhc"

#ifdef DEBUG
#define DBG(fmt, args...) 	printk(KERN_DEBUG "[%s]  " fmt "\n", \
				__FUNCTION__, ## args)
#else
#define DBG(fmt, args...)	do {} while (0)
#endif

static unsigned int debug_nodma;
static unsigned int debug_forcedma;
static unsigned int debug_quirks;

#define ESDHC_QUIRK_CLOCK_BEFORE_RESET			(1<<0)
#define ESDHC_QUIRK_FORCE_DMA				(1<<1)
#define ESDHC_QUIRK_NO_CARD_NO_RESET			(1<<2)
#define ESDHC_QUIRK_SINGLE_POWER_WRITE			(1<<3)

static void esdhc_prepare_data(struct esdhc_host *, struct mmc_data *);
static void esdhc_finish_data(struct esdhc_host *);

static void esdhc_send_command(struct esdhc_host *, struct mmc_command *);
static void esdhc_finish_command(struct esdhc_host *);

static void esdhc_dumpregs(struct esdhc_host *host)
{
	printk(KERN_DEBUG DRIVER_NAME ": ========= REGISTER DUMP ==========\n");

	printk(KERN_DEBUG DRIVER_NAME ": Sysaddr: 0x%08x | Blkattr: 0x%08x\n",
		fsl_readl(host->ioaddr + ESDHC_DMA_ADDRESS),
		fsl_readl(host->ioaddr + ESDHC_BLOCK_ATTR));
	printk(KERN_DEBUG DRIVER_NAME ": Argument: 0x%08x | COMMAND: 0x%08x\n",
		fsl_readl(host->ioaddr + ESDHC_ARGUMENT),
		fsl_readl(host->ioaddr + ESDHC_COMMAND));
	printk(KERN_DEBUG DRIVER_NAME ": Present: 0x%08x | DMA ctl: 0x%08x\n",
		fsl_readl(host->ioaddr + ESDHC_PRESENT_STATE),
		fsl_readl(host->ioaddr + ESDHC_DMA_SYSCTL));
	printk(KERN_DEBUG DRIVER_NAME ": PROCTL: 0x%08x | SYSCTL: 0x%08x\n",
		fsl_readl(host->ioaddr + ESDHC_PROTOCOL_CONTROL),
		fsl_readl(host->ioaddr + ESDHC_SYSTEM_CONTROL));
	printk(KERN_DEBUG DRIVER_NAME ": Int stat: 0x%08x\n",
		fsl_readl(host->ioaddr + ESDHC_INT_STATUS));
	printk(KERN_DEBUG DRIVER_NAME ": Intenab: 0x%08x | Sigenab: 0x%08x\n",
		fsl_readl(host->ioaddr + ESDHC_INT_ENABLE),
		fsl_readl(host->ioaddr + ESDHC_SIGNAL_ENABLE));
	printk(KERN_DEBUG DRIVER_NAME ": AC12 err: 0x%08x | Version: 0x%08x\n",
		fsl_readl(host->ioaddr + ESDHC_ACMD12_ERR),
		fsl_readl(host->ioaddr + ESDHC_HOST_VERSION));
	printk(KERN_DEBUG DRIVER_NAME ": Caps: 0x%08x | Watermark: 0x%08x\n",
		fsl_readl(host->ioaddr + ESDHC_CAPABILITIES),
		fsl_readl(host->ioaddr + ESDHC_WML));

	printk(KERN_DEBUG DRIVER_NAME ": ==================================\n");
}

/*****************************************************************************\
 *                                                                           *
 * Low level functions                                                       *
 *                                                                           *
\*****************************************************************************/

static void esdhc_reset(struct esdhc_host *host, u8 mask)
{
	unsigned long timeout;
	unsigned int sysctl;

	if (host->chip->quirks & ESDHC_QUIRK_NO_CARD_NO_RESET) {
		if (!(fsl_readl(host->ioaddr + ESDHC_PRESENT_STATE) &
			ESDHC_CARD_PRESENT))
			return;
	}

	setbits32(host->ioaddr + ESDHC_SYSTEM_CONTROL,
			(mask << ESDHC_RESET_SHIFT));

	if (mask & ESDHC_RESET_ALL) {
		host->clock = 0;
		host->bus_width = 0;
	}

	/* Wait max 100 ms */
	timeout = 100;

	/* hw clears the bit when it's done */
	sysctl = (mask << ESDHC_RESET_SHIFT);
	while (fsl_readl(host->ioaddr + ESDHC_SYSTEM_CONTROL) & sysctl) {
		if (timeout == 0) {
			printk(KERN_ERR "%s: Reset 0x%x never completed.\n",
				mmc_hostname(host->mmc), (int)mask);
			esdhc_dumpregs(host);
			return;
		}
		timeout--;
		mdelay(1);
	}
}

static void esdhc_init(struct esdhc_host *host)
{
	u32 intmask;

	esdhc_reset(host, ESDHC_RESET_ALL);

	setbits32(host->ioaddr + ESDHC_SYSTEM_CONTROL,
			(ESDHC_CLOCK_INT_EN | ESDHC_CLOCK_INT_STABLE));

	intmask = fsl_readl(host->ioaddr + ESDHC_INT_STATUS);
	fsl_writel(host->ioaddr + ESDHC_INT_STATUS, intmask);

	intmask = ESDHC_INT_DATA_END_BIT | ESDHC_INT_DATA_CRC |
		ESDHC_INT_DATA_TIMEOUT | ESDHC_INT_INDEX |
		ESDHC_INT_END_BIT | ESDHC_INT_CRC | ESDHC_INT_TIMEOUT |
		ESDHC_INT_DATA_AVAIL | ESDHC_INT_SPACE_AVAIL |
		ESDHC_INT_DMA_END | ESDHC_INT_DATA_END | ESDHC_INT_RESPONSE;

	if (host->card_insert)
		intmask |= ESDHC_INT_CARD_REMOVE;
	else
		intmask |= ESDHC_INT_CARD_INSERT;

	fsl_writel(host->ioaddr + ESDHC_INT_ENABLE, intmask);
	fsl_writel(host->ioaddr + ESDHC_SIGNAL_ENABLE, intmask);

	setbits32(host->ioaddr + ESDHC_DMA_SYSCTL, ESDHC_DMA_SNOOP);
}

static void reset_regs(struct esdhc_host *host)
{
	u32 intmask;

	intmask = fsl_readl(host->ioaddr + ESDHC_INT_STATUS);
	fsl_writel(host->ioaddr + ESDHC_INT_STATUS, intmask);

	intmask = ESDHC_INT_DATA_END_BIT | ESDHC_INT_DATA_CRC |
		ESDHC_INT_DATA_TIMEOUT | ESDHC_INT_INDEX |
		ESDHC_INT_END_BIT | ESDHC_INT_CRC | ESDHC_INT_TIMEOUT |
		ESDHC_INT_DATA_AVAIL | ESDHC_INT_SPACE_AVAIL |
		ESDHC_INT_DMA_END | ESDHC_INT_DATA_END | ESDHC_INT_RESPONSE;

	if (host->card_insert)
		intmask |= ESDHC_INT_CARD_REMOVE;
	else
		intmask |= ESDHC_INT_CARD_INSERT;

	fsl_writel(host->ioaddr + ESDHC_INT_ENABLE, intmask);
	fsl_writel(host->ioaddr + ESDHC_SIGNAL_ENABLE, intmask);

	if (host->bus_width == MMC_BUS_WIDTH_4) {
		intmask = fsl_readl(host->ioaddr + ESDHC_PROTOCOL_CONTROL);
		intmask |= ESDHC_CTRL_4BITBUS;
		fsl_writel(host->ioaddr + ESDHC_PROTOCOL_CONTROL, intmask);
	}
}

/*****************************************************************************
 *                                                                           *
 * Core functions                                                            *
 *                                                                           *
 *****************************************************************************/
/* Return the SG's virtual address */
static inline char *esdhc_sg_to_buffer(struct esdhc_host *host)
{
	return sg_virt(host->cur_sg);
}

static inline int esdhc_next_sg(struct esdhc_host *host)
{
	/*
	 * Skip to next SG entry.
	 */
	host->cur_sg++;
	host->num_sg--;

	/*
	 * Any entries left?
	 */
	if (host->num_sg > 0) {
		host->offset = 0;
		host->remain = host->cur_sg->length;
	}

	return host->num_sg;
}

static void esdhc_read_block_pio(struct esdhc_host *host)
{
	int blksize, chunk_remain;
	u32 data;
	char *buffer;
	int size;

	DBG("PIO reading\n");

	blksize = host->data->blksz;
	chunk_remain = 0;
	data = 0;

	buffer = esdhc_sg_to_buffer(host) + host->offset;

	while (blksize) {
		if (chunk_remain == 0) {
			data = fsl_readl(host->ioaddr + ESDHC_BUFFER);
			chunk_remain = min(blksize, 4);
		}

		size = min(host->remain, chunk_remain);

		chunk_remain -= size;
		blksize -= size;
		host->offset += size;
		host->remain -= size;

		while (size) {
			*buffer = data & 0xFF;
			buffer++;
			data >>= 8;
			size--;
		}

		if (host->remain == 0) {
			if (esdhc_next_sg(host) == 0) {
				BUG_ON(blksize != 0);
				return;
			}
			buffer = esdhc_sg_to_buffer(host);
		}
	}
}

static void esdhc_write_block_pio(struct esdhc_host *host)
{
	int blksize, chunk_remain;
	u32 data;
	char *buffer;
	int bytes, size;

	DBG("PIO writing\n");

	blksize = host->data->blksz;
	chunk_remain = 4;
	data = 0;

	bytes = 0;
	buffer = esdhc_sg_to_buffer(host) + host->offset;

	while (blksize) {
		size = min(host->remain, chunk_remain);

		chunk_remain -= size;
		blksize -= size;
		host->offset += size;
		host->remain -= size;

		while (size) {
			data >>= 8;
			data |= (u32)*buffer << 24;
			buffer++;
			size--;
		}

		if (chunk_remain == 0) {
			writel(data, host->ioaddr + ESDHC_BUFFER);
			chunk_remain = min(blksize, 4);
		}

		if (host->remain == 0) {
			if (esdhc_next_sg(host) == 0) {
				BUG_ON(blksize != 0);
				return;
			}
			buffer = esdhc_sg_to_buffer(host);
		}
	}
}

static void esdhc_transfer_pio(struct esdhc_host *host)
{
	u32 mask;

	BUG_ON(!host->data);

	if (host->num_sg == 0)
		return;

	if (host->data->flags & MMC_DATA_READ)
		mask = ESDHC_DATA_AVAILABLE;
	else
		mask = ESDHC_SPACE_AVAILABLE;

	while (fsl_readl(host->ioaddr + ESDHC_PRESENT_STATE) & mask) {
		if (host->data->flags & MMC_DATA_READ)
			esdhc_read_block_pio(host);
		else
			esdhc_write_block_pio(host);

		if (host->num_sg == 0)
			break;
	}

	DBG("PIO transfer complete.\n");
}

static void esdhc_prepare_data(struct esdhc_host *host, struct mmc_data *data)
{
	u8 count;
	unsigned blkattr = 0;
	unsigned target_timeout, current_timeout;
	unsigned int sysctl;

	WARN_ON(host->data);

	if (data == NULL)
		return;

	DBG("blksz %04x blks %04x flags %08x",
		data->blksz, data->blocks, data->flags);
	DBG("tsac %d ms nsac %d clk",
		data->timeout_ns / 1000000, data->timeout_clks);

	/* Sanity checks */
	BUG_ON(data->blksz * data->blocks > 524288);
	BUG_ON(data->blksz > host->mmc->max_blk_size);
	BUG_ON(data->blocks > 65535);

	if (host->clock == 0) {
		printk(KERN_ERR "%s: The SD_CLK isn't set\n",
			host->slot_descr);
		return;
	}
	/* timeout in us */
	target_timeout = data->timeout_ns / 1000 +
		(data->timeout_clks * 1000000) / host->clock;

	/*
	 * Figure out needed cycles.
	 * We do this in steps in order to fit inside a 32 bit int.
	 * The first step is the minimum timeout, which will have a
	 * minimum resolution of 6 bits:
	 * (1) 2^13*1000 > 2^22,
	 * (2) host->timeout_clk < 2^16
	 *     =>
	 *     (1) / (2) > 2^6
	 */
	count = 0;
	host->timeout_clk = host->clock/1000;

	current_timeout = (1 << 13) * 1000 / host->timeout_clk;
	while (current_timeout < target_timeout) {
		count++;
		current_timeout <<= 1;
		if (count >= 0xF)
			break;
	}

	if (count >= 0xF) {
		printk(KERN_WARNING "%s: Too large timeout requested!\n",
			mmc_hostname(host->mmc));
		count = 0xE;
	}
	if (data->blocks >= 0x50)
		count = 0xE;

	sysctl = fsl_readl(host->ioaddr + ESDHC_SYSTEM_CONTROL);
	sysctl &= (~ESDHC_TIMEOUT_MASK);
	fsl_writel(host->ioaddr + ESDHC_SYSTEM_CONTROL,
		sysctl | (count<<ESDHC_TIMEOUT_SHIFT));

	if (host->flags & ESDHC_USE_DMA) {
		int sg_count;
		unsigned int wml;
		unsigned int wml_value;

		sg_count = dma_map_sg(mmc_dev(host->mmc), data->sg,
					data->sg_len,
					(data->flags & MMC_DATA_READ)
					? DMA_FROM_DEVICE : DMA_TO_DEVICE);
		BUG_ON(sg_count != 1);

		fsl_writel(host->ioaddr + ESDHC_DMA_ADDRESS,
				sg_dma_address(data->sg));

		/* Disable the BRR and BWR interrupt */
		clrbits32(host->ioaddr + ESDHC_INT_ENABLE,
				(ESDHC_INT_DATA_AVAIL | ESDHC_INT_SPACE_AVAIL));
		clrbits32(host->ioaddr + ESDHC_SIGNAL_ENABLE,
				(ESDHC_INT_DATA_AVAIL | ESDHC_INT_SPACE_AVAIL));

		wml_value = data->blksz/4;
		if (data->flags & MMC_DATA_READ) {
			if (wml_value > 0x10)
				wml_value = 0x10;
			wml = (wml_value & ESDHC_WML_MASK) |
				((0x10 & ESDHC_WML_MASK)
				 << ESDHC_WML_WRITE_SHIFT);
		} else {
			if (wml_value > 0x80)
				wml_value = 0x80;
			wml = (0x10 & ESDHC_WML_MASK) |
				(((wml_value) & ESDHC_WML_MASK)
				 << ESDHC_WML_WRITE_SHIFT);
		}

		fsl_writel(host->ioaddr + ESDHC_WML, wml);
	} else {
		host->cur_sg = data->sg;
		host->num_sg = data->sg_len;

		host->offset = 0;
		host->remain = host->cur_sg->length;

		setbits32(host->ioaddr + ESDHC_INT_ENABLE,
				(ESDHC_INT_DATA_AVAIL | ESDHC_INT_SPACE_AVAIL));
		setbits32(host->ioaddr + ESDHC_SIGNAL_ENABLE,
				(ESDHC_INT_DATA_AVAIL | ESDHC_INT_SPACE_AVAIL));
	}

	/* We do not handle DMA boundaries */
	blkattr = data->blksz;
	blkattr |= (data->blocks << 16);
	fsl_writel(host->ioaddr + ESDHC_BLOCK_ATTR, blkattr);
}

static unsigned int esdhc_set_transfer_mode(struct esdhc_host *host,
	struct mmc_data *data)
{
	u32 mode = 0;

	WARN_ON(host->data);

	if (data == NULL)
		return 0;

	mode = ESDHC_TRNS_BLK_CNT_EN;
	if (data->blocks > 1)
		mode |= ESDHC_TRNS_MULTI;
	if (data->flags & MMC_DATA_READ)
		mode |= ESDHC_TRNS_READ;
	if (host->flags & ESDHC_USE_DMA)
		mode |= ESDHC_TRNS_DMA;

	return mode;
}

static void esdhc_finish_data(struct esdhc_host *host)
{
	struct mmc_data *data;
	u16 blocks;

	BUG_ON(!host->data);

	data = host->data;
	host->data = NULL;

	if (host->flags & ESDHC_USE_DMA)
		dma_unmap_sg(mmc_dev(host->mmc), data->sg, data->sg_len,
					(data->flags & MMC_DATA_READ)
					? DMA_FROM_DEVICE : DMA_TO_DEVICE);

	/*
	 * Controller doesn't count down when in single block mode.
	 */
	if ((data->blocks == 1) && (data->error == 0))
		blocks = 0;
	else
		blocks = fsl_readl(host->ioaddr + ESDHC_BLOCK_ATTR) >> 16;

	data->bytes_xfered = data->blksz * (data->blocks - blocks);

	if ((data->error == 0) && blocks) {
		printk(KERN_ERR "%s: Controller signalled completion even "
			"though there were blocks left.\n",
			mmc_hostname(host->mmc));
		data->error = -EIO;
	}

	if ((blocks == 0) && (data->error == -ETIMEDOUT)) {
		printk(KERN_ERR "Controller transmitted completion even "
			"though there were timeout error.\n");
		data->error = 0;
	}

	DBG("Ending data transfer (%d bytes)", data->bytes_xfered);

	if (data->stop) {
		/*
		 * The controller needs a reset of internal state machines
		 * upon error conditions.
		 */
		if (data->error != 0) {
			esdhc_reset(host, ESDHC_RESET_CMD);
			esdhc_reset(host, ESDHC_RESET_DATA);
			reset_regs(host);
		}

		esdhc_send_command(host, data->stop);
	} else
		tasklet_schedule(&host->finish_tasklet);
}

static void esdhc_send_command(struct esdhc_host *host, struct mmc_command *cmd)
{
	unsigned int flags;
	u32 mask;
	unsigned long timeout;

	WARN_ON(host->cmd);

	DBG("Sending cmd (%d)", cmd->opcode);

	/* Wait max 10 ms */
	timeout = 10;

	mask = ESDHC_CMD_INHIBIT;
	if ((cmd->data != NULL) || (cmd->flags & MMC_RSP_BUSY))
		mask |= ESDHC_DATA_INHIBIT;

	/* We shouldn't wait for data inihibit for stop commands, even
	   though they might use busy signaling */
	if (host->mrq->data && (cmd == host->mrq->data->stop))
		mask &= ~ESDHC_DATA_INHIBIT;

	while (fsl_readl(host->ioaddr + ESDHC_PRESENT_STATE) & mask) {
		if (timeout == 0) {
			printk(KERN_ERR "%s: Controller never released "
				"inhibit bit(s).\n", mmc_hostname(host->mmc));
			esdhc_dumpregs(host);
			cmd->error = -EIO;
			tasklet_schedule(&host->finish_tasklet);
			return;
		}
		timeout--;
		mdelay(1);
	}

	mod_timer(&host->timer, jiffies + 10 * HZ);

	host->cmd = cmd;

	esdhc_prepare_data(host, cmd->data);

	fsl_writel(host->ioaddr + ESDHC_ARGUMENT, cmd->arg);

	flags = esdhc_set_transfer_mode(host, cmd->data);

	if ((cmd->flags & MMC_RSP_136) && (cmd->flags & MMC_RSP_BUSY)) {
		printk(KERN_ERR "%s: Unsupported response type!\n",
			mmc_hostname(host->mmc));
		cmd->error = -EINVAL;
		tasklet_schedule(&host->finish_tasklet);
		return;
	}

	if (!(cmd->flags & MMC_RSP_PRESENT))
		flags |= ESDHC_CMD_RESP_NONE;
	else if (cmd->flags & MMC_RSP_136)
		flags |= ESDHC_CMD_RESP_LONG;
	else if (cmd->flags & MMC_RSP_BUSY)
		flags |= ESDHC_CMD_RESP_SHORT_BUSY;
	else
		flags |= ESDHC_CMD_RESP_SHORT;

	if (cmd->flags & MMC_RSP_CRC)
		flags |= ESDHC_CMD_CRC_EN;
	if (cmd->flags & MMC_RSP_OPCODE)
		flags |= ESDHC_CMD_INDEX_EN;
	if (cmd->data)
		flags |= ESDHC_CMD_DATA;

	fsl_writel(host->ioaddr + ESDHC_COMMAND,
		 ESDHC_MAKE_CMD(cmd->opcode, flags));
}

static void esdhc_finish_command(struct esdhc_host *host)
{
	int i;

	BUG_ON(host->cmd == NULL);

	if (host->cmd->flags & MMC_RSP_PRESENT) {
		if (host->cmd->flags & MMC_RSP_136) {
			/* CRC is stripped so we need to do some shifting. */
			for (i = 0; i < 4; i++) {
				host->cmd->resp[i] = fsl_readl(host->ioaddr +
						ESDHC_RESPONSE + (3-i)*4) << 8;
				if (i != 3)
					host->cmd->resp[i] |=
						(fsl_readl(host->ioaddr
							+ ESDHC_RESPONSE
							+ (2-i)*4) >> 24);
			}
		} else
			host->cmd->resp[0] = fsl_readl(host->ioaddr +
							ESDHC_RESPONSE);
	}

	host->cmd->error = 0;

	DBG("Ending cmd (%d)", host->cmd->opcode);

	if (host->cmd->data)
		host->data = host->cmd->data;
	else
		tasklet_schedule(&host->finish_tasklet);

	host->cmd = NULL;
}

static void esdhc_set_clock(struct esdhc_host *host, unsigned int clock)
{
	int div, pre_div;
	u16 clk;
	unsigned long timeout;

	if (clock == host->clock)
		return;

	clrbits32(host->ioaddr + ESDHC_SYSTEM_CONTROL, ESDHC_CLOCK_MASK);

	if (clock == 0)
		goto out;

	if (host->max_clk / 16 > clock) {
		for (pre_div = 1; pre_div < 256; pre_div *= 2) {
			if ((host->max_clk / pre_div) < (clock*16))
				break;
		}
	} else
		pre_div = 1;

	for (div = 1; div <= 16; div++) {
		if ((host->max_clk / (div*pre_div)) <= clock)
			break;
	}

	pre_div >>= 1;
	div -= 1;

	clk = (div << ESDHC_DIVIDER_SHIFT) | (pre_div << ESDHC_PREDIV_SHIFT);
	setbits32(host->ioaddr + ESDHC_SYSTEM_CONTROL, clk);

	/* Wait max 10 ms */
	timeout = 10;
	while (timeout) {
		timeout--;
		mdelay(1);
	}

	setbits32(host->ioaddr + ESDHC_SYSTEM_CONTROL, ESDHC_CLOCK_CARD_EN);
	esdhc_dumpregs(host);

out:
	host->clock = clock;
	if (host->clock == 0)
		setbits32(host->ioaddr + ESDHC_SYSTEM_CONTROL,
					ESDHC_CLOCK_DEFAULT);
}

static void esdhc_set_power(struct esdhc_host *host, unsigned short power)
{

	if (host->power == power)
		return;

	if (power == (unsigned short)-1)
		host->power = power;
}

/*****************************************************************************\
 *                                                                           *
 * MMC callbacks                                                             *
 *                                                                           *
\*****************************************************************************/

static void esdhc_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct esdhc_host *host;
	unsigned long flags;

	host = mmc_priv(mmc);

	spin_lock_irqsave(&host->lock, flags);

	WARN_ON(host->mrq != NULL);

	host->mrq = mrq;

	if (!(fsl_readl(host->ioaddr + ESDHC_PRESENT_STATE) &
			ESDHC_CARD_PRESENT)) {
		host->mrq->cmd->error = -ETIMEDOUT;
		tasklet_schedule(&host->finish_tasklet);
	} else
		esdhc_send_command(host, mrq->cmd);

	mmiowb();
	spin_unlock_irqrestore(&host->lock, flags);
}

static void esdhc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct esdhc_host *host;
	unsigned long flags;
	u32 ctrl;

	host = mmc_priv(mmc);

	spin_lock_irqsave(&host->lock, flags);

	/*
	 * Reset the chip on each power off.
	 * Should clear out any weird states.
	 */

	if (ios->power_mode == MMC_POWER_OFF) {
		fsl_writel(host->ioaddr + ESDHC_SIGNAL_ENABLE, 0);
		esdhc_init(host);
	}

	esdhc_set_clock(host, ios->clock);

	if (ios->power_mode == MMC_POWER_OFF)
		esdhc_set_power(host, -1);
	else
		esdhc_set_power(host, ios->vdd);

	ctrl = fsl_readl(host->ioaddr + ESDHC_PROTOCOL_CONTROL);

	if (ios->bus_width == MMC_BUS_WIDTH_4) {
		ctrl |= ESDHC_CTRL_4BITBUS;
		host->bus_width = MMC_BUS_WIDTH_4;
	} else {
		ctrl &= ~ESDHC_CTRL_4BITBUS;
		host->bus_width = MMC_BUS_WIDTH_1;
	}

	fsl_writel(host->ioaddr + ESDHC_PROTOCOL_CONTROL, ctrl);

	mmiowb();
	spin_unlock_irqrestore(&host->lock, flags);
}

static int esdhc_get_ro(struct mmc_host *mmc)
{
	struct esdhc_host *host;
	unsigned long flags;
	int present;

	host = mmc_priv(mmc);

	spin_lock_irqsave(&host->lock, flags);

	present = fsl_readl(host->ioaddr + ESDHC_PRESENT_STATE);

	spin_unlock_irqrestore(&host->lock, flags);

	/* esdhc is different form sdhc */
	return (present & ESDHC_WRITE_PROTECT);
}

static const struct mmc_host_ops esdhc_ops = {
	.request	= esdhc_request,
	.set_ios	= esdhc_set_ios,
	.get_ro		= esdhc_get_ro,
};

/*****************************************************************************\
 *                                                                           *
 * Tasklets                                                                  *
 *                                                                           *
\*****************************************************************************/

static void esdhc_tasklet_card(unsigned long param)
{
	struct esdhc_host *host;

	host = (struct esdhc_host *)param;

	spin_lock(&host->lock);

	if (!(fsl_readl(host->ioaddr + ESDHC_PRESENT_STATE) &
				ESDHC_CARD_PRESENT)) {
		if (host->mrq) {
			printk(KERN_ERR "%s: Card removed during transfer!\n",
				mmc_hostname(host->mmc));
			printk(KERN_ERR "%s: Resetting controller.\n",
				mmc_hostname(host->mmc));

			esdhc_reset(host, ESDHC_RESET_CMD);
			esdhc_reset(host, ESDHC_RESET_DATA);

			host->mrq->cmd->error = -EIO;
			tasklet_schedule(&host->finish_tasklet);
		}
		host->card_insert = 0;
	} else {
		esdhc_reset(host, ESDHC_INIT_CARD);
		host->card_insert = 1;
	}

	spin_unlock(&host->lock);

	mmc_detect_change(host->mmc, msecs_to_jiffies(500));
}

static void esdhc_tasklet_finish(unsigned long param)
{
	struct esdhc_host *host;
	unsigned long flags;
	struct mmc_request *mrq;

	host = (struct esdhc_host *)param;

	spin_lock_irqsave(&host->lock, flags);

	del_timer(&host->timer);

	mrq = host->mrq;

	DBG("Ending request, cmd (%d)", mrq->cmd->opcode);

	/*
	 * The controller needs a reset of internal state machines
	 * upon error conditions.
	 */
	if ((mrq->cmd->error != 0) ||
		(mrq->data && ((mrq->data->error != 0) ||
		(mrq->data->stop &&
			(mrq->data->stop->error != 0))))) {

		/* Some controllers need this kick or reset won't work here */
		if (host->chip->quirks & ESDHC_QUIRK_CLOCK_BEFORE_RESET) {
			unsigned int clock;

			/* This is to force an update */
			clock = host->clock;
			host->clock = 0;
			esdhc_set_clock(host, clock);
		}

		/* Spec says we should do both at the same time, but Ricoh
		   controllers do not like that. */
		if (mrq->cmd->error != -ETIMEDOUT) {
			esdhc_reset(host, ESDHC_RESET_CMD);
			esdhc_reset(host, ESDHC_RESET_DATA);
			reset_regs(host);
			esdhc_dumpregs(host);
		}
	}

	host->mrq = NULL;
	host->cmd = NULL;
	host->data = NULL;

	mmiowb();
	spin_unlock_irqrestore(&host->lock, flags);

	mmc_request_done(host->mmc, mrq);
}

static void esdhc_timeout_timer(unsigned long data)
{
	struct esdhc_host *host;
	unsigned long flags;

	host = (struct esdhc_host *)data;

	spin_lock_irqsave(&host->lock, flags);

	if (host->mrq) {
		printk(KERN_ERR "%s: Timeout waiting for hardware "
			"interrupt.\n", mmc_hostname(host->mmc));
		esdhc_dumpregs(host);

		if (host->data) {
			host->data->error = -ETIMEDOUT;
			esdhc_finish_data(host);
		} else {
			if (host->cmd)
				host->cmd->error = -ETIMEDOUT;
			else
				host->mrq->cmd->error = -ETIMEDOUT;

			tasklet_schedule(&host->finish_tasklet);
		}
	}

	mmiowb();
	spin_unlock_irqrestore(&host->lock, flags);
}

/*****************************************************************************\
 *                                                                           *
 * Interrupt handling                                                        *
 *                                                                           *
\*****************************************************************************/

static void esdhc_cmd_irq(struct esdhc_host *host, u32 intmask)
{
	BUG_ON(intmask == 0);

	if (!host->cmd) {
		printk(KERN_ERR "%s: Got command interrupt even though no "
			"command operation was in progress.\n",
			mmc_hostname(host->mmc));
		esdhc_dumpregs(host);
		return;
	}

	if (intmask & ESDHC_INT_TIMEOUT) {
		host->cmd->error = -ETIMEDOUT;
		tasklet_schedule(&host->finish_tasklet);
	} else if (intmask & ESDHC_INT_RESPONSE)
		esdhc_finish_command(host);
	else {
		if (intmask & ESDHC_INT_CRC)
			host->cmd->error = -EILSEQ;
		else if (intmask & (ESDHC_INT_END_BIT | ESDHC_INT_INDEX))
			host->cmd->error = -EIO;
		else
			host->cmd->error = -EINVAL;

		tasklet_schedule(&host->finish_tasklet);
	}
}

static void esdhc_data_irq(struct esdhc_host *host, u32 intmask)
{
	BUG_ON(intmask == 0);

	if (!host->data) {
		/*
		 * A data end interrupt is sent together with the response
		 * for the stop command.
		 */
		if ((intmask & ESDHC_INT_DATA_END) ||
			(intmask & ESDHC_INT_DMA_END))
			return;

		printk(KERN_ERR "%s: Got data interrupt even though no "
			"data operation was in progress.\n",
			mmc_hostname(host->mmc));
		esdhc_dumpregs(host);

		return;
	}

	if (intmask & ESDHC_INT_DATA_TIMEOUT)
		host->data->error = -ETIMEDOUT;
	else if (intmask & ESDHC_INT_DATA_CRC)
		host->data->error = -EILSEQ;
	else if (intmask & ESDHC_INT_DATA_END_BIT)
		host->data->error = -EIO;

	if (host->data->error != 0)
		esdhc_finish_data(host);
	else {
		if (intmask & (ESDHC_INT_DATA_AVAIL | ESDHC_INT_SPACE_AVAIL))
			esdhc_transfer_pio(host);

		/*
		 * We currently don't do anything fancy with DMA
		 * boundaries, but as we can't disable the feature
		 * we need to at least restart the transfer.
		 */
		if (intmask & ESDHC_INT_DMA_END)
			fsl_writel(host->ioaddr + ESDHC_DMA_ADDRESS,
				fsl_readl(host->ioaddr + ESDHC_DMA_ADDRESS));

		if (intmask & ESDHC_INT_DATA_END)
			esdhc_finish_data(host);
	}
}

static irqreturn_t esdhc_irq(int irq, void *dev_id)
{
	irqreturn_t result;
	struct esdhc_host *host = dev_id;
	u32 status;

	spin_lock(&host->lock);

	status = fsl_readl(host->ioaddr + ESDHC_INT_STATUS);

	if (!status || status == 0xffffffff) {
		result = IRQ_NONE;
		goto out;
	}

	if (status & (ESDHC_INT_CARD_INSERT | ESDHC_INT_CARD_REMOVE)) {
		if (status & ESDHC_INT_CARD_INSERT) {
			if ((fsl_readl(host->ioaddr + ESDHC_PRESENT_STATE) &
				ESDHC_CARD_PRESENT)) {
				DBG("***  got card-insert interrupt");
				fsl_writel(host->ioaddr + ESDHC_INT_ENABLE,
					ESDHC_INT_INSERT_MASK);
				fsl_writel(host->ioaddr + ESDHC_SIGNAL_ENABLE,
					ESDHC_INT_INSERT_MASK);
			}
		}
		if (status & ESDHC_INT_CARD_REMOVE) {
			if (!(fsl_readl(host->ioaddr + ESDHC_PRESENT_STATE) &
				ESDHC_CARD_PRESENT)) {
				DBG("***  got card-remove interrupt");
				fsl_writel(host->ioaddr + ESDHC_INT_ENABLE,
					ESDHC_INT_REMOVE_MASK);
				fsl_writel(host->ioaddr + ESDHC_SIGNAL_ENABLE,
					ESDHC_INT_REMOVE_MASK);
			}
		}

		tasklet_schedule(&host->card_tasklet);
	}

	status &= ~(ESDHC_INT_CARD_INSERT | ESDHC_INT_CARD_REMOVE);

	if (status & ESDHC_INT_CMD_MASK) {
		fsl_writel(host->ioaddr + ESDHC_INT_STATUS,
			status & ESDHC_INT_CMD_MASK);
		esdhc_cmd_irq(host, status & ESDHC_INT_CMD_MASK);
	}

	if (status & ESDHC_INT_DATA_MASK) {
		fsl_writel(host->ioaddr + ESDHC_INT_STATUS,
			status & ESDHC_INT_DATA_MASK);
		esdhc_data_irq(host, status & ESDHC_INT_DATA_MASK);
	}

	status &= ~(ESDHC_INT_CMD_MASK | ESDHC_INT_DATA_MASK);

	if (status) {
		printk(KERN_ERR "%s: Unexpected interrupt 0x%08x.\n",
			mmc_hostname(host->mmc), status);
		esdhc_dumpregs(host);

		fsl_writel(host->ioaddr + ESDHC_INT_STATUS, status);
	}

	result = IRQ_HANDLED;

	mmiowb();
out:
	spin_unlock(&host->lock);

	return result;
}

/*****************************************************************************\
 *                                                                           *
 * Suspend/resume                                                            *
 *                                                                           *
\*****************************************************************************/

#ifdef CONFIG_PM

static int esdhc_suspend(struct of_device *ofdev, pm_message_t state)
{
	struct esdhc_chip *chip;
	int i, ret;

	chip = dev_get_drvdata(&ofdev->dev);
	if (!chip)
		return 0;

	DBG("Suspending...");

	for (i = 0; i < chip->num_slots; i++) {
		if (!chip->hosts[i])
			continue;
		ret = mmc_suspend_host(chip->hosts[i]->mmc, state);
		if (ret) {
			for (i--; i >= 0; i--)
				mmc_resume_host(chip->hosts[i]->mmc);
			return ret;
		}
	}

	for (i = 0; i < chip->num_slots; i++) {
		if (!chip->hosts[i])
			continue;
		free_irq(chip->hosts[i]->irq, chip->hosts[i]);
	}

	return 0;
}

static int esdhc_resume(struct of_device *ofdev)
{
	struct esdhc_chip *chip;
	int i, ret;

	chip = dev_get_drvdata(&ofdev->dev);
	if (!chip)
		return 0;

	DBG("Resuming...");

	for (i = 0; i < chip->num_slots; i++) {
		if (!chip->hosts[i])
			continue;
		ret = request_irq(chip->hosts[i]->irq, esdhc_irq,
			IRQF_SHARED, chip->hosts[i]->slot_descr,
			chip->hosts[i]);
		if (ret)
			return ret;
		esdhc_init(chip->hosts[i]);
		mmiowb();
		ret = mmc_resume_host(chip->hosts[i]->mmc);
		if (ret)
			return ret;
	}

	return 0;
}

#else

#define esdhc_suspend NULL
#define esdhc_resume NULL

#endif

/*****************************************************************************\
 *                                                                           *
 * Device probing/removal                                                    *
 *                                                                           *
\*****************************************************************************/

static int __devinit esdhc_probe_slot(struct of_device *ofdev, int slot)
{
	struct device_node *np = ofdev->node;
	struct device_node *cpu;
	void __iomem *immap = NULL;
	unsigned int sdhccm;
	int ret;
	unsigned int version;
	struct esdhc_chip *chip;
	struct mmc_host *mmc;
	struct esdhc_host *host;
	struct resource res;

	unsigned int caps;

	chip = dev_get_drvdata(&(ofdev->dev));
	BUG_ON(!chip);

	mmc = mmc_alloc_host(sizeof(struct esdhc_host), &(ofdev->dev));
	if (!mmc)
		return -ENOMEM;

	host = mmc_priv(mmc);
	host->mmc = mmc;

	host->chip = chip;
	chip->hosts[slot] = host;

	ret = of_address_to_resource(np, 0, &res);
	if (ret)
		goto free;

	host->addr = res.start;
	host->size = res.end - res.start + 1;
	host->irq = irq_of_parse_and_map(np, 0);

	printk(KERN_DEBUG "slot %d at 0x%08lx, irq %d\n",
			slot, host->addr, host->irq);

	snprintf(host->slot_descr, 20, "esdhc:slot%d", slot);

	if (!request_mem_region(host->addr, host->size, DRIVER_NAME)) {
		ret = -EBUSY;
		goto release;
	}

	host->ioaddr = ioremap_nocache(host->addr, host->size);
	if (!host->ioaddr) {
		ret = -ENOMEM;
		goto release;
	}

	esdhc_reset(host, ESDHC_RESET_ALL);

	version = fsl_readl(host->ioaddr + ESDHC_HOST_VERSION);
	if (version != 0x01)
		printk(KERN_ERR "%s: Unknown controller version (%d). "
			"You may experience problems.\n", host->slot_descr,
			version);

	caps = fsl_readl(host->ioaddr + ESDHC_CAPABILITIES);

	if (debug_nodma)
		DBG("DMA forced off\n");
	else if (debug_forcedma) {
		DBG("DMA forced on\n");
		host->flags |= ESDHC_USE_DMA;
	} else if (chip->quirks & ESDHC_QUIRK_FORCE_DMA)
		host->flags |= ESDHC_USE_DMA;
	else if (!(caps & ESDHC_CAN_DO_DMA))
		DBG("Controller doesn't have DMA capability\n");
	else
		host->flags |= ESDHC_USE_DMA;

	/* max_clk = CSB/SCCR[SDHCCM] */
	cpu = of_find_node_by_type(NULL, "cpu");
	if (cpu) {
		unsigned int size;
		const u32 *prop = of_get_property(cpu, "bus-frequency", &size);
		host->max_clk = *prop;
		of_node_put(cpu);
	} else
		host->max_clk = 396000000;

	immap = ioremap(get_immrbase(), 0x1000);
	if (!immap)
		return -ENOMEM;

	sdhccm = (in_be32(immap + MPC837X_SCCR_OFFS) & MPC837X_SDHCCM_MASK)
			>> MPC837X_SDHCCM_SHIFT;
	iounmap(immap);

	if (sdhccm == 0) {
		printk(KERN_ERR "The eSDHC clock was disable!\n");
		return -EBADSLT;
	} else
		host->max_clk /= sdhccm;

	/*
	 * Set host parameters.
	 */
	mmc->ops = &esdhc_ops;
	mmc->f_min = 400000;
	mmc->f_max = min((int)host->max_clk, 50000000);
	mmc->caps = MMC_CAP_4_BIT_DATA | MMC_CAP_MULTIWRITE;

	if (caps & ESDHC_CAN_DO_HISPD)
		mmc->caps |= MMC_CAP_SD_HIGHSPEED;

	mmc->ocr_avail = 0;
	if (caps & ESDHC_CAN_VDD_330)
		mmc->ocr_avail |= MMC_VDD_32_33|MMC_VDD_33_34;
	if (caps & ESDHC_CAN_VDD_300)
		mmc->ocr_avail |= MMC_VDD_29_30|MMC_VDD_30_31;
	if (caps & ESDHC_CAN_VDD_180)
		mmc->ocr_avail |= MMC_VDD_165_195;

	if (mmc->ocr_avail == 0) {
		printk(KERN_ERR "%s: Hardware doesn't report any "
			"support voltages.\n", host->slot_descr);
		ret = -ENODEV;
		goto unmap;
	}

	spin_lock_init(&host->lock);

	/*
	 * Maximum number of segments. Hardware cannot do scatter lists.
	 */
	if (host->flags & ESDHC_USE_DMA)
		mmc->max_hw_segs = 1;
	else
		mmc->max_hw_segs = 16;
	mmc->max_phys_segs = 16;

	/*
	 * Maximum number of sectors in one transfer. Limited by DMA boundary
	 * size (512KiB).
	 */
	mmc->max_req_size = 524288;

	/*
	 * Maximum segment size. Could be one segment with the maximum number
	 * of bytes.
	 */
	mmc->max_seg_size = mmc->max_req_size;

	/*
	 * Maximum block size. This varies from controller to controller and
	 * is specified in the capabilities register.
	 */
	mmc->max_blk_size = (caps & ESDHC_MAX_BLOCK_MASK) >>
					ESDHC_MAX_BLOCK_SHIFT;
	if (mmc->max_blk_size > 3) {
		printk(KERN_ERR "%s: Invalid maximum block size.\n",
			host->slot_descr);
		ret = -ENODEV;
		goto unmap;
	}
	mmc->max_blk_size = 512 << mmc->max_blk_size;

	/*
	 * Maximum block count.
	 */
	mmc->max_blk_count = 65535;

	/*
	 * Init tasklets.
	 */
	tasklet_init(&host->card_tasklet,
		esdhc_tasklet_card, (unsigned long)host);
	tasklet_init(&host->finish_tasklet,
		esdhc_tasklet_finish, (unsigned long)host);

	setup_timer(&host->timer, esdhc_timeout_timer, (unsigned long)host);

	esdhc_init(host);

#ifdef CONFIG_MMC_DEBUG
	esdhc_dumpregs(host);
#endif

	ret = request_irq(host->irq, esdhc_irq, IRQF_SHARED,
		host->slot_descr, host);
	if (ret)
		goto untasklet;

	mmiowb();

	mmc_add_host(mmc);

	printk(KERN_INFO "%s: ESDHC at 0x%08lx irq %d %s\n", mmc_hostname(mmc),
		host->addr, host->irq,
		(host->flags & ESDHC_USE_DMA)?"DMA":"PIO");

	return 0;

untasklet:
	tasklet_kill(&host->card_tasklet);
	tasklet_kill(&host->finish_tasklet);
unmap:
	iounmap(host->ioaddr);
release:
	release_mem_region(host->addr, host->size);
free:
	mmc_free_host(mmc);

	return ret;
}

static void esdhc_remove_slot(struct of_device *ofdev, int slot)
{
	struct esdhc_chip *chip;
	struct mmc_host *mmc;
	struct esdhc_host *host;

	chip = dev_get_drvdata(&(ofdev->dev));
	host = chip->hosts[slot];
	mmc = host->mmc;

	chip->hosts[slot] = NULL;

	mmc_remove_host(mmc);

	esdhc_reset(host, ESDHC_RESET_ALL);

	free_irq(host->irq, host);

	del_timer_sync(&host->timer);

	tasklet_kill(&host->card_tasklet);
	tasklet_kill(&host->finish_tasklet);

	iounmap(host->ioaddr);

	release_mem_region(host->addr, host->size);

	mmc_free_host(mmc);
}

static int __devinit esdhc_probe(struct of_device *ofdev,
	const struct of_device_id *match)
{
	int ret, i;
	u8 slots;
	struct esdhc_chip *chip;

	BUG_ON(ofdev == NULL);
	BUG_ON(match == NULL);

	slots = ESDHC_SLOTS_NUMBER;
	DBG("found %d slot(s)", slots);
	if (slots == 0)
		return -ENODEV;

	chip = kmalloc(sizeof(struct esdhc_chip) +
		sizeof(struct esdhc_host *) * slots, GFP_KERNEL);
	if (!chip) {
		ret = -ENOMEM;
		goto err;
	}

	chip->ofdev = ofdev;
	chip->quirks = ESDHC_QUIRK_NO_CARD_NO_RESET;

	if (debug_quirks)
		chip->quirks = debug_quirks;

	chip->num_slots = slots;
	dev_set_drvdata(&(ofdev->dev), chip);

	for (i = 0; i < slots; i++) {
		ret = esdhc_probe_slot(ofdev, i);
		if (ret) {
			for (i--; i >= 0; i--)
				esdhc_remove_slot(ofdev, i);
			goto free;
		}
	}

	return 0;

free:
	dev_set_drvdata(&(ofdev->dev), NULL);
	kfree(chip);

err:
	return ret;
}

static int __devexit esdhc_remove(struct of_device *ofdev)
{
	int i;
	struct esdhc_chip *chip;

	chip = dev_get_drvdata(&(ofdev->dev));

	if (chip) {
		for (i = 0; i < chip->num_slots; i++)
			esdhc_remove_slot(ofdev, i);

		dev_set_drvdata(&(ofdev->dev), NULL);

		kfree(chip);
	}

	return 0;
}

/*-------------------------------------------------------------------------*/
static struct of_device_id fsl_esdhc_match[] = {
	{
		.compatible = "fsl,esdhc",
	},
	{},
};

MODULE_DEVICE_TABLE(of, fsl_esdhc_match);

static struct of_platform_driver esdhc_driver = {
	.name = 	DRIVER_NAME,
	.match_table =	fsl_esdhc_match,
	.probe = 	esdhc_probe,
	.remove =	__devexit_p(esdhc_remove),
	.suspend =	esdhc_suspend,
	.resume	=	esdhc_resume,
};

/*****************************************************************************\
 *                                                                           *
 * Driver init/exit                                                          *
 *                                                                           *
\*****************************************************************************/

static int __init esdhc_drv_init(void)
{
	printk(KERN_INFO DRIVER_NAME
		": Freescale Enhanced Secure Digital Host Controller driver\n");

	return of_register_platform_driver(&esdhc_driver);
}

static void __exit esdhc_drv_exit(void)
{
	DBG("Exiting\n");

	of_unregister_platform_driver(&esdhc_driver);
}

module_init(esdhc_drv_init);
module_exit(esdhc_drv_exit);

module_param(debug_nodma, uint, 0444);
module_param(debug_forcedma, uint, 0444);
module_param(debug_quirks, uint, 0444);

MODULE_AUTHOR("Xiaobo Xie<X.Xie@freescale.com>");
MODULE_DESCRIPTION("Enhanced Secure Digital Host Controller driver");
MODULE_LICENSE("GPL");

MODULE_PARM_DESC(debug_nodma, "Forcefully disable DMA transfers.");
MODULE_PARM_DESC(debug_forcedma, "Forcefully enable DMA transfers.");
MODULE_PARM_DESC(debug_quirks, "Force certain quirks.");
