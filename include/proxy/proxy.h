/*
 * proxy.h -- definitions for Proxy Codec Card Driver
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#ifndef _PROXY_H_
#define _PROXY_H_

#include <linux/ioctl.h>

/*
 * Ioctl definitions
 */

struct proxy_indcmn_arg {
	unsigned char addr;
	unsigned char data;
};

/* Use 'x' as magic number */
#define PROXYIOC_MAGIC 'x'

#define PROXYIOC_ZV_ENABLE	_IO(PROXYIOC_MAGIC, 1)
#define PROXYIOC_ZV_DISABLE	_IO(PROXYIOC_MAGIC, 2)
#define PROXYIOC_RD_ICMNAREA	_IOWR(PROXYIOC_MAGIC, 3, struct proxy_indcmn_arg)
#define PROXYIOC_WR_ICMNAREA	_IOW(PROXYIOC_MAGIC,  4, struct proxy_indcmn_arg)
#define PROXYIOC_RD_CMNAREA	_IOWR(PROXYIOC_MAGIC, 5, struct proxy_indcmn_arg)
#define PROXYIOC_WR_CMNAREA	_IOW(PROXYIOC_MAGIC,  6, struct proxy_indcmn_arg)
#define PROXYIOC_GET_STATUS	_IOR(PROXYIOC_MAGIC,  7, unsigned char)
#define PROXYIOC_CHK_STATUS	_IOWR(PROXYIOC_MAGIC, 8, unsigned char)
#define PROXYIOC_CHK_IRQ	_IOR(PROXYIOC_MAGIC,  9, unsigned char)
#define PROXYIOC_AWAKE_STATUS	_IO(PROXYIOC_MAGIC, 10)
#define PROXYIOC_AWAKE_IRQ	_IO(PROXYIOC_MAGIC, 11)
#define PROXYIOC_HARDRESET	_IO(PROXYIOC_MAGIC, 12)	/* debugging tool */

#define PROXYIOC_MAXNR 12


/*
 * Address difinitions for Indirect Common Memory Area
 */

