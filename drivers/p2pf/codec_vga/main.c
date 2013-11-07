/*
 *  drivers/spd/codec_vga/main.c
 *
 * Copyright (C) 2008-2009 Panasonic Co.,LTD.
 */
/*  $Id: main.c 6021 2010-03-30 07:29:34Z Noguchi Isao $ */


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/ioctl.h>
#include <linux/ioport.h> /* request_mem_region, release_mem_region */
#include <linux/kthread.h>                   /* kthread_run,... */
#include <linux/sched.h>
#include <linux/byteorder/generic.h> /* cpu_to_be16, be16_to_cpu */
#include <linux/errno.h>
#include <linux/err.h>                       /* IS_ERR,PTR_ERR,... */
#include <linux/fb.h>
#include <asm/types.h>
#include <asm/uaccess.h>
#include <asm/io.h>             /* ioread*, iowrie*, memset_io */
#include <asm/system.h>         /* smp_mb */
#include <linux/semaphore.h>      /* semaphore's */
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <linux/p2pf_fpga_devices.h>
#include <linux/codec_vga.h>

#include "codec_vga.h"
#include "debug.h"

/* typedef unsigned int ui32_t; */
typedef unsigned long ui32_t;
#include <asm/dmac-ioctl.h>
extern int MPC83xxDmacDirectMode(int, ui32_t, ui32_t, ui32_t, ui32_t);

#define CODEC_VGA_VER "0.07"


/*
 *  Structure of parameters for local FB
 *
 */
struct codec_vga_fb_par{
    int devno;                  /* device number = 0,1,... */
	void* virt_addr;           // FB virtual address
    struct cvga_ioc_fb_mode mode;
};


/*
 *  Parameters for this driver
 */
struct codec_vga_param {

    /* local FB parameters */
    struct {
        struct fb_info info;
        struct codec_vga_fb_par par;
    } fb[CVGA_FB_NUM];

    /* resource of i/o mem */
    struct resource *reg_rsc;    /* CODEC-FPGA regs. */
    struct resource *spbuff_rsc[CVGA_SPRITE_NUM]; /* CODEC-FPGA sprite buffers */
    struct resource *vgabuff_rsc;                 /* CODEC-FPGA VGA buffer */

    /* virtual address mapped i/o mem */
    unsigned long reg_addr;                     /* CODEC-FPGA regs. */
    unsigned long spbuff_addr[CVGA_SPRITE_NUM]; /* CODEC-FPGA sprite buffers */
    unsigned long vgabuff_addr;                 /* CODEC-FPGA VGA buffer */

    /* phy address mapped i/o mem */
    unsigned long reg_paddr;                     /* CODEC-FPGA regs. */
    unsigned long reg_size;
    unsigned long spbuff_paddr[CVGA_SPRITE_NUM]; /* CODEC-FPGA sprite buffers */
    unsigned long vgabuff_paddr;                 /* CODEC-FPGA VGA buffer */

    /* mutex */
    struct semaphore    sem;
    struct semaphore    sem_dmac;

    /* kernel thred for cyclic dmac */
    struct task_struct *kth_cycdmac;
    int flag_kthread_exit;

    /* for VGA pair */
    __u32  out_vpair;
    __u32  state_vpairs;

    /* If this variable is not zero, use CPU not DMA to
       transfering. */
    int flag_cpu_transfer;
    wait_queue_head_t   queue_cpu;
    struct work_struct  work_cpu;
    struct t_data_work {
        int num;
        volatile unsigned long *src;
        volatile unsigned long *dst;
    } data_cpu;        

    int dma_ch;

} param;

/* driver base name */
static const char *codec_vga_name = "CODEC-VGA FB";

/* default H phase */
static const __u16 init_h_phase = 0;

/* default V phase */
static const __u16 init_v_phase = 0;

/*------------------- macro and inline functions --------------------------*/

/* Physical addressof VGA buffer plane */
#define VGA_PLANE_PHYADR(no)    ((void*)(param.vgabuff_paddr + (no)*CVGA_VRAM_SIZE))

/* Virtual addressof VGA buffer plane */
#define VGA_PLANE_ADDR(no)      ((void*)(param.vgabuff_addr + (no)*CVGA_VRAM_SIZE))

/* virtual address of CODEC-FPGA VGA registers */
#define VGA_REG_ADDR(off)   ((void*)(param.reg_addr + off))

/* 16bit read/write to CODEC-FPGA VGA registers */
#define vga_reg_read16(off) ({__u16 _r=be16_to_cpu(ioread16be(VGA_REG_ADDR(off)));smp_mb();_r;})
#define vga_reg_write16(val,off) do{iowrite16be(cpu_to_be16(val),VGA_REG_ADDR(off));smp_mb();}while(0)

/* 32bit read/write to CODEC-FPGA VGA registers */
#define vga_reg_read32(off) ({__u32 _r=be32_to_cpu(ioread32be(VGA_REG_ADDR(off)));smp_mb();_r;})
#define vga_reg_write32(val,off) do{iowrite32be(cpu_to_be32(val),VGA_REG_ADDR(off));smp_mb();}while(0)

/*------------------- chipset specific functions --------------------------*/

/* set vphase value to register */
static void set_phase(const __u16 h_phase, const __u16 v_phase)
{
/*         vga_reg_write16(val,CODEC_VGA_VVHP_OFFSET); */
/*     } */

/*     /\* V_PHASE *\/ */
/*     { */
/*         __u16 val = vga_reg_read16(CODEC_VGA_VVVP_OFFSET); */
/*         val = (val&~CODEC_VGA_VVVP_MASK_VVP) | (v_phase&CODEC_VGA_VVVP_MASK_VVP); */
/*         vga_reg_write16(val,CODEC_VGA_VVVP_OFFSET); */
/*     } */
}

/* get vphase value from register */
static void get_phase(__u16 *const p_h_phase, __u16 *const p_v_phase)
{
    /* H_PHASE */
    if(NULL!=p_h_phase){
/*         __u16 val = vga_reg_read16(CODEC_VGA_VVHP_OFFSET); */
/*         *p_h_phase = val&CODEC_VGA_VVHP_MASK_VHP; */
        *p_h_phase = 0;
    }

    /* V_PHASE */
    if(NULL!=p_v_phase){
/*         __u16 val = vga_reg_read16(CODEC_VGA_VVVP_OFFSET); */
/*         *p_v_phase = val&CODEC_VGA_VVVP_MASK_VVP; */
        *p_v_phase = 0;
    }
}

/* chage output plane */
static void chg_outplane(const __u32 plane)
{
    __u16 val = vga_reg_read16(CODEC_VGA_VCR_OFFSET);
    val = (val&~CODEC_VGA_VCR_MASK_VGA_SEL)|(plane&CODEC_VGA_VCR_MASK_VGA_SEL);
    vga_reg_write16(val,CODEC_VGA_VCR_OFFSET);
}

/* mute controll */
static void mute_ctrl(const int muteoff)
{
    __u16 val = vga_reg_read16(CODEC_VGA_VCR_OFFSET);
    if(muteoff)
        val |= CODEC_VGA_VCR_BIT_VMUTEOFF;
    else
        val &= ~CODEC_VGA_VCR_BIT_VMUTEOFF;
    vga_reg_write16(val,CODEC_VGA_VCR_OFFSET);
}

/* mute status */
static int mute_status(void)
{
    __u16 val = vga_reg_read16(CODEC_VGA_VCR_OFFSET);
    return (val&CODEC_VGA_VCR_BIT_VMUTEOFF)?1:0;
}

