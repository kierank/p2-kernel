/**
 * proxy.c -- Proxy Codec Card Driver
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 **/


//DCM_MOD_ID:029 2004.10.07 Y.Takano
#ifdef DUMMY_CARDMGR
	# undef MODULE
#endif

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/uaccess.h>

#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ds.h>

#include <proxy/proxy.h>
#include "proxydrv.h"
#include "flash.h"
#include "debug.h"

#line __LINE__ "proxydrv.c" /* Replace full path(__FILE__) to proxydrv.c. */

#ifdef FLASH_SYSTEM
//flash.c
extern void proxy_read_value_from_flash(proxy_nvram_value_t *);
#endif //FLASH_SYSTEM

#ifdef NVRAM_SYSTEM
//nvram.c
extern void proxy_read_value_from_nvram(proxy_nvram_value_t *);
extern void proxy_check_invalid_value(proxy_nvram_value_t *);
#endif //NVRAM_SYSTEM

//Module parameters
MODULE_AUTHOR("Panasonic");
MODULE_DESCRIPTION("Panasonic Proxy codec card driver");
MODULE_LICENSE("GPL");

//Tentative Implementation for Flash Memory Backup 2006-JUL-08

static int proxy_major = PROXY_MAJOR;
module_param(proxy_major, int, S_IRUGO | S_IWUSR);

//Newer, simpler way of listing specific interrupts
static int irq_list[4] = { -1 };

static proxy_dev_t proxy_dev_table[PROXY_MAX_DEV];
static proxy_dev_t *proxy_dev_p[PROXY_MAX_DEV];

spinlock_t proxy_dev_p_lock = SPIN_LOCK_UNLOCKED;

DECLARE_WAIT_QUEUE_HEAD(proxy_card_status_wq);
static unsigned char proxy_card_status = 0;
spinlock_t proxy_card_status_lock = SPIN_LOCK_UNLOCKED;

static void proxy_release(struct pcmcia_device *link);

static int proxy_get_status(void *arg)
{
	int ret;
	unsigned long flags;

	PRINT_FUNC;

	spin_lock_irqsave(&proxy_card_status_lock, flags);
	ret = __put_user(proxy_card_status, (unsigned char *)arg);
	if(ret){
		PERROR("failed __put_user");
		spin_unlock_irqrestore(&proxy_card_status_lock, flags);
		return -EFAULT;
	}
	spin_unlock_irqrestore(&proxy_card_status_lock, flags);

	return 0;

}//proxy_get_status

static int proxy_check_status(void *arg)
{
	int ret;
	unsigned char last_card_status;
	wait_queue_t wait;
	unsigned long flags;

	PRINT_FUNC;

	//expand on interruptible_sleep_on() ----->

	//SLEEP_ON_VAR
	init_waitqueue_entry(&wait, current);

	current->state = TASK_INTERRUPTIBLE;

	//SLEEP_ON_HEAD
	add_wait_queue(&proxy_card_status_wq, &wait);

	ret = __get_user(last_card_status, (unsigned char *)arg);
	if(ret){
		PERROR("failed __get_user");

		//SLEEP_ON_TAIL
		remove_wait_queue(&proxy_card_status_wq, &wait);
		return -EFAULT;
	}

	if(last_card_status == proxy_card_status){
		schedule();
	}

	//SLEEP_ON_TAIL
	remove_wait_queue(&proxy_card_status_wq, &wait);

	//<----- expand on interruptible_sleep_on()

	spin_lock_irqsave(&proxy_card_status_lock, flags);
	ret = __put_user(proxy_card_status, (unsigned char *)arg);
	if(ret){
		PERROR("failed __put_user");
		spin_unlock_irqrestore(&proxy_card_status_lock, flags);
		return -EFAULT;
	}
	spin_unlock_irqrestore(&proxy_card_status_lock, flags);

	return 0;

}//proxy_check_status

static int proxy_check_irq(proxy_dev_t **dev_p, void *arg)
{
	int ret;
	proxy_dev_t *dev;
	wait_queue_t wait;
	unsigned long flags;

	PRINT_FUNC;

	spin_lock_irqsave(&proxy_dev_p_lock, flags);
	dev = *dev_p;
	if(!dev){
		spin_unlock_irqrestore(&proxy_dev_p_lock, flags);
		PERROR("dev is null");
		return -EFAULT;
	}
	spin_unlock(&proxy_dev_p_lock);

	//expand on interruptible_sleep_on() ----->

	//SLEEP_ON_VAR
	init_waitqueue_entry(&wait, current);

	current->state = TASK_INTERRUPTIBLE;

	//SLEEP_ON_HEAD
	add_wait_queue(&dev->intr_wq, &wait);

	if(!dev->intr_status){
		schedule();
	}

	//SLEEP_ON_TAIL
	remove_wait_queue(&dev->intr_wq, &wait);

	//<----- expand on interruptible_sleep_on()
	spin_lock_irq(&dev->dev_lock);
	ret = __put_user(dev->intr_status, (unsigned char *)arg);
	if(ret){
		spin_unlock_irqrestore(&dev->dev_lock, flags);
		PERROR("failed __put_user");
		return -EFAULT;
	}

	dev->intr_status &= ~(PROXY_IRQ_FRV_ERR | PROXY_IRQ_FWRITE_OK | PROXY_IRQ_FWRITE_NG);
	spin_unlock_irqrestore(&dev->dev_lock, flags);

	return 0;

}//proxy_check_irq

static int proxy_read_common(proxy_dev_t *dev, unsigned char addr, unsigned char *r_data)
{
	PRINT_FUNC;

	if(!dev->common_base){
		PERROR("not set dev->common_base");
		return -EFAULT;
	}

	*r_data = ioread8(dev->common_base + addr);

	PINFO("read address = 0x%x", addr);
	PINFO("read data    = 0x%x", *r_data);

	return 0;
}