#define PROXY_IND_ZV_ENABLE		0x00	/*   [7] ZV_Enable		R/W */
#define PROXY_IND_SERVICE_MODE		0x00	/*   [0] SERVICE_MODE		R/W */
/* Add following line for P2-HD cameras to support AVC-I 2007/Apr/11	*/
#define PROXY_IND_FRAME_REFERENCE	0x03	/* [0] FRAME_REFERENCE		R/W */
#define PROXY_IND_FILE_FORMAT		0x04	/* [7:0] FILE_FORMAT		R/W */
#define PROXY_IND_FRAME_RATE		0x05	/* [7:0] FRAME_RATE		R/W */
#define PROXY_IND_VIDEO_FORMAT		0x06	/* [7:0] VIDEO_FORMAT		R/W */
#define PROXY_IND_VIDEO_CODEC		0x07	/* [7:0] VIDEO_CODEC		R/W */
#define PROXY_IND_VIDEO_RATE_L		0x08	/* [7:0] VIDEO_RATE[7:0]	R/W */
#define PROXY_IND_VIDEO_RATE_H		0x09	/* [7:0] VIDEO_RATE[15:8]	R/W */
#define PROXY_IND_VIDEO_PROFILE		0x0a	/* [7:6] VIDEO_PROFILE		R/W */
#define PROXY_IND_VIDEO_ASPECT		0x0a	/* [5:4] VIDEO_ASPECT		R/W */
#define PROXY_IND_VIDEO_RESYNC		0x0a	/*   [3] VIDEO_RESYNC		R/W */
#define PROXY_IND_VIDEO_VBVPAR		0x0a	/*   [2] VIDEO_VBVPAR		R/W */
#define PROXY_IND_VIDEO_METYPE		0x0a	/*   [1] VIDEO_METYPE		R/W */
#define PROXY_IND_SCENE_CNG_CHK		0x0a	/*   [0] SCENE_CHANGE_CHK	R/W */
#define PROXY_IND_VIDEO_GVOP_L		0x0b	/* [7:0] VIDEO_GVOP[7:0]	R/W */
#define PROXY_IND_VIDEO_GVOP_H		0x0c	/* [7:0] VIDEO_GVOP[15:8]	R/W */
#define PROXY_IND_VIDEO_PACKET_L	0x0d	/* [7:0] VIDEO_PACKET[7:0]	R/W */
#define PROXY_IND_VIDEO_PACKET_H	0x0e	/* [7:0] VIDEO_PACKET[12:8]	R/W */
#define PROXY_IND_VIDEO_VBVSIZE		0x0f	/* [7:0] VIDEO_VBVSIZE		R/W */
#define PROXY_IND_AUDIO_CH		0x10	/* [7:0] AUDIO_CH		R/W */
#define PROXY_IND_AUDIO_SAMPLE		0x11	/* [7:0] AUDIO_SAMPLE		R/W */
#define PROXY_IND_AUDIO_CODEC		0x12	/* [7:0] AUDIO_CODEC		R/W */
#define PROXY_IND_AUDIO_RATE		0x13	/* [7:0] AUDIO_RATE		R/W */
#define PROXY_IND_AUDIO_CHSEL		0x14	/* [7:0] AUDIO_CHSEL		R/W */
#define PROXY_IND_PRE_FRAME_L		0x15	/* [7:0] Pre_Frame[7:0]		R/W */
#define PROXY_IND_PRE_FRAME_H		0x16	/*   [0] Pre_Frame[8]		R/W */
#define PROXY_IND_REC_MODE		0x16	/*   [7] Rec_Mode		R/W */
#define PROXY_IND_TC_SUPER_ON		0x17	/*   [7] TC_Super_On		R/W */
#define PROXY_IND_DF_FLAG		0x17	/*   [6] DF_Flag		R/W */
#define PROXY_IND_AF_BY_TC		0x17	/*   [5] AF_BY_TC		R/W */
#define PROXY_IND_SUPER_VPOSI		0x18	/* [7:0] Super_Vposi		R/W */
#define PROXY_IND_SUPER_HPOSI		0x19	/* [7:0] Super_Hposi		R/W */
#define PROXY_IND_FPGA_VERUP		0x1a	/*   [0] FPGA_VERUP		R/W */
#define PROXY_IND_META_SIZE_L		0x1c	/* [7:0] META_SIZE[7:0]		R/W */
#define PROXY_IND_META_SIZE_H		0x1d	/* [7:0] META_SIZE[15:8]	R/W */
#define PROXY_IND_FRVERR		0x1e	/*   [7] FRV_ERR		R/W */
#define PROXY_IND_FWRITE_OK		0x1e	/*   [6] FWRITE_OK		R/W */
#define PROXY_IND_FWRITE_NG		0x1e	/*   [5] FWRITE_NG		R/W */
#define PROXY_IND_FRV_RST		0x1f	/*   [7] FRV_RST		R/W */
						/* ----- FPGA ----------------- */
#define PROXY_IND_FPGA_DEVID_L		0x20	/* [7:0] Device_ID[7:0]		R  */
#define PROXY_IND_FPGA_DEVID_H		0x21	/* [7:0] Device_ID[15:8]	R  */
#define PROXY_IND_FPGA_REVID_L		0x22	/* [7:0] Revision_ID[7:0]	R  */
#define PROXY_IND_FPGA_REVID_H		0x23	/* [7:0] Revision_ID[15:8]	R  */
#define PROXY_IND_FPGA_BLD_Y_L		0x24	/* [7:0] Build_Year[7:0]	R  */
#define PROXY_IND_FPGA_BLD_Y_H		0x25	/* [7:0] Build_Year[15:8]	R  */
#define PROXY_IND_FPGA_BLD_D		0x26	/* [7:0] Build_Day[7:0]		R  */
#define PROXY_IND_FPGA_BLD_M		0x27	/* [7:0] Build_Month[7:0]	R  */
						/* ----- FR-V ----------------- */