/* raster read control */
static void rstr_ctrl(const int rstron)
{
    __u16 val = vga_reg_read16(CODEC_VGA_VCR_OFFSET);
    if(rstron)
        val |= CODEC_VGA_VCR_BIT_RSTR;
    else
        val &= ~CODEC_VGA_VCR_BIT_RSTR;
    vga_reg_write16(val,CODEC_VGA_VCR_OFFSET);
}

/* raster status */
static int rstr_status(void)
{
    __u16 val = vga_reg_read16(CODEC_VGA_VCR_OFFSET);
    return (val&CODEC_VGA_VCR_BIT_RSTR)?1:0;
}

/* blink read control */
static void blink_ctrl(const int blink)
{
    __u16 val = vga_reg_read16(CODEC_VGA_VCR_OFFSET);
    if(blink)
        val |= CODEC_VGA_VCR_BIT_BLINK;
    else
        val &= ~CODEC_VGA_VCR_BIT_BLINK;
    vga_reg_write16(val,CODEC_VGA_VCR_OFFSET);
}

/* blink status */
static int blink_status(void)
{
    __u16 val = vga_reg_read16(CODEC_VGA_VCR_OFFSET);
    return (val&CODEC_VGA_VCR_BIT_BLINK)?1:0;
}

/* change sprite position and enable/disable visible */
static void set_sprite_pos(const struct cvga_ioc_sprite_pos *const pos)
{
    /* position */
    {
        /* read register */
        __u32 val = vga_reg_read32(CODEC_VGA_SPC_OFFSET(pos->no));

        /* x-pos */
        val &= ~(CODEC_VGA_SPC_MASK_SPCX<<CODEC_VGA_SPC_SHIFT_SPCX);
        val |= (pos->xpos&CODEC_VGA_SPC_MASK_SPCX)<<CODEC_VGA_SPC_SHIFT_SPCX;

        /* y-pos */
        val &= ~(CODEC_VGA_SPC_MASK_SPCY<<CODEC_VGA_SPC_SHIFT_SPCY);
        val |= (pos->ypos&CODEC_VGA_SPC_MASK_SPCY)<<CODEC_VGA_SPC_SHIFT_SPCY;

        /* write-back to register */
        vga_reg_write32(val,CODEC_VGA_SPC_OFFSET(pos->no));
    }

    /* visible */
    {
        __u16 val = vga_reg_read16(CODEC_VGA_SPCR_OFFSET);
        __u16 bit = CODEC_VGA_SPCR_BIT_SPR_EN(pos->no);
        if(pos->visible)
            val |= bit;
        else
            val &= ~bit;
        vga_reg_write16(val,CODEC_VGA_SPCR_OFFSET);
    }
}

/* get info. about sprite position and visible */
static void get_sprite_pos(struct cvga_ioc_sprite_pos *const pos)
{
    /* position */
    {
        /* read register */
        __u32 val = vga_reg_read32(CODEC_VGA_SPC_OFFSET(pos->no));

        /* x-pos */
        pos->xpos = (val>>CODEC_VGA_SPC_SHIFT_SPCX) & CODEC_VGA_SPC_MASK_SPCX;

        /* y-pos */
        pos->ypos = (val>>CODEC_VGA_SPC_SHIFT_SPCY) & CODEC_VGA_SPC_MASK_SPCY;
    }

    /* visible */
    {
        __u16 val = vga_reg_read16(CODEC_VGA_SPCR_OFFSET);
        __u16 bit = CODEC_VGA_SPCR_BIT_SPR_EN(pos->no);
        pos->visible = (val&bit)?1:0;
    }
}

/* check visiblity of sprite */
static int chk_sprite_visible(const int no)
{
    __u16 val = vga_reg_read16(CODEC_VGA_SPCR_OFFSET);
    __u16 bit = CODEC_VGA_SPCR_BIT_SPR_EN(no);
    return (val&bit)?1:0;
}


/* set sprite size & data */
static void set_sprite_data(const struct cvga_ioc_sprite_data *const data)
{
    /* size */
    {
        /* read register */
        __u16 val = vga_reg_read16(CODEC_VGA_SPSR_OFFSET(data->no));
        __u32 xsize = (data->xsize)>>3;
        __u32 ysize = (data->ysize)>>3;

        /* x-size */
        val &= ~(CODEC_VGA_SPSR_MASK_SPSX<<CODEC_VGA_SPSR_SHIFT_SPSX);
        val |= ((xsize-1)&CODEC_VGA_SPSR_MASK_SPSX)<<CODEC_VGA_SPSR_SHIFT_SPSX;

        /* y-size */
        val &= ~(CODEC_VGA_SPSR_MASK_SPSY<<CODEC_VGA_SPSR_SHIFT_SPSY);
        val |= ((ysize-1)&CODEC_VGA_SPSR_MASK_SPSY)<<CODEC_VGA_SPSR_SHIFT_SPSY;

        /* write-back to register */
        vga_reg_write16(val,CODEC_VGA_SPSR_OFFSET(data->no));
    }

    /* data */
    {
        __u32 cnt;
        __u16 *ptr;
        for(cnt=0,ptr=(__u16*)param.spbuff_addr[data->no]; cnt<data->sp_cnt; cnt++, ptr++)
            iowrite16be(cpu_to_be16(data->sp_data[cnt]), (void*)ptr);
    }
}

/* get sprite size & data */
static void get_sprite_data(struct cvga_ioc_sprite_data *const data)
{
    /* size */
    {
        /* read register */
        __u16 val = vga_reg_read16(CODEC_VGA_SPSR_OFFSET(data->no));
        __u16 xsize, ysize;

        /* x-size */
        xsize = ((val >> CODEC_VGA_SPSR_SHIFT_SPSX) & CODEC_VGA_SPSR_MASK_SPSX) + 1;
        data->xsize = xsize<<3;

        /* y-size */
        ysize = ((val >> CODEC_VGA_SPSR_SHIFT_SPSY) & CODEC_VGA_SPSR_MASK_SPSY) + 1;
        data->ysize = ysize<<3;

        /* sp_cnt*/
        data->sp_cnt = data->xsize * data->ysize;
    }

    /* data */
    {
        __u32 cnt;
        __u16 *ptr;
        for(cnt=0,ptr=(__u16*)param.spbuff_addr[data->no]; cnt<data->sp_cnt; cnt++, ptr++)
            data->sp_data[cnt] = be16_to_cpu(ioread16be((void*)ptr));
    }
}

static void init_vga_reg(void)
{
    int n;
    /*
     * Initial Setting to CODEC-VGA register
     */
#ifndef CONFIG_CODEC_VGA_NO_RESET
    vga_reg_write16(CODEC_VGA_VCR_SDRRST, CODEC_VGA_VCR_OFFSET); /* VGA_CONTROL_REG(VCR) */
#endif  /* CONFIG_CODEC_VGA_NO_RESET */
    vga_reg_write16(0x0000,CODEC_VGA_SPCR_OFFSET); /* SPRITE_CONTROL_REG(SPCR) */
    for(n=0; n<CVGA_SPRITE_NUM; n++){
        vga_reg_write16(0x0000,CODEC_VGA_SPC_OFFSET(n)); /* SPRITE_COODINATE(SPC) */
        vga_reg_write16(0x0000,CODEC_VGA_SPSR_OFFSET(n));
    }
    set_phase(init_h_phase,init_v_phase); /* H/V phase */
}

static void trans_cpu_wq(struct work_struct *work)
{
    int n;
    static const int MAX = (4096/sizeof(unsigned long));
    for(n=MAX;n>0;n--){
        if(param.data_cpu.num<=0)
            goto done;
        *param.data_cpu.dst++ = *param.data_cpu.src++;
        param.data_cpu.num--;
    }
    
    schedule_work(&param.work_cpu);
    return;

 done:
    wake_up_interruptible(&param.queue_cpu);
}


