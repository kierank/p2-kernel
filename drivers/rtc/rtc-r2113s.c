/*
 * A SPI driver for the Ricoh R2113S RTC
 *
 * Copyright (C) 2006 Atsushi Nemoto <anemo@mba.ocn.ne.jp>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * The board specific init code should provide characteristics of this
 * device:
 *     Mode 1 (High-Active, Shift-Then-Sample), High Avtive CS
 */
/* $Id: rtc-r2113s.c 6585 2010-04-22 04:28:32Z Noguchi Isao $ */

#include <linux/bcd.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/rtc.h>
#include <linux/rtc_user.h>
#include <linux/workqueue.h>
#include <linux/spi/spi.h>

#define DRV_VERSION "0.2"

#define R2113S_REG_SECS	0
#define R2113S_REG_MINS	1
#define R2113S_REG_HOURS	2
#define R2113S_REG_WDAY	3
#define R2113S_REG_DAY	4
#define R2113S_REG_MONTH	5
#define R2113S_REG_YEAR	6
#define R2113S_REG_CTL1	14
#define R2113S_REG_CTL2	15

#define R2113S_SECS_MASK	0x7f
#define R2113S_MINS_MASK	0x7f
#define R2113S_HOURS_MASK	0x3f
#define R2113S_WDAY_MASK	0x03
#define R2113S_DAY_MASK	0x3f
#define R2113S_MONTH_MASK	0x1f

#define R2113S_BIT_PM	0x20	/* REG_HOURS */
#define R2113S_BIT_Y2K	0x80	/* REG_MONTH */
#define R2113S_BIT_24H	0x20	/* REG_CTL1 */
#define R2113S_BIT_PON	0x10	/* REG_CTL2 */
#define R2113S_BIT_XSTP	0x20	/* REG_CTL2 */
#define R2113S_BIT_VDET	0x40	/* REG_CTL2 */
#define R2113S_BIT_VDSL	0x80	/* REG_CTL2 */

#define R2113S_CMD_W(addr)	(((addr) << 4) | 0x08)	/* single write */
#define R2113S_CMD_R(addr)	(((addr) << 4) | 0x0c)	/* single read */
#define R2113S_CMD_MW(addr)	(((addr) << 4) | 0x00)	/* burst write */
#define R2113S_CMD_MR(addr)	(((addr) << 4) | 0x04)	/* burst read */

struct r2113s_plat_data {
	struct rtc_device *rtc;
	int rtc_24h;
    unsigned long pon_stat; 
};

static int
r2113s_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct spi_device *spi = to_spi_device(dev);
	struct r2113s_plat_data *pdata = spi->dev.platform_data;
	u8 txbuf[5+7], *txp;
	int ret;

	/* Transfer 5 bytes before writing SEC.  This gives 31us for carry. */
	txp = txbuf;
	txbuf[0] = R2113S_CMD_R(R2113S_REG_CTL2); /* cmd, ctl2 */
	txbuf[1] = 0xff;	/* dummy */
	txbuf[2] = R2113S_CMD_R(R2113S_REG_CTL2); /* cmd, ctl2 */
	txbuf[3] = 0xff;	/* dummy */
	txbuf[4] = R2113S_CMD_MW(R2113S_REG_SECS); /* cmd, sec, ... */
	txp = &txbuf[5];
	txp[R2113S_REG_SECS] = BIN2BCD(tm->tm_sec);
	txp[R2113S_REG_MINS] = BIN2BCD(tm->tm_min);
	if (pdata->rtc_24h) {
		txp[R2113S_REG_HOURS] = BIN2BCD(tm->tm_hour);
	} else {
		/* hour 0 is AM12, noon is PM12 */
		txp[R2113S_REG_HOURS] = BIN2BCD((tm->tm_hour + 11) % 12 + 1) |
			(tm->tm_hour >= 12 ? R2113S_BIT_PM : 0);
	}
	txp[R2113S_REG_WDAY] = BIN2BCD(tm->tm_wday);
	txp[R2113S_REG_DAY] = BIN2BCD(tm->tm_mday);
	txp[R2113S_REG_MONTH] = BIN2BCD(tm->tm_mon + 1) |
		(tm->tm_year >= 100 ? R2113S_BIT_Y2K : 0);
	txp[R2113S_REG_YEAR] = BIN2BCD(tm->tm_year % 100);
	/* write in one transfer to avoid data inconsistency */
	ret = spi_write_then_read(spi, txbuf, sizeof(txbuf), NULL, 0);
	udelay(62);	/* Tcsr 62us */
	return ret;
}

