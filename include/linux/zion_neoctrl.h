#ifndef __ZION_NEOCTRL_H__
#define __ZION_NEOCTRL_H__


#ifdef __KERNEL__

/* Minor Ports */
#define ZION_NEOCTRL_PORTS   1  /* Number of Ports */

typedef struct _ZION_NEOCTRL_PARAM
{
  wait_queue_head_t zion_neoctrl_wait_queue;
  spinlock_t params_lock;
} zion_neoctrl_params_t;

#define NEOCTRL_PARAM(param,minor) \
      ((zion_neoctrl_params_t *)((param)->zion_private[minor]))

/* init & exit module */
int init_zion_neoctrl(void);
void exit_zion_neoctrl(void);

#endif /* __KERNEL__ */

#endif  /* __ZION_NEOCTRL_H__ */