/*
 *  trans local FB ==> VGA buff plane (DMA)
 */
static int fb_to_vga_dma(const struct fb_info *const info, const __u32 plane)
{
	int ret = 0;
	ssize_t size = CVGA_SCREEN_SIZE;
    const struct codec_vga_fb_par *par = (const struct codec_vga_fb_par *const)info->par;
	void *src = (void *)virt_to_bus(par->virt_addr);
	void *dst = VGA_PLANE_PHYADR(plane);

    /* trans local FB ==> VGA buff plane */
	ret = MPC83xxDmacDirectMode(param.dma_ch, (unsigned long)src, (unsigned long)dst, size, DMAC_WRITE);

    /* complete */
	return ret;
}


/*
 *  trans local FB ==> VGA buff plane (CPU)
 */
static int fb_to_vga_cpu(const struct fb_info *const info, const __u32 plane)
{
	int ret = 0;
    const struct codec_vga_fb_par *par = (const struct codec_vga_fb_par *const)info->par;
	param.data_cpu.src = (volatile unsigned long *)par->virt_addr;
	param.data_cpu.dst = (volatile unsigned long *)VGA_PLANE_ADDR(plane);
	param.data_cpu.num = (CVGA_SCREEN_SIZE+3)/sizeof(unsigned long);

    /* trans local FB ==> VGA buff plane */
    schedule_work(&param.work_cpu);
    if(wait_event_interruptible(param.queue_cpu, param.data_cpu.num<=0)) 
        return -ERESTARTSYS;

    /* complete */
	return ret;
}


/*
 *  trans local FB <== VGA buff plane (DMA)
 */
static int vga_to_fb_dma(const struct fb_info *const info, const __u32 plane)
{
	int ret = 0;
	ssize_t size = CVGA_SCREEN_SIZE;
    const struct codec_vga_fb_par *par = (const struct codec_vga_fb_par *const)info->par;
	void *dst = (void *)virt_to_bus(par->virt_addr);
	void *src = VGA_PLANE_PHYADR(plane);

    /* trans local FB ==> VGA buff plane */
	ret = MPC83xxDmacDirectMode(param.dma_ch, (unsigned long)src, (unsigned long)dst, size, DMAC_READ);

    /* complete */
	return ret;
}


/*
 *  trans local FB <== VGA buff plane (CPU)
 */
static int vga_to_fb_cpu(const struct fb_info *const info, const __u32 plane)
{
	int ret = 0;
    const struct codec_vga_fb_par *par = (const struct codec_vga_fb_par *const)info->par;
	param.data_cpu.dst = (volatile unsigned long *)par->virt_addr;
	param.data_cpu.src = (volatile unsigned long *)VGA_PLANE_ADDR(plane);
	param.data_cpu.num = (CVGA_SCREEN_SIZE+3)/sizeof(unsigned long);

    /* trans local FB <== VGA buff plane */
    schedule_work(&param.work_cpu);
    if(wait_event_interruptible(param.queue_cpu, param.data_cpu.num<=0)) 
        return -ERESTARTSYS;

    /* complete */
	return ret;
}


static int encode_var(struct fb_var_screeninfo *const var)
{
	/*
	 * Fill the 'var' structure based on the values in 'par' and maybe other
	 * values read out of the hardware.
	 */

	memset(var, 0, sizeof(struct fb_var_screeninfo));

	var->xres		= CVGA_X_RES;
	var->yres		= CVGA_Y_RES;
	var->xres_virtual	= CVGA_X_RES;
	var->yres_virtual	= CVGA_Y_RES;
	var->xoffset		= 0;
	var->yoffset		= 0;

	var->bits_per_pixel	= CVGA_BITS_PER_PIXEL;
	var->grayscale		= 0;

	var->red.offset		= 11;
	var->red.length		= 5;
	var->red.msb_right	= 0;
	var->green.offset	= 5;
	var->green.length	= 6;
	var->green.msb_right	= 0;
	var->blue.offset	= 0;
	var->blue.length	= 5;
	var->blue.msb_right	= 0;
	var->transp.offset	= 0;
	var->transp.length	= 0;
	var->transp.msb_right	= 0;

	var->nonstd		= 0;
	var->activate		= FB_ACTIVATE_NOW;

	var->height		= -1;
	var->width		= -1;

	var->accel_flags	= 0;

	var->pixclock		= 0;
	var->left_margin	= 0;
	var->right_margin	= 0;
	var->upper_margin	= 0;
	var->lower_margin	= 0;
	var->hsync_len		= 0;
	var->vsync_len		= 0;

	var->sync 		= 0;
	var->vmode		= FB_VMODE_INTERLACED;

	return 0;
}


static int encode_fix(const struct codec_vga_fb_par *const par, struct fb_fix_screeninfo *const fix)
{
	/*
	 * This function should fill in the 'fix' structure based on the values
	 * in the `par' structure.
	 */

	memset(fix, 0, sizeof(struct fb_fix_screeninfo));

	snprintf(fix->id, sizeof(fix->id),"%s #%d",codec_vga_name, par->devno);

	fix->smem_start		= (unsigned long)par->virt_addr;
	fix->smem_len		= CVGA_VRAM_SIZE;
	fix->type 		= FB_TYPE_PACKED_PIXELS;
	fix->type_aux		= 0;
	fix->visual		= FB_VISUAL_TRUECOLOR;
	fix->xpanstep		= 0;
	fix->ypanstep		= 0;
	fix->ywrapstep		= 0;
	fix->line_length	= CVGA_X_RES * CVGA_BYTES_PER_PIXEL;
	fix->mmio_start		= 0;
	fix->mmio_len		= 0;
	fix->accel		= FB_ACCEL_NONE; /*0*/

	return 0;
}

/**
 *  kernel thread to do dma in cyclic.
 */
static int cycdmac_kthread(void *data)
{
    int devno;
    static const unsigned long interval = (HZ*10)/1000; /* 10msec */
    unsigned long timeout;

    /* main loop */
    for(;;){

        /* sleep in 10msec */
        for(timeout = interval;;){
            long remain;
            set_current_state(TASK_INTERRUPTIBLE);
            remain = schedule_timeout(timeout);
            if(remain==0)
                break;
            timeout = remain;
        }

        /* exit kthread */
        if(param.flag_kthread_exit){
            _DEBUG("kthread is exiting\n");
            break;
        }

        for(devno=0; devno<CVGA_FB_NUM; devno++){

            struct fb_info *info = &param.fb[devno].info;
            struct codec_vga_fb_par *par = &param.fb[devno].par;
            __u32 vpair,plane,bit;

            /* get semaphore for DMAC */
            if(down_interruptible(&param.sem_dmac)){
                _WARN("devno=%d: kthread is interrupted.\n", devno);
                continue;
            }
                

           /* get semaphore */
            if(down_interruptible(&param.sem)){
                up(&param.sem_dmac);
                continue;
            }

            /* check cyclic mode */
            if( ! par->mode.cyclic){
                up(&param.sem);
                up(&param.sem_dmac);
                continue;
            }

            /* check remain */
            if( par->mode.remain >0 ){
                par->mode.remain--;
                up(&param.sem);
                up(&param.sem_dmac);
                continue;
            }

            /* get parameter */
            vpair = par->mode.vpair;
            par->mode.remain = par->mode.interval;

            bit = param.state_vpairs&(1<<vpair);
            plane = (vpair * 2) + (bit?0:1);

            /* release semaphore */
            up(&param.sem);

            /* local FB ==> VGA buffer plane */
            _DEBUG("Start dmac in kthread: FB#%d => plane%#d(vpare#%d)\n",
                   devno, plane, vpair);
            if(param.flag_cpu_transfer?fb_to_vga_cpu(info,plane):fb_to_vga_dma(info,plane)){
                _ERR("unable to dmac in kthread: FB#%d => plane#%d(vpare#%d)\n",
                     devno, plane, vpair);
                up(&param.sem_dmac);
                continue;
            }

           /* get semaphore */
            if(down_interruptible(&param.sem)){
                up(&param.sem_dmac);
                continue;
            }
            
            /* switch palane */
            if(bit)
                param.state_vpairs &= ~(1<<vpair);
            else
                param.state_vpairs |= (1<<vpair);
            if(param.out_vpair==vpair)
                chg_outplane(plane);

            /* release semaphore */
            up(&param.sem);

            /* release semaphore for DMAC */
            up(&param.sem_dmac);
        }
                

    } /* for(;;) */

    /* Wait until we are told to stop */
    for (;;) {
        set_current_state(TASK_INTERRUPTIBLE);
        if (kthread_should_stop())
            break;
        schedule();
    }
    __set_current_state(TASK_RUNNING);
    return 0;
}


