/**
 * nvram.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 **/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/interrupt.h>

#include "proxy.h"
#include "debug.h"

#ifdef NVRAM_SYSTEM

static void __proxy_read_value_from_nvram(proxy_nvram_value_t nvram[], int n)
{
	int i;

	PRINT_FUNC;
	for (i = 0; i < n; i++) {
		nvram[i].service_mode	= *(volatile unsigned char *)(NVR_PXY_SERVICE_MODE + i);
		nvram[i].file_format	= *(volatile unsigned char *)(NVR_PXY_FILE_FORMAT + i);
		nvram[i].video_codec	= *(volatile unsigned char *)(NVR_PXY_VIDEO_CODEC + i);
		nvram[i].video_profile	= *(volatile unsigned char *)(NVR_PXY_VIDEO_PROFILE + i);
		nvram[i].video_gvop	= *(volatile unsigned short *)(NVR_PXY_VIDEO_GVOP_W + (i * 0x2));
		nvram[i].video_packet	= *(volatile unsigned short *)(NVR_PXY_VIDEO_PACKET_W + (i * 0x2));
		nvram[i].video_vbvsize	= *(volatile unsigned char *)(NVR_PXY_VIDEO_VBVSIZE + i);
		nvram[i].audio_sample	= *(volatile unsigned char *)(NVR_PXY_AUDIO_SAMPLE + i);
		nvram[i].audio_codec	= *(volatile unsigned char *)(NVR_PXY_AUDIO_CODEC + i);
		nvram[i].meta_size	= *(volatile unsigned short *)(NVR_PXY_META_SIZE_W + (i * 0x2));
		nvram[i].FRAME_RATE.frame_rate	= *(volatile unsigned char *)(NVR_PXY_FRAME_RATE + i);
		nvram[i].REC_RATE.video_rate	= *(volatile unsigned short *)(NVR_PXY_VIDEO_RATE_W + (i * 0x2));
		nvram[i].REC_RATE.audio_ch	= *(volatile unsigned char *)(NVR_PXY_AUDIO_CH + i);
		nvram[i].REC_RATE.audio_rate	= *(volatile unsigned char *)(NVR_PXY_AUDIO_RATE + i);
		nvram[i].REC_RATE.audio_chsel	= *(volatile unsigned char *)(NVR_PXY_AUDIO_CHSEL + i);
		nvram[i].TC_SUPER.tc_super_on	= *(volatile unsigned char *)(NVR_PXY_TC_Super_On + i);
		nvram[i].TC_SUPER.super_vposi	= *(volatile unsigned char *)(NVR_PXY_Super_Vposi + i);
		nvram[i].TC_SUPER.super_hposi	= *(volatile unsigned char *)(NVR_PXY_Super_Hposi + i);

		PINFO("##### NVRAM[%d] #####", i);
		PINFO("service_mode  = 0x%02x", nvram[i].service_mode);
		PINFO("file_format   = 0x%02x", nvram[i].file_format);
		PINFO("video_codec   = 0x%02x", nvram[i].video_codec);
		PINFO("video_profile = 0x%02x", nvram[i].video_profile);
		PINFO("video_gvop    = 0x%04x", nvram[i].video_gvop);
		PINFO("video_packet  = 0x%04x", nvram[i].video_packet);
		PINFO("video_vbvsize = 0x%02x", nvram[i].video_vbvsize);
		PINFO("audio_sample  = 0x%02x", nvram[i].audio_sample);
		PINFO("audio_codec   = 0x%02x", nvram[i].audio_codec);
		PINFO("meta_size     = 0x%04x", nvram[i].meta_size);
		PINFO("----- FRAME_RATE -----");
		PINFO("frame_rate    = 0x%02x", nvram[i].FRAME_RATE.frame_rate);
		PINFO("----- REC_RATE -----");
		PINFO("video_rate    = 0x%04x", nvram[i].REC_RATE.video_rate);
		PINFO("audio_ch      = 0x%02x", nvram[i].REC_RATE.audio_ch);
		PINFO("audio_rate    = 0x%02x", nvram[i].REC_RATE.audio_rate);
		PINFO("audio_chsel   = 0x%02x", nvram[i].REC_RATE.audio_chsel);
		PINFO("----- TC_SUPER -----");
		PINFO("tc_super_on   = 0x%02x", nvram[i].TC_SUPER.tc_super_on);
		PINFO("super_vposi   = 0x%02x", nvram[i].TC_SUPER.super_vposi);
		PINFO("super_hposi   = 0x%02x", nvram[i].TC_SUPER.super_hposi);
  }
}

