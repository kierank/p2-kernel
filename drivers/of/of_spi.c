/*
 * SPI OF support routines
 * Copyright (C) 2008 Secret Lab Technologies Ltd.
 *
 * Support routines for deriving SPI device attachments from the device
 * tree.
 */
/* $Id$ */

#include <linux/of.h>
#include <linux/device.h>
#include <linux/spi/spi.h>
#include <linux/of_spi.h>
#include <linux/spi/flash.h>

/**
 * of_register_spi_devices - Register child devices onto the SPI bus
 * @master:	Pointer to spi_master device
 * @np:		parent node of SPI device nodes
 *
 * Registers an spi_device for each child node of 'np' which has a 'reg'
 * property.
 */
void of_register_spi_devices(struct spi_master *master, struct device_node *np)
{
	struct spi_device *spi;
	struct device_node *nc;
	const u32 *prop;
	int rc;
	int len;

	for_each_child_of_node(np, nc) {
		/* Alloc an spi_device */
		spi = spi_alloc_device(master);
		if (!spi) {
			dev_err(&master->dev, "spi_device alloc error for %s\n",
				nc->full_name);
			spi_dev_put(spi);
			continue;
		}

		/* Select device driver */
		if (of_modalias_node(nc, spi->modalias,
				     sizeof(spi->modalias)) < 0) {
			dev_err(&master->dev, "cannot find modalias for %s\n",
				nc->full_name);
			spi_dev_put(spi);
			continue;
		}

		/* Device address */
		prop = of_get_property(nc, "reg", &len);
		if (!prop || len < sizeof(*prop)) {
			dev_err(&master->dev, "%s has no 'reg' property\n",
				nc->full_name);
			spi_dev_put(spi);
			continue;
		}
		spi->chip_select = *prop;

		/* Mode (clock phase/polarity/etc.) */
		if (of_find_property(nc, "spi-cpha", NULL))
			spi->mode |= SPI_CPHA;
		if (of_find_property(nc, "spi-cpol", NULL))
			spi->mode |= SPI_CPOL;

		/* Device speed */
		prop = of_get_property(nc, "spi-max-frequency", &len);
		if (!prop || len < sizeof(*prop)) {
			dev_err(&master->dev, "%s has no 'spi-max-frequency' property\n",
				nc->full_name);
			spi_dev_put(spi);
			continue;
		}
		spi->max_speed_hz = *prop;

		/* IRQ */
		spi->irq = irq_of_parse_and_map(nc, 0);

		/* Store a pointer to the node in the device structure */
		of_node_get(nc);
		spi->dev.archdata.of_node = nc;

		/* Register the new device */
		request_module(spi->modalias);
		rc = spi_add_device(spi);
		if (rc) {
			dev_err(&master->dev, "spi_device register error %s\n",
				nc->full_name);
			spi_dev_put(spi);
		}

	}
}
EXPORT_SYMBOL(of_register_spi_devices);


#ifdef CONFIG_SPI_MASTER

#ifdef CONFIG_MTD_M25P80

struct spi_flash_info {
    struct flash_platform_data pdata;
    char name[32];
    char type[32];
    struct mtd_partition parts[MAX_MTD_DEVICES];
    char partname[MAX_MTD_DEVICES][32];
};

static int __init of_spi_flash_info (struct device_node *np,
                                     struct spi_board_info *board_info)
{
    int retval=0;
    struct device_node *nc;
    struct spi_flash_info *info=NULL;
    struct flash_platform_data *pdata;
    const void *prop;
    int len;

    info = (struct spi_flash_info *)kzalloc(sizeof(struct spi_flash_info),GFP_KERNEL);
    if(!info) {
        retval = -ENOMEM;
        goto fail;
    }
    pdata = &info->pdata;
    pdata->private = (void*)info;
    pdata->name = info->name;
    pdata->type = info->type;
    pdata->parts = &info->parts[0];
    pdata->nr_parts=0;

    /* name */
    strlcpy(info->name,board_info->modalias,sizeof(info->name));

    /* type */
    prop = of_get_property(np, "type", &len);
    if(!prop||len<1) {
        pr_err("%s has no 'type' property\n",np->full_name);
        retval = -ENODEV;
        goto fail;
    }
    strlcpy(info->type,(const char*)prop,len);

