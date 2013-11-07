/*
 *  linux/drivers/p2pf/p2gpio/p2gpio.h
 */
/* $Id: p2gpio.h 14402 2011-05-18 02:49:52Z Noguchi Isao $ */

#ifndef __P2GPIO_H__
#define __P2GPIO_H__

#include <linux/kernel.h>  /* for kernel module */

#include <linux/p2gpio_user.h>

#define P2GPIO_DEVNAME     "p2gpio"
#define P2GPIO_DRV_VERSION "0.01"
#define P2GPIO_PROCNAME    "driver/p2gpio"

/*
 *print messages
 */

#define P2GPIO_WARNING 1
#define P2GPIO_DEBUG 1

#ifdef P2GPIO_WARNING
#ifndef P2GPIO_DEBUG
#define P2GPIO_DEBUG 1
#endif /* P2GPIO_DEBUG */
#endif  /* P2GPIO_WARNING */

#define PINFO( fmt, args... )	pr_info("[%s] " fmt, P2GPIO_DEVNAME, ## args)

#define PERROR( fmt, args... )	pr_err("[%s] ERROR: " fmt, P2GPIO_DEVNAME, ## args)

#ifdef P2GPIO_WARNING
#define PWARNING( fmt, args... )	pr_warning("[%s] WARNNING: " fmt, P2GPIO_DEVNAME, ## args)
#else  /* !P2GPIO_WARNING */
#define PWARNING( fmt, args... )
#endif  /* P2GPIO_WARNING */

#ifdef P2GPIO_DEBUG
#define PDEBUG( fmt, args... )	pr_debug("[%s:(%d)] " fmt, P2GPIO_DEVNAME, __LINE__, ## args)
#else  /* ! P2GPIO_DEBUG */
#define PDEBUG( fmt, args... )
#endif  /* P2GPIO_DEBUG */



struct p2gpio_dev;

/* structure for port map */
struct p2gpio_pmap {

    /* port name */
    const char *name;

    /* virtual port number */
    unsigned int vport;

    /* real gpio port number */
    unsigned int gpio;

    /* flag */
    unsigned long flag;
#define P2GPIO_PMAP_RONLY       (1<<0)     /* read-only */
#define P2GPIO_PMAP_REVERSE     (1<<1)     /* reverse polarity */
#define P2GPIO_PMAP_INTERRUPT   (1<<16)    /* support interrupt */

    /* events flag for poll() */
    short events;
#define P2GPIO_PMAP_MAX_EVENTS    (sizeof(short)*8)

    /* callback function */
    void (*fn_callback)(const unsigned int gpio,
                        const struct p2gpio_pmap * const pmap);
}; 

#define __P2GPIO_ENTRY_PMAP(_name,_gpio,_flag,_events,_fn)  \
    {                                                       \
        .name = #_name,                                     \
        .vport = P2GPIO_VPORT_##_name,                      \
        .gpio = _gpio,                                      \
        .flag = _flag,                                      \
        .events = _events,                                  \
        .fn_callback = _fn,                                 \
    }

#define P2GPIO_ENTRY_PMAP(_name,_gpio,_flag)  \
    __P2GPIO_ENTRY_PMAP(_name,                \
                        _gpio,                \
                        _flag,                \
                        0,                    \
                        NULL)

#define P2GPIO_ENTRY_PMAP_INT(_name,_gpio,_flag,_events,_fn) \
    __P2GPIO_ENTRY_PMAP(_name,                               \
                        _gpio,                               \
                        (_flag) | P2GPIO_PMAP_INTERRUPT,     \
                        _events,                             \
                        _fn)


/* structure for operation functions */
struct p2gpio_operations {
    int (*ioctl)(struct p2gpio_dev *, unsigned int, unsigned long);
};

/*  */
struct p2gpio_dev_info {
    char name[32];
    struct p2gpio_operations *ops;
    const struct p2gpio_pmap *pmap;
    int nr_pmap;
    int nr_dipsw, nr_rotsw, nr_led;
};

/* structure for private device data  */
struct p2gpio_dev {
    struct list_head entry;
    wait_queue_head_t   queue;
    spinlock_t lock;
    short detect;
    void *praivate;
};


/*
 * prototype
 */

#ifndef CONFIG_P2GPIO_UNKNOWN

int __p2gpio_init_info( struct p2gpio_dev_info *info );
void __p2gpio_cleanup_info( struct p2gpio_dev_info *info );

#else  /* CONFIG_P2GPIO_UNKNOWN */

static inline int __p2gpio_init_info( struct p2gpio_dev_info *info )
{
    PERROR("Not exist LOW-level driver");
    return -ENODEV;
}

#define __p2gpio_cleanup_info(info )

#endif  /* CONFIG_P2GPIO_UNKNOWN */


#endif  /* __P2GPIO_H__ */