static int
r2113s_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct spi_device *spi = to_spi_device(dev);
	struct r2113s_plat_data *pdata = spi->dev.platform_data;
	u8 txbuf[5], rxbuf[7];
	int ret;

	/* Transfer 5 byte befores reading SEC.  This gives 31us for carry. */
	txbuf[0] = R2113S_CMD_R(R2113S_REG_CTL2); /* cmd, ctl2 */
	txbuf[1] = 0xff;	/* dummy */
	txbuf[2] = R2113S_CMD_R(R2113S_REG_CTL2); /* cmd, ctl2 */
	txbuf[3] = 0xff;	/* dummy */
	txbuf[4] = R2113S_CMD_MR(R2113S_REG_SECS); /* cmd, sec, ... */
    memset(rxbuf,0xff,sizeof(rxbuf));

	/* read in one transfer to avoid data inconsistency */
	ret = spi_write_then_read(spi, txbuf, sizeof(txbuf),
				  rxbuf, sizeof(rxbuf));
	udelay(62);	/* Tcsr 62us */
	if (ret < 0)
		return ret;

	tm->tm_sec = BCD2BIN(rxbuf[R2113S_REG_SECS] & R2113S_SECS_MASK);
	tm->tm_min = BCD2BIN(rxbuf[R2113S_REG_MINS] & R2113S_MINS_MASK);
	if (pdata->rtc_24h) {
        tm->tm_hour = BCD2BIN(rxbuf[R2113S_REG_HOURS] & R2113S_HOURS_MASK);
	} else {
        tm->tm_hour = BCD2BIN((rxbuf[R2113S_REG_HOURS] & R2113S_HOURS_MASK)&~R2113S_BIT_PM);
		tm->tm_hour %= 12;
		if (rxbuf[R2113S_REG_HOURS] & R2113S_BIT_PM)
			tm->tm_hour += 12;
	}
	tm->tm_wday = BCD2BIN(rxbuf[R2113S_REG_WDAY] & R2113S_WDAY_MASK);
	tm->tm_mday = BCD2BIN(rxbuf[R2113S_REG_DAY] & R2113S_DAY_MASK);
	tm->tm_mon =
		BCD2BIN(rxbuf[R2113S_REG_MONTH] & R2113S_MONTH_MASK) - 1;
	/* year is 1900 + tm->tm_year */
	tm->tm_year = BCD2BIN(rxbuf[R2113S_REG_YEAR]) +
		((rxbuf[R2113S_REG_MONTH] & R2113S_BIT_Y2K) ? 100 : 0);

	if (rtc_valid_tm(tm) < 0) {
		dev_err(&spi->dev, "retrieved date/time is not valid.\n");
		rtc_time_to_tm(0, tm);
	}

	return 0;
}

static int
r2113s_ioctl(struct device *dev, unsigned int cmd, unsigned long arg)
{
    int retval = 0;
	struct spi_device *spi = to_spi_device(dev);
	struct r2113s_plat_data *pdata = spi->dev.platform_data;

    switch(cmd){

    case RTC_PON_STATUS:
        {
            unsigned long stat=0;
            if(pdata->pon_stat&R2113S_BIT_PON)
                stat |= RTC_STAT_ZERO_VOLT;
            if(pdata->pon_stat&R2113S_BIT_XSTP)
                stat |= RTC_STAT_OSC_STOP;
            if(pdata->pon_stat&R2113S_BIT_VDET)
                stat |= RTC_STAT_LOW_VOLT;
            if(copy_to_user((unsigned long __user *)arg,&stat,sizeof(unsigned long)))
                retval = -EFAULT;
        }
        break;

    case RTC_PON_CLEAR:
        {
            u8 buf[2];
            buf[0] = R2113S_CMD_W(R2113S_REG_CTL2);
            buf[1] = 0;
            retval = spi_write_then_read(spi, buf, sizeof(buf), NULL, 0);
            if (retval < 0)
                break;
            pdata->pon_stat = 0;
        }
        break;

    default:
        retval = -ENOIOCTLCMD;
    }

    return retval;
}

static const struct rtc_class_ops r2113s_rtc_ops = {
	.read_time	= r2113s_rtc_read_time,
	.set_time	= r2113s_rtc_set_time,
    .ioctl      = r2113s_ioctl,
};

static struct spi_driver r2113s_driver;

