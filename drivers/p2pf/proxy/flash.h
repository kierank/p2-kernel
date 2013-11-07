#ifndef __PROXY_FLASH_H__
#define __PROXY_FLASH_H__

// Macros for direct access to Flash memory
#define BACKUP_ADDR			0x81A40000
#define BASE_ADDRESS			(BACKUP_ADDR - 0x80000000)

#define MIN_AREA_NUM			1
#define MAX_AREA_NUM			15

#define AREA_SIZE			0x00040000
#define SECTOR_SIZE			0x00020000
#define BUP_BLOCK_SIZE			0x00002000

#define BLOCK_ACTIVE			0x0000
#define BLOCK_NON_ACTIVE		0xFFFF
#define BLOCK_CRC_ERROR			0x0001

#define BLOCK_USED			0x0000
#define BLOCK_NOT_USED			0xFFFF

// Macros for read data from Flash memory
#define NUM_AREA_USED			12

#define ROM_AREA_NO_UPDATE		1
#define ROM_AREA_NO_VERSION		2
#define ROM_AREA_NO_PROXY		5
#define ROM_AREA_NO_UNKNOWN		-1

#define ROM_PROXY_SETTINGS_SIZE		32

#endif // __PROXY_FLASH_H__