static int proxy_write_common(proxy_dev_t *dev, unsigned char addr, unsigned char w_data)
{
	PRINT_FUNC;

	if(!dev->common_base){
		PERROR("not set dev->common_base");
		return -EFAULT;
	}

	iowrite8(w_data, dev->common_base + addr);

	PINFO("write address = 0x%x", addr);
	PINFO("write data    = 0x%x", w_data);

	return 0;
}

static int proxy_read_common_to_user(proxy_dev_t **dev_p, void *arg)
{
	int ret;
	struct proxy_indcmn_arg indcmn;
	proxy_dev_t *dev;
	unsigned long flags;

	PRINT_FUNC;

	ret = __copy_from_user(&indcmn, arg, sizeof(struct proxy_indcmn_arg));
	if(ret){
		PERROR("failed __copy_from_user");
		return ret;
	}

	if((u_char)indcmn.addr > 0x0f){
		PERROR("invalid address");
		return -EFAULT;
	}

	spin_lock_irqsave(&proxy_dev_p_lock, flags);
	dev = *dev_p;
	if(!dev){
		spin_unlock_irqrestore(&proxy_dev_p_lock, flags);
		return -EFAULT;
	}
	spin_unlock(&proxy_dev_p_lock);

	spin_lock_irq(&dev->dev_lock);
	ret = proxy_read_common(dev, indcmn.addr, &indcmn.data);
	spin_unlock_irqrestore(&dev->dev_lock, flags);
	if(ret){
		return ret;
	}

	ret = __copy_to_user(arg, &indcmn, sizeof(struct proxy_indcmn_arg));
	if(ret){
		PERROR("failed __copy_to_user");
		return ret;
	}

	return 0;

}//proxy_read_common_to_user

static int proxy_write_common_from_user(proxy_dev_t **dev_p, const void *arg)
{
	int ret;
	struct proxy_indcmn_arg indcmn;
	proxy_dev_t *dev;
	unsigned long flags;

	PRINT_FUNC;

	ret = __copy_from_user(&indcmn, arg, sizeof(struct proxy_indcmn_arg));
	if(ret){
		PERROR("failed __copy_from_user");
		return ret;
	}

	if((u_char)indcmn.addr > 0x0f){
		PERROR("invalid address");
		return -EFAULT;
	}

	spin_lock_irqsave(&proxy_dev_p_lock, flags);
	dev = *dev_p;
	if(!dev){
		spin_unlock_irqrestore(&proxy_dev_p_lock, flags);
		return -EFAULT;
	}
	spin_unlock(&proxy_dev_p_lock);

	spin_lock_irq(&dev->dev_lock);
	ret = proxy_write_common(dev, indcmn.addr, indcmn.data);
	spin_unlock_irqrestore(&dev->dev_lock, flags);
	if(ret){
		return ret;
	}

	return 0;

}//proxy_write_common_from_user

static int proxy_read_icommon_to_user(proxy_dev_t **dev_p, void *arg)
{
	int ret;
	struct proxy_indcmn_arg indcmn;
	proxy_dev_t *dev;
	unsigned long flags;

	PRINT_FUNC;

	ret = __copy_from_user(&indcmn, arg, sizeof(struct proxy_indcmn_arg));
	if(ret){
		PERROR("failed __copy_from_user");
		return ret;
	}

	if((u_char)indcmn.addr > 0x30){
		PERROR("invalid address");
		return -EFAULT;
	}

	spin_lock_irqsave(&proxy_dev_p_lock, flags);
	dev = *dev_p;
	if(!dev){
		spin_unlock_irqrestore(&proxy_dev_p_lock, flags);
		return -EFAULT;
	}
	spin_unlock(&proxy_dev_p_lock);

	spin_lock_irq(&dev->dev_lock);
	ret = proxy_write_common(dev, CISREG_ICTRL0, ICTRL0_COMMON);
	if(ret){
		spin_unlock_irqrestore(&dev->dev_lock, flags);
		return ret;
	}

	ret = proxy_write_common(dev, CISREG_IADDR0, indcmn.addr);
	if(ret){
		spin_unlock_irqrestore(&dev->dev_lock, flags);
		return ret;
	}

	ret = proxy_read_common(dev, CISREG_IDATA0, &indcmn.data);
	if(ret){
		spin_unlock_irqrestore(&dev->dev_lock, flags);
		return ret;
	}
	spin_unlock_irqrestore(&dev->dev_lock, flags);

	ret = __copy_to_user(arg, &indcmn, sizeof(struct proxy_indcmn_arg));
	if(ret){
		PERROR("failed __copy_to_user");
		return ret;
	}

	return 0;

}//proxy_read_icommon_to_user

static int proxy_write_icommon_from_user(proxy_dev_t **dev_p, const void *arg)
{
	int ret;
	struct proxy_indcmn_arg indcmn;
	proxy_dev_t *dev;
	unsigned long flags;

	PRINT_FUNC;

	ret = __copy_from_user(&indcmn, arg, sizeof(struct proxy_indcmn_arg));
	if(ret){
		PERROR("failed __copy_from_user");
		return ret;
	}

	if((u_char)indcmn.addr > 0x30){
		PERROR("invalid address");
		return -EFAULT;
	}

	spin_lock_irqsave(&proxy_dev_p_lock, flags);
	dev = *dev_p;
	if(!dev){
		spin_unlock_irqrestore(&proxy_dev_p_lock, flags);
		return -EFAULT;
	}
	spin_unlock(&proxy_dev_p_lock);

	spin_lock_irq(&dev->dev_lock);
	ret = proxy_write_common(dev, CISREG_ICTRL0, ICTRL0_COMMON);
	if(ret){
		spin_unlock_irqrestore(&dev->dev_lock, flags);
		return ret;
	}
	ret = proxy_write_common(dev, CISREG_IADDR0, indcmn.addr);
	if(ret){
		spin_unlock_irqrestore(&dev->dev_lock, flags);
		return ret;
	}
	ret = proxy_write_common(dev, CISREG_IDATA0, indcmn.data);
	if(ret){
		spin_unlock_irqrestore(&dev->dev_lock, flags);
		return ret;
	}
	spin_unlock_irqrestore(&dev->dev_lock, flags);
	return 0;

}//proxy_write_icommon_from_user

