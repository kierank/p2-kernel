/*****
Register Map of ZION (General)
*****/

#ifndef __ZION_COMMON_REGS_H__
#define __ZION_COMMON_REGS_H__

/*** PCI Regs. ***/
#define NEO_PCI_COMMAND                   (0x04)  /*0x04-0x05*/
#define NEO_MEMORY_SPACE_ENABLE           (((u16)1)<<1)

#define NEO_PCI_INTERRUPT_STATUS          (0x40)  /*0x40-0x41*/
#define NEO_DMA_DONE(ch)                  (((u16)1)<<(ch))
#define NEO_DMA_DONE_MASK                 (0x00ff)
#define NEO_BACKEND_INT_REQ               (((u16)1)<<10)

#define NEO_PCI_INTERRUPT_CONTROL         (0x47)  /*8bit*/
#define NEO_BACKEND_INT_ENABLE            (((u8)1)<<3)
#define NEO_DMA_INT_ENABLE                (((u8)1)<<0)

#define NEO_MBUS_ERROR_INT_ENABLE            (((u8)1)<<4)

/*** MBUS Regs. ***/
#define NEO_Interrupt_Clear_A    (0x0010)
#define NEO_Interrupt_Status_A   (0x0012)
#define NEO_Interrupt_Mask_A     (0x0014)
#define NEO_Interrupt_Gen_A      (0x0016)

#define NEO_Interrupt_Clear_B    (0x0018)
#define NEO_Interrupt_Status_B   (0x001A)
#define NEO_Interrupt_Mask_B     (0x001C)
#define NEO_Interrupt_Gen_B      (0x001E)

#endif /* __ZION_COMMON_REGS_H__ */
