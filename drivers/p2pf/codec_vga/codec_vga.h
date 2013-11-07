/*
 *  include/asm/codec_fpga.h
 *  --- H/W definitions for CODEC-FPGA
 *  $Id: codec_vga.h 5901 2010-03-24 23:56:01Z Noguchi Isao $
 */

#ifndef __CODEC_VGA_H__
#define __CODEC_VGA_H__

#include <linux/types.h>


/*
 * VGA Registers Definitions
 */

/* VGA_CONTROL_REG(VCR) */
#define CODEC_VGA_VCR_OFFSET     (0x0000)
#define CODEC_VGA_VCR_BIT_VMUTEOFF     (1<<15)
#define CODEC_VGA_VCR_BIT_FRP_SEL      (1<<14)
#define CODEC_VGA_VCR_BIT_RSTR         (1<<13)
#define CODEC_VGA_VCR_BIT_BLINK        (1<<12)
#define CODEC_VGA_VCR_SDRRST           (1<<8)
#define CODEC_VGA_VCR_FLEN_VGA_SEL     5
#define CODEC_VGA_VCR_MASK_VGA_SEL     ((1<<CODEC_VGA_VCR_FLEN_VGA_SEL)-1)
#define CODEC_VGA_VCR_SHIFT_VGA_SEL    0

/* SPRITE_CONTROL_REG(SPCR) */
#define CODEC_VGA_SPCR_OFFSET    (0x0002)
#define CODEC_VGA_SPCR_BIT_SPR_EN(n)   ((1<<(n))&0x000F)

/* SPRITE_COODINATE(SPC) n=0..3 !! 32bit !! */
#define CODEC_VGA_SPC_OFFSET(n) (0x0004 + (n)*4)
#define CODEC_VGA_SPC_FLEN_SPCX     10
#define CODEC_VGA_SPC_MASK_SPCX    ((1<<CODEC_VGA_SPC_FLEN_SPCX)-1)
#define CODEC_VGA_SPC_SHIFT_SPCX    16
#define CODEC_VGA_SPC_FLEN_SPCY     9
#define CODEC_VGA_SPC_MASK_SPCY    ((1<<CODEC_VGA_SPC_FLEN_SPCY)-1)
#define CODEC_VGA_SPC_SHIFT_SPCY    0

/* SPRITE_SIZE_REG(SPSR)  n=0..3 */
#define CODEC_VGA_SPSR_OFFSET(n) (0x0014 + (n)*2)
#define CODEC_VGA_SPSR_FLEN_SPSY   5
#define CODEC_VGA_SPSR_MASK_SPSY   ((1<<CODEC_VGA_SPSR_FLEN_SPSY)-1)
#define CODEC_VGA_SPSR_SHIFT_SPSY  8
#define CODEC_VGA_SPSR_FLEN_SPSX   5
#define CODEC_VGA_SPSR_MASK_SPSX   ((1<<CODEC_VGA_SPSR_FLEN_SPSX)-1)
#define CODEC_VGA_SPSR_SHIFT_SPSX  0



#endif  /* __CODEC_VGA_H__ */