static irqreturn_t proxy_interrupt(int irq, void *dev_id)
{
	unsigned char stat = 0;
	unsigned long flags;
	proxy_dev_t **dev_p = (proxy_dev_t **)dev_id;
	proxy_dev_t *dev;

	PRINT_FUNC;

	if(!dev_id)
		return IRQ_NONE;

	spin_lock_irqsave(&proxy_dev_p_lock, flags);
	dev = *dev_p;
	if(!dev){
		PINFO("dev == NULL");
		spin_unlock_irqrestore(&proxy_dev_p_lock, flags);
		return IRQ_NONE;
	}

	spin_unlock(&proxy_dev_p_lock);
	PINFO("dev = 0x%p", dev);

	//get interrupt status
	spin_lock_irq(&dev->dev_lock);

	proxy_write_common(dev, CISREG_ICTRL0, ICTRL0_COMMON);
	proxy_write_common(dev, CISREG_IADDR0, PROXY_IND_FRVERR);
	proxy_read_common(dev, CISREG_IDATA0, &stat);

	PINFO("interrupt status = 0x%x", stat);
	
	if(stat & (PROXY_IRQ_FRV_ERR | PROXY_IRQ_FWRITE_OK | PROXY_IRQ_FWRITE_NG)){
		PINFO("detect card interruption");

		//clear interrupt status
		proxy_write_common(dev, CISREG_IDATA0, stat);

		dev->intr_status |= stat;
		spin_unlock_irqrestore(&dev->dev_lock, flags);

		wake_up_interruptible(&dev->intr_wq);
	}
	else{
		spin_unlock_irqrestore(&dev->dev_lock, flags);
	}

	return IRQ_HANDLED;
}

static int proxy_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	unsigned long flags;
	proxy_dev_t *dev;
	proxy_dev_t **dev_p;

	PRINT_FUNC;

	dev_p = (proxy_dev_t **)filp->private_data;

	switch(cmd){

	case PROXYIOC_ZV_ENABLE:
		PINFO("PROXYIOC_ZV_ENABLE");
		spin_lock_irqsave(&proxy_dev_p_lock, flags);
		dev = *dev_p;
		if(!dev){
			PINFO("dev == NULL");
			spin_unlock_irqrestore(&proxy_dev_p_lock, flags);
			return -EFAULT;
		}
		spin_unlock(&proxy_dev_p_lock);

		spin_lock_irq(&dev->dev_lock);
		ret = proxy_write_common(dev, CISREG_ICTRL0, ICTRL0_COMMON);
		if(ret){
			spin_unlock_irqrestore(&dev->dev_lock, flags);
			break;
		}

		ret = proxy_write_common(dev, CISREG_IADDR0, PROXY_IND_ZV_ENABLE);
		if(ret){
			spin_unlock_irqrestore(&dev->dev_lock, flags);
			break;
		}

		ret = proxy_write_common(dev, CISREG_IDATA0, 0x80);
		spin_unlock_irqrestore(&dev->dev_lock, flags);
		break;

	case PROXYIOC_ZV_DISABLE:
		PINFO("PROXYIOC_ZV_DISABLE");
		spin_lock_irqsave(&proxy_dev_p_lock, flags);
		dev = *dev_p;
		if(!dev){
			PINFO("dev == NULL");
			spin_unlock_irqrestore(&proxy_dev_p_lock, flags);
			return -EFAULT;
		}
		spin_unlock(&proxy_dev_p_lock);

		spin_lock_irq(&dev->dev_lock);
		ret = proxy_write_common(dev, CISREG_ICTRL0, ICTRL0_COMMON);
		if(ret){
			spin_unlock_irqrestore(&dev->dev_lock, flags);
			break;
		}

		ret = proxy_write_common(dev, CISREG_IADDR0, PROXY_IND_ZV_ENABLE);
		if(ret){
			spin_unlock_irqrestore(&dev->dev_lock, flags);
			break;
		}

		ret = proxy_write_common(dev, CISREG_IDATA0, 0x00);
		spin_unlock_irqrestore(&dev->dev_lock, flags);
		break;

	case PROXYIOC_RD_ICMNAREA:
		PINFO("PROXYIOC_RD_ICMNAREA");
		ret = proxy_read_icommon_to_user(dev_p, (void *)arg);
		break;

	case PROXYIOC_WR_ICMNAREA:
		PINFO("PROXYIOC_WR_ICMNAREA");
		ret = proxy_write_icommon_from_user(dev_p, (void *)arg);
		break;

	case PROXYIOC_RD_CMNAREA:
		PINFO("PROXYIOC_RD_CMNAREA");
		ret = proxy_read_common_to_user(dev_p, (void *)arg);
		break;

	case PROXYIOC_WR_CMNAREA:
		PINFO("PROXYIOC_WR_CMNAREA");
		ret = proxy_write_common_from_user(dev_p, (void *)arg);
		break;

	case PROXYIOC_GET_STATUS:
		PINFO("PROXYIOC_GET_STATUS");
		ret = proxy_get_status((void *)arg);
		break;

	case PROXYIOC_CHK_STATUS:
		PINFO("PROXYIOC_CHK_STATUS");
		ret = proxy_check_status((void *)arg);
		break;

	case PROXYIOC_CHK_IRQ:
		PINFO("PROXYIOC_CHK_IRQ");
		ret = proxy_check_irq(dev_p, (void *)arg);
		break;

	case PROXYIOC_AWAKE_STATUS:
		PINFO("PROXYIOC_AWAKE_STATUS");
		wake_up_interruptible(&proxy_card_status_wq);
		break;

	case PROXYIOC_AWAKE_IRQ:
		PINFO("PROXYIOC_AWAKE_IRQ");
		spin_lock_irqsave(&proxy_dev_p_lock, flags);
		dev = *dev_p;
		if(!dev){
			PINFO("dev == NULL");
			spin_unlock_irqrestore(&proxy_dev_p_lock, flags);
			return -EFAULT;
		}
		spin_unlock(&proxy_dev_p_lock);

		spin_lock_irq(&dev->dev_lock);
		wake_up_interruptible(&dev->intr_wq);
		spin_unlock_irqrestore(&dev->dev_lock, flags);
		break;

#ifdef PROXY_DEBUG
	case PROXYIOC_HARDRESET:
		PINFO("PROXYIOC_HARDRESET");
		break;
#endif//PROXY_DEBUG

	default:
		PERROR("ioctl unknown error");
		return -ENOTTY;
	}

	return ret;
}

