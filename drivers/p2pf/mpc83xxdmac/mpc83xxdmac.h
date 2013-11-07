/******************************************************************************
 *** FILENAME	: mpc83xxdmac.h
 *** MODULE	: MPC83xx DMA Control Driver (mpc83xxdmac)
 *** CONTENT	: Include header file
 *****************************************************************************/
/*
 *  Header files
 */
#include <linux/autoconf.h>

/*
 *  Driver information:
 *
 *    Major number: 120
 *    Driver name : mpc83xxdmac
 */
#define MPC83XXDMAC_MAJOR	120
#define	MPC83XXDMAC_NAME	"mpc83xxdmac"

#define MPC83XX_IRQ_DMA 71

#define	VERSION	"0.6"


/*
 *  MPC83xx DMA status flags
 */
enum MPC83XXDMAC_STATUS_FLAG_ENUM {
  MPC83XXDMAC_DMA_NONE    = (0),
  MPC83XXDMAC_DMA_SUCCESS = (1<<0),
  MPC83XXDMAC_DMA_ERROR   = (1<<1),
  MPC83XXDMAC_DMA_TIMEOUT = (1<<2),
};


/*
 *  MPC83xx DMAC driver parameters
 */
enum MPC83XXDMAC_DEFAULT_ENUM {
  MPC83XXDMAC_DMAC_MAX_SIZE = (16*1024*1024), /* MPC83xx DMAC max transfer size */
  MPC83XXDMAC_TIMEOUT = (3*HZ), /* MPC83xx DMAC timeout */
  MPC83XXDMA_CHNUM = 4,         /* MPC83xx DMAC channel number */
  MPC83XXDMAC_PAGE_ORDER = (CONFIG_MPC83XXDMAC_PAGE_ORDER), /* MPC83xx DMAC buffer size */
};


/*
 *  Debug out
 */
//#define DEBUG_MODE

#ifdef DEBUG_MODE
#define DbgPrint( _x_ ) printk _x_ 
#else
#define DbgPrint( _x_ )
#endif

/*
 * MPC83XX DMA/Messaging Unit Registers
 */

#define MPC83XX_DMAUNIT_START   0x8000
#define MPC83XX_DMAUNIT_SIZE    0x0300

#define OMISR        0x0030 // Outbound message interrupt status register
#define OMIMR        0x0034 // Outbound message interrupt mask register
#define IMR0         0x0050 // Inbound message register
#define IMR1         0x0054 // Inbound message register 1
#define OMR0         0x0058 // Outbound message register 0
#define OMR1         0x005C // Outbound message register 1
#define ODR          0x0060 // Outbound doorbell register
#define IDR          0x0068 // Inbound doorbell register
#define IMISR        0x0080 // Inbound message interrupt status register
#define IMIMR        0x0084 // Inbound message interrupt mask register
#define DMAMR0       0x0100 // DMA 0 mode register
#define DMASR0       0x0104 // DMA 0 status register
#define DMACDAR0     0x0108 // DMA 0 current descriptor address register
#define DMASAR0      0x0110 // DMA 0 source address register
#define DMADAR0      0x0118 // DMA 0 destination address register
#define DMABCR0      0x0120 // DMA 0 byte count register
#define DMANDAR0     0x0124 // DMA 0 next descriptor address register
#define DMAMR1       0x0180 // DMA 1 mode register
#define DMASR1       0x0184 // DMA 1 status register
#define DMACDAR1     0x0188 // DMA 1 current descriptor address register
#define DMASAR1      0x0190 // DMA 1 source address register
#define DMADAR1      0x0198 // DMA 1 destination address register
#define DMABCR1      0x01A0 // DMA 1 byte count register
#define DMANDAR1     0x01A4 // DMA 1 next descriptor address register
#define DMAMR2       0x0200 // DMA 2 mode register
#define DMASR2       0x0204 // DMA 2 status register
#define DMACDAR2     0x0208 // DMA 2 current descriptor address register
#define DMASAR2      0x0210 // DMA 2 source address register
#define DMADAR2      0x0218 // DMA 2 destination address register
#define DMABCR2      0x0220 // DMA 2 byte count register
#define DMANDAR2     0x0224 // DMA 2 next descriptor address register
#define DMAMR3       0x0280 // DMA 3 mode register
#define DMASR3       0x0284 // DMA 3 status register
#define DMACDAR3     0x0288 // DMA 3 current descriptor address register
#define DMASAR3      0x0290 // DMA 3 source address register
#define DMADAR3      0x0298 // DMA 3 destination address register
#define DMABCR3      0x02A0 // DMA 3 byte count register
#define DMANDAR3     0x02A4 // DMA 3 next descriptor address register
#define DMAGSR       0x02A8 // DMA general status register

/* DMA Mode Register bits */
#define DMAMR_DMSEN         0x00100000
#define DMAMR_EOTIE_INT     0x00000080
#define DMAMR_TEM_ERROR     0x00000000
#define DMAMR_TEM_NOERROR   0x00000008
#define DMAMR_CTM_DIRECT    0x00000004
#define DMAMR_CTM_CHAINING  0x00000000
#define DMAMR_CC            0x00000000
#define DMAMR_CS            0x00000001

/* DMA Status Register bits */
#define DMASR_TE            0x00000080
#define DMASR_CB            0x00000004
#define DMASR_EOSI          0x00000002
#define DMASR_EOCDI         0x00000001

/* DMA Current Descriptor Address Register bits [Chain mode] */
#define DMACDAR_SNEN        0x00000010
#define DMACDAR_EOSI_EN     0x00000008
#define DMACDAR_EOSI_DIS    0x00000000


/* DMA Mode Register */
unsigned int MPC83xxDmaModeReg[MPC83XXDMA_CHNUM] = {
  DMAMR0,
  DMAMR1,
  DMAMR2,
  DMAMR3
};

/* DMA Status Register */
unsigned int MPC83xxDmaStatusReg[MPC83XXDMA_CHNUM] = {
  DMASR0,
  DMASR1,
  DMASR2,
  DMASR3
};

/* DMA Source Address Register */
unsigned int MPC83xxDmaSourceAddressReg[MPC83XXDMA_CHNUM] = {
  DMASAR0,
  DMASAR1,
  DMASAR2,
  DMASAR3
};

/* DMA Destination Address Register */
unsigned int MPC83xxDmaDestinationAddressReg[MPC83XXDMA_CHNUM] = {
  DMADAR0,
  DMADAR1,
  DMADAR2,
  DMADAR3
};

/* DMA Byte Count Register */
unsigned int MPC83xxDmaCountReg[MPC83XXDMA_CHNUM] = {
  DMABCR0,
  DMABCR1,
  DMABCR2,
  DMABCR3
};

/* DMA Current Descriptor Address Register [Chain mode] */
unsigned int MPC83xxDmaCurrentDescAddressReg[MPC83XXDMA_CHNUM] = {
  DMACDAR0,
  DMACDAR1,
  DMACDAR2,
  DMACDAR3
};