//==============================================================================
//  FB operations
//==============================================================================

/**
 *  fb_check_var method
 */
static int codec_vga_fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	return -EINVAL;
}

/**
 *  fb_set_par method
 */
static int codec_vga_fb_set_par(struct fb_info *info)
{
	/*
	 * Set the hardware according to 'par'.
	 */
  
    /*
     *  Nothing to do
     */

	return 0;
}

/**
 * fb_setcolreg method
 */
static int codec_vga_fb_setcolreg(unsigned regno, unsigned red, unsigned green,
			     unsigned blue, unsigned transp,
			     struct fb_info *info)
{
  //
  // Set a single color register. The values supplied have a 16 bit
  // magnitude.
  // Return != 0 for invalid regno.
  //

  return -EINVAL;
}


/**
 *  fb_ioctl method
 */
static int codec_vga_fb_ioctl(struct fb_info *info, unsigned int cmd, unsigned long arg)
{
    int ret=0;
    struct codec_vga_fb_par *par = (struct codec_vga_fb_par*)info->par;
    int devno = par->devno;

    /* check device number */
    if(devno>=CVGA_FB_NUM||devno<0){
        _ERR("Invalid FB device no=%d\n",devno);
        ret=-ENODEV;
        goto exit;
    }

    /*
     *
     */
    switch(cmd){

    case CVGA_IOC_FB_PURGE:
        _DEBUG("ioctl(CVGA_IOC_FB_PURGE) is called\n");
        {
            __u32 vpair,plane,bit;

            vpair = (__u32)arg;
            if(vpair>=CVGA_VPAIR_NUM){
                _ERR("Invalid VGA plane pair number=%d\n",vpair);
                ret=-EINVAL;
                goto exit;
            }

            /* get semaphore for DMAC */
            if(down_interruptible(&param.sem_dmac)){
                ret = -ERESTARTSYS;
                goto exit;
            }
            
            bit = param.state_vpairs&(1<<vpair);
            plane = (vpair * 2) + (bit?0:1);

            /* local FB ==> VGA buffer plane */
            _DEBUG("start dmac : FB#%d => plane%#d(vpare#%d)\n",devno,plane,vpair);
            ret = param.flag_cpu_transfer?fb_to_vga_cpu(info,plane):fb_to_vga_dma(info,plane);
            if(ret){
                _ERR("unable to dmac : FB#%d => plane#%d(vpare#%d)\n",devno,plane,vpair);
                up(&param.sem_dmac);
                goto exit;
            }

            /* get semaphore */
            if(down_interruptible(&param.sem)){
                up(&param.sem_dmac);
                ret = -ERESTARTSYS;
                goto exit;
            }

            /* switch palane */
            if(par->mode.switchable){
                if(bit)
                    param.state_vpairs &= ~(1<<vpair);
                else
                    param.state_vpairs |= (1<<vpair);
                if(param.out_vpair==vpair)
                    chg_outplane(plane);
            }

            /* release semaphore */
            up(&param.sem);

            /* release semaphore for dmac */
            up(&param.sem_dmac);


        }
        break;


    case CVGA_IOC_FB_EGRUP:
        _DEBUG("ioctl(CVGA_IOC_FB_EGRUP) is called\n");
        {
            __u32 vpair,plane,bit;

            vpair = (__u32)arg;
            if(vpair>=CVGA_VPAIR_NUM){
                _ERR("Invalid VGA plane pair number=%d\n",vpair);
                ret=-EINVAL;
                goto exit;
            }

            /* get semaphore for DMAC */
            if(down_interruptible(&param.sem_dmac)){
                ret = -ERESTARTSYS;
                goto exit;
            }
            
            bit = param.state_vpairs&(1<<vpair);
            plane = (vpair * 2) + (bit?0:1);

            /* local VGA ==> FB buffer plane */
            _DEBUG("start dmac : FB#%d => plane%#d(vpare#%d)\n",devno,plane,vpair);
            ret = param.flag_cpu_transfer?vga_to_fb_cpu(info,plane):vga_to_fb_dma(info,plane);
            if(ret){
                _ERR("unable to dmac : FB#%d => plane#%d(vpare#%d)\n",devno,plane,vpair);
                up(&param.sem_dmac);
                goto exit;
            }

            /* get semaphore */
            if(down_interruptible(&param.sem)){
                up(&param.sem_dmac);
                ret = -ERESTARTSYS;
                goto exit;
            }

            /* switch palane */
            if(par->mode.switchable){
                if(bit)
                    param.state_vpairs &= ~(1<<vpair);
                else
                    param.state_vpairs |= (1<<vpair);
                if(param.out_vpair==vpair)
                    chg_outplane(plane);
            }

            /* release semaphore */
            up(&param.sem);

            /* release semaphore for dmac */
            up(&param.sem_dmac);


        }
        break;


    case CVGA_IOC_FB_SETMODE:
        _DEBUG("ioctl(CVGA_IOC_FB_SETMODE) is called\n");
        {
            struct cvga_ioc_fb_mode mode;

            /* get parameter from arguments */
            if (copy_from_user((void*)&mode, (const void __user *)arg,
                               sizeof(struct cvga_ioc_fb_mode))) {
                _ERR("failed copy_from_user()\n");
                ret = -EFAULT;
                goto exit;
            }

            /* parameter check */
            if(mode.cyclic) {
                if(mode.interval==0){
                    _ERR("Invalid parameter: interval=%ld\n",mode.interval);
                    ret = -EINVAL;
                    goto exit;
                }
                mode.remain = mode.interval;
                if(mode.vpair>=CVGA_VPAIR_NUM);
            }else{
                mode.remain = mode.interval = 0;
            }

            /* get semaphore */
            if(down_interruptible(&param.sem)){
                ret = -ERESTARTSYS;
                goto exit;
            }
           
            /* set parameters */
            memcpy(&par->mode, &mode, sizeof(struct cvga_ioc_fb_mode));
            
            /* release semaphore */
            up(&param.sem);
        }
        break;

    case CVGA_IOC_FB_GETMODE:
        _DEBUG("ioctl(CVGA_IOC_FB_GETMODE) is called\n");
        {
            struct cvga_ioc_fb_mode mode;

            /* get semaphore */
            if(down_interruptible(&param.sem)){
                ret = -ERESTARTSYS;
                goto exit;
            }
           
            /* get parameters */
            memcpy(&mode, &par->mode, sizeof(struct cvga_ioc_fb_mode));
            
            /* release semaphore */
            up(&param.sem);

            /*  */
            if (copy_to_user((void __user *)arg, (void*)&mode, 
                               sizeof(struct cvga_ioc_fb_mode))) {
                _ERR("failed copy_to_user()\n");
                ret = -EFAULT;
                goto exit;
            }

        }
        break;

    case CVGA_IOC_VGA_SWITCH:
        _DEBUG("ioctl(CVGA_IOC_VGA_SWITCH) is called\n");
        {
            __u32 vpair,plane,bit;

            vpair = (__u32)arg;
            if(vpair>=CVGA_VPAIR_NUM){
                _ERR("Invalid VGA plane pair number=%d\n",vpair);
                ret=-EINVAL;
                goto exit;
            }
            
            /* get semaphore */
            if(down_interruptible(&param.sem)){
                ret = -ERESTARTSYS;
                goto exit;
            }

            /*  */
            bit = param.state_vpairs&(1<<vpair);
            plane = (vpair * 2) + (bit?0:1);
            if(bit)
                param.state_vpairs &= ~(1<<vpair);
            else
                param.state_vpairs |= (1<<vpair);
            if(param.out_vpair==vpair)
                chg_outplane(plane);

            /* release semaphore */
            up(&param.sem);
           
        }
        break;

    case CVGA_IOC_VGA_CHANGE:
        _DEBUG("ioctl(CVGA_IOC_VGA_CHANGE) is called\n");
        {
            __u32 vpair,plane,bit;

            vpair = (__u32)arg;
            if(vpair>=CVGA_VPAIR_NUM){
                _ERR("Invalid VGA plane pair number=%d\n",vpair);
                ret=-EINVAL;
                goto exit;
            }
            
            /* get semaphore */
            if(down_interruptible(&param.sem)){
                ret = -ERESTARTSYS;
                goto exit;
            }

            /* NOT need to change */
            if(param.out_vpair==vpair){
                up(&param.sem);
                goto exit;
            }

            /*  */
            bit = param.state_vpairs&(1<<vpair);
            plane = (vpair * 2) + (bit?1:0);
            chg_outplane(plane);
            param.out_vpair = vpair;

            /* release semaphore */
            up(&param.sem);
        }
        break;

    case CVGA_IOC_VGA_MUTE:
        _DEBUG("ioctl(CVGA_IOC_VGA_MUTE) is called\n");
        {
            int muteoff = (int)arg?1:0;
            
            /* get semaphore */
            if(down_interruptible(&param.sem)){
                ret = -ERESTARTSYS;
                goto exit;
            }

            /* mute control */
            mute_ctrl(muteoff);

            /* release semaphore */
            up(&param.sem);
        }
        break;

    case CVGA_IOC_VGA_RSTR:
        _DEBUG("ioctl(CVGA_IOC_VGA_RSTR) is called\n");
        {
            int enable = (int)arg?1:0;
            
            /* get semaphore */
            if(down_interruptible(&param.sem)){
                ret = -ERESTARTSYS;
                goto exit;
            }

            /* mute control */
            rstr_ctrl(enable);

            /* release semaphore */
            up(&param.sem);
        }
        break;

    case CVGA_IOC_VGA_BLINK:
        _DEBUG("ioctl(CVGA_IOC_VGA_BLINK) is called\n");
        {
            int enable = (int)arg?1:0;
            
            /* get semaphore */
            if(down_interruptible(&param.sem)){
                ret = -ERESTARTSYS;
                goto exit;
            }

            /* mute control */
            blink_ctrl(enable);

            /* release semaphore */
            up(&param.sem);
        }
        break;

    case CVGA_IOC_VGA_TRANSMODE:
        _DEBUG("ioctl(CVGA_IOC_VGA_TRANSMODE) is called\n");
        {
            /* get semaphore */
            if(down_interruptible(&param.sem)){
                ret = -ERESTARTSYS;
                goto exit;
            }

            /* setting */
            param.flag_cpu_transfer = (int)arg?1:0;

            /* release semaphore */
            up(&param.sem);
        }
        break;

    case CVGA_IOC_VGA_PHASE:
        _DEBUG("ioctl(CVGA_IOC_VGA_PHASE) is called\n");
        {
            struct cvga_ioc_vga_phase phase;

            /* get parameter from arguments */
            if (copy_from_user((void*)&phase, (const void __user *)arg,
                               sizeof(struct cvga_ioc_vga_phase))) {
                _ERR("failed copy_from_user()\n");
                ret = -EFAULT;
                goto exit;
            }

            /* parameter check */
            if((phase.h_phase>=CVGA_X_RES)||(phase.v_phase>=CVGA_Y_RES)){
                _ERR("Invalid parameter: (h_phase,v_phase)=(%d,%d)\n",
                       phase.h_phase, phase.v_phase);
                ret = -EINVAL;
                goto exit;
            }

            /* get semaphore */
            if(down_interruptible(&param.sem)){
                ret = -ERESTARTSYS;
                goto exit;
            }
           
            /* set parameters */
            set_phase(phase.h_phase,phase.v_phase);
            
            /* release semaphore */
            up(&param.sem);
        }
        break;

    case CVGA_IOC_VGA_GETINFO:
        _DEBUG("ioctl(CVGA_IOC_VGA_GETINFO) is called\n");
        {
            struct cvga_ioc_vga_info info;

            /* get semaphore */
            if(down_interruptible(&param.sem)){
                ret = -ERESTARTSYS;
                goto exit;
            }
           
            /* get info */
            info.muteoff = mute_status();
            info.rstron = rstr_status();
            info.blink = blink_status();
            info.out_vpair = param.out_vpair;
            info.state_vpairs = param.state_vpairs;
            get_phase(&info.h_phase,&info.v_phase);
            info.trans_mode = param.flag_cpu_transfer;
            
            /* release semaphore */
            up(&param.sem);

            /* copy to user */
            if (copy_to_user((void __user *)arg, (void*)&info, 
                               sizeof(struct cvga_ioc_vga_info))) {
                _ERR("failed copy_to_user()\n");
                ret = -EFAULT;
                goto exit;
            }
        }
        break;

    case CVGA_IOC_SPRITE_SETPOS:
        _DEBUG("ioctl(CVGA_IOC_SPRITE_SETPOS) is called\n");
        {
            struct cvga_ioc_sprite_pos pos;

            /* get parameter from arguments */
            if (copy_from_user((void*)&pos, (const void __user *)arg,
                               sizeof(struct cvga_ioc_sprite_pos))) {
                _ERR("failed copy_from_user()\n");
                ret = -EFAULT;
                goto exit;
            }

            /* check parameters */
            if( (pos.no<0) || (pos.no>=CVGA_SPRITE_NUM) ){
                _ERR("Invalid parameter: (sprite buff no = %d)\n",pos.no);
                ret = -EINVAL;
                goto exit;
            }
            if( (pos.xpos>=CVGA_X_RES) || (pos.ypos>=CVGA_Y_RES) ){
                _ERR("Invalid parameter: (sprite position = (%d,%d)\n",
                       pos.xpos, pos.ypos);
                ret = -EINVAL;
                goto exit;
            }

            /* get semaphore */
            if(down_interruptible(&param.sem)){
                ret = -ERESTARTSYS;
                goto exit;
            }

            /* set parameters */
            set_sprite_pos(&pos);

            /* release semaphore */
            up(&param.sem);
        }
        break;

    case CVGA_IOC_SPRITE_GETPOS:
        _DEBUG("ioctl(CVGA_IOC_SPRITE_GETPOS) is called\n");
        {
            struct cvga_ioc_sprite_pos pos;

            /* get parameter from arguments */
            if (copy_from_user((void*)&pos, (const void __user *)arg,
                               sizeof(struct cvga_ioc_sprite_pos))) {
                _ERR("failed copy_from_user()\n");
                ret = -EFAULT;
                goto exit;
            }

            /* check parameters */
            if( (pos.no<0) || (pos.no>=CVGA_SPRITE_NUM) ){
                _ERR("Invalid parameter: (sprite buff no = %d)\n",pos.no);
                ret = -EINVAL;
                goto exit;
            }

            /* get semaphore */
            if(down_interruptible(&param.sem)){
                ret = -ERESTARTSYS;
                goto exit;
            }

            /* get parameters */
            get_sprite_pos(&pos);

            /* release semaphore */
            up(&param.sem);

            /*  */
            if (copy_to_user((void __user *)arg, (void*)&pos, 
                               sizeof(struct cvga_ioc_sprite_pos))) {
                _ERR("failed copy_to_user()\n");
                ret = -EFAULT;
                goto exit;
            }
        }
        break;

    case CVGA_IOC_SPRITE_SETDATA:
        _DEBUG("ioctl(CVGA_IOC_SPRITE_SETDATA) is called\n");
        {
            struct cvga_ioc_sprite_data *data;

            /* allocate buffer */
            data = (struct cvga_ioc_sprite_data *)kmalloc(sizeof(struct cvga_ioc_sprite_data),GFP_KERNEL);
            if(NULL==data){
                _ERR("Unable to allocate memory for \"struct cvga_ioc_sprite_data\"\n");
                ret = -ENOMEM;
                goto exit;
            }

            /* get parameter from arguments */
            if (copy_from_user((void*)data, (const void __user *)arg,
                               sizeof(struct cvga_ioc_sprite_data))) {
                kfree((void*)data);
                _ERR("failed copy_from_user()\n");
                ret = -EFAULT;
                goto exit;
            }

            /* check parameters */
            if( (data->no<0) || (data->no>=CVGA_SPRITE_NUM) ){
                kfree((void*)data);
                _ERR("Invalid parameter: (sprite buff no = %d)\n",data->no);
                ret = -EINVAL;
                goto exit;
            }
            if( (data->xsize==0) || (data->ysize==0) || (data->xsize&7) || (data->ysize&7)
                || ((data->xsize * data->ysize) > CVGA_SPRITE_MAX_PIXS) ){
                kfree((void*)data);
                _ERR("Invalid parameter: (sprite size = (%d,%d)\n", data->xsize, data->ysize);
                ret = -EINVAL;
                goto exit;
            }
            if( data->sp_cnt==0 || data->sp_cnt>CVGA_SPRITE_MAX_PIXS ){
                _ERR("Invalid parameter: (sprite buff len = %d pix)\n", data->sp_cnt);
                kfree((void*)data);
                ret = -EINVAL;
                goto exit;
            }

            /* check visiblity */
            if(chk_sprite_visible(data->no)){
                _WARN("failed to write to sprite buffer #%d in order to be in visible now\n",data->no);
                kfree((void*)data);
                ret = -EIO;
                goto exit;
            }

            /* get semaphore */
            if(down_interruptible(&param.sem)){
                kfree((void*)data);
                ret = -ERESTARTSYS;
                goto exit;
            }

            /* set parameters */
            set_sprite_data(data);

            /* release semaphore */
            up(&param.sem);

            /* free buffer */
            kfree((void*)data);
        }
        break;

    case CVGA_IOC_SPRITE_GETDATA:
        _DEBUG("ioctl(CVGA_IOC_SPRITE_GETDATA) is called\n");
        {
            struct cvga_ioc_sprite_data *data;

            /* allocate buffer */
            data = (struct cvga_ioc_sprite_data *)kmalloc(sizeof(struct cvga_ioc_sprite_data),GFP_KERNEL);
            if(NULL==data){
                _ERR("Unable to allocate memory for \"struct cvga_ioc_sprite_data\"\n");
                ret = -ENOMEM;
                goto exit;
            }

            /* get parameter from arguments */
            if (copy_from_user((void*)data, (const void __user *)arg,
                               sizeof(struct cvga_ioc_sprite_data))) {
                kfree((void*)data);
                _ERR("failed copy_from_user()\n");
                ret = -EFAULT;
                goto exit;
            }

            /* check parameters */
            if( (data->no<0) || (data->no>=CVGA_SPRITE_NUM) ){
                _ERR("Invalid parameter: (sprite buff no = %d)\n",data->no);
                kfree((void*)data);
                ret = -EINVAL;
                goto exit;
            }

            /* check visiblity */
            if(chk_sprite_visible(data->no)){
                _WARN("fail to read from sprite buffer #%d in order to be in visible now\n",data->no);
                kfree((void*)data);
                ret = -EIO;
                goto exit;
            }

            /* get semaphore */
            if(down_interruptible(&param.sem)){
                kfree((void*)data);
                ret = -ERESTARTSYS;
                goto exit;
            }

            /* get parameters */
            get_sprite_data(data);

            /* release semaphore */
            up(&param.sem);

            /*  */
            if (copy_to_user((void __user *)arg, (void*)data, 
                               sizeof(struct cvga_ioc_sprite_data))) {
                kfree((void*)data);
                _ERR("failed copy_to_user()\n");
                ret = -EFAULT;
                goto exit;
            }

            /* free buffer */
            kfree((void*)data);

        }
        break;

	default:
		_ERR("NO such ioctl command = 0x%08lX\n",(unsigned long)arg);
		ret = -ENOTTY;
	}


 exit:
    /* complete */
    return ret;
}


