/*
 * include/linux/p2msudev_user.h
 */
/* $Id: p2msudev_user.h 5608 2010-03-09 08:03:31Z Noguchi Isao $ */

#ifndef __LINUX_P2MSUDEV_USER_H__
#define __LINUX_P2MSUDEV_USER_H__

#include <linux/ioctl.h> /* needed for the _IOW etc stuff used later */
#ifdef __KERNEL__
#include <linux/time.h>
#else  /* ! __KERNEL__ */
#include <sys/time.h>
#endif  /* __KERNEL__ */

/*
 *  structures
 */

/* for ioctl(P2MSUDEV_IOC_KEYEV_GETINFO) */
struct p2msudev_ioc_keyev_info {
    unsigned long   key_bit_pattern;
    unsigned long   key_sample_count;
    unsigned long   jiffies;
    struct timeval  tv;
}; 

/* for ioctl(P2MSUDEV_IOC_LED_GETVAL) ot ioctl(P2MSUDEV_IOC_LED_SETVAL) */
struct p2msudev_ioc_led_ctrl {
	unsigned char  no;
    unsigned char  bright;
    unsigned char  timing;
};

/* for ioctl(P2MSUDEV_IOC_BUZZER_GETVAL) ot ioctl(P2MSUDEV_IOC_BUZZER_SETVAL) */
struct p2msudev_ioc_buzzer_ctrl {
    int start;
    int repeat;
    int fmode;
    unsigned int beep_cnt;
    unsigned int silent_cnt;
};

/*
 * IOCTL command definitions
 */

/* Magic number */
#define P2MSUDEV_IOC_MAGIC  (0xD0)

/* ADC */
#define NR_P2MSUDEV_IOC_ADC_READ        0x00
#define NR_P2MSUDEV_IOC_ADC_RESET       0x01
#define NR_P2MSUDEV_IOC_ADC_RESET_CHECK 0x02   
#define P2MSUDEV_IOC_ADC_READ           _IO(P2MSUDEV_IOC_MAGIC, NR_P2MSUDEV_IOC_ADC_READ)
#define P2MSUDEV_IOC_ADC_RESET          _IO(P2MSUDEV_IOC_MAGIC, NR_P2MSUDEV_IOC_ADC_RESET)
#define P2MSUDEV_IOC_ADC_RESET_CHECK    _IO(P2MSUDEV_IOC_MAGIC, NR_P2MSUDEV_IOC_ADC_RESET_CHECK)

/* KEYEV */
#define NR_P2MSUDEV_IOC_KEYEV_CTRL         0x10
#define NR_P2MSUDEV_IOC_KEYEV_STATUS       0x11
#define NR_P2MSUDEV_IOC_KEYEV_GETINFO      0x12
#define NR_P2MSUDEV_IOC_KEYEV_SCAN         0x13
#define NR_P2MSUDEV_IOC_KEYEV_PERIOD       0x14
#define P2MSUDEV_IOC_KEYEV_CTRL         _IO(P2MSUDEV_IOC_MAGIC, NR_P2MSUDEV_IOC_KEYEV_CTRL)
#define P2MSUDEV_IOC_KEYEV_STATUS       _IOR(P2MSUDEV_IOC_MAGIC, NR_P2MSUDEV_IOC_KEYEV_STATUS, unsigned long)
#define P2MSUDEV_IOC_KEYEV_GETINFO(nr_buff) \
    _IOC( _IOC_READ,P2MSUDEV_IOC_MAGIC, NR_P2MSUDEV_IOC_KEYEV_GETINFO, \
         sizeof(struct p2msudev_ioc_keyev_info [nr_buff]) )
#define P2MSUDEV_IOC_KEYEV_SCAN         _IOR(P2MSUDEV_IOC_MAGIC, NR_P2MSUDEV_IOC_KEYEV_SCAN, unsigned long)
#define P2MSUDEV_IOC_KEYEV_PERIOD       _IOR(P2MSUDEV_IOC_MAGIC, NR_P2MSUDEV_IOC_KEYEV_PERIOD, unsigned long)

/*  LED */
#define NR_P2MSUDEV_IOC_LED_SETVAL           0x20
#define NR_P2MSUDEV_IOC_LED_GETVAL           0x21
#define P2MSUDEV_IOC_LED_SETVAL(nr_buff) \
    _IOC( _IOC_WRITE, P2MSUDEV_IOC_MAGIC, NR_P2MSUDEV_IOC_LED_SETVAL, \
         sizeof(struct p2msudev_ioc_led_ctrl [nr_buff]) )
#define P2MSUDEV_IOC_LED_GETVAL(nr_buff) \
    _IOC( _IOC_READ|_IOC_WRITE, P2MSUDEV_IOC_MAGIC, NR_P2MSUDEV_IOC_LED_GETVAL, \
          sizeof(struct p2msudev_ioc_led_ctrl [nr_buff]) )