#define PROXY_IND_FRV_DEVID_L		0x28	/* [7:0] Device_ID[7:0]		R  */
#define PROXY_IND_FRV_DEVID_H		0x29	/* [7:0] Device_ID[15:8]	R  */
#define PROXY_IND_FRV_REVID_L		0x2a	/* [7:0] Revision_ID[7:0]	R  */
#define PROXY_IND_FRV_REVID_H		0x2b	/* [7:0] Revision_ID[15:8]	R  */
#define PROXY_IND_FRV_BLD_Y_L		0x2c	/* [7:0] Build_Year[7:0]	R  */
#define PROXY_IND_FRV_BLD_Y_H		0x2d	/* [7:0] Build_Year[15:8]	R  */
#define PROXY_IND_FRV_BLD_D		0x2e	/* [7:0] Build_Day[7:0]		R  */
#define PROXY_IND_FRV_BLD_M		0x2f	/* [7:0] Build_Month[7:0]	R  */
						/* ---------------------------- */
#define PROXY_IND_VUP_DATA		0x30	/* [7:0] VUP_DATA[7:0]		W  */


/*
 * Address difinitions for Common Memory Area
 */
#define PROXY_CMN_ICTRL0	0x02	/* Indirect Control_Lo		R/W */
#define PROXY_CMN_ICTRL1	0x03	/* Indirect Control_Hi		R/W */
#define PROXY_CMN_IADDR0	0x04	/* Indirect Address[ 7: 0]	R/W */
#define PROXY_CMN_IADDR1	0x05	/* Indirect Address[15: 8]	R/W */
#define PROXY_CMN_IADDR2	0x06	/* Indirect Address[23:16]	R/W */
#define PROXY_CMN_IADDR3	0x07	/* Indirect Address[25:24]	R/W */
#define PROXY_CMN_IDATA0	0x08	/* Indirect Data[ 7:0]		R/W */
#define PROXY_CMN_IDATA1	0x09	/* Indirect Data[15:8]		R/W */


/* for PROXYIOC_CHK_IRQ */
#define PROXY_IRQ_FWRITE_NG	0x20
#define PROXY_IRQ_FWRITE_OK	0x40
#define PROXY_IRQ_FRV_ERR	0x80


#ifdef __KERNEL__


/*
 * Address difinitions for NVRAM
 */
/* Macros for AJ-HPX2000/2100/3000   */
#define NVRAM_BASE		0xa4000000		/* Area 1 for NVRAM		*/
#define NVRAM_PROXY_OFFSET	0x0100

#define NPB	(NVRAM_BASE + NVRAM_PROXY_OFFSET)	/* NVRAM_PROXY_BASE */

#define NVR_PXY_SERVICE_MODE	(NPB + 0x00)
#define NVR_PXY_FILE_FORMAT	(NPB + 0x04)
#define NVR_PXY_FRAME_RATE	(NPB + 0x08)	/* relation: FRAME_RATE */
#define NVR_PXY_VIDEO_CODEC	(NPB + 0x0c)
#define NVR_PXY_VIDEO_RATE_W	(NPB + 0x10)	/* relation: REC_RATE */
#define NVR_PXY_VIDEO_PROFILE	(NPB + 0x16)
#define NVR_PXY_VIDEO_GVOP_W	(NPB + 0x1a)
#define NVR_PXY_VIDEO_PACKET_W	(NPB + 0x20)
#define NVR_PXY_VIDEO_VBVSIZE	(NPB + 0x26)
#define NVR_PXY_AUDIO_CH	(NPB + 0x2a)	/* relation: REC_RATE */
#define NVR_PXY_AUDIO_SAMPLE	(NPB + 0x2e)
#define NVR_PXY_AUDIO_CODEC	(NPB + 0x32)
#define NVR_PXY_AUDIO_RATE	(NPB + 0x36)	/* relation: REC_RATE */
#define NVR_PXY_AUDIO_CHSEL	(NPB + 0x3a)	/* relation: REC_RATE */
#define NVR_PXY_TC_Super_On	(NPB + 0x3e)	/* relation: TC_SUPER */
#define NVR_PXY_Super_Vposi	(NPB + 0x42)	/* relation: TC_SUPER */
#define NVR_PXY_Super_Hposi	(NPB + 0x46)	/* relation: TC_SUPER */
#define NVR_PXY_META_SIZE_W	(NPB + 0x4a)

