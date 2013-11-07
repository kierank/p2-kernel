#ifndef __ZION_AUDIO_PROC_REGS_H__
#define __ZION_AUDIO_PROC_REGS_H__

#define DMA12_Interrupt_Clear   (0x2510)
#define DMA12_Interrupt_Status  (0x2512)
#define DMA12_Interrupt_Mask    (0x2514)
#define DMA34_Interrupt_Clear   (0x2518)
#define DMA34_Interrupt_Status  (0x251A)
#define DMA34_Interrupt_Mask    (0x251C)

#define Dma1FrameStp              ((u16)1)
#define Dma1IntDmaDone       (((u16)1)<<1)
#define AudPbFrmAFall1       (((u16)1)<<2)
#define AudPbFrmBFall1       (((u16)1)<<3)
#define Dma1Empty            (((u16)1)<<4)
#define Dma1AlmostEmpt       (((u16)1)<<5)
#define Dma1AlmostFull       (((u16)1)<<6)
#define Dma1Full             (((u16)1)<<7)
#define Dma2FrameStp         (((u16)1)<<8)
#define Dma2IntDmaDone       (((u16)1)<<9)
#define AudPbFrmAFall2       (((u16)1)<<10)
#define AudPbFrmBFall2       (((u16)1)<<11)
#define Dma2Empty            (((u16)1)<<12)
#define Dma2AlmostEmpty      (((u16)1)<<13)
#define Dma2AlmostFull       (((u16)1)<<14)
#define Dma2Full             (((u16)1)<<15)

#endif /* __ZION_AUDIO_PROC_IF_REGS_H__ */
