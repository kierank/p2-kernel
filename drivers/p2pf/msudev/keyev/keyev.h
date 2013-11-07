/*
 *  keyev.h
 */
/* $Id: keyev.h 5704 2010-03-15 01:18:50Z Noguchi Isao $ */

#ifndef __KEYEV_H__
#define __KEYEV_H__

#include <linux/stddef.h>

struct msudev_keyev_ops {

    /* initilazie */
    int (*init_key)(struct msudev_keyev_ops *ops);

    /* cleanup */
    void (*cleanup_key)(struct msudev_keyev_ops *ops);

    /* get key map data */
    int (*keyscan)(struct msudev_keyev_ops *ops,
                   unsigned long *keymap, unsigned long *smpl_cnt);

#ifdef CONFIG_MSUDEV_KEYEV_PROC
    /* proc read function */
    int (*procfunc_read)(struct msudev_keyev_ops *ops,
                         char *buff, char **start, off_t offset, int count, int *eof, void *data);
#endif  /* CONFIG_MSUDEV_KEYEV_PROC */

    /* local ioctl */
    int (*ioctl)(struct msudev_keyev_ops *ops,unsigned int cmd, unsigned long arg);

    /* get sample period */
    unsigned long sample_period;

    /* private data */
    void *data;
};

/*
 * To get operational function declared in lower driver
 */
#ifdef CONFIG_MSUDEV_KEYEV_DEV_NONE
static inline struct msudev_keyev_ops * __keyev_get_ops(void){return NULL;}
#else  /* ! CONFIG_MSUDEV_KEYEV_DEV_NONE */
struct msudev_keyev_ops * __keyev_get_ops(void);
#endif  /* CONFIG_MSUDEV_KEYEV_DEV_NONE */

/* key event handler using in lower driver */
void __msudev_keyev_handler(void);


#endif  /* __KEYEV_H__ */