static const void *proxy_majority_decision(const void *s1, const void *s2, const void *s3, size_t n)
{
	PINFO("### &s1=0x%p, &s2=0x%p, &s3=0x%p", s1, s2, s3);

	if (memcmp(s1, s2, n) == 0) {
		PINFO("s1 == s2 --> s1");
		return s1;
	} else if (memcmp(s1, s3, n) == 0) {
		PINFO("s1 == s3 --> s1");
		return s1;
	} else if (memcmp(s2, s3, n) == 0) {
		PINFO("s2 == s3 --> s2");
		return s2;
	} else {
		PINFO("s1 != s2 != s3 --> s1");
		return s1;
	}
}

static void proxy_copy_valid_value(
		void *dest_addr, unsigned int offset, proxy_nvram_value_t nvram[], size_t size)
{
	const void *src_addr = proxy_majority_decision(
		(void *)((unsigned int)&nvram[0] + offset),
		(void *)((unsigned int)&nvram[1] + offset),
		(void *)((unsigned int)&nvram[2] + offset),
		size);
	
	memcpy(dest_addr, src_addr, size);
}

void proxy_read_value_from_nvram(proxy_nvram_value_t *val)
{
	proxy_nvram_value_t nvram[3];

	PRINT_FUNC;
	__proxy_read_value_from_nvram(nvram, 3);

	proxy_copy_valid_value(
		&val->service_mode,
		(unsigned int)&val->service_mode - (unsigned int)val,
		nvram, sizeof(unsigned char));

	proxy_copy_valid_value(
		&val->file_format,
		(unsigned int)&val->file_format - (unsigned int)val,
		nvram, sizeof(unsigned char));

	proxy_copy_valid_value(
		&val->video_codec,
		(unsigned int)&val->video_codec - (unsigned int)val,
		nvram, sizeof(unsigned char));

	proxy_copy_valid_value(
		&val->video_profile,
		(unsigned int)&val->video_profile - (unsigned int)val,
		nvram, sizeof(unsigned char));

	proxy_copy_valid_value(
		&val->video_gvop,
		(unsigned int)&val->video_gvop - (unsigned int)val,
		nvram, sizeof(unsigned short));

	proxy_copy_valid_value(
		&val->video_packet,
		(unsigned int)&val->video_packet - (unsigned int)val,
		nvram, sizeof(unsigned short));

	proxy_copy_valid_value(
		&val->video_vbvsize,
		(unsigned int)&val->video_vbvsize - (unsigned int)val,
		nvram, sizeof(unsigned char));

	proxy_copy_valid_value(
		&val->audio_sample,
		(unsigned int)&val->audio_sample - (unsigned int)val,
		nvram, sizeof(unsigned char));

	proxy_copy_valid_value(
		&val->audio_codec,
		(unsigned int)&val->audio_codec - (unsigned int)val,
		nvram, sizeof(unsigned char));

	proxy_copy_valid_value(
		&val->meta_size,
		(unsigned int)&val->meta_size - (unsigned int)val,
		nvram, sizeof(unsigned short));

	proxy_copy_valid_value(
		&val->FRAME_RATE,
		(unsigned int)&val->FRAME_RATE - (unsigned int)val,
		nvram, sizeof(unsigned char) * 1);

	proxy_copy_valid_value(
		&val->REC_RATE,
		(unsigned int)&val->REC_RATE - (unsigned int)val,
		nvram, sizeof(unsigned short) + (sizeof(unsigned char) * 3));

	proxy_copy_valid_value(
		&val->TC_SUPER,
		(unsigned int)&val->TC_SUPER - (unsigned int)val,
		nvram, sizeof(unsigned char) * 3);
}

void proxy_check_invalid_value(proxy_nvram_value_t *val)
{
	proxy_nvram_value_t invalid_val;

	PRINT_FUNC;
	memset(&invalid_val, 0, sizeof(proxy_nvram_value_t));

	if (memcmp(val, &invalid_val, sizeof(proxy_nvram_value_t))) {
		PINFO("The initial values read from NVRAM were judged as valid.");
		return;
	}

	PERROR("The initial values read from NVRAM were judged as invalid.");
	val->service_mode = 0x00;
	val->file_format = 0x01;
	val->video_codec = 0x00;
	val->video_profile = 0xca;
	val->video_gvop = 0x0000;
	val->video_packet = 0x0000;
	val->video_vbvsize = 0x00;
	val->audio_sample = 0x02;
	val->audio_codec = 0x01;
	val->meta_size = 92;
	val->FRAME_RATE.frame_rate = 0x00;
	val->REC_RATE.video_rate = 0x05dc;
	val->REC_RATE.audio_ch = 0x02;
	val->REC_RATE.audio_rate = 0x01;
	val->REC_RATE.audio_chsel = 0x08;
	val->TC_SUPER.tc_super_on = 0x00;
	val->TC_SUPER.super_vposi = 0x00;
	val->TC_SUPER.super_hposi = 0x00;
}
#endif //NVRAM_SYSTEM
