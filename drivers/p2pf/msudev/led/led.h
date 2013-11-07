/*
 *  led.h
 */
/* $Id: led.h 5704 2010-03-15 01:18:50Z Noguchi Isao $ */

#ifndef __LED_H__
#define __LED_H__

#include <linux/stddef.h>
#include <linux/p2msudev_user.h>

struct msudev_led_ops {

    /* Initilazie */
    int (*init_led)(struct msudev_led_ops *ops);

    /* cleanup */
    void (*cleanup_led)(struct msudev_led_ops *ops);

    /* setting */
    int (*set_led)(struct msudev_led_ops *ops, struct p2msudev_ioc_led_ctrl *param);

    /* getting */
    int (*get_led)(struct msudev_led_ops *ops, struct p2msudev_ioc_led_ctrl *param);

#ifdef CONFIG_MSUDEV_LED_PROC
    /* proc read function */
    int (*procfunc_read)(struct msudev_led_ops *ops,
                         char *buff, char **start, off_t offset, int count, int *eof, void *data);
#endif  /* CONFIG_MSUDEV_LED_PROC */

    /* local ioctl */
    int (*ioctl)(struct msudev_led_ops *ops,unsigned int cmd, unsigned long arg);

    /* LED number */
    unsigned int(*nr_led)(struct msudev_led_ops *ops);

    /* private data */
    void *data;
};

/*
 * To get operational function declared in lower driver
 */
#ifdef CONFIG_MSUDEV_LED_DEV_NONE
static inline struct msudev_led_ops * __led_get_ops(void) {return NULL;}
#else  /* ! CONFIG_MSUDEV_LED_DEV_NONE */
struct msudev_led_ops * __led_get_ops(void);
#endif  /* CONFIG_MSUDEV_LED_DEV_NONE */

#endif  /* __LED_H__ */
