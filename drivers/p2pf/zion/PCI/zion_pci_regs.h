/*****
Register Map of ZION (PCI)
*****/

#ifndef __ZION_PCI_REGS_H__
#define __ZION_PCI_REGS_H__

/********** PCI Config Registers **********/
#define NEO_PCI_COMMAND                   (0x04)  /*0x04-0x05*/
#define NEO_BUS_MASTER_ENABLE             (((u16)1)<<2)

#define NEO_PCI_STATUS                    (0x06)  /*0x06-0x07*/

#define NEO_PCI_LATENCY_TIMER             (0x0d)  /*0x0d (8bit)*/

#define NEO_PCI_INTERRUPT_STATUS          (0x40)  /*0x40-0x41*/
#define NEO_DMA_DONE(ch)                  (((u16)1)<<(ch))
#define NEO_BACKEND_INT_REQ               (((u16)1)<<10)

#define NEO_PCI_BURST_LENGTH              (0x44)  /*8bit*/

#define NEO_PCI_SYSTEM_CONTROL            (0x45)  /*0x45-0x46*/
#define NEO_FIFO_RESET                    (((u16)1)<<9)
#define NEO_SYSTEM_RESET                  (((u16)1)<<8)

#define NEO_PCI_SDRAM_BASE_ADDRESS        (0x58)  /*0x58-0x59 (11bit)*/
#define SDRAM_BASE_GRADUATION             ((1L)<<16)  /* for convenience */

#define NEO_PCI_DMA_ADDRESS_COUNTER       (0x5c)  /*0x5c-0x5f*/

#define NEO_PCI_DMA_COMMAND(ch)            ((0x60)+(ch)*4)  /*0x60-0x7d (16bit each)*/
#define NEO_DMA_RUN                        (((u16)1)<<1)
#define NEO_DMA_OPEN                       (((u16)1)<<0)
#define NEO_IO_DERECTION_READ              ((u16)0)
#define NEO_IO_DERECTION_WRITE             (((u16)1)<<2)

#define NEO_PCI_DMA_CHAIN_ADDRESS(ch)      ((0x80)+(ch)*4)  /*0x80-0x9f (32bit each)*/

#define NEO_PCI_DMA_BUFFER_RW_POINTER(ch) ((0xa0)+(ch)*4)  /*0xa0-0xbf (32bit each)*/

/********** MBUS Regs. **********/

#define NEO_MBUS_BUFFER_LOWER_ADDRESS(ch) ((0x0100)+(ch)*4)
#define NEO_MBUS_BUFFER_UPPER_ADDRESS(ch) ((0x0120)+(ch)*4)

#define NEO_MBUS_MEM_ENABLE               (0x150)
#define NEO_MBUS_MEMORY_ENABLE            (0x0100)

#define NEO_MBUS_TARGET_CONTROL           (0x154)
#define NEO_WRITE_LINE_SIZE_MASK          (0x0700)
#define NEO_WRITE_LINE_SIZE_8DW           (0x0300)
#define NEO_WRITE_LINE_SIZE_32DW          (0x0500)
#define NEO_WRITE_LINE_SIZE_128DW         (0x0700)
#define NEO_READ_LINE_SIZE_MASK           (0x0003)
#define NEO_READ_LINE_SIZE_32DW           (0x0000)
#define NEO_READ_LINE_SIZE_64DW           (0x0001)
#define NEO_READ_LINE_SIZE_128DW          (0x0002)
#define NEO_READ_LINE_SIZE_16DW           (0x0003)

#define NEO_MBUS_CACHE_CONTROL            (0x156)
#define NEO_LINE_RST                      (0x0100)
#define NEO_CACHE_CLR                     (0x0001)

#endif /* __ZION_PCI_REGS_H__ */
