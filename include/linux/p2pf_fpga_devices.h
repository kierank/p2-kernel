/*
 * include/linux/p2pf_fpga_devices.h
 *
 * Definitions for any platform device related flags or structures for
 * FPGA devices of P2 platform
 *
 */
/* $Id: p2pf_fpga_devices.h 6388 2010-04-14 06:20:30Z Noguchi Isao $ */

#ifndef __LINUX_P2PF_FPGA_DEVICES_H__
#define __LINUX_P2PF_FPGA_DEVICES_H__

#include <linux/types.h>
#include <linux/ioport.h>

struct codecvga_platform_data {
    int sprite_num;             /* number of sprite */
    int dma_chan;               /* DMAC channel */
};


struct p2msu_wdt_platform_data {
    u32 ticks;                /* Watchdog timeout in ticks */
    u32 period;                 /* Period in ticks [msec] */
};


#endif /* __LINUX_P2PF_FPGA_DEVICES_H__ */
