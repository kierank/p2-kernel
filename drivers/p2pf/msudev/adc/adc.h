/*
 *  adc.h
 */
/* $Id: adc.h 5704 2010-03-15 01:18:50Z Noguchi Isao $ */

#ifndef __ADC_H__
#define __ADC_H__

#include <linux/stddef.h>

struct msudev_adc_ops {

    /* initilazie */
    int (*init_adc)(struct msudev_adc_ops *ops);

    /* cleanup */
    void (*cleanup_adc)(struct msudev_adc_ops *ops);

    /* get value */
    int (*get_value)(struct msudev_adc_ops *ops, int chan, unsigned long *p_value);

    /* reset cntrol */
    int (*do_reset)(struct msudev_adc_ops *ops, int reset_on);

    /* check reset status */
    int (*check_reset)(struct msudev_adc_ops *ops, int *check);

#ifdef CONFIG_MSUDEV_ADC_PROC
    /* proc read function */
    int (*procfunc_read)(struct msudev_adc_ops *ops,
                         char *buff, char **start, off_t offset, int count, int *eof, void *data);
#endif  /* CONFIG_MSUDEV_ADC_PROC */

    /* local ioctl */
    int (*ioctl)(struct msudev_adc_ops *ops,unsigned int cmd, unsigned long arg);

    /* chanel number */
    unsigned int (*nr_chan)(struct msudev_adc_ops *ops);

    /* private data */
    void *data;
};

/*
 * To get operational function declared in lower driver
 */
#ifdef CONFIG_MSUDEV_ADC_DEV_NONE
static inline struct msudev_adc_ops * __adc_get_ops(void) {return NULL;}
#else  /* ! CONFIG_MSUDEV_ADC_DEV_NONE */
struct msudev_adc_ops * __adc_get_ops(void);
#endif  /* CONFIG_MSUDEV_ADC_DEV_NONE */

#endif  /* __ADC_H__ */
