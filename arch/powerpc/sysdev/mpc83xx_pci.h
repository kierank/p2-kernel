/*
 * Copyright (C) 2006-2007 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Author: Vivek Mahajan <vivek.mahajan@freescale.com>
 *
 * Changelog:
 * Fri Aug 31 2007 Tony Li <tony.li@freescale.com>
 * - Add MPC837x support
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the  GNU General Public License along
 * with this program; if not, write  to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */
/* $Id: mpc83xx_pci.h 11618 2011-01-12 08:21:34Z Noguchi Isao $ */

#ifndef __MPC83XX_H__
#define __MPC83XX_H__

/* 2010/7/23, commented by Panasonic (SAV) */
/* #define	PCIE1_OFFSET	0x9000 */
/* #define	PCIE2_OFFSET	0xa000 */

/* #define CFG_PCIE1_BASE		0xA0000000 */
/* #define CFG_PCIE1_CFG_BASE	CFG_PCIE1_BASE */
/* #define CFG_PCIE1_CFG_SIZE	0x08000000 */
/* #define CFG_PCIE2_BASE		0xC0000000 */
/* #define CFG_PCIE2_CFG_BASE	CFG_PCIE2_BASE */
/* #define CFG_PCIE2_CFG_SIZE	0x08000000 */

#define	PEX_LTSSM_STAT	0x404
#define	PEX_LTSSM_STAT_L0	0x16
#define	PEX_GCLK_RATIO	0x440

/* 2010/7/23, added by Panasonic (SAV),
   2011/112, modified by Panasonic (SAV) */
#define PEX_OUTWIN0_AR		0xCA0
#define PEX_OUTWIN0_BAR		0xCA4
#define PEX_OUTWIN0_TAL		0xCA8
#define PEX_OUTWIN0_TAH		0xCAC

/* AXI-to-PCIE bridge 1,2 for MPC8315/7X */

struct pcie_pab_bridge {	/* Offset 0x800 from 0x9000,0xA000 */

	/* Global Registers */
	u32 pab_ver;
	u32 pab_cab;
	u32 pab_ctrl;
	u8 res1[8];
	u32 pab_dma_desc_timer;
	u8 res2[4];
	u32 pab_stat;
	u32 pab_rst_ctrl;
	u8 res3[0x1C];

	/* Outbound PIO Registers (Off:0x840) */
	u32 pab_outbound_pio_ctrl;
	u32 pab_outbound_pio_stat;
	u32 pab_sl_stat;
	u8 res4[0x94];

	/* Inbound PIO Registers (Off: 0x8E0) */
	u32 pab_inbound_pio_ctrl;
	u32 pab_inbound_pio_stat;
	u32 pab_mt_stat;
	u8 res5[0x94];

	/* CSR Slave Registers (Off: 0x980) */
	u32 pab_scr_sl_stat;
	u8 res6[0xC];

	/* DMA Registers (Off: 0x990) */
	u32 pab_dma_mt_stat;
	u8 res7[0xC];
	u32 pab_wdma_ctrl0;
	u32 pab_wdma_addr0;
	u32 pab_wdma_stat0;
	u32 pab_wdma_mt_stat0;
	u8 res8[0x90];
	u32 pab_rdma_ctrl0;
	u32 pab_rdma_addr0;
	u32 pab_rdma_stat0;
	u8 res9[0x94];

	/* Indirect Address Descriptor Registers (Off: 0xAE0) */
	u32 pab_iaddr_desc_sel;
	u32 pab_iaddr_desc_ctrl;
	u32 pab_iaddr_desc_stat;
	u32 pab_iaddr_desc_src_addr;
	u8 res10[0x4];
	u32 pab_iaddr_desc_dest_addr;
	u32 pab_iaddr_desc_nxt_ptr;
	u8 res11[0x24];

	/* MailBox registers (Off: 0xB20) */
	u32 pab_outbound_mb_ctrl;
	u32 pab_outbound_mb_data;
	u8 res12[0x38];
	u32 pab_inbound_mb_ctrl;
	u32 pab_inbound_mb_data;
	u8 res13[0x38];

