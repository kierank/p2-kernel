/* linux/drivers/spi/spi_gpio.c
 *
 * Copyright (c) 2010 Panasonic Co.,LTD.
 *
 * GPIO based SPI driver
 *
 */
/* $Id: spi_gpio.c 11201 2010-12-15 23:57:24Z Noguchi Isao $ */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>

#include <linux/gpio.h>

#include <linux/spi/spi.h>
#include <linux/spi/spi_bitbang.h>

#include "of_gpio_spi.h"


struct gpio_spi {
	struct spi_bitbang		 bitbang;

    u8 cs2gpio[128];
    unsigned int            port_clk;
    unsigned int            port_mosi; /* out */
    unsigned int            port_miso; /* in */

    struct gpio_spi_platform_data  *pdata;
};

static inline struct gpio_spi *spidev_to_sg(struct spi_device *spi)
{
	return spi_master_get_devdata(spi->master);
}

static inline void setsck(struct spi_device *dev, int on)
{
	struct gpio_spi *sg = spidev_to_sg(dev);
	gpio_set_value(sg->port_clk, on ? 1 : 0);
}

static inline void setmosi(struct spi_device *dev, int on)
{
	struct gpio_spi *sg = spidev_to_sg(dev);
	gpio_set_value(sg->port_mosi, on ? 1 : 0);
}

static inline u32 getmiso(struct spi_device *dev)
{
	struct gpio_spi *sg = spidev_to_sg(dev);
	return gpio_get_value(sg->port_miso) ? 1 : 0;
}

#define spidelay(x) ndelay(x)

#define	EXPAND_BITBANG_TXRX
#include <linux/spi/spi_bitbang.h>


static u32 gpio_spi_txrx_mode0(struct spi_device *spi,
				      unsigned nsecs, u32 word, u8 bits)
{
	return (spi->mode&SPI_LSB_FIRST)?
        bitbang_txrx_be_cpha0_lsb1st(spi, nsecs, 0, word, bits):
        bitbang_txrx_be_cpha0(spi, nsecs, 0, word, bits);
}

static u32 gpio_spi_txrx_mode1(struct spi_device *spi,
				      unsigned nsecs, u32 word, u8 bits)
{
	return (spi->mode&SPI_LSB_FIRST)?
        bitbang_txrx_be_cpha1_lsb1st(spi, nsecs, 0, word, bits):
        bitbang_txrx_be_cpha1(spi, nsecs, 0, word, bits);
}

static u32 gpio_spi_txrx_mode2(struct spi_device *spi,
				      unsigned nsecs, u32 word, u8 bits)
{
	return (spi->mode&SPI_LSB_FIRST)?
        bitbang_txrx_be_cpha0_lsb1st(spi, nsecs, 1, word, bits):
        bitbang_txrx_be_cpha0(spi, nsecs, 1, word, bits);
}

static u32 gpio_spi_txrx_mode3(struct spi_device *spi,
				      unsigned nsecs, u32 word, u8 bits)
{
	return (spi->mode&SPI_LSB_FIRST)?
        bitbang_txrx_be_cpha1_lsb1st(spi, nsecs, 1, word, bits):
        bitbang_txrx_be_cpha1(spi, nsecs, 1, word, bits);
}


static void gpio_spi_chipselect(struct spi_device *dev, int value)
{
	struct gpio_spi *sg = spidev_to_sg(dev);
	int pol = dev->mode & SPI_CS_HIGH ? 1 : 0;

    gpio_set_value(sg->cs2gpio[dev->chip_select], pol ? value : !value);
}

static int gpio_spi_probe(struct platform_device *dev)
{
	struct spi_master	*master=NULL;
	struct gpio_spi  *sp=NULL;
	struct gpio_spi_platform_data *pdata=NULL;
	int ret = 0;

	master = spi_alloc_master(&dev->dev, sizeof(struct gpio_spi));
	if (master == NULL) {
		dev_err(&dev->dev, "failed to allocate spi master\n");
		ret = -ENOMEM;
		goto err;
	}
    platform_set_drvdata(dev, master);

	/* copy in the plkatform data */
	pdata = dev->dev.platform_data;
	if (pdata == NULL) {
		ret = -ENODEV;
		goto err;
	}

    /*  */
	sp = spi_master_get_devdata(master);
	if (sp == NULL) {
		ret = -ENODEV;
		goto err;
	}
    memcpy(sp->cs2gpio,pdata->cs2gpio,sizeof(sp->cs2gpio));
    sp->port_clk = pdata->port_clk;
    sp->port_mosi = pdata->port_mosi;
    sp->port_miso = pdata->port_miso;
    if(pdata->od_mode){
        gpio_opendrain(sp->port_clk,1);
        gpio_opendrain(sp->port_mosi,1);
    }


	/* setup spi bitbang adaptor */
	sp->bitbang.master = spi_master_get(master);
	sp->bitbang.master->bus_num = pdata->bus_num;
	sp->bitbang.master->num_chipselect = pdata->max_chipselect;
	sp->bitbang.chipselect = gpio_spi_chipselect;
	sp->bitbang.txrx_word[SPI_MODE_0] = gpio_spi_txrx_mode0;
	sp->bitbang.txrx_word[SPI_MODE_1] = gpio_spi_txrx_mode1;
	sp->bitbang.txrx_word[SPI_MODE_2] = gpio_spi_txrx_mode2;
	sp->bitbang.txrx_word[SPI_MODE_3] = gpio_spi_txrx_mode3;

    /*  */
	ret = spi_bitbang_start(&sp->bitbang);
	if (ret)
		goto err_no_bitbang;

	return 0;

 err_no_bitbang:
	spi_master_put(sp->bitbang.master);
 err:
    if(master)
        kfree(master);
	return ret;

}

static int gpio_spi_remove(struct platform_device *dev)
{
	struct gpio_spi *sp = platform_get_drvdata(dev);

	spi_bitbang_stop(&sp->bitbang);
	spi_master_put(sp->bitbang.master);
    kfree(sp->bitbang.master);

	return 0;
}

MODULE_ALIAS("platform:gpio_spi");
static struct platform_driver gpio_spi_drv = {
	.probe		= gpio_spi_probe,
    .remove		= gpio_spi_remove,
    .driver		= {
		.name	= "gpio_spi",
		.owner	= THIS_MODULE,
    },
};

static int __init gpio_spi_init(void)
{
        return platform_driver_register(&gpio_spi_drv);
}

static void __exit gpio_spi_exit(void)
{
        platform_driver_unregister(&gpio_spi_drv);
}

module_init(gpio_spi_init);
module_exit(gpio_spi_exit);

MODULE_DESCRIPTION("SPI Driver by GPIO");
MODULE_AUTHOR("nobody");
MODULE_LICENSE("GPL");