static int __devinit r2113s_probe(struct spi_device *spi)
{
	int ret=0;
	struct rtc_device *rtc;
	struct r2113s_plat_data *pdata=NULL;

    /* check spi device mode */
    if((spi->mode & (SPI_CPHA|SPI_CPOL)) != (SPI_CPHA|SPI_CPOL)){
		dev_err(&spi->dev, "mode setting error: NOT mode-2.\n");
        ret = -EINVAL;
        goto kfree_exit;
    }
    if(!(spi->mode & SPI_CS_HIGH)){
		dev_err(&spi->dev, "mode setting error: chipselect is NOT active high.\n");
        ret = -EINVAL;
        goto kfree_exit;
    }
    if(!(spi->mode & SPI_3WIRE)){
		dev_err(&spi->dev, "mode setting error: SI/SO signals is NOT shared.\n");
        ret = -EINVAL;
        goto kfree_exit;
    }
    if(spi->mode & SPI_LSB_FIRST){
		dev_err(&spi->dev, "mode setting error: not MSB first send/recieve.\n");
        ret = -EINVAL;
        goto kfree_exit;
    }

	pdata = kzalloc(sizeof(struct r2113s_plat_data), GFP_KERNEL);
	if (!pdata){
		dev_err(&spi->dev, "not enough memory.\n");
		ret = -ENOMEM;
        goto kfree_exit;
    }
	spi->dev.platform_data = pdata;

	/* Check D7 of SECOND register */
	ret = spi_w8r8(spi, R2113S_CMD_R(R2113S_REG_SECS));
	if (ret < 0 || (ret & 0x80)) {
		dev_err(&spi->dev, "not found.(%d)\n",ret);
		goto kfree_exit;
	}

	dev_info(&spi->dev, "chip found, driver version " DRV_VERSION "\n");
	dev_info(&spi->dev, "spiclk %u KHz.\n",
		 (spi->max_speed_hz + 500) / 1000);

	/* turn RTC on if it was not on */
	ret = spi_w8r8(spi, R2113S_CMD_R(R2113S_REG_CTL2));
	if (ret < 0)
		goto kfree_exit;
	pdata->pon_stat = ret & (R2113S_BIT_XSTP | R2113S_BIT_VDET | R2113S_BIT_PON);
	if (pdata->pon_stat) {
        //		u8 buf[2];
        //		struct rtc_time tm;
        if (pdata->pon_stat & R2113S_BIT_PON)
            dev_err(&spi->dev, "voltage-zero detected.\n");
        else {
            if (pdata->pon_stat & R2113S_BIT_VDET)
                dev_err(&spi->dev, "voltage-low detected.\n");
            if (pdata->pon_stat & R2113S_BIT_XSTP)
                dev_err(&spi->dev, "oscillator-stop detected.\n");
        }
/* 		rtc_time_to_tm(0, &tm);	/\* 1970/1/1 *\/ */
/* 		ret = r2113s_rtc_set_time(&spi->dev, &tm); */
/* 		if (ret < 0) */
/* 			goto kfree_exit; */
/* 		buf[0] = R2113S_CMD_W(R2113S_REG_CTL2); */
/* 		buf[1] = 0; */
/* 		ret = spi_write_then_read(spi, buf, sizeof(buf), NULL, 0); */
/* 		if (ret < 0) */
/* 			goto kfree_exit; */
	}

	ret = spi_w8r8(spi, R2113S_CMD_R(R2113S_REG_CTL1));
	if (ret < 0)
		goto kfree_exit;
	if (ret & R2113S_BIT_24H)
		pdata->rtc_24h = 1;

	rtc = rtc_device_register(r2113s_driver.driver.name, &spi->dev,
				  &r2113s_rtc_ops, THIS_MODULE);

	if (IS_ERR(rtc)) {
		ret = PTR_ERR(rtc);
		goto kfree_exit;
	}

	pdata->rtc = rtc;

	return 0;
 kfree_exit:
    if(NULL!=pdata)
        kfree(pdata);
	return ret;
}

static int __devexit r2113s_remove(struct spi_device *spi)
{
	struct r2113s_plat_data *pdata = spi->dev.platform_data;
	struct rtc_device *rtc = pdata->rtc;

	if (rtc)
		rtc_device_unregister(rtc);
	kfree(pdata);
	return 0;
}

static struct spi_driver r2113s_driver = {
	.driver = {
		.name	= "rtc-r2113s",
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
	},
	.probe	= r2113s_probe,
	.remove	= __devexit_p(r2113s_remove),
};

static __init int r2113s_init(void)
{
    int ret;
    pr_info("RICOH R2113S Real Time Clock driver\n");
    ret = spi_register_driver(&r2113s_driver);
    if(ret<0)
        pr_err("ERROR: can't register SPI-device driver of rtc-r2113s: return=%d\n",ret);
    return ret;
}

static __exit void r2113s_exit(void)
{
	spi_unregister_driver(&r2113s_driver);
}

module_init(r2113s_init);
module_exit(r2113s_exit);

//MODULE_AUTHOR("Atsushi Nemoto <anemo@mba.ocn.ne.jp>");
MODULE_DESCRIPTION("Ricoh R2113S RTC driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);
