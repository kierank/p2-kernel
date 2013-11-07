/*
 * linux/drivers/spi/of_gpio_spi.h
 *
 * Copyright 2010 Panasonic, Inc
 *
 */
/* $Id: of_gpio_spi.h 11201 2010-12-15 23:57:24Z Noguchi Isao $ */

#ifndef __OF_GPIO_SPI_H__
#define __OF_GPIO_SPI_H__

#include <linux/types.h>
#include <linux/gpio.h>

struct gpio_spi_platform_data {
	u16	bus_num;
	bool	od_mode;            /* 2010/1/14, added by Panasonic */
	u16	max_chipselect;
    u8 cs2gpio[128];
    unsigned int            port_clk;
    unsigned int            port_mosi; /* out */
    unsigned int            port_miso; /* in */

/*     struct gpio_spi_platform_data *pdata; */
};

#endif /* __OF_GPIO_SPI_H__ */
