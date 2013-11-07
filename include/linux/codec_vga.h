/*
 *  linux/codec_vga.h
 *  $Id: codec_vga.h 5901 2010-03-24 23:56:01Z Noguchi Isao $
 */
#ifndef __LINUX_CODEC_VGA_H__
#define __LINUX_CODEC_VGA_H__

#include <asm/types.h>
#include <asm/page.h>
#include <linux/ioctl.h>
#include <linux/fb.h>

#ifndef __KERNEL__
#include <linux-include/linux/autoconf.h>
#endif  /* ! __KERNEL__ */

/**
 *  
 */

#ifdef CONFIG_CODEC_VGA_LFB_NUM
#define CVGA_FB_NUM         CONFIG_CODEC_VGA_LFB_NUM
#else  /* CONFIG_CODEC_VGA_LFB_NUM */
#define CVGA_FB_NUM         2
#endif  /* CONFIG_CODEC_VGA_LFB_NUM */

#ifdef CONFIG_CODEC_VGA_BUFF_NUM
#define CVGA_PLANE_NUM  CONFIG_CODEC_VGA_BUFF_NUM
#else  /* CONFIG_CODEC_VGA_BUFF_NUM */
#define CVGA_PLANE_NUM  32
#endif  /* CVGA_VGA_PLANE_NUN */
#define CVGA_VPAIR_NUM  (CVGA_PLANE_NUM/2)

#define CVGA_VRAM_SIZE      (1<<20) /* 1MB */
#define CVGA_VGA_SIZE       (CVGA_VRAM_SIZE * CVGA_PLANE_NUM)

/* resolusion */
#define CVGA_X_RES  800
#define CVGA_Y_RES  480

#define CVGA_BITS_PER_PIXEL  16
#define CVGA_BYTES_PER_PIXEL (CVGA_BITS_PER_PIXEL >> 3)

#define CVGA_SCREEN_SIZE (CVGA_X_RES*CVGA_Y_RES*CVGA_BYTES_PER_PIXEL)


#define CVGA_SPRITE_NUM     4
#define CVGA_SPRITE_BUFF_SIZE   0x1000
#define CVGA_SPRITE_MAX_PIXS    (CVGA_SPRITE_BUFF_SIZE/(CVGA_BITS_PER_PIXEL/8))


/**
 * ioctl(CVGA_IOC_FB_SETMODE
 * ioctl(CVGA_IOC_FB_GETMODE)
 */
struct cvga_ioc_fb_mode {
    int  switchable;
    int  cyclic;
    __u32  vpair;
    unsigned long  interval;
    unsigned long  remain;
}; 

/**
 *  ioctl(CVGA_IOC_VGA_PHASE)
 */
struct cvga_ioc_vga_phase{
    __u16  h_phase,v_phase;
}; 

/**
 *  ioctl(CVGA_IOC_VGA_GETINFO)
 */
struct cvga_ioc_vga_info{
    int  muteoff;
    int  rstron;
    int  blink;
    __u32  out_vpair;
    __u32  state_vpairs;
    __u16  h_phase,v_phase;
    int trans_mode;
};

/**
 *  ioctl(CVGA_IOC_SPRITE_SETPOS)
 *  ioctl(CVGA_IOC_SPRITE_GETPOS)
 */
struct cvga_ioc_sprite_pos {
    int no;
    int  visible;
    __u32  xpos, ypos;
}; 

/**
 *  ioctl(CVGA_IOC_SPRITE_SETDATA)
 *  ioctl(CVGA_IOC_SPRITE_GETDATA)
 */
struct cvga_ioc_sprite_data {
    int no;
    __u32  xsize, ysize;
    __u16  sp_data[CVGA_SPRITE_MAX_PIXS];
    __u32  sp_cnt;
}; 



/**
 *  ioctl commands
 */

#define CVGA_IOC_MAGIC 'x'

#define MASK_CVGA_IOC_TYPE      0xF0

#define CVGA_IOC_FB_PURGE       _IO(CVGA_IOC_MAGIC, 0x00)
#define CVGA_IOC_FB_EGRUP       _IO(CVGA_IOC_MAGIC, 0x01)
#define CVGA_IOC_FB_SETMODE     _IOW(CVGA_IOC_MAGIC, 0x02, struct cvga_ioc_fb_mode)
#define CVGA_IOC_FB_GETMODE     _IOR(CVGA_IOC_MAGIC, 0x03, struct cvga_ioc_fb_mode)
#define CVGA_IOC_VGA_SWITCH     _IO(CVGA_IOC_MAGIC, 0x10)
#define CVGA_IOC_VGA_CHANGE     _IO(CVGA_IOC_MAGIC, 0x11)
#define CVGA_IOC_VGA_MUTE       _IO(CVGA_IOC_MAGIC, 0x12)
#define CVGA_IOC_VGA_RSTR       _IO(CVGA_IOC_MAGIC, 0x13)
#define CVGA_IOC_VGA_PHASE      _IOW(CVGA_IOC_MAGIC, 0x14, struct cvga_ioc_vga_phase)
#define CVGA_IOC_VGA_GETINFO    _IOR(CVGA_IOC_MAGIC, 0x15, struct cvga_ioc_vga_info)
#define CVGA_IOC_VGA_TRANSMODE  _IO(CVGA_IOC_MAGIC, 0x16)
#define CVGA_IOC_VGA_BLINK      _IO(CVGA_IOC_MAGIC, 0x17)
#define CVGA_IOC_SPRITE_SETPOS  _IOW(CVGA_IOC_MAGIC, 0x20, struct cvga_ioc_sprite_pos)
#define CVGA_IOC_SPRITE_GETPOS  _IOWR(CVGA_IOC_MAGIC, 0x21, struct cvga_ioc_sprite_pos)
#define CVGA_IOC_SPRITE_SETDATA _IOW(CVGA_IOC_MAGIC, 0x22, struct cvga_ioc_sprite_data)
#define CVGA_IOC_SPRITE_GETDATA _IOWR(CVGA_IOC_MAGIC, 0x23, struct cvga_ioc_sprite_data)





#endif /* __LINUX_CODEC_VGA_H__ */