/* Macros for AJ-SPX800/900 AJ-SPC700	*/
#if 0
#define NVRAM_BASE		0xb4000000
#define NVRAM_PROXY_OFFSET 0x0600

#define NPB	(NVRAM_BASE + NVRAM_PROXY_OFFSET)	/* NVRAM_PROXY_BASE */

#define NVR_PXY_SERVICE_MODE	(NPB + 0x00)
#define NVR_PXY_FILE_FORMAT	(NPB + 0x04)
#define NVR_PXY_FRAME_RATE	(NPB + 0x08)	/* relation: FRAME_RATE */
#define NVR_PXY_VIDEO_CODEC	(NPB + 0x10)
#define NVR_PXY_VIDEO_RATE_W	(NPB + 0x16)	/* relation: REC_RATE */
#define NVR_PXY_VIDEO_PROFILE	(NPB + 0x1c)
#define NVR_PXY_VIDEO_GVOP_W	(NPB + 0x20)
#define NVR_PXY_VIDEO_PACKET_W	(NPB + 0x26)
#define NVR_PXY_VIDEO_VBVSIZE	(NPB + 0x2c)
#define NVR_PXY_AUDIO_CH	(NPB + 0x30)	/* relation: REC_RATE */
#define NVR_PXY_AUDIO_SAMPLE	(NPB + 0x34)
#define NVR_PXY_AUDIO_CODEC	(NPB + 0x38)
#define NVR_PXY_AUDIO_RATE	(NPB + 0x3c)	/* relation: REC_RATE */
#define NVR_PXY_AUDIO_CHSEL	(NPB + 0x40)	/* relation: REC_RATE */
#define NVR_PXY_TC_Super_On	(NPB + 0x44)	/* relation: TC_SUPER */
#define NVR_PXY_Super_Vposi	(NPB + 0x48)	/* relation: TC_SUPER */
#define NVR_PXY_Super_Hposi	(NPB + 0x4c)	/* relation: TC_SUPER */
#define NVR_PXY_META_SIZE_W	(NPB + 0x50)
#endif

typedef struct proxy_nvram_value {
	unsigned char    service_mode;
	unsigned char    file_format;
	unsigned char    video_codec;
	unsigned char    video_profile;
	unsigned short   video_gvop;
	unsigned short   video_packet;
	unsigned char    video_vbvsize;
	unsigned char    audio_sample;
	unsigned char    audio_codec;
	unsigned short   meta_size;
	struct {
		unsigned char  frame_rate;
	} FRAME_RATE;
	struct {
		unsigned short video_rate;
		unsigned char  audio_ch;
		unsigned char  audio_rate;
		unsigned char  audio_chsel;
	} REC_RATE;
	struct {
		unsigned char  tc_super_on;
		unsigned char  super_vposi;
		unsigned char  super_hposi;
	} TC_SUPER;
} proxy_nvram_value_t;


/*
 * Macros to help debugging
 */
#undef IR
#ifdef DEBUG_EVENT
#  define IR(n) \
do { \
  volatile unsigned char *addr = (volatile unsigned char *)0xba000000; \
  static int i = 0, r = 0; \
  if ((n)) { \
    if (!i) { \
      PDEBUG("======== INSERTION ========\n"); \
      *addr |= 0x80; \
      i++; \
    } \
  } else { \
    if (!r) { \
      PDEBUG("======== REMOVAL ========\n"); \
      *addr &= ~0x80; \
      r++; \
    } \
  } \
} while (0)
#else
#  define IR(n)
#endif	/* DEBUG_EVENT */

#endif	/* __KERNEL__ */


#endif	/* _PROXY_H_ */