    /* patitions */
	for_each_child_of_node(np, nc) {
        struct mtd_partition *p = &pdata->parts[pdata->nr_parts];
        /* offset & size */
        prop = of_get_property(nc, "reg", &len);
        if(!prop||len<(2*sizeof(u32))){
        pr_err("%s has no 'reg' property\n",nc->full_name);
            retval = -ENODEV;
            goto fail;
        }
        p->offset = *(const u32*)prop;
        p->size = *(const u32*)(prop+sizeof(u32));
        /* name  */
        prop = of_get_property(nc, "label", &len);
        if(!prop||len==0){
        pr_err("%s has no 'label' property\n",nc->full_name);
            retval = -ENODEV;
            goto fail;
        }
        p->name = info->partname[pdata->nr_parts];
        strlcpy(p->name, (const char*)prop, len);
        /*  */
        pdata->nr_parts++;
    }
    if(pdata->nr_parts<1){
        pr_err("%s has no partions\n",nc->full_name);
        retval = -ENODEV;
        goto fail;
    }

 fail:
    if(retval<0){
        if(info)
            kfree(info);
    } else {
        int i;
        pr_info("   flash_platform_data: name=%s, type=%s\n",
                pdata->name, pdata->type);
        for(i=0; i<pdata->nr_parts; i++){
            struct mtd_partition *p = &pdata->parts[i];
            pr_info("   flash_platform_data: part[%d]: name=%s, offset=0x%08x, size=0x%08x\n",
                    i, p->name, p->offset, p->size);
        }
        board_info->platform_data = (struct flash_platform_data *)pdata;
    }

    return retval;
}

#endif  /* CONFIG_MTD_M25P80 */

int __init of_spi_device_probe(char *type, char *compatible,
                                    struct spi_board_info *board_infos,
                                    unsigned int num_board_infos) 
{
    int retval = 0;
    int i = 0;
	struct device_node *spi;

	for_each_compatible_node(spi, type, compatible) {
        struct device_node *np=NULL;
		const u32 *prop;
        u32 bus_num;

		prop = of_get_property(spi, "reg", NULL);
		if (!prop)
			continue;
		bus_num = *(u32 *)prop;

        for_each_child_of_node(spi, np) {
            struct spi_board_info *bi;
            int len;

            if(i>=num_board_infos)
                break;

            bi = &board_infos[i];
            bi->bus_num = bus_num;

            /* Select device driver */
            if (of_modalias_node(np, bi->modalias,
                                 sizeof(bi->modalias)) < 0) {
                pr_err("%s : cannot find modalias\n", np->full_name);
                continue;
            }
        
            /* Device address */
            prop = of_get_property(np, "reg", &len);
            if (!prop || len < sizeof(*prop)) {
                pr_err("%s has no 'reg' property\n",np->full_name);
                continue;
            }
            bi->chip_select = *prop;

            /* Mode (clock phase/polarity/etc.) */
            if (of_find_property(np, "spi-cpha", NULL))
                bi->mode |= SPI_CPHA;
            if (of_find_property(np, "spi-cpol", NULL))
                bi->mode |= SPI_CPOL;

            /* chipselect polarity */
            if(of_get_property(np,"spi-cspol",NULL))
                bi->mode |= SPI_CS_HIGH;

            /* SI/SO signals shared */
            if (of_find_property(np, "spi-3wire", NULL))
                bi->mode |= SPI_3WIRE; 

            /* LSB first */
            if (of_find_property(np, "spi-lsb1st", NULL))
                bi->mode |= SPI_LSB_FIRST; 

            /* Device speed */
            prop = of_get_property(np, "spi-max-frequency", &len);
            if (!prop || len < sizeof(*prop)) {
                pr_err("%s has no 'spi-max-frequency' property\n",np->full_name);
                continue;
            }
            bi->max_speed_hz = *prop;

            /* IRQ */
            bi->irq = irq_of_parse_and_map(np, 0);
 
            pr_info("[spi-slave.%d] bus_num=0x%08x,modalias=\"%s\",mode=0x%02x,max_speed_hz=%d,irq=%d, full_name=\"%s\"\n",
                   bi->chip_select,bi->bus_num,bi->modalias,bi->mode,bi->max_speed_hz,bi->irq,np->full_name);

            /* device type */
            prop = of_get_property(np, "device_type", &len);
            if (prop && len>0) {
                const char *type __attribute__ ((unused))
                    = (const char *)prop;

#ifdef CONFIG_MTD_M25P80
                if(!strncmp(type,"rom",len)) {
                    pr_info("[spi-slave.%d] device type is \"%s\".\n",
                            bi->chip_select,type);
                    if(of_spi_flash_info (np, bi)<0)
                        continue;
                }
#endif  /* CONFIG_MTD_M25P80 */

            }

            i++;
        }
    }

    if(retval==0)
        retval = i;

    /* done */
    return retval;
}

EXPORT_SYMBOL(of_spi_device_probe);



#endif  /* CONFIG_SPI_MASTER */