/*  BUZZER */
#define NR_P2MSUDEV_IOC_BUZZER_SETVAL           0x30
#define NR_P2MSUDEV_IOC_BUZZER_GETVAL           0x31
#define P2MSUDEV_IOC_BUZZER_SETVAL \
    _IOC( _IOC_WRITE, P2MSUDEV_IOC_MAGIC, NR_P2MSUDEV_IOC_BUZZER_SETVAL, \
          sizeof(struct p2msudev_ioc_buzzer_ctrl) )
#define P2MSUDEV_IOC_BUZZER_GETVAL \
    _IOC( _IOC_READ|_IOC_WRITE, P2MSUDEV_IOC_MAGIC, NR_P2MSUDEV_IOC_BUZZER_GETVAL, \
          sizeof(struct p2msudev_ioc_buzzer_ctrl) )


/*
 *  macro definitions
 */

/* for ioctl(P2MSUDEV_IOC_ADC_READ) */
#define CH_P2MSUDEV_ADC_VREF            0		
#define CH_P2MSUDEV_ADC_ZEROPROTECT     1
#define	CH_P2MSUDEV_ADC_VATTERY         2
#define	CH_P2MSUDEV_ADC_TEMP            3
#define CH_P2MSUDEV_ADC_SSDTEMP         4

/* for ioctl(P2MSUDEV_IOC_KEYEV_CTRL) */
#define P2MSUDEV_IOC_KEYEV_CTRL_INIBUFF (1<<0)
#define P2MSUDEV_IOC_KEYEV_CTRL_CLRERR  (1<<1)
#define P2MSUDEV_IOC_KEYEV_CTRL_START   (1<<2)
#define P2MSUDEV_IOC_KEYEV_CTRL_STOP    (1<<3)

/* for ioctl(P2MSUDEV_IOC_KEYEV_STATUS) */
#define SHIFT_P2MSUDEV_IOC_KEYEV_STATUS_OVERRUN 16
#define P2MSUDEV_IOC_KEYEV_STATUS_OVERRUN  \
    (1<<SHIFT_P2MSUDEV_IOC_KEYEV_STATUS_OVERRUN) /* flag which shows overflow error */
#define SHIFT_P2MSUDEV_IOC_KEYEV_STATUS_UNDERRUN 17
#define P2MSUDEV_IOC_KEYEV_STATUS_UNDERRUN \
    (1<<SHIFT_P2MSUDEV_IOC_KEYEV_STATUS_UNDERRUN) /* flag which shows underflow error */
#define SHIFT_P2MSUDEV_IOC_KEYEV_STATUS_START 18
#define P2MSUDEV_IOC_KEYEV_STATUS_START    \
    (1<<SHIFT_P2MSUDEV_IOC_KEYEV_STATUS_START) /* flag which shows under operation */
#define P2MSUDEV_IOC_KEYEV_STATUS_NRBUFF   0x0000ffff /* mask for the number of input key buffer  */

/* Max value of argument of P2MSUDEV_IOC_KEYEV_GETINFO */
#define P2MSUDEV_NR_KEYEV_INFO 128

/* key bit patterns for ioctl(P2MSUDEV_IOC_KEYEV_GETINFO) */
#define P2MSUDEV_BIT_KEY_POWER  (1<<0)
#define	P2MSUDEV_BIT_KEY_UP     (1<<1)
#define P2MSUDEV_BIT_KEY_DOWN   (1<<2)
#define P2MSUDEV_BIT_KEY_LEFT   (1<<3)
#define P2MSUDEV_BIT_KEY_RIGHT  (1<<4)
#define P2MSUDEV_BIT_KEY_SET    (1<<5)
#define P2MSUDEV_BIT_KEY_EXIT   (1<<6)
#define P2MSUDEV_BIT_KEY_MENU   (1<<7)
#define	P2MSUDEV_BIT_KEY_START  (1<<8)
#define	P2MSUDEV_BIT_KEY_FUNC1  (1<<9)
#define	P2MSUDEV_BIT_KEY_FUNC2  (1<<10)
#define	P2MSUDEV_BIT_KEY_FUNC3  (1<<11)

/* LED number for ioctl(P2MSUDEV_IOC_LED_GETVAL) ot ioctl(P2MSUDEV_IOC_LED_SETVAL) */
#define P2MSUDEV_LED_LCD        0
#define P2MSUDEV_LED_POWER      1
#define P2MSUDEV_LED_P2CARD     2
#define P2MSUDEV_LED_SSD        3 /* old difinition */
#define P2MSUDEV_LED_HDD        3

/* Max value of argument of P2MSUDEV_IOC_LED_SETVAL() */
#define P2MSUDEV_NR_LED 4
#define P2MSUDEV_NR_LED_PARAM P2MSUDEV_NR_LED

#endif  /* __LINUX_P2MSUDEV_USER_H__ */