static int proxy_open(struct inode *inode, struct file *filp)
{
	int dev_no = iminor(inode);

	PRINT_FUNC;

	if(dev_no >= PROXY_MAX_DEV){
		PERROR("device not found");
		return -ENODEV;
	}

	if(!filp->private_data){
		PINFO("proxy_dev_p[%d] = 0x%p", dev_no, proxy_dev_p[dev_no]);
		filp->private_data = &proxy_dev_p[dev_no];
	}

	return 0;
}

static int proxy_close(struct inode *inode, struct file *filp)
{
	PRINT_FUNC;

	if(filp->private_data){
		filp->private_data = NULL;
	}

	return 0;
}

//file operations structure
static struct file_operations proxy_fops = {
	open	: proxy_open,
	release	: proxy_close,
	ioctl	: proxy_ioctl,
};

#ifdef DEBUG_PROXY_SET 
static void proxy_set_default_value(proxy_nvram_value_t *val)
{
	PINFO("Set tentative initial values !!DEBUG USE ONLY!!");

	val->service_mode = 0x00;
	val->file_format = 0x01;
	val->video_codec = 0x00;
	val->video_profile = 0xd8;
	val->video_gvop = 0x0000;
	val->video_packet = 0x0000;
	val->video_vbvsize = 0x00;
	val->audio_sample = 0x02;
	val->audio_codec = 0x01;
	val->meta_size = 92;
	val->FRAME_RATE.frame_rate = 0x00;
	val->REC_RATE.video_rate = 0x05dc;
	val->REC_RATE.audio_ch = 0x02;
	val->REC_RATE.audio_rate = 0x01;
	val->REC_RATE.audio_chsel = 0x08;
	val->TC_SUPER.tc_super_on = 0x00;
	val->TC_SUPER.super_vposi = 0x00;
	val->TC_SUPER.super_hposi = 0x00;
}
#endif//DEBUG_PROXY_SET 