	/* EP Interrupt Enable registers (off: 0xBA0) */
	u32 pab_intp_pex_enb;
	u32 pab_intp_pex_stat;
	u32 pab_intp_apio_vec1;
	u32 pab_intp_apio_vec2;
	u8 res14[0x10];
	u32 pab_intp_ppio_vec1;
	u32 pab_intp_ppio_vec2;
	u32 pab_intp_wdma_vec1;
	u32 pab_intp_wdma_vec2;
	u32 pab_intp_rdma_vec1;
	u32 pab_intp_rdma_vec2;
	u32 pab_intp_misc_vec;
	u8 res18[0x4];

	/* RC Interrupt Enable registers (Off: 0xBE0) */
	u32 pab_intp_axi_pio_enb;
	u32 pab_intp_axi_wdma_enb;
	u32 pab_intp_axi_rdma_enb;
	u32 pab_intp_axi_misc_enb;
	u32 pab_intp_axi_pio_stat;
	u32 pab_intp_axi_wdma_stat;
	u32 pab_intp_axi_rdma_stat;
	u32 pab_intp_axi_misc_stat;
	u8 res19[0x20];

	/* Vendor Defined Message Generation Registers (Off: 0xC20) */
	u32 pab_msg_vd_ctrl;
	u32 pab_msg_vd_hdr[4];
	u32 pab_msg_vd_data;
	u8 res20[0x8];
	u32 pab_msg_rx_vd_stat;
	u32 pab_msg_rx_vd_fifo[5];
	u8 res21[0x8];
	u32 pab_msi_rx_stat;
	u32 pab_msi_rx_fifo[5];
	u8 res22[0x28];

	/* Outbound address mapping registers (Off: 0xCA0) */
	struct pab_outbound_window {
		u32 ar;
		u32 bar;
		u32 tarl;
		u32 tarh;
	} pab_outbound_win[4];
	u8 res23[0x100];

	/* EP Inbound address mapping registers (Off: 0xDE0) */
	u32 pab_inbound_ta0;
	u32 pab_inbound_ta1;
	u32 pab_inbound_ta2;
	u32 pab_inbound_ta3;
	u8 res24[0x70];

	/* RC Inbound address mapping registers (Off: 0xDE0) */
	struct pab_inbound_window {
		u32 ar;
		u32 tar;
		u32 barl;
		u32 barh;
	} pab_inbound_win[4];
};

#define	PAB_VER_AXI		0x01000000

#define	PAB_CAP_AXI		0x00000001
#define	PAB_CAP_PXI		0x00000002
#define	PAB_CAP_WDMA		0x00000004
#define	PAB_CAP_RDMA		0x00000008
#define	PAB_CAP_AXI_SLV_MSK	0x00000070
#define	PAB_CAP_PEX_SLV_MSK	0x00000380
#define	PAB_CAP_NR_WDMA_MSK	0x00001C00
#define	PAB_CAP_NR_RDMA_MSK	0x00007000

#define	PAB_CTRL_AXI		0x00000001
#define	PAB_CTRL_PEX		0x00000002
#define	PAB_CTRL_WDMA		0x00000004
#define	PAB_CTRL_RDMA		0x00000008
#define	PAB_CTRL_AXI_BRST_4	0x00000010
#define	PAB_CTRL_PIO_ARBIT	~0x00000080
#define	PAB_CTRL_CHAIN_DESC(n)	(((n/2) >> 2) & ~0x00000300)	/* n=2,4,8 */

#define	PAB_STAT_AXI_INV	0x00000001
#define	PAB_STAT_MMAP_INV	0x00000002
#define	PAB_STAT_IO_INV		0x00000004
#define	PAB_STAT_CFG_INV	0x00000008
#define	PAB_STAT_PEX_INV	0x00000010

