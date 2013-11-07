#ifndef __ZION_DMAIF_REGS_H__
#define  __ZION_DMAIF_REGS_H__

/** register map **/

/* Backend interrupt control */
#define ZION_MBUS_IOSEL		(0x003C)
#define ZION_MBUS_INTMASK_B	(0x001C)
#define ZION_MBUS_INTGEN_B	(0x001E)

#define ZION_MBUS_INTCLR_A	(0x0010)
#define ZION_MBUS_INTSTTS_A	(0x0012)
#define ZION_MBUS_INTMASK_A	(0x0014)
#define ZION_MBUS_INTGEN_A	(0x0016)
#define ZION_MBUS_INTCLR_B	(0x0018)
#define ZION_MBUS_INTSTTS_B	(0x001A)
#define ZION_MBUS_INTMASK_B	(0x001C)
#define ZION_MBUS_INTGEN_B	(0x001E)

/* DMA I/F register map */
#define ZION_MBUS_EDMA_MODE( ch )	( (0x0200) + (ch)*0x80 )
#define ZION_MBUS_EDMA_DMACMD( ch )	( (0x0202) + (ch)*0x80 )
#define ZION_MBUS_EDMA_INTCLR( ch )				( (0x0204) + (ch)*0x80 )
#define ZION_MBUS_EDMA_INTSTTS( ch )				( (0x0206) + (ch)*0x80 )
#define ZION_MBUS_EDMA_INTENB( ch )				( (0x0208) + (ch)*0x80 )
#define ZION_MBUS_EDMA_PTRCTRL( ch )				( (0x0240) + (ch)*0x80 )
#define ZION_MBUS_EDMA_BUFF_LWR_ADRS( ch, no )	( (0x0244) + (ch)*0x80 + (no)*0x02 )
#define ZION_MBUS_EDMA_BUFF_UPR_ADRS( ch, no )	( (0x024C) + (ch)*0x80 + (no)*0x04 )
#define ZION_MBUS_EDMA_CURRBUFFPTR( ch, no )	( (0x025C) + (ch)*0x80 +(no)*0x04)
#define ZION_MBUS_EDMA_CURRPMT( ch )	( (0x023E) + (ch)*0x80 )


/* for DEBUG */
/* #ifdef NEO_DEBUG */
#define ZION_MBUS_EDMA_EDMA_STATE( ch )		( (0x020C) + (ch)*0x80 )
#define ZION_MBUS_EDMA_INT_SBUS_STATE( ch )	( (0x020E) + (ch)*0x80 )
#define ZION_MBUS_EDMA_EXT_MODE( ch )		( (0x020A) + (ch)*0x80 )
/* #endif */ /* NEO_DEBUG */

/** Macro definitions **/

/* Backend interrupt control */
#define ZION_MBUS_DMASEL_ENB	(0xFF00)
#define ZION_MBUS_INTB_DMAENB	( (unsigned short)(1 << 10) )
#define ZION_MBUS_INTGB_MPU1ENB	( (unsigned short)(1 << 0) )

#define ZION_MBUS_DMAINT		( (unsigned short)(1 << 10) )
#define ZION_MBUS_ALLINTCLR	(0x0000)

/* DMA commands */
#define ZION_MBUS_EDMA_OPEN		( (unsigned char)(1 << 0) )
#define ZION_MBUS_EDMA_CLOSE		(~ZION_MBUS_EDMA_OPEN)
#define ZION_MBUS_EDMA_WRITE		( (unsigned char)(1 << 2) )
#define ZION_MBUS_EDMA_READ		(~ZION_MBUS_EDMA_WRITE)
#define ZION_MBUS_EDMA_RUN		( (unsigned char)(1 << 1) )
#define ZION_MBUS_EDMA_STOP		(~ZION_MBUS_EDMA_RUN)

/* Interrupt masks */
#define ZION_MBUS_EDMA_DMADONEINT	( (unsigned short)(1 <<  0) )
#define ZION_MBUS_EDMA_SYNCFRMINT	( (unsigned short)(1 <<  2) )
#define ZION_MBUS_EDMA_EMPINT		( (unsigned short)(1 <<  4) )
#define ZION_MBUS_EDMA_AMSTEMPINT	( (unsigned short)(1 <<  5) )
#define ZION_MBUS_EDMA_AMSTFULLINT	( (unsigned short)(1 <<  6) )
#define ZION_MBUS_EDMA_FULLINT		( (unsigned short)(1 <<  7) )
#define ZION_MBUS_EDMA_DIFFCNTINT	( (unsigned short)(1 <<  8) )
#define ZION_MBUS_EDMA_UNDRFLWINT	( (unsigned short)(1 << 12) )
#define ZION_MBUS_EDMA_OVERFLWINT	( (unsigned short)(1 << 15) )
#define ZION_MBUS_EDMA_BUFFINT_MASK	(ZION_MBUS_EDMA_EMPINT      | \
                                     ZION_MBUS_EDMA_AMSTEMPINT  | \
									 ZION_MBUS_EDMA_AMSTFULLINT | \
									 ZION_MBUS_EDMA_FULLINT)
#define ZION_MBUS_EDMA_ERRINT_MASK	(ZION_MBUS_EDMA_DIFFCNTINT | \
                                     ZION_MBUS_EDMA_UNDRFLWINT | \
									 ZION_MBUS_EDMA_OVERFLWINT)



/**************** ZION device file(minor number) description ****************/

/*           <minor>         :   <dev name>  :       <description>           */
/*  ZION_DMAIF_PORT_OFFSET+ 0: zion_dmaif2frm: EDMA CH0 common port          */
/*  ZION_DMAIF_PORT_OFFSET+ 1: zion_dmaif0buf: EDMA CH0 buffer watching port */
/*  ZION_DMAIF_PORT_OFFSET+ 2: zion_dmaif0dma: EDMA CH0 DMA port             */
/*  ZION_DMAIF_PORT_OFFSET+ 3: zion_dmaif0frm: EDMA CH0 frame pulse port     */
/*  ZION_DMAIF_PORT_OFFSET+ 4: zion_dmaif2frm: EDMA CH1 common port          */
/*  ZION_DMAIF_PORT_OFFSET+ 5: zion_dmaif1buf: EDMA CH1 buffer watching port */
/*  ZION_DMAIF_PORT_OFFSET+ 6: zion_dmaif1dma: EDMA CH1 DMA port             */
/*  ZION_DMAIF_PORT_OFFSET+ 7: zion_dmaif1frm: EDMA CH1 frame pulse port     */
/*  ZION_DMAIF_PORT_OFFSET+ 8: zion_dmaif2frm: EDMA CH2 common port          */
/*  ZION_DMAIF_PORT_OFFSET+ 9: zion_dmaif2buf: EDMA CH2 buffer watching port */
/*  ZION_DMAIF_PORT_OFFSET+10: zion_dmaif2dma: EDMA CH2 DMA port             */
/*  ZION_DMAIF_PORT_OFFSET+11: zion_dmaif2frm: EDMA CH2 frame pulse port     */

#define ZION_EDMA_CMN_PORTNO( ch )	( (ch)*4 + 0 )
#define ZION_EDMA_BUF_PORTNO( ch )	( (ch)*4 + 1 )
#define ZION_EDMA_DMA_PORTNO( ch )	( (ch)*4 + 2 )
#define ZION_EDMA_FRM_PORTNO( ch )	( (ch)*4 + 3 )


#endif /* __ZION_DMAIF_REGS_H__ */