static void proxy_init_card_reg(proxy_dev_t *dev)
{
	proxy_nvram_value_t nvr;

	PRINT_FUNC;

	memset(&nvr, 0, sizeof(proxy_nvram_value_t));

#ifdef DEBUG_PROXY_SET
	proxy_set_default_value(&nvr);
#endif//DEBUG_PROXY_SET 

#ifdef NVRAM_SYSTEM
	proxy_read_value_from_nvram(&nvr);
	proxy_check_invalid_value(&nvr); 
#endif//NVRAM_SYSTEM

#ifdef FLASH_SYSTEM
	proxy_read_value_from_flash(&nvr);
#endif//FLASH_SYSTEM

	PINFO("##### SETTING VALUES #####");
	iowrite8(ICTRL0_COMMON, dev->common_base + CISREG_ICTRL0);

	PINFO("file_format   = 0x%02x", nvr.file_format);
	iowrite8(PROXY_IND_FILE_FORMAT, dev->common_base + CISREG_IADDR0);
	iowrite8(nvr.file_format, dev->common_base + CISREG_IDATA0);

	PINFO("video_codec   = 0x%02x", nvr.video_codec);
	iowrite8(PROXY_IND_VIDEO_CODEC, dev->common_base + CISREG_IADDR0);
	iowrite8(nvr.video_codec, dev->common_base + CISREG_IDATA0);

	PINFO("video_profile = 0x%02x", nvr.video_profile);
	iowrite8(PROXY_IND_VIDEO_PROFILE, dev->common_base + CISREG_IADDR0);
	iowrite8(nvr.video_profile, dev->common_base + CISREG_IDATA0);

	PINFO("video_gvop    = 0x%04x", nvr.video_gvop);
	iowrite8(PROXY_IND_VIDEO_GVOP_L, dev->common_base + CISREG_IADDR0);
	iowrite8(nvr.video_gvop & 0x00ff, dev->common_base + CISREG_IDATA0);
	iowrite8(PROXY_IND_VIDEO_GVOP_H, dev->common_base + CISREG_IADDR0);
	iowrite8(nvr.video_gvop >> 8, dev->common_base + CISREG_IDATA0);

	PINFO("video_packet  = 0x%04x", nvr.video_packet);
	iowrite8(PROXY_IND_VIDEO_PACKET_L, dev->common_base + CISREG_IADDR0);
	iowrite8(nvr.video_packet & 0x00ff, dev->common_base + CISREG_IDATA0);
	iowrite8(PROXY_IND_VIDEO_PACKET_H, dev->common_base + CISREG_IADDR0);
	iowrite8(nvr.video_packet >> 8, dev->common_base + CISREG_IDATA0);

	PINFO("video_vbvsize = 0x%02x", nvr.video_vbvsize);
	iowrite8(PROXY_IND_VIDEO_VBVSIZE, dev->common_base + CISREG_IADDR0);
	iowrite8(nvr.video_vbvsize, dev->common_base + CISREG_IDATA0);

	PINFO("audio_sample  = 0x%02x", nvr.audio_sample);
	iowrite8(PROXY_IND_AUDIO_SAMPLE, dev->common_base + CISREG_IADDR0);
	iowrite8(nvr.audio_sample, dev->common_base + CISREG_IDATA0);

	PINFO("audio_codec   = 0x%02x", nvr.audio_codec);
	iowrite8(PROXY_IND_AUDIO_CODEC, dev->common_base + CISREG_IADDR0);
	iowrite8(nvr.audio_codec, dev->common_base + CISREG_IDATA0);

	PINFO("meta_size     = 0x%04x", nvr.meta_size);
	iowrite8(PROXY_IND_META_SIZE_L, dev->common_base + CISREG_IADDR0);
	iowrite8(nvr.meta_size & 0x00ff, dev->common_base + CISREG_IDATA0);
	iowrite8(PROXY_IND_META_SIZE_H, dev->common_base + CISREG_IADDR0);
	iowrite8(nvr.meta_size >> 8, dev->common_base + CISREG_IDATA0);

	PINFO("----- FRAME_RATE -----");
	PINFO("frame_rate    = 0x%02x", nvr.FRAME_RATE.frame_rate);
	iowrite8(PROXY_IND_FRAME_RATE, dev->common_base + CISREG_IADDR0);
	iowrite8(nvr.FRAME_RATE.frame_rate, dev->common_base + CISREG_IDATA0);

	PINFO("----- REC_RATE -----");
	PINFO("video_rate    = 0x%04x", nvr.REC_RATE.video_rate);
	iowrite8(PROXY_IND_VIDEO_RATE_L, dev->common_base + CISREG_IADDR0);
	iowrite8(nvr.REC_RATE.video_rate & 0x00ff, dev->common_base + CISREG_IDATA0);
	iowrite8(PROXY_IND_VIDEO_RATE_H, dev->common_base + CISREG_IADDR0);
	iowrite8(nvr.REC_RATE.video_rate >> 8, dev->common_base + CISREG_IDATA0);

	//Add following setting for P2HD cameras to support AVC-I
	//Default setting = 1 (0x01) 2007/Apr/11 Panasonic
	PINFO("switch to frame reference      = 0x01  [0:Refer reg  1:Count frame pluse]");
	iowrite8(PROXY_IND_FRAME_REFERENCE, dev->common_base + CISREG_IADDR0);
	iowrite8((unsigned char)0x01, dev->common_base + CISREG_IDATA0);

	PINFO("audio_ch      = 0x%02x", nvr.REC_RATE.audio_ch);
	iowrite8(PROXY_IND_AUDIO_CH, dev->common_base + CISREG_IADDR0);
	iowrite8(nvr.REC_RATE.audio_ch, dev->common_base + CISREG_IDATA0);

	PINFO("audio_rate    = 0x%02x", nvr.REC_RATE.audio_rate);
	iowrite8(PROXY_IND_AUDIO_RATE, dev->common_base + CISREG_IADDR0);
	iowrite8(nvr.REC_RATE.audio_rate, dev->common_base + CISREG_IDATA0);

	PINFO("audio_chsel   = 0x%02x", nvr.REC_RATE.audio_chsel);
	iowrite8(PROXY_IND_AUDIO_CHSEL, dev->common_base + CISREG_IADDR0);
	iowrite8(nvr.REC_RATE.audio_chsel, dev->common_base + CISREG_IDATA0);

	PINFO("----- TC_SUPER -----");
	PINFO("tc_super_on   = 0x%02x", nvr.TC_SUPER.tc_super_on);
	iowrite8(PROXY_IND_TC_SUPER_ON, dev->common_base + CISREG_IADDR0);
	iowrite8(nvr.TC_SUPER.tc_super_on, dev->common_base + CISREG_IDATA0);

	PINFO("super_vposi   = 0x%02x", nvr.TC_SUPER.super_vposi);
	iowrite8(PROXY_IND_SUPER_VPOSI, dev->common_base + CISREG_IADDR0);
	iowrite8(nvr.TC_SUPER.super_vposi, dev->common_base + CISREG_IDATA0);

	PINFO("super_hposi   = 0x%02x", nvr.TC_SUPER.super_hposi);
	iowrite8(PROXY_IND_SUPER_HPOSI, dev->common_base + CISREG_IADDR0);
	iowrite8(nvr.TC_SUPER.super_hposi, dev->common_base + CISREG_IDATA0);

	//FR-V Reset
	PINFO("FR-V Reset");
	iowrite8(PROXY_IND_FRV_RST, dev->common_base + CISREG_IADDR0);
	iowrite8(0x80, dev->common_base + CISREG_IDATA0);

	//FR-V Unreset
	PINFO("FR-V Unreset");
	iowrite8(PROXY_IND_FRV_RST, dev->common_base + CISREG_IADDR0);
	iowrite8(0x00, dev->common_base + CISREG_IDATA0);

	//ZV Enable
	PINFO("service_mode  = 0x%02x", nvr.service_mode);
	iowrite8(PROXY_IND_ZV_ENABLE, dev->common_base + CISREG_IADDR0);
	iowrite8(0x80 | nvr.service_mode, dev->common_base + CISREG_IDATA0);

	PINFO("[proxy] ZV Port enabled.");
}

/*
static void proxy_cs_error(client_handle_t handle, int func, int ret)
{
	error_info_t err = {
		func, ret
	};

	PRINT_FUNC;

	CardServices(ReportError, handle, &err);

}//proxy_cs_error
*/

struct yenta_socket {
	struct pci_dev *dev;
	int cb_irq, io_irq;
	void __iomem *base;
	struct timer_list poll_timer;

	struct pcmcia_socket socket;
	struct cardbus_type *type;

	u32 flags;

	/* for PCI interrupt probing */
	unsigned int probe_status;

	/* A few words of private data for special stuff of overrides... */
	unsigned int private[8];

	/* PCI saved state */
	u32 saved_state[2];
};

#define RL5C4XX_MISC_CONTROL	0x2F /* 8 bit */
#define RL5C4XX_ZV_ENABLE	0x08

static inline u8 config_readb(struct yenta_socket *socket, unsigned offset)
{
	u8 val;
	pci_read_config_byte(socket->dev, offset, &val);
	return val;
}

static inline void config_writeb(struct yenta_socket *socket, unsigned offset, u8 val)
{
	pci_write_config_byte(socket->dev, offset, val);
}

#define RICOH_MISC_CONTROL	0xA0
#define RICOH_ZV_ENABLE		(1 << 10)