#define	PAB_ENG_STAT_AXI_PEND	0x00000001
#define	PAB_ENG_STAT_PEX_PEND	0x00000100
#define	PAB_ENG_STAT_WDMA_PEND	0x00010000
#define	PAB_ENG_STAT_RDMA_PEND	0x01000000

#define	PAB_RST_CTRL_AXI	0x00000001
#define	PAB_RST_CTLR_PEX	0x00000100
#define	PAB_RST_CTRL_WDMA	0x00010000
#define	PAB_RST_CTLR_RDMA	0x01000000

#define	PAB_AXI_PIO_CTRL_MEM	0x00000003
#define	PAB_AXI_PIO_CTRL_IO	0x00000005
#define	PAB_AXI_PIO_CTRL_CFG	0x00000009

#define	PAB_AXI_PIO_STAT_ERR	0x00000001
#define	PAB_AXI_PIO_STAT_BERR	0x00000002
#define	PAB_AXI_PIO_STAT_AERR	0x00000004
#define	PAB_AXI_PIO_STAT_PERR	0x00000008
#define	PAB_AXI_PIO_STAT_PRST	0x00000010

#define	PAB_PEX_PIO_CTRL_ENB	0x00000001

#define	PAB_PEX_PIO_STAT_ERR	0x00000001
#define	PAB_PEX_PIO_STAT_BERR	0x00000002
#define	PAB_PEX_PIO_STAT_AERR	0x00000004

#define	PAB_PEX_PIO_MSTAT_SERR	0x00000001
#define	PAB_PEX_PIO_MSTAT_DERR	0x00000002

#define	PAB_DMA_MSTAT_SERR	0x00000001
#define	PAB_DMA_MSTAT_DERR	0x00000002

#define	PAB_WDMA_CTRL0_DMA_STRT	0x00000001
#define	PAB_WDMA_CTRL0_DMA_SUSP	0x00000002
#define	PAB_WDMA_CTRL0_DESC_AVL	0x00000100
#define	PAB_WDMA_CTRL0_RLX_ORD	0x00000200
#define	PAB_WDMA_CTRL0_NO_SNP	0x00000400

#define	PAB_WDMA_STAT0_DMA_DONE		0x00000001
#define	PAB_WDMA_STAT0_DESC_DONE	0x00000002
#define	PAB_WDMA_STAT0_DESC_ERR		0x00000004
#define	PAB_WDMA_STAT0_DESC_TOT_ERR	0x00000008
#define	PAB_WDMA_STAT0_DESC_UPD_ERR	0x00000010
#define	PAB_WDMA_STAT0_DMA_ERR		0x00000020
#define	PAB_WDMA_STAT0_BRG_ERR		0x00000040
#define	PAB_WDMA_STAT0_PEX_RST		0x00000080

#define	PAB_WDMA_MSTAT0_SERR		0x00000001
#define	PAB_WDMA_MSTAT0_DERR		0x00000002

#define	PAB_RDMA_CTRL0_DMA_STRT		0x00000001
#define	PAB_RDMA_CTRL0_DMA_SUSP		0x00000002
#define	PAB_RDMA_CTRL0_DESC_AVL		0x00000100
#define	PAB_RDMA_CTRL0_RLX_ORD		0x00000200
#define	PAB_RDMA_CTRL0_NO_SNP		0x00000400

#define	PAB_RDMA_STAT0_DMA_DONE		0x00000001
#define	PAB_RDMA_STAT0_DESC_DONE	0x00000002
#define	PAB_RDMA_STAT0_DESC_ERR		0x00000004
#define	PAB_RDMA_STAT0_DESC_TOT_ERR	0x00000008
#define	PAB_RDMA_STAT0_DESC_UPD_ERR	0x00000010
#define	PAB_RDMA_STAT0_DMA_ERR		0x00000020
#define	PAB_RDMA_STAT0_BRG_ERR		0x00000040
#define	PAB_RDMA_STAT0_PEX_RST		0x00000080

#define	PAB_IADDR_DESC_SEL_WDMA		0x00000001

#define	PAB_MB_AXI_CTRL_RDY		0x00000001

