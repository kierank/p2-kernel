/*-----------------------------------------------------------------------------
    p2_mass_storage.h
-----------------------------------------------------------------------------*/
/* $Id: p2_mass_storage.h 3808 2009-09-01 02:07:03Z Hoshino Hiromasa $ */
#ifndef __P2_MASS_STORAGE_H__
#define __P2_MASS_STORAGE_H__

#include <linux/types.h>

/* ioctl code 			*/
#define P2USB_GET_DATA         0
#define P2USB_SET_DATA         1
#define P2USB_COMMAND_STATUS   2
#define P2USB_SET_STATE        3
#define P2USB_SET_SERIAL       4
#define P2USB_SET_PRODUCT_ID   5

#define P2USB_MAX_SERIAL	40

/* set/clear usb mode	*/
#define P2USB_DISABLE_USB_MODE 0
#define P2USB_ENABLE_USB_MODE  1

/* ioctl error code		*/
#define P2USB_WRONG_PARAM 1
#define P2USB_WRONG_STATE 2

/* ineternal macros		*/
#define P2USB_STALL       0x01
#define P2USB_DO_TRANSFER 0x02

/* definition of WhichReturn bit	*/
#define P2USB_CBW_BIT     0x0001
#define P2USB_CMD_BIT     0x0002
#define P2USB_SET_BIT     0x0004
#define P2USB_MRT_BIT     0x0008
#define P2USB_ERR_BIT     0x8000

/* definition of ConnectStatus bit	*/
#define P2USB_ACT_BIT     0x0001
#define P2USB_DACT_BIT    0x0100

/* definition of detail error		*/
#define CBW_REQUEST_ERROR     0x01
#define ALLOC_REQUEST_ERROR   0x02
#define QUEUE_REQUEST_ERROR   0x03
#define ALLOC_CONTEXT_ERROR   0x04
#define DMA_TRANSFER_ERROR    0x05
#define OTHER_TRANSFER_ERROR1 0x06
#define OTHER_TRANSFER_ERROR2 0x07
#define QUEUE_STATE_ERROR     0x08
#define INVALID_STATE_ERROR   0x09

/* Number of LUNs on the device --> (Actual number of slot - 1) */
#define P2USB_LUN	(CONFIG_P2USB_LUN)


/* Bulk-Only Command Structures */
struct p2usb_cbw {              /* Command Block Wrapper */
    __u32 signature;            /* 0x00 CBW Signature */
    __u32 tag;                  /* 0x04 Command Block Tag */
    __u32 data_transfer_length; /* 0x08 Number of bytes expected 
                                 *    to be transferred */
    __u8 flags;                 /* 0x0C   7:Direction of transfer, 
                                   6:Obsolete, 
                                   5-0:Reserved */
    __u8 lun;                   /* 0x0D 7-4:Reserved, 
                                   3-0:Logical Unit Number */
    __u8 cb_length;             /* 0x0E 7-5:Reserved, 
                                   4-0:Length of CBWCB (0x00-0x10) */
    __u8 cb[16];               /* 0x0F Command Block to be executed */
};

struct p2usb_csw {            /* Command Status Wrapper */
    __u32 signature;      /* 0x00 CSW Signature */
    __u32 tag;        /* 0x04 Command Status Tag */
    __u32 data_residue;   /* 0x08 Difference between data expected 
                        and data sent/received */
    __u8 status;      /* 0x0C Command Status */
};

/* used to ioctl(xx, GET_DATA, xx)	*/
typedef struct _P2USB_GET_DATA_STRUCT{
    __u16 WhichReturn;
    __u16 ConnectStatus;
    __u32 ReturnStatus;
    __u32 TransferedLength;
    __u32 SerialNumber;
    struct p2usb_cbw CbwData;
}P2USB_GET_DATA_STRUCT, *PP2USB_GET_DATA_STRUCT;

/* used to ioctl(xx, SET_DATA, xx)	*/
typedef struct _P2USB_SET_DATA_STRUCT{
    __u32 SetDataTransferInfo;
    __u32 TransferAddress;
    __u32 TransferLength;
    __u32 SerialNumber;
    struct p2usb_csw CswData;
}P2USB_SET_DATA_STRUCT, *PP2USB_SET_DATA_STRUCT;

/* used to ioctl(xx, COMMAND_STATUS, xx)	*/
typedef struct _P2USB_DMA_INFO_STRUCT{
    __u32 Boundary;
    __u32 TransferAddress0;
    __u32 TransferLength0;
    __u32 TransferAddress1;
    __u32 TransferLength1;
}P2USB_DMA_INFO_STRUCT, *PP2USB_DMA_INFO_STRUCT;

typedef struct _P2USB_COMMAND_STATUS_STRUCT{
    __u32 CommandStatusTransferInfo;
    __u32 SerialNumber;
    P2USB_DMA_INFO_STRUCT DmaInfo;
}P2USB_COMMAND_STATUS_STRUCT, *PP2USB_COMMAND_STATUS_STRUCT;

typedef struct {
    int len;
    const char *str;
} P2USB_SET_SERIAL_STRUCT, *PP2USB_SET_SERIAL_STRUCT;
#endif /* __P2_MASS_STORAGE_H__ */