static void ricoh_set_zv(struct pcmcia_socket *sock, int onoff)
{
	u8 reg;
	u16 reg2;
	struct yenta_socket *socket = container_of(sock, struct yenta_socket, socket);

	reg = readb(socket->base + 0x800 + RL5C4XX_MISC_CONTROL);

	pci_bus_read_config_word(socket->dev->bus, socket->dev->devfn, RICOH_MISC_CONTROL, &reg2);
	if (onoff){
		/* Zoom zoom, we will all go together, zoom zoom, zoom zoom */
		reg |=  RL5C4XX_ZV_ENABLE;
		reg2 |= RICOH_ZV_ENABLE;
	}
	else{
		reg &= ~RL5C4XX_ZV_ENABLE;
		reg2 &= ~RICOH_ZV_ENABLE;
	}

	writeb(reg, socket->base + 0x800 + RL5C4XX_MISC_CONTROL);
	pci_bus_write_config_word(socket->dev->bus, socket->dev->devfn, RICOH_MISC_CONTROL, reg2);
}

static inline int hwif_pci_to_id(struct pci_dev *pci)
{
	int devfn;

	if(pci == NULL){
		PERROR("pci is null");
		return -EFAULT;
	}

	devfn = pci->devfn;

	switch(devfn){
	case 144:	return 0;
	case 145:	return 1;
	case 152:	return 2;
	case 153:	return 3;
	case 160:	return 4;
	case 161:	return 5;
	}
	PINFO("unknown device");
	return -EINVAL;
}

#define CS_CHECK(fn, ret) \
do { last_fn = (fn); if((last_ret = (ret)) != 0) goto cs_failed; } while (0)

#define CS_CHECK_UNLOCK(fn, ret) \
do { last_fn = (fn); if((last_ret = (ret)) != 0){ \
	spin_unlock_irqrestore(&proxy_dev_p_lock, flags); \
	goto cs_failed;} } while (0)

static int proxy_config(struct pcmcia_device *link)
{
	int i;
/*
	client_handle_t handle = link->handle;
*/
	int last_fn, last_ret;
	int dev_no;
	proxy_dev_t *dev;
	tuple_t tuple;
	u_char buf[64];
	cisparse_t parse;
	win_req_t req;
	memreq_t mem;
	proxy_info_t *proxy_info = link->priv;

	unsigned long flags;
/*
	//DCM_MOD_ID:049 2004.11.04 Y.Takano
#ifdef DUMMY_CARDMGR
	int first = 0;
#endif
*/
	PRINT_FUNC;

	for(i = 0; i < PROXY_MAX_DEV; i++){
		if(proxy_dev_table[i].link == link)
			break;
	}

	if(i == PROXY_MAX_DEV){
		PERROR("device not found");
		proxy_release(link);
		return -ENODEV;
	}

	dev = &proxy_dev_table[i];

	PINFO("call GetDeviceNumber");
	dev_no = hwif_pci_to_id(link->socket->cb_dev);
	if(dev_no < 0){
		last_ret = dev_no;
		 goto cs_failed;
	}
	PINFO(" GetDeviceNumber: dev_no = %d", dev_no);

	tuple.DesiredTuple = CISTPL_CONFIG;
	tuple.Attributes = 0;
	tuple.TupleData = buf;
	tuple.TupleDataMax = sizeof(buf);
	tuple.TupleOffset = 0;

	PINFO("get CISTPL_CONFIG");
	CS_CHECK(GetFirstTuple, pcmcia_get_first_tuple(link, &tuple));
	CS_CHECK(GetTupleData, pcmcia_get_tuple_data(link, &tuple));
	CS_CHECK(ParseTuple, pcmcia_parse_tuple(link, &tuple, &parse));

	link->conf.ConfigBase = parse.config.base;
	link->conf.Present = parse.config.rmask[0];

	//Configure card
	//link->state |= DEV_CONFIG;

	//Scan the CIS for configuration table entries.
	tuple.DesiredTuple = CISTPL_CFTABLE_ENTRY;
	CS_CHECK(GetFirstTuple, pcmcia_get_first_tuple(link, &tuple));
	CS_CHECK(GetTupleData, pcmcia_get_tuple_data(link, &tuple));
	CS_CHECK(ParseTuple, pcmcia_parse_tuple(link, &tuple, &parse));

	link->conf.ConfigIndex = parse.cftable_entry.index;

/*
	//DCM_MOD_ID:050 2004.11.04 Y.Takano
#ifdef DUMMY_CARDMGR
	PINFO("call GetFirstDetectStatus");
	CS_CHECK(GetFirstDetectStatus, handle, &first);
	PINFO("GetFirstDetectStatus succeed: first = %d", first);
	//spin_lock_irqsave(&dev->dev_lock, flags);
	if(first){
		for (i = 0; i < PROXY_MAX_DEV; i++){
			if(proxy_dev_table[i].dev_status & PROXY_FIRST_DETECT){
				break;
			}
		}
		if(i == PROXY_MAX_DEV){
			PINFO("first Detection");
			dev->dev_status |= PROXY_FIRST_DETECT;
		}
	}

	if(!(dev->dev_status & PROXY_FIRST_DETECT)){
		//DCM_MOD_ID:051 2004.11.04 Y.Takano
		PINFO("Not first Detection");
		//spin_unlock_irqrestore(&dev->dev_lock, flags);
		//This actually configures the PCMCIA socket
		PINFO("call RequestConfiguration");
		CS_CHECK(RequestConfiguration, handle, &link->conf);

		sprintf(proxy_info->node.dev_name, PROXY_DEVICE_NAME);
		proxy_info->node.major = PROXY_MAJOR;
		proxy_info->node.minor = dev_no;
		link->dev = &proxy_info->node;

		link->state &= ~DEV_CONFIG_PENDING;

		PINFO("EXIT");

		return;
	}
	//spin_unlock_irqrestore(&dev->dev_lock, flags);
#endif//DUMMY_CARDMGR
*/
	//Maps a window of card memory into system memory
	req.Attributes = WIN_MEMORY_TYPE_CM | WIN_DATA_WIDTH_8 | WIN_ENABLE;
	req.Base = 0;
	req.Size = 0;
	req.AccessSpeed = 100;

	PINFO("call RequestWindow");
	CS_CHECK(RequestWindow, pcmcia_request_window(&link, &req, &link->win));

	PINFO("RequestWindow: Base = %lx", req.Base);
	PINFO("RequestWindow: Size = %x", req.Size);
	PINFO("RequestWindow: AccessSpeed = %x", req.AccessSpeed);

	/*
	 * Sets the address of card memory that is mapped to the base of
	 * a memory window to CardOffset
	 */

	mem.CardOffset = 0;
	mem.Page = 0;

	PINFO("call MapMemPage");
	CS_CHECK(MapMemPage, pcmcia_map_mem_page(link->win, &mem));

	PINFO("MapMemPage: mem.CardOffset = %x", mem.CardOffset);

	spin_lock_irqsave(&proxy_dev_p_lock, flags);

	proxy_dev_p[dev_no] = dev;
	PINFO("proxy_dev_p[%d] = 0x%p", dev_no, dev);

	//Allocate an interrupt line.
	link->irq.Instance = &proxy_dev_p[dev_no];
	PINFO("call RequestIRQ");
	CS_CHECK_UNLOCK(RequestIRQ, pcmcia_request_irq(link, &link->irq));
	PINFO("irq = %d", link->irq.AssignedIRQ);

	spin_unlock(&proxy_dev_p_lock);

	spin_lock_irq(&dev->dev_lock);

	//This actually configures the PCMCIA socket
	PINFO("call RequestConfiguration");
	CS_CHECK_UNLOCK(RequestConfiguration, pcmcia_request_configuration(link, &link->conf));

	//ZV Enable
	PINFO("call ZVEnable (ZV Port enable)");
	ricoh_set_zv(link->socket, 1);

	dev->dev_status |= PROXY_RICOH_ZV_ENA;

	dev->common_base = (u_char *)ioremap_nocache(req.Base, req.Size);
	if(!dev->common_base){
		PERROR("cannot ioremap!");
		last_ret = -ENOMEM;
		goto cs_failed;
	}

	proxy_init_card_reg(dev);

	sprintf(proxy_info->node.dev_name, PROXY_DEVICE_NAME);
	proxy_info->node.major = PROXY_MAJOR;
	proxy_info->node.minor = dev_no;
	link->dev_node = &proxy_info->node;

	//link->state &= ~DEV_CONFIG_PENDING;

	spin_unlock(&dev->dev_lock);

	spin_lock_irq(&proxy_card_status_lock);

	proxy_card_status |= (1 << dev_no);

	spin_unlock_irqrestore(&proxy_card_status_lock, flags);

	wake_up_interruptible(&proxy_card_status_wq);

	return 0;

cs_failed:
	PINFO("cs_failed");
	spin_lock_irqsave(&dev->dev_lock, flags);
	dev->dev_status &= ~(PROXY_FIRST_DETECT | PROXY_RICOH_ZV_ENA);
	spin_unlock_irqrestore(&dev->dev_lock, flags);
	//proxy_cs_error(handle, last_fn, last_ret);

	proxy_release(link);

	//link->state &= ~DEV_CONFIG_PENDING;

	return last_ret;
}

