#ifndef __SDOP_IOCTL_H__
#define __SDOP_IOCTL_H__

struct sdop_err_chk{
	unsigned int slot;
	unsigned long errcode;
};

//For fsdm
typedef enum {SDOP_FAT, SDOP_BITMAP, SDOP_DIR, SDOP_DATA, SDOP_TMP, SDOP_MULTIACCESS, SDOP_ALL} sdop_area_t;

typedef struct {
	sdop_area_t	area;//[in]
	int num;//[in]
	int blk_size;//[out]
	unsigned long *addr;//[out]
} sdop_fsdm_param_t;

typedef struct {
	sdop_area_t	area;//[in]
	int	block;//[in]
	void *bus_addr;//[out]
} sdop_fsdm_addr_t;

//マルチアクセスDMサイズ取得用
typedef struct {
	sdop_area_t area;//[in]
	int blk_num;//[out]
	unsigned long blk_size;//[out]
	unsigned long total_size;//[out]
} sdop_fsdm_size_t;

#define SDOP_IOCTL_START_CHECK		_IOR('x', 1, struct sdop_err_chk)
#define SDOP_IOCTL_STOP_CHECK		_IOW('x', 2, unsigned int)
#define SDOP_IOCTL_SET_ERRCODE		_IOW('x', 3, struct sdop_err_chk) //for debug
#define SDOP_IOCTL_SET_IGNORE_RW	_IO('x',  4)
#define SDOP_IOCTL_CLEAR_IGNORE_RW	_IO('x',  5)
#define SDOP_IOCTL_SET_PLAY_MODE	_IO('x',  6)
#define SDOP_IOCTL_CLEAR_PLAY_MODE	_IO('x',  7)
#define SDOP_IOCTL_GET_CARD_STATUS	_IOR('x', 8, struct sd_card_status)
#define SDOP_IOCTL_CHECK_CARD_STATUS	_IOR('x', 9, struct sd_card_status)
#define SDOP_IOCTL_GET_ALL_CARD_STATUS	_IOR('x',10, unsigned char)
#define SDOP_IOCTL_AWAKE_CARD_STATUS	_IO('x', 11)
#define SDOP_IOCTL_TERMINATE		_IO('x', 12)
#define SDOP_IOCTL_GET_ERR_STATUS	_IOR('x',13, struct sd_err_status)
#define SDOP_IOCTL_SET_ERR_STATUS	_IOW('x',14, struct sd_err_status)
#define SDOP_IOCTL_GET_ERRNO		_IOR('x',15, struct sd_errno)
#define SDOP_IOCTL_CLEAR_OPEN_REQ	_IOR('x',16, unsigned int)
#define SDOP_IOCTL_CLEAR_RELEASE_REQ	_IOR('x',17, unsigned int)
#define SDOP_IOCTL_SET_DETECT_STATUS	_IOR('x',18, unsigned char)

//For fsdm
#define SDOP_IOCTL_FSDM_MALLOC		_IOW('x',30, sdop_fsdm_param_t)
#define SDOP_IOCTL_FSDM_FREE		_IOR('x',31, sdop_fsdm_param_t)
#define SDOP_IOCTL_GET_BUS_ADDR		_IOR('x',32, sdop_fsdm_addr_t)
#define SDOP_IOCTL_GET_FSDM_SIZE    _IOR('x',33, sdop_fsdm_addr_t)

#endif //__SDOP_IOCTL_H__
