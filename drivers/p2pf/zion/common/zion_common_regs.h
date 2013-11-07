#ifndef __ZION_COMMON_REGS__
#define __ZION_COMMON_REGS__

/* M-Bus Register Addresses in Common Part */

#define NEO_VERSION              (0x0000)
#define NEO_Mode                 (0x0002)
#define NEO_OnChipBus_Module     (0x0008)

#define NEO_ClockEnb(x)  ((0x0008)+(x)*2)

#define WorkRam_Reserve          (0x0020)
#define WorkRam_Control          (0x0022)
#define WorkRam_Length           (0x0024)
#define WorkRam_Draw_Data        (0x0026)
#define WorkRam_Src_Address      (0x0028)
#define WorkRam_Dest_Address     (0x002A)

#define SDRAM_Base_Address       (0x002C)  /* Dword */
#define SDRAM_Address            (0x0030)  /* Dword */

#define PlaneAdr                 (0x0034)

#define NEO_IO_Select            (0x003C)  /* Dword */

#define Event_Flag_Set_A         (0x0040)
#define Event_Flag_Clear_A       (0x0042)
#define Event_Flag_Set_B         (0x0044)
#define Event_Flag_Clear_B       (0x0046)
#define Event_Flag_Set_C         (0x0048)
#define Event_Flag_Clear_C       (0x004A)
#define Event_Flag_Set_D         (0x004C)
#define Event_Flag_Clear_D       (0x004E)

#define NEO_Configuration_Status (0x003C)

#endif  /* __ZION_COMMON_REGS__ */