static void proxy_release(struct pcmcia_device *link)
{
	int ret;
	proxy_dev_t *dev;
	//unsigned long flags;

	PRINT_FUNC;

	dev = ((proxy_info_t *)(link->priv))->dev;

/*
#ifdef DUMMY_CARDMGR
	//spin_lock_irqsave(&dev->dev_lock, flags);
	if(dev->dev_status & PROXY_RICOH_ZV_ENA){
		//ZV Disable
		PINFO("call ZVDisable(Ricoh)");
		CardServices(ZVEnable, link->handle, 0);
		printk("<1>[proxy] ZV Port disabled.\n");
		dev->dev_status &= ~(PROXY_FIRST_DETECT | PROXY_RICOH_ZV_ENA);

		//spin_unlock_irqrestore(&dev->dev_lock, flags);
		wake_up_interruptible(&dev->intr_wq);
	}
	else{
		//spin_unlock_irqrestore(&dev->dev_lock, flags);
	}
#else
	//ZV Disable
	PINFO("call ZVDisable(Ricoh)");
	CardServices(ZVEnable, link->handle, 0);
	printk("<1>[proxy] ZV Port disabled.\n");
	wake_up_interruptible(&dev->intr_wq);
#endif
*/
	ricoh_set_zv(link->socket, 0);

	//Unlink the device chain
//link->dev = NULL;

	//Don't bother checking to see if these succeed or not
	if(link->win){
		ret = pcmcia_release_window(link->win);
		if(ret != CS_SUCCESS)
			PWARNING("ReleaseWindow(local->amem) ret = %x", ret);

		iounmap((void *)dev->common_base);
		dev->common_base = NULL;
		link->win = NULL;
	}

	pcmcia_disable_device(link);
}