#define	PAB_MB_PEX_CTRL_RDY		0x00000001

#define	PAB_INTP_PEX_ENB_AXI_DN		0x00000001
#define	PAB_INTP_PEX_ENB_AXI_ERR	0x00000002
#define	PAB_INTP_PEX_ENB_PEX_DN		0x00000004
#define	PAB_INTP_PEX_ENB_PEX_ERR	0x00000008
#define	PAB_INTP_PEX_ENB_WDMA_CHN_DN	0x00000010
#define	PAB_INTP_PEX_ENB_WDMA_DN	0x00000020
#define	PAB_INTP_PEX_ENB_WDMA_ERR	0x00000040
#define	PAB_INTP_PEX_ENB_RDMA_CHN_DN	0x00000080
#define	PAB_INTP_PEX_ENB_RDMA_DN	0x00000100
#define	PAB_INTP_PEX_ENB_RDMA_ERR	0x00000200
#define	PAB_INTP_PEX_ENB_MB_RDY		0x00000400
#define	PAB_INTP_PEX_ENB_AXI_RST	0x00000800

#define	PAB_INTP_PEX_STAT_AXI_DN	0x00000001
#define	PAB_INTP_PEX_STAT_AXI_ERR	0x00000002
#define	PAB_INTP_PEX_STAT_PEX_DN	0x00000004
#define	PAB_INTP_PEX_STAT_PEX_ERR	0x00000008
#define	PAB_INTP_PEX_STAT_WDMA_CHN_DN	0x00000010
#define	PAB_INTP_PEX_STAT_WDMA_DN	0x00000020
#define	PAB_INTP_PEX_STAT_WDMA_ERR	0x00000040
#define	PAB_INTP_PEX_STAT_RDMA_CHN_DN	0x00000080
#define	PAB_INTP_PEX_STAT_RDMA_DN	0x00000100
#define	PAB_INTP_PEX_STAT_RDMA_ERR	0x00000200
#define	PAB_INTP_PEX_STAT_MB_RDY	0x00000400
#define	PAB_INTP_PEX_STAT_AXI_RST	0x00000800

#define	PAB_INTP_APIO_VEC_VAL		0x0000001F
#define	PAB_INTP_PPIO_VEC_VAL		0x0000001F
#define	PAB_INTP_WDMA_VEC_VAL		0x0000001F
#define	PAB_INTP_RDMA_VEC_VAL		0x0000001F

#define	PAB_INTP_MISC_INTDIE		0x00000100
#define	PAB_INTP_MISC_INTCIE		0x00000080
#define	PAB_INTP_MISC_INTBIE		0x00000040
#define	PAB_INTP_MISC_INTAIE		0x00000020
#define	PAB_INTP_MISC_MSIIE		0x00000008
#define	PAB_INTP_MISC_RSTIE		0x00000002

#define	PAB_INTP_AXI_PIO_ENB_DONE	0x00000001
#define	PAB_INTP_AXI_PIO_ENB_ERR	0x00000100
#define	PAB_INTP_PEX_PIO_ENB_DONE	0x00010000
#define	PAB_INTP_PEX_PIO_ENB_ERR	0x01000000

#define	PAB_INTP_AXI_WDMA_ENB_CHN_DONE	0x00000001
#define	PAB_INTP_AXI_WDMA_ENB_DONE	0x00000100
#define	PAB_INTP_AXI_WDMA_ENB_ERR	0x00010000

#define	PAB_INTP_AXI_RDMA_ENB_CHN_DONE	0x00000001
#define	PAB_INTP_AXI_RDMA_ENB_DONE	0x00000100
#define	PAB_INTP_AXI_RDMA_ENB_ERR	0x00010000

