#ifndef __ZION_DVCIF_REGS_H__
#define __ZION_DVCIF_REGS_H__

#define DV_Interrupt_Clear   (0x2010)
#define DV_Interrupt_Status  (0x2012)
#define DV_Interrupt_Mask    (0x2014)

#define PbFrame              ((u16)1)
#define PbSect               (((u16)1)<<1)
#define PbInt0               (((u16)1)<<2)
#define PbInt1               (((u16)1)<<3)
#define PbEmpty              (((u16)1)<<4)
#define PbAlmostEmpty        (((u16)1)<<5)
#define PbAlmostFull         (((u16)1)<<6)
#define PbFull               (((u16)1)<<7)
#define RecFrame             (((u16)1)<<8)
#define RecSect              (((u16)1)<<9)
#define RecInt0              (((u16)1)<<10)
#define RecInt1              (((u16)1)<<11)
#define RecEmpty             (((u16)1)<<12)
#define RecAlmostEmpty       (((u16)1)<<13)
#define RecAlmostFull        (((u16)1)<<14)
#define RecFull              (((u16)1)<<15)

#endif /* __ZION_DVCIF_REGS_H__ */
