#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/poll.h>
#include <linux/pci.h>
#include <linux/fb.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/ioctl.h>
#include <asm/uaccess.h>

#include <linux/zion.h>
#include "zion_vga_params.h"

#ifdef CONFIG_PPC_83xx
 typedef unsigned int ui32_t;
 #include <asm/dmac-ioctl.h>
 extern int MPC83xxDmacDirectMode(int, ui32_t, ui32_t, ui32_t, ui32_t);
#endif //CONFIG_PPC_83xx

/*
 *  If your driver supports multiple boards, you should make these arrays,
 *  or allocate them dynamically (using kmalloc()).
 */
struct zion_vga_info {
	struct fb_info fb_info;
	unsigned long ram_address; //virtual address
} zion_fb_info;

struct zion_vga_par{
	//empty
} zion_current_par;

static struct fb_var_screeninfo default_var;
static char zion_vga_name[16] = "ZION VGA FB";
/*
static int current_par_valid = 0;
*/

static unsigned long dma_direction[2];
static int direction_select = 0;

#ifdef CONFIG_ZION_RESOLUTION_SETUP
 static __u32 ZIONVGA_X_RES = CONFIG_ZION_X_RESOLUTION;
 static __u32 ZIONVGA_Y_RES = CONFIG_ZION_Y_RESOLUTION;
#else
 static __u32 ZIONVGA_X_RES = 640;
 static __u32 ZIONVGA_Y_RES = 480;
#endif //CONFIG_ZION_RESOLUTION_SETUP

/*------------------- chipset specific functions --------------------------*/

