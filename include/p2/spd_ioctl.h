/*
 P2card driver IOCTL header
 $Id: spd_ioctl.h 223 2006-09-19 11:03:44Z hiraoka $
 */
#ifndef _SPD_IOCTL_H
#define _SPD_IOCTL_H

#include <linux/types.h>

enum SPD_IOC_ENUM {
  P2_IOC_MAGIC                = 'p',
  P2_GET_CARD_STATUS          = _IO(P2_IOC_MAGIC,  1),
  P2_CHECK_CARD_STATUS        = _IO(P2_IOC_MAGIC,  2),
  P2_AWAKE_CARD_STATUS        = _IO(P2_IOC_MAGIC,  4),
  P2_COMMAND_LOG_SENSE        = _IO(P2_IOC_MAGIC,  5),
  P2_COMMAND_BLK_ERASE        = _IO(P2_IOC_MAGIC,  6),
  P2_GET_CARD_ID              = _IO(P2_IOC_MAGIC,  7),
  P2_CHECK_WRITE_PROTECT      = _IO(P2_IOC_MAGIC,  8),
  P2_GET_DMA_STATUS           = _IO(P2_IOC_MAGIC,  9),
  P2_SET_DMA_STATUS           = _IO(P2_IOC_MAGIC, 10),
  P2_CHECK_READING            = _IO(P2_IOC_MAGIC, 11),
  P2_DIRECT_WRITE             = _IO(P2_IOC_MAGIC, 12),
  P2_DIRECT_READ              = _IO(P2_IOC_MAGIC, 13),
  P2_GET_ERR_STATUS           = _IO(P2_IOC_MAGIC, 14),
  P2_SET_ERR_STATUS           = _IO(P2_IOC_MAGIC, 15),
  P2_GET_ERRNO                = _IO(P2_IOC_MAGIC, 16),
  P2_GET_CARD_PARAMS          = _IO(P2_IOC_MAGIC, 19),
  P2_KERNEL_GET_CARD_PARAMS   = _IO(P2_IOC_MAGIC, 20),
  P2_COMMAND_CARD_INITIALIZE  = _IO(P2_IOC_MAGIC, 21),
  P2_COMMAND_IDENTIFY_DEVICE  = _IO(P2_IOC_MAGIC, 22),
  P2_TERMINATE                = _IO(P2_IOC_MAGIC, 23),
  P2_DIRECT_SEQ_WRITE         = _IO(P2_IOC_MAGIC, 24),
  P2_COMMAND_CARD_RESCUE      = _IO(P2_IOC_MAGIC, 25),

  P2_COMMAND_SECURED_REC      = _IO(P2_IOC_MAGIC, 26),
  P2_COMMAND_START_REC        = _IO(P2_IOC_MAGIC, 27),
  P2_COMMAND_END_REC          = _IO(P2_IOC_MAGIC, 28),
  P2_FSMI_WRITE               = _IO(P2_IOC_MAGIC, 29),
  P2_SDCMD_WRITE              = _IO(P2_IOC_MAGIC, 30),
  P2_COMMAND_SD_DEVICE_RESET  = _IO(P2_IOC_MAGIC, 31),
  P2_COMMAND_TABLE_RECOVER    = _IO(P2_IOC_MAGIC, 32),
  P2_COMMAND_LMG              = _IO(P2_IOC_MAGIC, 33),
  P2_COMMAND_GET_SST          = _IO(P2_IOC_MAGIC, 34),
  P2_COMMAND_GET_DINFO        = _IO(P2_IOC_MAGIC, 35),
  P2_COMMAND_DMG              = _IO(P2_IOC_MAGIC, 36),
  P2_COMMAND_ESW              = _IO(P2_IOC_MAGIC, 37),
  P2_COMMAND_GET_PH           = _IO(P2_IOC_MAGIC, 38),
  P2_COMMAND_GO_HIBERNATE     = _IO(P2_IOC_MAGIC, 39),
  P2_COMMAND_AU_ERASE         = _IO(P2_IOC_MAGIC, 40),
  P2_SET_CARD_PARAMS          = _IO(P2_IOC_MAGIC, 41),
  P2_DIRECT_RMW               = _IO(P2_IOC_MAGIC, 42),
  P2_COMMAND_SET_NEW_AU       = _IO(P2_IOC_MAGIC, 43),
  P2_COMMAND_GET_LINFO        = _IO(P2_IOC_MAGIC, 44),
  P2_COMMAND_DAU              = _IO(P2_IOC_MAGIC, 45),
  P2_COMMAND_SET_DPARAM       = _IO(P2_IOC_MAGIC, 46),
  P2_COMMAND_GET_PHS          = _IO(P2_IOC_MAGIC, 47),
  P2_SET_AUT_STATUS           = _IO(P2_IOC_MAGIC, 48),
  P2_CLEAR_CARD_STATUS        = _IO(P2_IOC_MAGIC, 49),
  P2_PREPROCESS_IO_RETRY      = _IO(P2_IOC_MAGIC, 50),

