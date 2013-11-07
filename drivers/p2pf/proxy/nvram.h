#ifndef __PROXY_NVRAM_H__
#define __PROXY_NVRAM_H__

/*
 * Address difinitions for NVRAM
 */
/* Macros for AJ-HPX2000/2100/3000   */
#define NVRAM_BASE		0xF0100000//0xa4000000		/* Area 1 for NVRAM		*/
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

#endif // __PROXY_NVRAM_H__