static int zionvga_encode_var(struct fb_var_screeninfo *var)
{
	/*
	 * Fill the 'var' structure based on the values in 'par' and maybe other
	 * values read out of the hardware.
	 */

	memset(var, 0, sizeof(struct fb_var_screeninfo));

	var->xres		= ZIONVGA_X_RES;
	var->yres		= ZIONVGA_Y_RES;
	var->xres_virtual	= ZIONVGA_X_RES;
	var->yres_virtual	= ZIONVGA_Y_RES;
	var->xoffset		= 0;
	var->yoffset		= 0;

	var->bits_per_pixel	= ZIONVGA_BITS_PER_PIXEL;
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

static int zionvga_set_par(struct fb_info *info)
{
	/*
	 * Set the hardware according to 'par'.
	 */
  
//	struct zion_vga_par *par = (struct zion_vga_par *)ptr;  

//	zion_current_par = *par;
//	current_par_valid = 1;
	//...
	return -EINVAL;
}

static int zionvga_encode_fix(struct fb_fix_screeninfo *fix)
{
	/*
	 * This function should fill in the 'fix' structure based on the values
	 * in the `par' structure.
	 */

	memset(fix, 0, sizeof(struct fb_fix_screeninfo));

	strcpy(fix->id, zion_vga_name);

	fix->smem_start		= zion_fb_info.ram_address;
	fix->smem_len		= ZIONVGA_VRAM_SIZE;
	fix->type 		= FB_TYPE_PACKED_PIXELS;
	fix->type_aux		= 0;
	fix->visual		= FB_VISUAL_TRUECOLOR;
	fix->xpanstep		= 0;
	fix->ypanstep		= 0;
	fix->ywrapstep		= 0;
	fix->line_length	= ZIONVGA_X_RES * ZIONVGA_BYTES_PER_PIXEL;
	fix->mmio_start		= 0;
	fix->mmio_len		= 0;
	fix->accel		= 0;

	return 0;
}

static int zionvga_setcolreg(unsigned regno, unsigned red, unsigned green,
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

/* ------------ Interfaces to hardware functions ------------*/
/*
struct fbgen_hwswitch zion_vga_switch = {
  encode_var : zionvga_encode_var,
  setcolreg : zionvga_setcolreg,
};

*/
static int zionvga_rstr_disable(void)
{
  u16 reg;
  zion_params_t *params = find_zion(0);
  if(params == NULL){
	return -ENODEV;
  }

  reg = mbus_readw(MBUS_ADDR(params,ZIONVGA_VGA_SETTING));
  reg &= ~VGA_RSTR_EN;
  mbus_writew(reg, MBUS_ADDR(params,ZIONVGA_VGA_SETTING));  
  reg = mbus_readw(MBUS_ADDR(params,ZIONVGA_VGA_SETTING)); //Just for assurance
  return 0;
}

static int zionvga_rstr_enable(void)
{
  u16 reg;
  zion_params_t *params = find_zion(0);
  if(params == NULL){
	return -ENODEV;
  }

  reg = mbus_readw(MBUS_ADDR(params,ZIONVGA_VGA_SETTING));
  reg |= VGA_RSTR_EN;
  mbus_writew(reg, MBUS_ADDR(params,ZIONVGA_VGA_SETTING));  
  reg = mbus_readw(MBUS_ADDR(params,ZIONVGA_VGA_SETTING)); //Just for assurance
  return 0;
}

static int zionvga_image_reflection(int frame)
{
  u16 reg;
  zion_params_t *params = find_zion(0);
  if(params == NULL){
	return -ENODEV;
  }

  reg = mbus_readw(MBUS_ADDR(params,ZIONVGA_VGA_SETTING));

  if(frame==0)
    {
      reg &= ~(VGA_SEL);
    }
  else
    {
      reg |= VGA_SEL;
    }

  //reg |= (VGA_RSTR_EN|CSC_EN|TRS_ON);
#if defined(CONFIG_P2PF_K240) || defined(CONFIG_P2PF_K202) || defined(CONFIG_P2PF_K246A)
  reg |= (CSC_EN);
#else
  reg |= (CSC_EN|TRS_ON);  //VGA_RSTR_EN is supporsed to be set by TX
#endif //CONFIG_P2PF_K240 || CONFIG_P2PF_K246A

  mbus_writew(reg, MBUS_ADDR(params,ZIONVGA_VGA_SETTING));
  
  //Just for assurance
  reg = mbus_readw(MBUS_ADDR(params,ZIONVGA_VGA_SETTING));

  return 0;
}

static int zionvga_start_dma(void)
{
	int ret = direction_select;
#ifdef CONFIG_PPC_83xx
	int ch = ZIONVGA_DMA_CH;
	ssize_t size = (unsigned long)(dma_direction[1])-(unsigned long)(dma_direction[0]);
	void *source = (void *)virt_to_bus((void *)(zion_fb_info.ram_address));
	void *direction = (void *)(dma_direction[direction_select]);
#endif
	int result = -1;

#ifdef CONFIG_PPC_83xx
	result = MPC83xxDmacDirectMode(ch, (unsigned int)source, (unsigned int)direction, size, DMAC_WRITE);
#endif
	if(result < 0){
		ret = result;
	}
	else{
#ifdef CONFIG_ZION_VGA_AUTOMATIC_UPDATE
		ret = zionvga_image_reflection(direction_select);
#endif // CONFIG_ZION_VGA_AUTOMATIC_UPDATE
	}

#ifdef CONFIG_ZION_VGA_DMA_DIRECTION_CHANGE
	direction_select = (direction_select+1) & (2-1);
#endif // CONFIG_ZION_VGA_DMA_DIRECTION_CHANGE
	return ret;
}

int zion_set_params(struct zionvga_reset_arg *reset_arg)
{
  u16 reg = 0;
  int pal_line = reset_arg->pal_line;
  int spl_line = reset_arg->spl_line;
  zion_params_t *params = find_zion(0);
  if(params == NULL){
	return -ENODEV;
  }

  reg = mbus_readw(MBUS_ADDR(params,ZIONVGA_VGA_SETTING));

  reg &= VGA_RSTR_EN;  //Don't touch VGA_RSTR_EN bit (It should be set by TX)

  if(pal_line==576)
    {
      reg |= PAL_LINE_SEL;
      ZIONVGA_Y_RES = 576;
    }
  else if(pal_line==480)
    {
      reg &= ~PAL_LINE_SEL;
      ZIONVGA_Y_RES = 480;
    }
  else
    {
      PERROR("Unsupported VGA SIZE : PAL-Line=%d",pal_line);
      return -EINVAL;
    }

  if(spl_line==720)
    {
      reg |= SPL_SEL;
      ZIONVGA_X_RES = 720;
    }
  else if(spl_line==640)
    {
      reg &= ~SPL_SEL;
      ZIONVGA_X_RES = 640;
    }
  else
    {
      PERROR("Unsupported VGA SIZE : SPL-Line=%d",spl_line);
      return -EINVAL;      
    }

#if defined(CONFIG_P2PF_K240) || defined(CONFIG_P2PF_K202) || defined(CONFIG_P2PF_K246A)
  reg |= (CSC_EN);
#else
  reg |= (CSC_EN|TRS_ON);
#endif //CONFIG_P2PF_K240 || CONFIG_P2PF_K246A

  mbus_writew(reg, MBUS_ADDR(params,ZIONVGA_VGA_SETTING));

  // Just for assurance
  reg = mbus_readw(MBUS_ADDR(params,ZIONVGA_VGA_SETTING));

  zionvga_encode_var( &default_var );
  zion_fb_info.fb_info.var = default_var;

  return 0;
}

static int zionvga_init_phase(struct zionvga_phase_arg *phase)
{
  zion_params_t *params = find_zion(0);
  unsigned short v_phase = phase->v_phase;
  unsigned short h_phase = phase->h_phase;
  if(params == NULL){
	return -ENODEV;
  }

  mbus_writew(v_phase, MBUS_ADDR(params, ZIONVGA_VIDEO_V_PHASE));
  mbus_writew(v_phase, MBUS_ADDR(params, ZIONVGA_SYSTEM_V_PHASE));
  mbus_writew(h_phase, MBUS_ADDR(params, ZIONVGA_VIDEO_H_PHASE));
  mbus_writew(h_phase, MBUS_ADDR(params, ZIONVGA_SYSTEM_H_PHASE));

  // Just for Assurance
  v_phase = mbus_readw(MBUS_ADDR(params, ZIONVGA_VIDEO_V_PHASE));
  v_phase = mbus_readw(MBUS_ADDR(params, ZIONVGA_SYSTEM_V_PHASE));
  h_phase = mbus_readw(MBUS_ADDR(params, ZIONVGA_VIDEO_H_PHASE));
  h_phase = mbus_readw(MBUS_ADDR(params, ZIONVGA_SYSTEM_H_PHASE));

  return 0;
}

static int zionvga_ioctl(struct fb_info *info, unsigned int cmd, unsigned long arg)
{
  struct zionvga_init_arg init_arg;
  struct zionvga_reset_arg reset_arg;
  struct zionvga_phase_arg phase_arg;
  u16 color;
  u16 *pixel_p;
  unsigned long i;
  int ret, frame;

  switch(cmd){

  case ZIONVGA_IOC_HARDRESET:
    if (copy_from_user(&reset_arg, (void *)arg, sizeof(struct zionvga_reset_arg)))
      {
	PERROR("failed copy_from_user()\n");
	return -EFAULT;
      }
    PERROR("IOCTL HARDRESET\n");

    return zion_set_params(&reset_arg);

  case ZIONVGA_IOC_RSTR_STOP:
    return zionvga_rstr_disable();

  case ZIONVGA_IOC_RSTR_START:
    return zionvga_rstr_enable();

  case ZIONVGA_IOC_INIT_FB:
    PDEBUG("IOCTL INIT_FB\n");
    if (!access_ok(VERIFY_READ, (void *)arg,
		   sizeof(struct zionvga_init_arg))) {
      PERROR("failed access_ok()\n");
      return -EFAULT;
    }
    if (copy_from_user(&init_arg, (void *)arg,
		       sizeof(struct zionvga_init_arg))) {
      PERROR("failed copy_from_user()\n");
      return -EFAULT;
    }

    color = ((init_arg.red & 0x1f) << 11) | ((init_arg.green & 0x3f) << 5)
      | (init_arg.blue & 0x1f);
    pixel_p = (u16 *)zion_fb_info.ram_address;

    for (i = 0; i < ZIONVGA_VRAM_SIZE; i += ZIONVGA_BYTES_PER_PIXEL) {
      *pixel_p = color;
      pixel_p++;
    }

    return 0;

  case ZIONVGA_IOC_PURGE_FB:
    PDEBUG("IOCTL PURGE_FB\n");

    ret = zionvga_start_dma();
    if(ret < 0){
      return ret;
    }
    else{
      if(copy_to_user((void*)arg, &ret, sizeof(int)))
        {
          PERROR("failed copy_to_user()\n");
          return -EFAULT;
        }
      return 0;
    }

  case ZIONVGA_IOC_UPDATE:
    if (copy_from_user(&frame, (void *)arg, sizeof(int)))
      {
	PERROR("failed copy_from_user()\n");
	return -EFAULT;
      }

    return zionvga_image_reflection(frame);

  case ZIONVGA_IOC_INIT_PHASE:
    if(copy_from_user(&phase_arg, (void *)arg, sizeof(struct zionvga_phase_arg)))
      {
	PERROR("failed copy_from_user()\n");
	return -EFAULT;
      }

    return zionvga_init_phase(&phase_arg);

	default:
		PERROR("invalid argument\n");
		return -EINVAL;
	}
}

static int zionvga_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	struct page *map, *mapend;

	map = virt_to_page(zion_fb_info.ram_address);
	mapend = virt_to_page(zion_fb_info.ram_address + (ZIONVGA_VRAM_SIZE - 1));

	while(map <= mapend) {
		set_bit(PG_reserved, &((map)->flags));
		map++;
	}

	vma->vm_flags |= VM_RESERVED;

	if(remap_pfn_range(vma, vma->vm_start,
			(__pa(zion_fb_info.ram_address) >> PAGE_SHIFT),
			vma->vm_end - vma->vm_start, vma->vm_page_prot)){
		PERROR("failed remap_page_range()\n");
		return -EAGAIN;
	}

	return 0;
}

static int zionvga_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	return -EINVAL;
}

