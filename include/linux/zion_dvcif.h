#ifndef __ZION_DVCIF_H__
#define __ZION_DVCIF_H__

#define ZION_DVCIF_WAIT_INTERRUPTION     _IOR(ZION_MAGIC, 50, unsigned short)
#define ZION_DVCIF_WAKEUP                _IO(ZION_MAGIC, 51)
#define ZION_DVCIF_GET_INTERRUPT_STAT    _IOR(ZION_MAGIC, 52, unsigned short)

#ifdef __KERNEL__

/* Minor Ports */
#define ZION_DVCIF_PORTS   3  /* Number of Ports */
#define ZION_DVCIF_GENERIC 0
#define ZION_DVCIF_PB      1
#define ZION_DVCIF_REC     2

#define ZION_DVCIF_AWAKE       0
#define ZION_DVCIF_FORCE_AWAKE 1
#define ZION_DVCIF_SLEEP       2
#define ZION_DVCIF_NORMAL      3

#define ZION_DVCIF_MAX_OPEN 8

typedef struct _ZION_DVCIF_WAIT_INFO
{
  struct list_head zion_wait_threads;
  u16 int_status;
  int condition;
} zion_dvcif_wait_info_t;

typedef struct _ZION_DVCIF_PARAM
{
  wait_queue_head_t zion_dvcif_wait_queue;
  spinlock_t queue_lock;
  struct list_head zion_wait_threads;
} zion_dvcif_params_t;

#define DVCIF_PARAM(param,minor) \
      ((zion_dvcif_params_t *)((param)->zion_private[minor]))

/* init & exit module */
int init_zion_dvcif(void);
void exit_zion_dvcif(void);

#endif /* __KERNEL__ */

#endif  /* __ZION_DVCIF_H__ */
