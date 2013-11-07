/**
 * flash.c
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

#include <proxy/proxy.h>
#include "flash.h"
#include "debug.h"

#ifdef FLASH_SYSTEM

union CONVERT_8BYTE
{
	unsigned long long	m_ulonglong;
	unsigned long		m_ulong[2];
	unsigned short		m_ushort[4];
	unsigned char		m_byte[8];
};

static int search_current_block( int *o_piIndex, int i_iAreaNum )
{
	int iAreaBaseOffset;
	int iBlock, iCurrentBlock = -1;

	// Calcurate start address of recording data
	iAreaBaseOffset = AREA_SIZE*( i_iAreaNum - 1 );

	// Search latest backuped area(block) in all blocks
	for( iBlock = 0; iBlock < AREA_SIZE/BUP_BLOCK_SIZE; iBlock++ ){
		off_t SeekByte;
		union CONVERT_8BYTE cvt8_BlockHeader;
		off_t SeekAddress = 0;

		// Calcurate offset address of each blocks
		SeekByte = iAreaBaseOffset + BUP_BLOCK_SIZE*iBlock;

		// Seek start address of the latest block
		SeekAddress = SeekByte + (BASE_ADDRESS + 0x80000000);
		PINFO( "Seek header address: %08lx ", SeekAddress );

		// Read Block Header
		cvt8_BlockHeader.m_ulonglong = 0xFFFFFFFFFFFFFFFFLL;
		cvt8_BlockHeader.m_ulong[0] = *(unsigned long *)(SeekAddress);
		cvt8_BlockHeader.m_ulong[1] = *(unsigned long *)(SeekAddress+4);
		PINFO( "Header: %08lx ", cvt8_BlockHeader.m_ulong[0] );

		// Determine whether the block, and if the block is ACTIVE && USED, updates iCurrentBlock
		if( cvt8_BlockHeader.m_ulong[0] == ( (BLOCK_ACTIVE << 16) + BLOCK_USED ) ){
			iCurrentBlock = iBlock;
			PINFO( "Find Active Block: %04x ", iCurrentBlock );
		}
	}

	// Could not be able to find active block
	if( iCurrentBlock == -1 ){
		PERROR( "RomMgr: FAIL to search block" );
		return -1;
	}
	*o_piIndex = iCurrentBlock;

	return 0;
}

static int read_values_from_flash( int i_iAreaNum, unsigned long *o_pulDataBuf, unsigned short i_usDataSize )
{
	int iCurrentBlock = -1;
	int iAreaBaseOffset;
	off_t iBlockStartAddress;
	int iAddress = 0;
	int iItr = 0;

	// Check out arguments			 
	if( i_iAreaNum < MIN_AREA_NUM || MAX_AREA_NUM < i_iAreaNum
		|| o_pulDataBuf == NULL
		|| i_usDataSize > ( BUP_BLOCK_SIZE - 8 ) ){
		PERROR( "RomMgr: Invalid argument : Area Number = %d, Request Size = %d", i_iAreaNum, i_usDataSize );
		return 10;
	}

	// Search latest block			 
	search_current_block( &iCurrentBlock, i_iAreaNum );
	if( iCurrentBlock < 0 ){
		PERROR( "RomMgr: FAIL to find Current Block : Area Number = %d", i_iAreaNum );
		return 11;
	}

	// Calcurate start address of the area	 
	iAreaBaseOffset = AREA_SIZE*( i_iAreaNum - 1 );

	// Calcurate offset address of the block
	iBlockStartAddress = iAreaBaseOffset + BUP_BLOCK_SIZE*iCurrentBlock;

	// Seek to the start address of the block
	// 8 = size of INDEX & CRC
	iAddress = iBlockStartAddress + 8 + (BASE_ADDRESS + 0x80000000);

	// Read values form memory
	for ( iItr = 0; iItr < (int)(i_usDataSize/4); iItr+=1 ) {
		o_pulDataBuf[iItr] = *(unsigned long *)(iAddress+ iItr * 4);
		PINFO( "RomMgr: read data : Address[%08x] = %08x ",
			(iAddress + iItr * 4), (*(unsigned long *)(iAddress+iItr*4)) );
	}

	return 0;
}

void proxy_read_value_from_flash(proxy_nvram_value_t *val)
{
	unsigned long ulProxySettings[(ROM_PROXY_SETTINGS_SIZE/4)];
	int ret;

	union{
		unsigned long l;
		unsigned char b[4];
	}convl;

	union{
		unsigned short s;
		unsigned char b[2];
	}convs;

	// Read current settings form flash memory via rommgr
	ret = read_values_from_flash(ROM_AREA_NO_PROXY, ulProxySettings, ROM_PROXY_SETTINGS_SIZE);
	if ( ret == 0 ) {
		PINFO("[Proxy]: Read settings from Flash memory" );

		// Set values read from flash memory
		convl.l = (unsigned long)ulProxySettings[0];
		val->service_mode 		= (unsigned char)convl.b[0];
		val->file_format 		= (unsigned char)convl.b[1];
		val->FRAME_RATE.frame_rate	= (unsigned char)convl.b[2];
		val->video_codec 		= (unsigned char)convl.b[3];

		convl.l = (unsigned long)ulProxySettings[1];
		convs.b[0] = (unsigned char)convl.b[0];
		convs.b[1] = (unsigned char)convl.b[1];
		val->REC_RATE.video_rate	= (unsigned short)convs.s;
		val->video_profile		= (unsigned char)convl.b[2];
		// skip convl.b[3] due to byte padding

		convl.l = (unsigned long)ulProxySettings[2];
		convs.b[0] = (unsigned char)convl.b[0];
		convs.b[1] = (unsigned char)convl.b[1];
		val->video_gvop			= (unsigned short)convs.s;
		convs.b[0] = (unsigned char)convl.b[2];
		convs.b[1] = (unsigned char)convl.b[3];
		val->video_packet 		= (unsigned short)convs.s;

		convl.l = (unsigned long)ulProxySettings[3];
		val->video_vbvsize	      = (unsigned char)convl.b[0];
		val->REC_RATE.audio_ch 		= (unsigned char)convl.b[1];
		val->audio_sample 		= (unsigned char)convl.b[2];
		val->audio_codec 		= (unsigned char)convl.b[3];

		convl.l = (unsigned long)ulProxySettings[4];
		val->REC_RATE.audio_rate	= (unsigned char)convl.b[0];
		val->REC_RATE.audio_chsel	= (unsigned char)convl.b[1];
		val->TC_SUPER.tc_super_on 	= (unsigned char)convl.b[2];
		val->TC_SUPER.super_vposi	= (unsigned char)convl.b[3];

		convl.l = (unsigned long)ulProxySettings[5];
		val->TC_SUPER.super_hposi       = (unsigned char)convl.b[0];
		// skip convl.b[1] due to byte padding 
		convs.b[0] = (unsigned char)convl.b[2];
		convs.b[1] = (unsigned char)convl.b[3];
		val->meta_size			= (unsigned short)convs.s;

		PINFO("##### NVRAM[%d] #####", ROM_AREA_NO_PROXY);
		PINFO("service_mode  = 0x%02x", val->service_mode);
		PINFO("file_format   = 0x%02x", val->file_format);
		PINFO("video_codec   = 0x%02x", val->video_codec);
		PINFO("video_profile = 0x%02x", val->video_profile);
		PINFO("video_gvop    = 0x%04x", val->video_gvop);
		PINFO("video_packet  = 0x%04x", val->video_packet);
		PINFO("video_vbvsize = 0x%02x", val->video_vbvsize);
		PINFO("audio_sample  = 0x%02x", val->audio_sample);
		PINFO("audio_codec   = 0x%02x", val->audio_codec);
		PINFO("meta_size     = 0x%04x", val->meta_size);
		PINFO("----- FRAME_RATE -----");
		PINFO("frame_rate    = 0x%02x", val->FRAME_RATE.frame_rate);
		PINFO("----- REC_RATE -----");
		PINFO("video_rate    = 0x%04x", val->REC_RATE.video_rate);
		PINFO("audio_ch      = 0x%02x", val->REC_RATE.audio_ch);
		PINFO("audio_rate    = 0x%02x", val->REC_RATE.audio_rate);
		PINFO("audio_chsel   = 0x%02x", val->REC_RATE.audio_chsel);
		PINFO("----- TC_SUPER -----");
		PINFO("tc_super_on   = 0x%02x", val->TC_SUPER.tc_super_on);
		PINFO("super_vposi   = 0x%02x", val->TC_SUPER.super_vposi);
		PINFO("super_hposi   = 0x%02x", val->TC_SUPER.super_hposi);

	}else {
		PINFO("[Proxy]: Error returns read settings from Flash memory, use default values [%d]", ret );
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
}
#endif //FLASH_SYSTEM