static struct fb_ops zion_vga_ops = {
	owner		: THIS_MODULE,
	fb_check_var	: zionvga_check_var,
	fb_set_par	: zionvga_set_par,
	fb_setcolreg	: zionvga_setcolreg,
#ifdef CONFIG_FB_CFB_FILLRECT
	fb_fillrect	: cfb_fillrect,
#endif //CONFIG_FB_CFB_FILLRECT
#ifdef CONFIG_FB_CFB_COPYAREA
	fb_copyarea	: cfb_copyarea,
#endif //CONFIG_FB_CFB_COPYAREA
#ifdef CONFIG_FB_CFB_IMAGEBLIT
	fb_imageblit	: cfb_imageblit,
#endif //CONFIG_FB_CFB_IMAGEBLIT
//	fb_cursor	: soft_cursor,
	fb_ioctl	: zionvga_ioctl,
	fb_mmap		: zionvga_mmap, 
};

int init_zion_vga(void)
{
	int ret;
	zion_params_t *params = find_zion(0);
	u32 off_addr; 

	if(params == NULL){
		return -ENODEV;
	}

	PINFO("ZION VGA Frame Buffer Driver Installed.\n");
	PDEBUG("compiled "__DATE__" "__TIME__"\n");

	//Check Fireball
	if((params->revision & 0xF000) == 0xF000){
		PINFO("This is fireball! ZION VGA cannot be used!!\n");
		return 0;
	}

#ifdef CONFIG_ZION_FB_SETUP

	PINFO("Setting FB Address ... %p and %p\n",
		(void *)CONFIG_ZION_FB_FIRST_ADDR, (void *)CONFIG_ZION_FB_SECOND_ADDR);
	mbus_writel(CONFIG_ZION_FB_FIRST_ADDR, MBUS_ADDR(params,ZIONVGA_BUFFER_BASE_ADDRESS_0));
	mbus_writel(CONFIG_ZION_FB_SECOND_ADDR, MBUS_ADDR(params,ZIONVGA_BUFFER_BASE_ADDRESS_1));

#endif //CONFIG_ZION_FB_SETUP

	//Get VGA FB Region
	off_addr = mbus_readl(MBUS_ADDR(params, ZIONVGA_BUFFER_BASE_ADDRESS_0));
	dma_direction[0] = params->whole_sdram_addr + off_addr;

	PINFO("FB1_BUS_ADDR:%p\n",(void *)dma_direction[0]);

	off_addr = mbus_readl(MBUS_ADDR(params, ZIONVGA_BUFFER_BASE_ADDRESS_1));
	dma_direction[1] =  params->whole_sdram_addr + off_addr;

	PINFO("FB2_BUS_ADDR:%p\n",(void *)dma_direction[1]);

	memset(&zion_fb_info, 0, sizeof(struct zion_vga_info));

	zion_fb_info.ram_address = (unsigned long)__get_free_pages(GFP_KERNEL | __GFP_DMA, ZIONVGA_VRAM_ORDER);

	if(!zion_fb_info.ram_address){
		PERROR("failed get_free_pages(): ram_address\n");
		ret = -ENOMEM;
		goto fail_gfp_fb;
	}

	memset((void *)zion_fb_info.ram_address, 0, ZIONVGA_VRAM_SIZE);

	PDEBUG("ram_address = 0x%08lx (virtual)\n", (unsigned long)zion_fb_info.ram_address);
	PDEBUG("ram_address = 0x%08lx (physical)\n", virt_to_bus((void *)zion_fb_info.ram_address));

	zion_fb_info.fb_info.screen_base = (void __iomem *)zion_fb_info.ram_address;
	zion_fb_info.fb_info.fbops = &zion_vga_ops;

	//setup var_screen_info
	zionvga_encode_var(&default_var);
	zion_fb_info.fb_info.var = default_var;

	zionvga_encode_fix(&zion_fb_info.fb_info.fix);
	zion_fb_info.fb_info.par = &zion_current_par;

	// This should give a reasonable default video mode
	ret = fb_alloc_cmap(&zion_fb_info.fb_info.cmap, 256, 0);
	if(ret){
		PERROR( "failed fb_alloc_cmap()\n" );
		ret = -ENOMEM;
		goto fail_alloc_cmap;
	}

	if(register_framebuffer(&zion_fb_info.fb_info) < 0){ 
		PERROR("failed register_framebuffer()\n");
		ret = -EINVAL;
		goto fail_reg_fb;
	}

	PINFO("fb%d: %s frame buffer device\n",	zion_fb_info.fb_info.node, zion_fb_info.fb_info.fix.id);

	return 0;

fail_reg_fb:
	fb_dealloc_cmap( &zion_fb_info.fb_info.cmap);

fail_alloc_cmap:
	free_pages(zion_fb_info.ram_address, ZIONVGA_VRAM_ORDER);
  
fail_gfp_fb:
	return ret;
}


/*
 *  Cleanup
 */
void exit_zion_vga(void)
{
	zion_params_t *params = find_zion(0);

	//Check Fireball
	if((params->revision & 0xF000) == 0xF000){
		return;
	}

	/*
	 * If your driver supports multiple boards, you should unregister and
	 * clean up all instances.
	 */
	unregister_framebuffer(&zion_fb_info.fb_info);

	free_pages(zion_fb_info.ram_address, ZIONVGA_VRAM_ORDER);

	fb_dealloc_cmap(&zion_fb_info.fb_info.cmap);

	PINFO("ZION VGA cleanup\n");
}