/**
 *  fb_mmap method
 */
static int codec_vga_fb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
    struct codec_vga_fb_par *par = (struct codec_vga_fb_par *)info->par;
/* 	struct page *map, *mapend; */

/* 	map = virt_to_page((unsigned long)info->par->virt_addr); */
/* 	mapend = virt_to_page((unsigned long)info->par->virt_addr + (CVGA_VRAM_SIZE - 1)); */

/* 	while(map <= mapend) { */
/* 		set_bit(PG_reserved, &((map)->flags)); */
/* 		map++; */
/* 	} */

	vma->vm_flags |= VM_RESERVED;

	if(remap_pfn_range(vma, vma->vm_start,
                       (__pa(par->virt_addr) >> PAGE_SHIFT),
                       vma->vm_end - vma->vm_start, vma->vm_page_prot)){
		_ERR("failed remap_page_range()\n");
		return -EAGAIN;
	}

	return 0;
}

/**
 * fb_ops structure
 */
static struct fb_ops codec_vga_fb_ops = {
	.owner          = THIS_MODULE,
	.fb_check_var	= codec_vga_fb_check_var,
	.fb_set_par     = codec_vga_fb_set_par,
	.fb_setcolreg	= codec_vga_fb_setcolreg,
#ifdef CONFIG_FB_CFB_FILLRECT
	.fb_fillrect    = cfb_fillrect,
#endif //CONFIG_FB_CFB_FILLRECT
#ifdef CONFIG_FB_CFB_COPYAREA
	.fb_copyarea	= cfb_copyarea,
#endif //CONFIG_FB_CFB_COPYAREA
#ifdef CONFIG_FB_CFB_IMAGEBLIT
	.fb_imageblit	= cfb_imageblit,
#endif //CONFIG_FB_CFB_IMAGEBLIT
	.fb_ioctl       = codec_vga_fb_ioctl,
	.fb_mmap		= codec_vga_fb_mmap, 
};

