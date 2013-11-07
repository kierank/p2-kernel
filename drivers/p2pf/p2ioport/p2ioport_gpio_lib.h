/*
 *  linux/drivers/p2pf/p2ioport/p2ioport_gpio_lib.h
 */
/* $Id: p2ioport_gpio_lib.h 10439 2010-11-16 04:47:46Z Noguchi Isao $ */

#ifndef __P2IOPORT_GPIO_LIB_H__
#define __P2IOPORT_GPIO_LIB_H__

struct p2ioport_gpio_pmap {
    unsigned int vport;                  /* virtual port number */
    unsigned int gpio;                   /* real gpio port number */
    unsigned long flag;
#define P2IOPORT_GPIO_PMAP_RONLY  (1<<0)      /* read-only */
#define P2IOPORT_GPIO_PMAP_REVERSE (1<<1)     /* reverse polarity */
}; 

int __p2ioport_get_gpio( const unsigned int gpio, int *const p_val );

int __p2ioport_set_gpio( const unsigned int gpio, const int val );

int __p2ioport_get_vport( struct p2ioport_gpio_pmap *pmap, int nr_pmap,
                          unsigned int vport, int *p_val );

int __p2ioport_set_vport( struct p2ioport_gpio_pmap *pmap, int nr_pmap,
                          unsigned int vport, int val );

int __p2ioport_lock_gpio( const unsigned int gpio, int *const p_val );

int __p2ioport_unlock_gpio( const unsigned int gpio );

int __p2ioport_lock_vport( struct p2ioport_gpio_pmap *pmap, int nr_pmap,
                           unsigned int vport, int *p_val );

int __p2ioport_unlock_vport( struct p2ioport_gpio_pmap *pmap, int nr_pmap,
                             unsigned int vport );

#endif  /* __P2IOPORT_GPIO_LIB_H__ */
