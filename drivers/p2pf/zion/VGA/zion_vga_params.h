/*
 * drivers/spd/zion/VGA/zion_vga.h -- ZION VGA frame buffer driver
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 * 
 * This Driver comes from MSC VGA Driver made by Y.Takano
 * Modification for ZION is made by S.Horita (horita.seiji001@jp.panasonic.com)
 */
#ifndef _ZION_VGA_PARAMS_H_
#define _ZION_VGA_PARAMS_H_

#define ZIONVGA_DMA_CH                (3)
#define ZIONVGA_VGA_SETTING           (0x2100)
#define VGA_RSTR_EN                   (0x0100)
#define VGA_SEL                       (0x0200)
#define CSC_EN                        (0x0010)
#define SPL_SEL                       (0x0008)
#define TRS_ON                        (0x0004)
#define PAL_LINE_SEL                  (0x0001)
#define ZIONVGA_VIDEO_V_PHASE     (0x2102)
#define ZIONVGA_VIDEO_H_PHASE     (0x2104)
#define ZIONVGA_
#define ZIONVGA_BUFFER_BASE_ADDRESS_0 (0x2108)
#define ZIONVGA_BUFFER_BASE_ADDRESS_1 (0x210C)
#define ZIONVGA_SYSTEM_V_PHASE    (0x2110)
#define ZIONVGA_SYSTEM_H_PHASE    (0x2112)

#define ZIONVGA_BITS_PER_PIXEL  16
#define ZIONVGA_BYTES_PER_PIXEL (ZIONVGA_BITS_PER_PIXEL >> 3)

#endif /* _ZION_VGA_PARAMS_H_ */
