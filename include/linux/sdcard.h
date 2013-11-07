#ifndef __SDCARD_H__
#define __SDCARD_H__

#include <linux/blkpg.h>

struct sdcard_block_erase
{
	unsigned long start;	/* start sector */
	unsigned long size;	/* number of sectors */
};

typedef enum {SD, SDHC, SDXC} SdCardType_t;

struct sdcard_params
{
	unsigned long	is_sdhc; //dont use
	SdCardType_t	card_type;
	long		speed_class;
	unsigned long	performance_move;
	unsigned long	au_size;
	unsigned long	erase_size;
	unsigned long	erase_timeout;
	unsigned long	erase_offset;
};

typedef unsigned char sd_slot_image;

struct sd_card_status {
	sd_slot_image open_request;	/* mount request */
	sd_slot_image release_request;	/* umount request */
	sd_slot_image slot_image;	/* card inserted or not */
	sd_slot_image open_image;	/* card mounted or not */
};

struct sd_err_status {
	sd_slot_image carderr;
};

struct sd_errno {  
	int no;
};

#define SDCARD_GET_CSD			_IOR('x', 255, unsigned char)
#define SDCARD_GET_CID			_IOR('x', 254, unsigned char)
#define SDCARD_BLOCK_ERASE		_IOW('x', 253, struct sdcard_block_erase)
#define SDCARD_TOTAL_SECTORS		_IOR('x', 252, unsigned long)
#define SDCARD_CHECK_EXISTENCE		_IOR('x', 251, unsigned char)
#define SDCARD_REMOVAL_WAIT		_IOR('x',250, int)
#define SDCARD_WAKE_UP			_IO('x',249)
#define SDCARD_GET_CARD_PARAMS		_IOR('x', 248, struct sdcard_params)
#define SDCARD_KERNEL_GET_CARD_PARAMS	_IOR('x', 247, struct sdcard_params)
#define SDCARD_LED_ON			_IO('x',246)
#define SDCARD_LED_OFF			_IO('x',245)
#define SDCARD_GET_SD_STATUS		_IOR('x', 244, unsigned char)

/*
 * Return values for SDCARD_CHECK_EXISTENCE
 *
 * SDCARD_ABSENCE -> SD card doesn't exist in the slot.
 * SDCARD_NORMAL_STATES -> SD card exists, and you can R/W normally.
 * SDCARD_NEW_CARD -> SD card exists, and needs mount or re-mount operation.
 *
 */

#define SDCARD_ABSENCE		0
#define SDCARD_NORMAL_STATE	1
#define SDCARD_NEW_CARD		2

#endif
