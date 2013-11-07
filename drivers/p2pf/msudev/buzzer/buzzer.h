/*
 *  buzzer.h
 */
/* $Id: buzzer.h 5704 2010-03-15 01:18:50Z Noguchi Isao $ */

#ifndef __BUZZER_H__
#define __BUZZER_H__

#include <linux/stddef.h>
#include <linux/p2msudev_user.h>

struct msudev_buzzer_ops {

    /* Initilazie */
    int (*init_buzzer)(struct msudev_buzzer_ops *ops);

    /* cleanup */
    void (*cleanup_buzzer)(struct msudev_buzzer_ops *ops);

    /* setting */
    int (*set_buzzer)(struct msudev_buzzer_ops *ops, struct p2msudev_ioc_buzzer_ctrl *param);

    /* getting */
    int (*get_buzzer)(struct msudev_buzzer_ops *ops, struct p2msudev_ioc_buzzer_ctrl *param);

#ifdef CONFIG_MSUDEV_BUZZER_PROC
    /* proc read function */
    int (*procfunc_read)(struct msudev_buzzer_ops *ops,
                         char *buff, char **start, off_t offset, int count, int *eof, void *data);
#endif  /* CONFIG_MSUDEV_BUZZER_PROC */

    /* local ioctl */
    int (*ioctl)(struct msudev_buzzer_ops *ops,unsigned int cmd, unsigned long arg);

    /* private data */
    void *data;
};

/*
 * To get operational function declared in lower driver
 */
#ifdef CONFIG_MSUDEV_BUZZER_DEV_NONE
static inline struct msudev_buzzer_ops * __buzzer_get_ops(void) {return NULL;}
#else  /* ! CONFIG_MSUDEV_BUZZER_DEV_NONE */
struct msudev_buzzer_ops * __buzzer_get_ops(void);
#endif  /* CONFIG_MSUDEV_BUZZER_DEV_NONE */

#endif  /* __BUZZER_H__ */
