#ifndef __ZION_MATRIX_REGS_H__
#define __ZION_MATRIX_REGS_H__

#define MATRIX_MPU_Control(x)      ((0x0A00)+((x)*(0x40)))
#define MATRIX_MPU_BootAdr(x)      ((0x0A04)+((x)*(0x40)))
#define MATRIX_MPU_IPAdr(x)        ((0x0A06)+((x)*(0x40)))
#define MATRIX_MPU_SPAdr(x)        ((0x0A08)+((x)*(0x40)))
#define MATRIX_MPU_Load_Config(x)  ((0x0A0C)+((x)*(0x40)))
#define MATRIX_Interrupt(x)        ((0x0A0E)+((x)*(0x40)))
#define MATRIX_IntStatus_Mask      (0x0f00)
#define MATRIX_IntMask_Mask        (0x00f0)
#define MATRIX_IntReset_Mask       (0x000f)
#define MATRIX_Int1Sel(x)          ((0x0A10)+((x)*(0x40)))
#define MATRIX_Int2Sel(x)          ((0x0A18)+((x)*(0x40)))

#endif /* __ZION_MATRIX_REGS_H__ */