#define	PAX_INTP_AXI_MISC_ENB_MB_RDY	0x00000001
#define	PAX_INTP_AXI_MISC_ENB_PEX_RST	0x00000002
#define	PAX_INTP_AXI_MISC_ENB_PEX_INT	0x00000004
#define	PAX_INTP_AXI_MISC_ENB_PEX_MSI	0x00000008
#define	PAX_INTP_AXI_MISC_ENB_PEX_VEN	0x00000010
#define	PAX_INTP_AXI_MISC_ENB_PEX_INTA	0x00000020
#define	PAX_INTP_AXI_MISC_ENB_PEX_INTB	0x00000040
#define	PAX_INTP_AXI_MISC_ENB_PEX_INTC	0x00000080
#define	PAX_INTP_AXI_MISC_ENB_PEX_INTD	0x00000100
#define	PAX_INTP_AXI_MISC_ENB_UR	0x00000200
#define	PAX_INTP_AXI_MISC_ENB_ABRT	0x00000400
#define	PAX_INTP_AXI_MISC_ENB_TOT	0x00000800
#define	PAX_INTP_AXI_MISC_ENB_BAD_TLP	0x00001000
#define	PAX_INTP_AXI_MISC_ENB_PSN_TLP	0x00002000
#define	PAX_INTP_AXI_MISC_ENB_ECRC_ERR	0x00004000
#define	PAX_INTP_AXI_MISC_ENB_RCV_OVFW	0x00008000

#define	PAB_INTP_AXI_PIO_STAT_DONE	0x00000001
#define	PAB_INTP_AXI_PIO_STAT_ERR	0x00000100
#define	PAB_INTP_PEX_PIO_STAT_DONE	0x00010000
#define	PAB_INTP_PEX_PIO_STAT_ERR	0x01000000

#define	PAB_INTP_AXI_WDMA_STAT_CHN_DONE	0x00000001
#define	PAB_INTP_AXI_WDMA_STAT_DONE	0x00000100
#define	PAB_INTP_AXI_WDMA_STAT_ERR	0x00010000

#define	PAB_INTP_AXI_RDMA_STAT_CHN_DONE	0x00000001
#define	PAB_INTP_AXI_RDMA_STAT_DONE	0x00000100
#define	PAB_INTP_AXI_RDMA_STAT_ERR	0x00010000

#define	PAX_INTP_AXI_MISC_STAT_MB_RDY	0x00000001
#define	PAX_INTP_AXI_MISC_STAT_PEX_RST	0x00000002
#define	PAX_INTP_AXI_MISC_STAT_PEX_INT	0x00000004
#define	PAX_INTP_AXI_MISC_STAT_PEX_MSI	0x00000008
#define	PAX_INTP_AXI_MISC_STAT_PEX_VEN	0x00000010
#define	PAX_INTP_AXI_MISC_STAT_PEX_INTA	0x00000020
#define	PAX_INTP_AXI_MISC_STAT_PEX_INTB	0x00000040
#define	PAX_INTP_AXI_MISC_STAT_PEX_INTC	0x00000080
#define	PAX_INTP_AXI_MISC_STAT_PEX_INTD	0x00000100
#define	PAX_INTP_AXI_MISC_STAT_UR	0x00000200
#define	PAX_INTP_AXI_MISC_STAT_ABRT	0x00000400
#define	PAX_INTP_AXI_MISC_STAT_TOT	0x00000800
#define	PAX_INTP_AXI_MISC_STAT_BAD_TLP	0x00001000
#define	PAX_INTP_AXI_MISC_STAT_PSN_TLP	0x00002000
#define	PAX_INTP_AXI_MISC_STAT_ECRC_ERR	0x00004000
#define	PAX_INTP_AXI_MISC_STAT_RCV_OVFW	0x00008000

#define	PAB_MSG_VD_CTRL_RDY		0x00000001
#define	PAB_MSG_VD_CTRL_4DW_HDR		0x00000002
#define	PAB_MSG_VD_CTRL_DW_DATA		0x00000004

#define	PAB_MSG_RX_VD_STAT_AVBL		0x00000001
#define	PAB_MSG_RX_VD_STAT_CNT		0x0000001E

#define	PAB_MSI_RX_STAT_AVBL		0x00000001
#define	PAB_MSI_RX_STAT_CNT		0x0000001E