//==============================================================================
//  init/cleanup
//==============================================================================

/**
 *  probing device
 */
static int __init codec_vga_probe(struct platform_device *dev)
{
	int ret=0;
    int n;
    int flag_alloc_cmap[CVGA_FB_NUM];
    int flag_reg_fb[CVGA_FB_NUM];
    struct codecvga_platform_data *pdata;

    /* initialize local parameter */
    memset(&param,0,sizeof(struct codec_vga_param));

    /*
     * Allocate resource of i/o memory
     *  and mapping i/o memory
     */

	platform_set_drvdata(dev, &param);
	pdata = (struct codecvga_platform_data *)dev->dev.platform_data;
    if (pdata == NULL) {
        _ERR("platform data is NOT found.\n");
		ret = -EINVAL;
		goto fail;
	}

    /* CODEC-FPGA VGA registers */
    {
        struct resource *r = platform_get_resource(dev, IORESOURCE_MEM, 0);
        if(!r){
            _ERR("Can NOT get resource for CODEC-FPGA VGA register\n");
            ret = -EINVAL;
            goto fail;
        }
        param.reg_paddr = (unsigned long)r->start;
        param.reg_size = (unsigned long)(r->end - r->start + 1);
        param.reg_rsc = request_mem_region(r->start, (r->end - r->start + 1),
                                       "CODEC-FPGA VGA Registers");
        if ( NULL==param.reg_rsc ){
            _ERR("Can NOT allocate CODEC-FPGA VGA register area\n");
            ret = -ENXIO;
            goto fail;
        }
        param.reg_addr = (unsigned long)ioremap(r->start, (r->end - r->start + 1));
        if ( NULL==(void*)param.reg_addr ){
            _ERR("Can NOT map CODEC-FPGA VGA register area\n");
            ret = -ENXIO;
            goto fail;
        }
    }
    
    /* CODEC-FPGA sprite buffes */
    if(pdata->sprite_num < CVGA_SPRITE_NUM){
        _ERR("invalid sprite number = %d(<%d)\n",pdata->sprite_num,CVGA_SPRITE_NUM);
        ret = -EINVAL;
        goto fail;
    }
    for(n=0; n<CVGA_SPRITE_NUM; n++){
        struct resource *r = platform_get_resource(dev, IORESOURCE_MEM, n+2);
        if(!r){
            _ERR("Can NOT get resource for CODEC-FPGA sprite buffes[%d].\n",n);
            ret = -EINVAL;
            goto fail;
        }
        param.spbuff_paddr[n] = r->start;
        if((r->end - r->start + 1) != CVGA_SPRITE_BUFF_SIZE){
            _ERR("invalid sprite buffer#%d size = %d(!=%d)\n",
                 n, (r->end - r->start + 1),CVGA_SPRITE_BUFF_SIZE);
            ret = -EINVAL;
            goto fail;
        }
        param.spbuff_rsc[n]
            = request_mem_region(r->start, CVGA_SPRITE_BUFF_SIZE, "CODEC-FPGA sprite Buffes");
        if ( NULL==param.spbuff_rsc[n] ){
            _ERR("Can NOT allocate CODEC-FPGA sprite buffer[%d] area\n",n);
            ret = -ENXIO;
            goto fail;
        }
        param.spbuff_addr[n]
            = (unsigned long)ioremap(r->start, CVGA_SPRITE_BUFF_SIZE);
        if ( NULL==(void*)param.spbuff_addr[n] ){
            _ERR("Can NOT map CODEC-FPGA sprite buffer[%d] area\n",n);
            ret = -ENXIO;
            goto fail;
        }
        //memset_io((void*)param.spbuff_addr[n],0x00,CVGA_SPRITE_BUFF_SIZE);

    }

    /* CODEC-FPGA VGA buffer */
    {
        struct resource *r = platform_get_resource(dev, IORESOURCE_MEM, 1);
        if(!r){
            _ERR("Can NOT get resource for CODEC-FPGA VGA buffer\n");
            ret = -EINVAL;
            goto fail;
        }
        param.vgabuff_paddr = r->start;
        if((r->end - r->start + 1) != CVGA_VGA_SIZE){
            _ERR("invalid VGA buffer size = %d(!=%d)\n",(r->end - r->start + 1),CVGA_VGA_SIZE);
            ret = -EINVAL;
            goto fail;
        }
        param.vgabuff_rsc = request_mem_region(r->start, CVGA_VGA_SIZE,"CODEC-FPGA VGA Buffer");
        if ( NULL==param.vgabuff_rsc ){
            _ERR("Can NOT allocate CODEC-FPGA VGA Buffer\n");
            ret = -ENXIO;
            goto fail;
        }
        param.vgabuff_addr = (unsigned long)ioremap(r->start,CVGA_VGA_SIZE);
        if ( NULL==(void*)param.vgabuff_addr ){
            _ERR("Can NOT map CODEC-FPGA VGA register area\n");
            ret = -ENXIO;
            goto fail;
        }
        //memset_io((void*)param.vgabuff_addr,0x00,CVGA_VGA_SIZE);
    }


    /*
     * Initial Setting to CODEC-VGA register
     */
    init_vga_reg();



    /*
     * Set parameters depend on local FB
     */
    memset ((void*)flag_alloc_cmap,0x00,sizeof(int)*CVGA_FB_NUM);
    memset ((void*)flag_reg_fb,0x00,sizeof(int)*CVGA_FB_NUM);
    for(n=0; n<CVGA_FB_NUM; n++){
        struct codec_vga_fb_par *par = &param.fb[n].par;
        struct fb_info *info = &param.fb[n].info;

        /* local FB parameters */
        {
            /* zero clear */
            memset((void*)par,0x00,sizeof(struct codec_vga_fb_par));

            /* device number */
            par->devno = n;

            /* FB virtual address */
            par->virt_addr
                = (void*)__get_free_pages(GFP_KERNEL | __GFP_DMA, get_order(CVGA_VRAM_SIZE));
            if (NULL == par->virt_addr ){
                _ERR("Can NOT allocate local FB #%d\n",n);
                ret = -ENOMEM;
                goto fail;
            }
            _DEBUG("Local FB #%d = 0x%08lx (virtual)\n", n,(unsigned long) par->virt_addr);
            _DEBUG("Local FB #%d = 0x%08lx (physical)\n", n,virt_to_bus(par->virt_addr));

            /* initializ FB area */
            {
                struct page *map, *mapend;

                map = virt_to_page((unsigned long)par->virt_addr);
                mapend = virt_to_page((unsigned long)par->virt_addr + (CVGA_VRAM_SIZE - 1));

                while(map <= mapend) {
                    set_bit(PG_reserved, &((map)->flags));
                    map++;
                }
                //memset(par->virt_addr, 0x00, CVGA_VRAM_SIZE);
            } 
        }

        /* fb_info */
        {
            /* zero clear */
            memset((void*)info,0x00,sizeof(struct fb_info));

            /*  */
            info->screen_base = (void __iomem *)par->virt_addr;
            info->screen_size = CVGA_SCREEN_SIZE;
            info->fbops = &codec_vga_fb_ops;
            info->par = par;

            /* fb_var */
            encode_var(&info->var);

            /* fb_fix */
            encode_fix(par,&info->fix);

            // This should give a reasonable default video mode
            ret = fb_alloc_cmap(&info->cmap, 256, 0);
            if(ret){
                _ERR( "failed to allocate cmap (FB #%d)\n",n );
                goto fail;
            }
            flag_alloc_cmap[n]=1;

            /* register FB */
            if((ret = register_framebuffer(info)) < 0){ 
                _ERR("failed to register framebuffer #%d\n",n);
                goto fail;
            }
            flag_reg_fb[n]=1;
            _DEBUG("Local FB#%d: /dev/fb%d(%s) is registered\n", n, info->node, info->fix.id);
        }
    } 

    /* DMA channel */
    param.dma_ch = pdata->dma_chan;
    if(param.dma_ch<0||param.dma_ch>=4) {
        _ERR("invalid DMA-cahhnel = %d\n",param.dma_ch);
        ret = -EINVAL;
        goto fail;
    }

    /*
     * initial semaphore
     */
    init_MUTEX(&param.sem);
    init_MUTEX(&param.sem_dmac);

    /*
     * initial queue head
     */
    init_waitqueue_head(&param.queue_cpu);

    /* initial work queue */
    INIT_WORK(&param.work_cpu, trans_cpu_wq);

    /*
     *  Start kthread
     */
    {
        struct task_struct *th=NULL;
        param.flag_kthread_exit=0;
        th = kthread_run(cycdmac_kthread, &param, "codec-vga-cycdmac");
        if (IS_ERR(th)) {
            _ERR("Unable to start kernel thread\n");
            ret = PTR_ERR(th);
            goto fail;
        }
        param.kth_cycdmac = th;
        _DEBUG("kernel thread is started\n");
    }



    /* complete */
    return 0;

 fail:


    /*
     *  Terminate kthread
     */
    if(NULL!=param.kth_cycdmac){
        param.flag_kthread_exit=1;
        kthread_stop(param.kth_cycdmac);
        param.kth_cycdmac = NULL;
        param.flag_kthread_exit=0;
    }

    /*
     * parameters depend on local FB
     */
    for(n=0; n<CVGA_FB_NUM; n++){

        struct codec_vga_fb_par *par = &param.fb[n].par;
        struct fb_info *info = &param.fb[n].info;

        /* fb_info */
        {
            /* cmap */
            if(flag_alloc_cmap[n])
                fb_dealloc_cmap( &info->cmap);

            if(flag_reg_fb[n])
                unregister_framebuffer(info);
         }

        /* local FB parameters */
        if (NULL != par->virt_addr ){
            struct page *map, *mapend;
            map = virt_to_page((unsigned long)par->virt_addr);
            mapend = virt_to_page((unsigned long)par->virt_addr + (CVGA_VRAM_SIZE - 1));
            while(map <= mapend) {
                clear_bit(PG_reserved, &((map)->flags));
                map++;
            }
            free_pages((unsigned long)par->virt_addr, get_order(CVGA_VRAM_SIZE));
            par->virt_addr = NULL;
        }

    } 


    /* CODEC-FPGA VGA buffer */
    if(NULL != param.vgabuff_rsc){
        release_mem_region(param.vgabuff_paddr, CVGA_VGA_SIZE);
        param.vgabuff_rsc = NULL;
    }
    if(NULL != (void*)param.vgabuff_addr){
        iounmap((void*)param.vgabuff_addr);
        param.vgabuff_addr = 0;
    }
    param.vgabuff_paddr=0;

    /* CODEC-FPGA sprite buffes */
    for(n=0; n<CVGA_SPRITE_NUM; n++){
        if ( NULL != param.spbuff_rsc[n]){
            release_mem_region(param.spbuff_paddr[n],CVGA_SPRITE_BUFF_SIZE);
            param.spbuff_rsc[n] = NULL;
        }
        if ( NULL != (void*)param.spbuff_addr[n]){
            iounmap((void*)param.spbuff_addr[n]);
            param.spbuff_addr[n] = 0;
        }
    }

    /* CODEC-FPGA VGA registers */
    if(NULL != param.reg_rsc){
        release_mem_region(param.reg_paddr,param.reg_size);
        param.reg_rsc = NULL;
    }
    if(NULL != (void*)param.reg_addr){
        iounmap((void*)param.reg_addr);
        param.reg_addr = 0;
    }
    param.reg_paddr = 0;

    return ret;
}