  P2U_IOC_MAGIC               = 'u',
  P2UIOC_CHECK_POWER_MODE     = _IO(P2U_IOC_MAGIC,  1),
  P2UIOC_IDENTIFY_DEVICE      = _IO(P2U_IOC_MAGIC,  2),
  P2UIOC_READ_MASTER_DMA      = _IO(P2U_IOC_MAGIC,  3),
  P2UIOC_WRITE_MASTER_DMA     = _IO(P2U_IOC_MAGIC,  4),
  P2UIOC_GET_STATUS           = _IO(P2U_IOC_MAGIC,  8),
  P2UIOC_LOG_SENSE            = _IO(P2U_IOC_MAGIC,  9),
  P2UIOC_LOG_WRITE            = _IO(P2U_IOC_MAGIC, 10),
  P2UIOC_BLK_ERASE            = _IO(P2U_IOC_MAGIC, 11),
  P2UIOC_SEC_ERASE            = _IO(P2U_IOC_MAGIC, 12),
  P2UIOC_FW_UPDATE            = _IO(P2U_IOC_MAGIC, 13),
};


typedef struct _spd_scatterlist_t {
  unsigned int bus_address;
  unsigned int count;
} spd_scatterlist_t;


typedef unsigned char p2_slot_image;
struct p2_card_status {
  p2_slot_image open_request;    /* mount request */
  p2_slot_image release_request; /* umount request */
  p2_slot_image slot_image;      /* card inserted or not */
  p2_slot_image open_image;      /* card mounted or not */
};


struct p2_dma_status {
  p2_slot_image write;
  p2_slot_image read;
};


struct p2_log_sense_arg {
  int page;
  unsigned long buffer;
};


struct p2_blk_erase_arg {
  unsigned long sector;
  unsigned char count;
};


struct p2_direct_arg {
  unsigned long sector;
  int count;
  void *sg_table;
  int sg_table_size;
};


struct p2_sg_table {
  unsigned long addr;
  unsigned long count;
}; /* never use!? */


struct p2_err_status {
  p2_slot_image carderr;
};


struct p2_errno {
  int no;
};


struct p2_params {
  unsigned char  p2_version;
  unsigned long  p2_sys_start;
  unsigned long  p2_sys_sectors;
  unsigned long  p2_protect_start;
  unsigned long  p2_protect_sectors;
  unsigned long  p2_AU_sectors;
  unsigned long  p2_sys_RU_sectors;
  unsigned long  p2_user_RU_sectors;
  unsigned short p2_application_flag;
};


struct SET_ATA_REG {
  __u8 Feature;    /* Feature       */
  __u8 SecCnt;     /* Sector Count  */
  __u8 SecNum;     /* Sector Number */
  __u8 CylLow;     /* Cylinder Low  */
  __u8 CylHigh;    /* Cylinder High */
  __u8 HeadNum;    /* Head Number   */
  __u8 Command;    /* Command       */
};


struct P2_SET_DATA {
  __u32 Tag;                        /* Serial Number    */
  __u32 TransferAddress;            /* Transfer Address */
  struct SET_ATA_REG cbSetATAReg;   /* ATA Command      */
};


struct GET_ATA_REG {
  __u8 Error;      /* Error         */
  __u8 SecCnt;     /* Sector Count  */
  __u8 SecNum;     /* Sector Number */
  __u8 CylLow;     /* Cylinder Low  */
  __u8 CylHigh;    /* Cylinder High */
  __u8 HeadNum;    /* Head Number   */
  __u8 Status;     /* Status        */
};


struct P2_GET_DATA {
  __u32 Tag;                        /* Serial Number    */
  __u32 TransferLength;             /* Transfer Length  */
  struct GET_ATA_REG cbGetATAReg;   /* ATA Command      */
};


enum SPD_SDCMD_ENUM {
  P2_SDCMD_CREATE_DIR = 0,
  P2_SDCMD_UPDATE_CI  = 1,
};


struct p2_directw_entry {
  unsigned long sector;
  int count;
  unsigned int sg_bus_address;
  unsigned int sg_entry_size;
};


struct p2_directw_list {
  int num;
  int pos;
  struct p2_directw_entry *entry;
};


struct p2_sdcmd_w_arg {
  unsigned char cmd; /* SPD_SDCMD_ENUM */
  unsigned char id;
  struct p2_direct_arg *arg;
};


struct p2_sdcmd_d_arg {
  unsigned long sd_arg;
  char *buf;
};


#endif /* _SPD_IOCTL_H */
