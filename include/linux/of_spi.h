/*
 * OpenFirmware SPI support routines
 * Copyright (C) 2008 Secret Lab Technologies Ltd.
 *
 * Support routines for deriving SPI device attachments from the device
 * tree.
 */
/* $Id: of_spi.h 11136 2010-12-14 05:30:50Z Noguchi Isao $ */

#ifndef __LINUX_OF_SPI_H
#define __LINUX_OF_SPI_H

#include <linux/of.h>
#include <linux/spi/spi.h>

extern void of_register_spi_devices(struct spi_master *master,
				    struct device_node *np);

/* 2010/12/13, added by Panasonic (SAV) ---> */
#ifdef CONFIG_SPI_MASTER

int __init of_spi_device_probe(char *type, char *compatible,
                               struct spi_board_info *board_infos,
                               unsigned int num_board_infos);


#endif  /* CONFIG_SPI_MASTER */
/* <--- 2010/12/13, added by Panasonic (SAV) */

#endif /* __LINUX_OF_SPI */