#define	PAB_AXI_AMAP_CTRL_ENB		0x00000001
#define	PAB_AXI_AMAP_CTRL_CFG		~0x00000006
#define	PAB_AXI_AMAP_CTRL_IO		(0x02 & ~0x00000004)
#define	PAB_AXI_AMAP_CTRL_MEM		(0x04 & ~0x00000002)
#define	PAB_AXI_AMAP_CTRL_RLX_ORD	0x00000008
#define	PAB_AXI_AMAP_CTRL_NO_SNOP	0x00000010
#define	PAB_AXI_AMAP_CTRL_AXI_SZ	0xFFFFFC00

#define	PAB_AXI_AMAP_AXI_WIN_BASE	0xFFFFFFFC
#define	PAB_AXI_AMAP_PEX_WIN_BASE_LO	0xFFFFFFFC

#define	PAB_PEX_AMAP_WIN_ENB		0x00000001
#define	PAB_PEX_AMAP_WIN_ADDR		0xFFFFFFFE

#define	PAB_PEX_AMAP_WIN_1MB		0x000FF000
#define	PAB_PEX_AMAP_WIN_2MB		0x001FF000
#define	PAB_PEX_AMAP_WIN_4MB		0x003FF000
#define	PAB_PEX_AMAP_WIN_8MB		0x007FF000
#define	PAB_PEX_AMAP_WIN_16MB		0x00FFF000
#define	PAB_PEX_AMAP_WIN_32MB		0x01FFF000
#define	PAB_PEX_AMAP_WIN_64MB		0x03FFF000
#define	PAB_PEX_AMAP_WIN_128MB		0x07FFF000
#define	PAB_PEX_AMAP_WIN_256MB		0x0FFFF000
#define	PAB_PEX_AMAP_CTRL_ENB		0x00000001
#define	PAB_PEX_AMAP_CTRL_PF		0x00000004
#define	PAB_PEX_AMAP_CTRL_NPF		0x00000006
#define	PAB_PEX_AMAP_CTRL_NO_SNP_OVR	0x00000008
#define	PAB_PEX_AMAP_CTRL_NO_SNP	0x00000010
#define	PAB_PEX_AMAP_WIN_ATTR		((PAB_PEX_AMAP_CTRL_ENB | \
						PAB_PEX_AMAP_CTRL_PF) & \
						~(PAB_PEX_AMAP_CTRL_NO_SNP | \
						PAB_PEX_AMAP_CTRL_NO_SNP_OVR))

struct pciectrl_83xx {	/* Offset 0x404 from 0x9000, 0xA000 */
	u8 pciecfg_header[0x404];
	u32 pex_ltssm_stat;
	u8 res1[0x30];
	u32 pex_ack_replay_timeou;
	u8 res2[4];
	u32 pex_gclk_ratio;
	u8 res3[0xC];
	u32 pex_pm_timer;
	u32 pex_pme_timeout;	/* ep mode */
	u8 res4[0x4];
	u32 pex_aspm_req_timer; /* rc mode */
	u8 res5[0x18];
	u32 pex_ssvid_update;	/* ep mode */
	u8 res6[0x34];
	u32 pex_cfg_ready;
	u8 res7[0x24];
	u32 pex_bar_sizel;	/* ep mode */
	u8 res8[0x4];
	u32 pex_bar_sel;	/* ep mode */
	u8 res9[0x20];
	u32 pex_bar_pf;	/* ep mode */
	u8 res10[0x88];
	u32 pex_pme_to_ack_tor;	/* ep mode */
	u8 res11[0xC];
	u32 pex_ss_intr_mask;	/* rc mode */
	u8 res12[0x25C];
	struct pcie_pab_bridge pab_bridge;
};


#define	PCIE_CFG_RDY		0x00000001
#define	PCIE_PME_TOR		0x03FFFFFF
#define	PCIE_PME_ACK_TOR	0x003FFFFF

#endif				/* __MPC83XX_H__ */