static int proxy_probe(struct pcmcia_device *link)
{
	int i;
	int dev_no;
	proxy_dev_t *dev;
	proxy_info_t *proxy_info;
/*
	client_reg_t client_reg;
	int ret;
*/
	unsigned long flags;

	PRINT_FUNC;

	for(i = 0; i < PROXY_MAX_DEV; i++){
		if(proxy_dev_table[i].link == NULL)
			break;
	}

	if(i == PROXY_MAX_DEV){
		PWARNING(": all devices in use");
		return -ENODEV;
	}

	dev = &proxy_dev_table[i];

	dev_no = hwif_pci_to_id(link->socket->cb_dev);

	if(dev_no < 4)
		return -EINVAL;

	//Allocate space for private device-specific data
	proxy_info = kmalloc(sizeof(proxy_info_t), GFP_KERNEL);
	if(!proxy_info){
		PERROR("kmalloc() failed");
		return -ENOMEM;
	}
	memset(proxy_info, 0, sizeof(proxy_info_t));

	proxy_info->link = link;
	link->priv = proxy_info;

	//Initialize the proxy_dev_t structure
	spin_lock_irqsave(&dev->dev_lock, flags);
	dev->link = link;
	dev->intr_status = 0;
	dev->common_base = NULL;
	spin_unlock_irqrestore(&dev->dev_lock, flags);
	proxy_info->dev = dev;
/*
	//Initialize the dev_link_t structure
	init_timer(&link->release);
	link->release.function = &proxy_release;
	link->release.data = (u_long)link;
*/
	//Interrupt setup
	link->irq.Attributes = IRQ_TYPE_EXCLUSIVE | IRQ_HANDLE_PRESENT | IRQ_TYPE_DYNAMIC_SHARING;
	link->irq.IRQInfo1 = IRQ_INFO2_VALID | IRQ_LEVEL_ID;

	if(irq_list[0] == -1) {
		link->irq.IRQInfo2 = 0xdeb8;
	}
	else{
		for(i = 0; i < 4; i++){
			link->irq.IRQInfo2 |= 1 << irq_list[i];
		}
	}

	link->irq.Handler = &proxy_interrupt;
	link->irq.Instance = &proxy_dev_p[dev_no];

	//General socket configuration defaults can go here
	link->conf.Attributes = CONF_ENABLE_IRQ;
	//link->conf.Vcc = 33;
	link->conf.IntType = INT_MEMORY | INT_ZOOMED_VIDEO;

	//Register with Card Services
	//link->next = dev_list;
	//dev_list = link;
/*
	client_reg.dev_info = &dev_info;
	client_reg.Attributes = INFO_MEM_CLIENT | INFO_CARD_SHARE;
	client_reg.EventMask = CS_EVENT_CARD_INSERTION | CS_EVENT_CARD_REMOVAL;
	client_reg.event_handler = &proxy_event;
	client_reg.Version = 0x0210;
	client_reg.event_callback_args.client_data = link;

	PINFO("call RegisterClient");
	ret = CardServices(RegisterClient, &link->handle, &client_reg);

	PINFO("return from RegisterClient");
	if(ret != CS_SUCCESS){
		PERROR("RegisterClient failed");
		proxy_cs_error(link->handle, RegisterClient, ret);
		proxy_detach(link);
		return NULL;
	}
*/
	return proxy_config(link);
}

static void proxy_remove(struct pcmcia_device *link)
{
	proxy_dev_t *dev;

	unsigned long flags;
	PRINT_FUNC;

	proxy_release(link);
/*
	dev_link_t **linkp;

	//Locate device structure
	for(linkp = &dev_list; *linkp; linkp = &(*linkp)->next){
		if(*linkp == link){
			break;
		}
	}
	if(*linkp == NULL){
		return;
	}
*/
	dev = ((proxy_info_t *)(link->priv))->dev;
/*
	//Break the link with Card Services
	if(link->handle){
		CardServices(DeregisterClient, link->handle);
	}
*/

	//Unlink device structure, and free it
	//*linkp = link->next;

	spin_lock_irqsave(&dev->dev_lock, flags);
	dev->link = NULL;
	spin_unlock_irqrestore(&dev->dev_lock, flags);

	//This points to the parent proxy_info_t struct
	kfree(link->priv);
	link->priv = NULL;
}

static int proxy_suspend(struct pcmcia_device *link)
{
	PRINT_FUNC;

	return 0;
}

static int proxy_resume(struct pcmcia_device *link)
{
	PRINT_FUNC;

	return 0;
}

static struct pcmcia_device_id proxy_ids[] = {
	PCMCIA_DEVICE_PROD_ID123("Panasonic", "AJ-YAX800", "Proxy Card", 0x89C87178, 0x4C922CC1, 0xCB83DABC),
	PCMCIA_DEVICE_NULL
};
MODULE_DEVICE_TABLE(pcmcia, proxy_ids);

static struct pcmcia_driver proxy_driver = {
	.owner		= THIS_MODULE,
	.drv		= {
		.name	= "proxy",
	},
	.probe		= proxy_probe,
	.remove		= proxy_remove,
	.id_table	= proxy_ids,
	.suspend	= proxy_suspend,
	.resume		= proxy_resume,
};

#ifdef DUMMY_CARDMGR
int proxy_init(void)
#else
static int __init proxy_init(void)
#endif
{
	int i;
	int ret;

	printk(KERN_INFO "[%s] Proxy codec card driver (Rev"PROXY_REVISION")\n", PROXY_DEVICE_NAME);

	for(i = 0; i < PROXY_MAX_DEV; i++){
		proxy_dev_table[i].link = NULL;
		proxy_dev_table[i].dev_status = 0;
		init_waitqueue_head(&proxy_dev_table[i].intr_wq);
		spin_lock_init(&proxy_dev_table[i].dev_lock);
	}

	ret = register_chrdev(proxy_major, PROXY_DEVICE_NAME, &proxy_fops);
	if(ret < 0){
		PERROR("can't get major %d", proxy_major);
		return ret;
	}

	if(proxy_major == 0){
		proxy_major = ret; /* dynamic allocation */
	}

	ret = pcmcia_register_driver(&proxy_driver);
	if(ret < 0){
		PERROR("pcmcia_register_driver error : %08x", ret);
		unregister_chrdev(proxy_major, PROXY_DEVICE_NAME);
	}

	return ret;
}

static void __exit proxy_cleanup(void)
{
	PRINT_FUNC;

	pcmcia_unregister_driver(&proxy_driver);
	unregister_chrdev(proxy_major, PROXY_DEVICE_NAME);
/*
	while (dev_list != NULL){
		del_timer(&dev_list->release);
		if(dev_list->state & DEV_CONFIG){
			PINFO("call proxy_release");
			proxy_release((u_long)dev_list);
		}
		PINFO("call proxy_detach");
		proxy_detach(dev_list);
	}
*/
}

#ifndef DUMMY_CARDMGR
module_init(proxy_init);
module_exit(proxy_cleanup);
#endif