/*
 *  Cleanup
 */
static int __exit codec_vga_remove(struct platform_device *dev)
{
    int n;

    /*
     *  Terminate kthread
     */
    param.flag_kthread_exit=1;
    kthread_stop(param.kth_cycdmac);
    param.kth_cycdmac = NULL;
    param.flag_kthread_exit=0;

    /*
     * parameters depend on local FB
     */
    for(n=0; n<CVGA_FB_NUM; n++){

        struct codec_vga_fb_par *par = &param.fb[n].par;
        struct fb_info *info = &param.fb[n].info;

        /* fb_info */
        {
            /* cmap */
            fb_dealloc_cmap( &info->cmap);

            /* ungegister FB driver */
            unregister_framebuffer(info);
        }

        /* local FB parameters */
        {
            struct page *map, *mapend;
            map = virt_to_page((unsigned long)par->virt_addr);
            mapend = virt_to_page((unsigned long)par->virt_addr + (CVGA_VRAM_SIZE - 1));
            while(map <= mapend) {
                clear_bit(PG_reserved, &((map)->flags));
                map++;
            }
            free_pages((unsigned long)par->virt_addr, get_order(CVGA_VRAM_SIZE));
            par->virt_addr = NULL;
        }

    } 


    /* CODEC-FPGA VGA buffer */
    release_mem_region(param.vgabuff_paddr, CVGA_VGA_SIZE);
    param.vgabuff_rsc = NULL;
    iounmap((void*)param.vgabuff_addr);
    param.vgabuff_addr = 0;
    param.vgabuff_paddr = 0;

    /* CODEC-FPGA sprite buffes */
    for(n=0; n<CVGA_SPRITE_NUM; n++){
        release_mem_region(param.spbuff_paddr[n],CVGA_SPRITE_BUFF_SIZE);
        param.spbuff_rsc[n] = NULL;
        iounmap((void*)param.spbuff_addr[n]);
        param.spbuff_addr[n] = 0;
        param.spbuff_paddr[n] = 0;
    }

    /* CODEC-FPGA VGA registers */
    release_mem_region(param.reg_paddr,param.reg_size);
    param.reg_rsc = NULL;
    iounmap((void*)param.reg_addr);
    param.reg_addr = 0;
    param.reg_size = 0;

    return 0;
}



MODULE_ALIAS("platform:codec_vga");
static struct platform_driver codec_vga_driver = {
	.remove = __exit_p(codec_vga_remove),
	.driver = {
		.name = "codec_vga",
		.owner = THIS_MODULE,
	},
};


/**
 *  inititalize this module
 */
static int __init init_codec_vga(void)
{
    int retval;

    /* Initial Message */
	_INFO("CODEC-VGA Frame Buffer Driver (Ver.%s)\n", CODEC_VGA_VER);
	_DEBUG("compiled "__DATE__" "__TIME__"\n");

    /* probing device */
    retval = platform_driver_probe(&codec_vga_driver, codec_vga_probe);
    if(retval<0)
        _ERR("probing device is failed. retval=%d @ %s(%d).\n",
             retval, __FILE__, __LINE__);

    /* complete */
    return retval;
}

/*
 *  Cleanup
 */
static void __exit exit_codec_vga(void)
{
	platform_driver_unregister(&codec_vga_driver);
	_INFO("CODEC-VGA FB driver cleanup\n");
}


MODULE_DESCRIPTION("CODEC-VGA FB driver");
MODULE_LICENSE("GPL");

module_init(init_codec_vga);
module_exit(exit_codec_vga);



