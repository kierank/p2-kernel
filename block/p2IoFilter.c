#include <linux/kernel.h>
#include <linux/workqueue.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/compiler.h>
#include <linux/rbtree.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <p2/spd.h>
#include <linux/rtctrl.h>

/* �С�������ֹ� */
#define P2IOFILTER_VERSION "1.10"

/* �ǥХå��ѥ�å�������� */
/* #define DBG_TRACE */
/* #define DBG_PRINT */

#if defined(DBG_TRACE)
# define PTRACE(fmt, args...)  printk(KERN_INFO "p2Io:>>%s" fmt "\n",\
           __FUNCTION__, ## args);
#else
# define PTRACE(fmt, args...)
#endif /* DBG_TRACE */

#if defined(DBG_PRINT)
# define PDEBUG(fmt, args...)  printk(KERN_INFO "p2Io:%d:" fmt, __LINE__, ## args);
#else
# define PDEBUG(fmt, args...)
#endif /* DBG_PRINT */


/* �Хåե��μ��� */
enum buffer_types
{
  P2BF_FAT,			/* FAT�ΰ���(bio��META�ե饰��Ω�äƤ�����) */
  P2BF_SYS,			/* SYS�ΰ��ѡ�system�ΰ�� */
  P2BF_USR1,			/* USER�ΰ��2�̤�LRU�ǻ��ѡ� */
  P2BF_USR2,
  P2BF_MAX_BFS,			/* �Хåե��ο� */
};

/* ���줾��ΥХåե��Υ����ॢ���Ȼ��� */
#define FAT_TMOUT (HZ >> 1)		/* 0.5sec(Windows�ե��륿�ɥ饤�Ф�Ʊ��) */
#define SYS_TMOUT (HZ >> 1)		/* 0.5sec(Windows�ե��륿�ɥ饤�Ф�Ʊ��) */
#define DEF_TMOUT (180*HZ)		/* �桼���ΰ�˻Ȥ� */

#define EXEC_FIFO_MAX_DEPTH  (2)        /* AU���̤֤�ޤ�exec_fifo�ˤĤʤ��Τ������ */

/* p2IoFilter_buffer->flags �Υӥå� */
enum buffer_flags
{
  P2BF_ACTIVE,			/* ���ΥХåե��ϻ��Ѥ��Ƥ��� */
  P2BF_TMOUT,			/* ���ΥХåե��ϥ����ॢ���Ȥ��� */
};

struct p2IoFilter_info;

/***
struct p2IoFilter_buffer
���줾�����α���塼���Ф��Ƴ�����Ƥ���
�����ѥѥ�᡼���ȥ��塼�μ���
***/
struct p2IoFilter_buffer
{

  unsigned long flags;		/* ���塼�ξ��֤򼨤��ե饰 */
  unsigned long start;		/* ���߰��äƤ����ΰ����Ƭ�������ֹ� */
  unsigned long block_size;	/* ���߰��äƤ����ΰ�Υ������������� */
  struct rb_root buffer;	/* ��α���塼�μ��� */
  unsigned long last_access;	/* �ǽ�������������(jiffies) */
  struct timer_list unplug_timer;	/* �����񤭤��������ޡ� */
  struct work_struct unplug_work;	/* �����񤭤����ѥ����� */
  unsigned long max_wait_time;	/* �����ॢ���Ȼ��� */
  spinlock_t buffer_lock;	/* ���ΥХåե��˴ؤ���lock */
  struct request_queue *queue;	/* ���ΥХåե�����°���Ƥ���queue */
  struct p2IoFilter_info *info;	/* ���ΥХåե�����°���Ƥ���Filter */
  int id;					/* ���ΥХåե��μ���(buffer_types) */
};

/***
struct p2IoFilter_info
�ơ��ΥǥХ������Ф��ƤҤȤĳ�����Ƥ��빽¤��
***/
struct p2IoFilter_info
{

  struct p2IoFilter_buffer buffers[P2BF_MAX_BFS];	/* ���줾����ΰ�򰷤���α���塼 */
  struct list_head exec_fifo;	/* �¹Ԥν������Ǥ���request��FIFO */
  unsigned long exec_fifo_depth; /* �¹Խ�������Ƥ���ǡ������̡ʥ�����ñ�̡� */
  struct workqueue_struct *unplug_works;	/* �����ॢ���ȥ������ѤΥ�����塼 */
  unsigned long sys_boundary;	/* P2�Υ����ƥ��ΰ�ȥ桼�����ΰ�ζ��������� */
  unsigned long sys_block_sectors;	/* P2�Υ����ƥ��ΰ�Υ������������� */
  unsigned long usr_block_sectors;	/* P2�Υ桼�����ΰ�Υ������������� */
  unsigned long sys_start_sector;	/* P2�Υ����ƥ��ΰ����Ƭ�������ֹ� [�ٱ����] */
  unsigned long usr_start_sector;	/* P2�Υ桼�����ΰ����Ƭ�������ֹ� [�ٱ����] */
  spinlock_t fifo_lock;	/* �¹�FIFO�˴ؤ���lock */
  struct delayproc_info_s *dpinfo; /* �ٱ�������� [�ٱ����] */
  dev_t dev; /* �ǥХ����ֹ� [�ٱ����] */
};

/* bio��read�����Ǥ��ä��Ȥ��ˤϡ�BIO_RW�ӥåȤ����Ƥ��� */
#define BIO_READ_DIR(bio) ((bio->bi_rw & (1UL << BIO_RW)) ? 0 : 1)


/* �ץ�ȥ�������� */
static void p2IoFilter_update_timelimit (struct p2IoFilter_buffer *buffer);
static int p2IoFilter_check_total_size (struct p2IoFilter_buffer *buffer);
static struct p2IoFilter_buffer *p2IoFilter_find_area (struct p2IoFilter_info *info, unsigned long start, int is_dp);
static struct request *elv_rb_find_tail (struct rb_root *root, sector_t sector);
static int p2IoFilter_check_boundary (unsigned long start, unsigned long length,
				      unsigned long block_start,
				      unsigned long block_size);
static inline void p2IoFilter_prepare_exec (struct p2IoFilter_info *info, struct request *rq);
static void p2IoFilter_flush_buffered_rq (struct p2IoFilter_buffer *buffer);
static inline void p2IoFilter_set_block_params (struct p2IoFilter_info *info,
						unsigned long rq_start,
						unsigned long *block_start,
						unsigned long *block_size);
static void p2IoFilter_update_buffer (struct p2IoFilter_info *info,
				      struct p2IoFilter_buffer *buffer,
				      struct request *rq);
static int p2IoFilter_merge (struct request_queue * q, struct request **req, struct bio *bio);
static void p2IoFilter_merged_request (struct request_queue * q, struct request *req, int type);
static void p2IoFilter_merged_requests (struct request_queue * q, struct request *req, struct request *next);
static int p2IoFilter_allow_merge (struct request_queue * q, struct request *req, struct bio *bio);
static void p2IoFilter_do_dispatch_request(struct request_queue *q);
static int p2IoFilter_dispatch_requests (struct request_queue * q, int force);
static void p2IoFilter_add_request (struct request_queue *q, struct request *rq);
static int p2IoFilter_queue_empty (struct request_queue * q);
static struct request *p2IoFilter_former_request (struct request_queue * q, struct request *rq);
static struct request *p2IoFilter_latter_request (struct request_queue * q, struct request *rq);
static void p2IoFilter_time_expired (unsigned long data);
static void p2IoFilter_unplug_device (struct work_struct *work);
static int p2IoFilter_ready_queue (struct request_queue * q);
static void p2IoFilter_force_dispatch (struct request_queue * q, int area);
static int p2IoFilter_init_buffers (struct request_queue * q,
				    struct p2IoFilter_info *info,
				    struct p2IoFilter_buffer *buffer);
static void *p2IoFilter_init_queue (struct request_queue * q);
static void p2IoFilter_exit_queue (elevator_t * e);


/* -------------- �ٱ������Ϣ������ؿ� -------------- */
#if defined(CONFIG_DELAYPROC)

/* SYS��USR1�ΰ褫�ɤ�����Ƚ�� [�ٱ����] */
# define BUF_IS_DELAYPROC(buf) ((buf->id == P2BF_SYS) || (buf->id == P2BF_USR1))
# define RQ_IS_DELAYPROC(rq) (rq_is_dirent(rq) || (!rq_is_rt(rq) && !rq_is_fat(rq)))
# define BIO_IS_DELAYPROC(bio) (bio_dirent(bio) || (!bio_rt(bio) && !bio_fat(bio)))

/* �ꥯ�����ȥ��塼�ѥ�᡼�� [�ٱ����] XXX ��Ĵ�� */
enum p2IoFilter_queue_thresh {
  DELAYPROC_UNPLUG_THRESH = 50,
  DELAYPROC_NR_REQUESTS = BLKDEV_MAX_RQ * 10,
};


/* �ٱ�����оݳ�inode��ǧ�Ѵؿ� [�ٱ����] */
#include <linux/p2fat_fs.h>
static int dp_inode_is_fat(struct inode *inode)
{
  if (NULL != inode) {
    if (inode->i_ino == P2FAT_FAT_INO) {
      return (1);
    }
  }
  return (0);
}


static int dp_inode_is_rt(struct inode *inode)
{
  if (NULL != inode) {
    return (int)(inode_is_rt(inode));
  }
  return (0);
}

/* �ٱ�����оݳ�inode��ǧ�Ѵؿ��ꥹ�� [�ٱ����] */
static dp_except_inode_t dp_except_inode_list[] = {
  dp_inode_is_rt,
  dp_inode_is_fat,
  NULL,
};


/* --- �ٱ����write���� ��Ϣ --- */
# if defined(CONFIG_DELAYPROC_WRITE_ORDER)

/* �ٱ����write���֢��Хåե��Ѵ� */
static int dp_dattype_order[DELAYPROC_ORDER_MAXNR] = {
  DELAYPROC_DATTYPE_FILE,
  DELAYPROC_DATTYPE_FILE,
  DELAYPROC_DATTYPE_META,
  DELAYPROC_DATTYPE_META
};


static int dp_buffer_order[DELAYPROC_ORDER_MAXNR] = {
  P2BF_USR1,
  P2BF_SYS,
  P2BF_SYS,
  P2BF_USR1
};


static unsigned char p2IoFilter_check_dp_order(struct p2IoFilter_info *info, unsigned char order)
{
  struct p2IoFilter_buffer *buffer = NULL;
  struct delayproc_info_s *dpinfo = NULL;
  struct rb_root *root = NULL;
  struct rb_node *node = NULL;

  dpinfo = info->dpinfo;
  buffer = &info->buffers[dp_buffer_order[order]];

  root = &buffer->buffer;
  node = rb_first(root);

  while (node) {
    struct request *req = rb_entry_rq(node);

    PDEBUG("order=%d %s(%lX) -- %s\n",
	   order,
	   (rq_is_dirent(req))?"Meta":"Normal",
	   req->sector,
	   (DELAYPROC_DATTYPE_META == dp_dattype_order[order])?"Meta":"Normal");

    if ((0 != rq_is_dirent(req))
	^ (DELAYPROC_DATTYPE_META == dp_dattype_order[order])) {
      PDEBUG("order(%d) find next node\n", order);
      node = rb_next(node);
    } else {
      PDEBUG("find buffer(order=%d sector=%lX)!!\n", order, req->sector);
      return order;
    }

  }
  
  return (unsigned char)-1;
}


static struct p2IoFilter_buffer *
p2IoFilter_order2buffer(struct p2IoFilter_info *info, unsigned char order)
{
  struct delayproc_info_s *dpinfo = NULL;
  /* ǰ�Τ�����������å� */
  if (unlikely(NULL == info->dpinfo)) {
    printk("[p2IoFilter] dpinfo is NULL at %s!\n", __FUNCTION__);
    return (NULL);
  }
  dpinfo = info->dpinfo;

  if (order >= DELAYPROC_ORDER_MAXNR) {
    printk("[p2IoFilter] dpinfo->order is invalid(%d)!\n", order);
    return (NULL);
  }

  /* �ٱ����write���֤���Хåե������� */
  PDEBUG(" order=%d\n", order);

  order = p2IoFilter_check_dp_order(info, order);
  if ((unsigned char)-1 == order) {
    PDEBUG("[p2IoFilter] Cannot find buffer!\n");
    return (NULL);
  }
  return (&info->buffers[dp_buffer_order[order]]);
}


# else /* ! CONFIG_DELAYPROC_WRITE_ORDER */

# endif /* CONFIG_DELAYPROC_WRITE_ORDER */
/* ------------------------------ [�ٱ����write����] */

/* �ٱ������ɬ�פˤʤä�I/O scheduler�᥽�åɤ�sysfs [�ٱ����] */
static int
p2IoFilter_set_req (struct request_queue *q, struct request *rq, gfp_t gfp_mask)
{
  PTRACE();

  rq->elevator_private = NULL;
  rq->elevator_private2 = NULL;
  INIT_LIST_HEAD (&rq->waitlist);

  return 0;
}


static void
p2IoFilter_put_req (struct request *rq)
{
  PTRACE();

  rq->elevator_private = NULL;
  rq->elevator_private2 = NULL;
}


static void
p2IoFilter_completed_req (struct request_queue *q, struct request *rq)
{
  struct p2IoFilter_info *info = q->elevator->elevator_data;
  PTRACE();

  if (!info) return;
  del_delayproc_req_waitlist (info->dpinfo, rq);
}


static void
p2IoFilter_force_delayproc (struct request_queue *q)
{
  struct p2IoFilter_info *info = q->elevator->elevator_data;
  unsigned long flags = 0;
# if defined(CONFIG_DELAYPROC_WRITE_ORDER)
  struct p2IoFilter_buffer *buffer = NULL;
  struct delayproc_info_s *dpinfo = info->dpinfo;
  int i = 0;
# endif /* CONFIG_DELAYPROC_WRITE_ORDER */
  PTRACE();
  
  /* �ٱ��������θƤӽФ��ʤΤǼ����ǥ�å��򤫤��� */
  spin_lock_irqsave (q->queue_lock, flags);
  
  /* �ٱ�����Υ���ƥ����ȤʤΤǡ��ٱ�����ӥåȤ�Ω�Ƥ� */
  set_task_delayprocd( current );

# if defined(CONFIG_DELAYPROC_WRITE_ORDER)

  /* �ٱ����write���֤˱����ƥХåե������򤷤�flush���� */

  /* �ٱ����write���֤��������ͤ���ǧ */
  i = get_delayproc_order(dpinfo);
  if (i >= DELAYPROC_ORDER_MAXNR) {
    printk("[p2IoFilter] Invalid dpinfo->order at force delayproc(%d)!\n", i);
    i = 0;
  }

  for (; i < DELAYPROC_ORDER_MAXNR; i++) {
    PDEBUG("flush order=%d\n", i);
    buffer = p2IoFilter_order2buffer(info, i);
    if (buffer) {
      set_delayproc_order(dpinfo, i);
      p2IoFilter_flush_buffered_rq (buffer);
      break;
    }
  }
  if (NULL == buffer) {
    PDEBUG("[p2IoFilter] buffer is NULL. set order %d to -1 (i=%d)!\n", get_delayproc_order(dpinfo), i);
    set_delayproc_order(dpinfo, (unsigned char)-1);
    spin_unlock_irqrestore (q->queue_lock, flags);
    return;
  }

# else /* ! CONFIG_DELAYPROC_WRITE_ORDER */

  /* �����˴ط��ʤ��ٱ�����ѥХåե����٤Ƥ�flush���� */
  p2IoFilter_flush_buffered_rq (&info->buffers[P2BF_SYS]);
  p2IoFilter_flush_buffered_rq (&info->buffers[P2BF_USR1]);

# endif /* CONFIG_DELAYPROC_WRITE_ORDER */

  /* dispatch�¹� */
  if (!list_empty (&info->exec_fifo)) {
    p2IoFilter_do_dispatch_request (q);
  } else {
    PDEBUG("Empty exec_fifo\n");
  }
  
  /* �����񤭽Ф��¹� */
  blk_start_queueing (q);
  spin_unlock_irqrestore (q->queue_lock, flags);
}


static int
__normal_flush_delayproc_buffer (struct p2IoFilter_buffer *buffer)
{
  struct p2IoFilter_info *info = buffer->info;
  struct delayproc_info_s *dpinfo = info->dpinfo;
  unsigned long offset = 0;
  unsigned long blk_size = 0;
  unsigned long before_quot = (unsigned long)-1; /* -1�ǽ���� */
  int nr_blk = 0;
  unsigned long irq_flags = 0L;

# if defined(CONFIG_DELAYPROC_WRITE_ORDER)
  struct rb_root *root = &buffer->buffer;
  struct rb_node *node = rb_first (root);
  unsigned char order = get_delayproc_order(dpinfo);
  PDEBUG(" == order:%d ==\n", order);
# endif /* CONFIG_DELAYPROC_WRITE_ORDER */

  /* �ΰ��̤��ٱ����I/O�֥�å��������ȥ֥�å�������� */
  if (buffer->id == P2BF_SYS) {
    /* �����ƥ��ΰ� */
    blk_size = dpinfo->params.size_sys;
    nr_blk = dpinfo->params.nr_sys;
    offset = info->sys_start_sector;
  } else {
    /* �桼���ΰ� */
    blk_size = dpinfo->params.size_usr;
    nr_blk = dpinfo->params.nr_usr;
    offset = info->usr_start_sector;
  }

  /* exec_fifo��å� */
  spin_lock_irqsave (&info->fifo_lock, irq_flags);

  /* �ᥤ����� */
# if defined(CONFIG_DELAYPROC_WRITE_ORDER)

  while (node) {
    struct request *rq = rb_entry_rq (node);
    unsigned long current_quot = ((unsigned long)(rq->sector - offset))/blk_size;
    PDEBUG("== nr_blk=%d order=%d ==\n", nr_blk, order);
    PDEBUG(" of=%lX, cs=%lX, b=%ld, c=%ld\n",
	   offset, rq->sector, before_quot, current_quot);

    if ((0 != rq_is_dirent(rq))
	^ (DELAYPROC_DATTYPE_META == dp_dattype_order[order])) {
      PDEBUG("find next node\n");
      node = rb_next(node);

      if (NULL == node) {
	if (order+1 < DELAYPROC_ORDER_MAXNR &&
	    dp_buffer_order[order] == dp_buffer_order[order+1]) {
	  PDEBUG("Increment order %d->%d\n", get_delayproc_order(dpinfo), order+1);
	  order++;
	  set_delayproc_order(dpinfo, order);
	  node = rb_first(root);
	} else {
	  PDEBUG("break!\n");
	  break;
	}
      }
      PDEBUG("continue!\n");
      continue;
    }

    PDEBUG("find rq(sector=%lX)!!\n", rq->sector);

    /* ����Ʊ���ͤ������ */
    if ((unsigned long)-1 == before_quot) {
      before_quot = current_quot;
    }

    /* �¹��̤�Ĵ�� */			  
    if (before_quot != current_quot) { 
      nr_blk--;
    }

    /* �¹��̤�Ķ�����齪λ */
    if (nr_blk < 1) {
      PDEBUG(" break!\n");
      break;
    }

    /* wait�ꥹ�Ȥ��ɲá��¹�FIFO�ذ�ư */
    add_delayproc_req_waitlist (dpinfo, rq);
    p2IoFilter_prepare_exec (info, rq);

    before_quot = current_quot;
    node = rb_first(root);
  }
  PDEBUG("exit while loop\n");

# else /* ! CONFIG_DELAYPROC_WRITE_ORDER */

  while (rb_first (&buffer->buffer)) {
    struct request *rq = rb_entry_rq (rb_first (&buffer->buffer));
    unsigned long current_quot = ((unsigned long)(rq->sector - offset))/blk_size;
    PDEBUG("== nr_blk=%d ==\n", nr_blk);
    PDEBUG(" of=%lX, cs=%lX, b=%ld, c=%ld\n",
	   offset, rq->sector, before_quot, current_quot);

    /* ����Ʊ���ͤ������ */
    if ((unsigned long)-1 == before_quot) {
      before_quot = current_quot;
    }

    /* �¹��̤�Ĵ�� */			  
    if (before_quot != current_quot) {
      nr_blk--;
    }

    /* �¹��̤�Ķ�����齪λ */
    if (nr_blk < 1) {
      PDEBUG(" break!\n");
      break;
    }

    /* wait�ꥹ�Ȥ��ɲá��¹�FIFO�ذ�ư */
    add_delayproc_req_waitlist (dpinfo, rq);
    p2IoFilter_prepare_exec (info, rq);

    before_quot = current_quot;
  }

# endif /* CONFIG_DELAYPROC_WRITE_ORDER */

  /* exec_fifo��å���� */
  spin_unlock_irqrestore (&info->fifo_lock, irq_flags);

  /* buffer�������ä���̵�������� */
  if (!rb_first (&buffer->buffer)) {
    /* �Хåե���̵�������� */
    PDEBUG(" In-Active buffer%d\n", buffer->id);
    if (!test_and_clear_bit (P2BF_ACTIVE, &buffer->flags)) {
      PDEBUG(" Already In-Active buffer%d\n", buffer->id);
    }

    /* �����ॢ���Ȥ�̵�������� */
    del_timer_sync (&buffer->unplug_timer);
  }
  return 1;
}


static int
__sync_flush_delayproc_buffer (struct p2IoFilter_buffer *buffer)
{
  struct p2IoFilter_info *info = buffer->info;
  struct delayproc_info_s *dpinfo = info->dpinfo;
  unsigned long irq_flags = 0L;
# if defined(CONFIG_DELAYPROC_WRITE_ORDER)
  struct rb_root *root = &buffer->buffer;
  struct rb_node *node = rb_first (root);
  unsigned char order = get_delayproc_order(dpinfo);
  PDEBUG(" == order:%d ==\n", order);
# endif /* CONFIG_DELAYPROC_WRITE_ORDER */

  /* exec_fifo��å� */
  spin_lock_irqsave (&info->fifo_lock, irq_flags);

  /* �ᥤ����� */
# if defined(CONFIG_DELAYPROC_WRITE_ORDER)

  while (node) {
    struct request *rq = rb_entry_rq (node);

    if ((0 != rq_is_dirent(rq))
	^ (DELAYPROC_DATTYPE_META == dp_dattype_order[order])) {
      PDEBUG("find next node\n");
      node = rb_next(node);

      if (NULL == node) {
	if (order+1 < DELAYPROC_ORDER_MAXNR &&
	    dp_buffer_order[order] == dp_buffer_order[order+1]) {
	  PDEBUG("Increment order %d->%d\n", dpinfo->order, order+1);
	  order++;
	  set_delayproc_order(dpinfo, order);
	  node = rb_first(root);
	} else {
	  PDEBUG("break!\n");
	  break;
	}
      }
      PDEBUG("continue!\n");
      continue;
    }
    PDEBUG("find rq(sector=%lX)!!\n", rq->sector);

    add_delayproc_req_waitlist(dpinfo, rq);
    p2IoFilter_prepare_exec (info, rq);

    node = rb_first(root);
  }

# else /* ! CONFIG_DELAYPROC_WRITE_ORDER */

  while (rb_first (&buffer->buffer)) {
    struct request *rq = rb_entry_rq (rb_first (&buffer->buffer));

    add_delayproc_req_waitlist(dpinfo, rq);
    p2IoFilter_prepare_exec (info, rq);
  }

# endif /* CONFIG_DELAYPROC_WRITE_ORDER */

  /* exec_fifo��å���� */
  spin_unlock_irqrestore (&info->fifo_lock, irq_flags);

  /* �Хåե���̵�������� */
  PDEBUG(" In-Active buffer%d\n", buffer->id);
  if (!test_and_clear_bit (P2BF_ACTIVE, &buffer->flags)) {
    PDEBUG( " Already In-Active buffer%d\n", buffer->id );
  }

  /* �����ॢ���Ȥ�̵�������� */
  del_timer_sync (&buffer->unplug_timer);

  return 1;
}


static int
__sync_sys_flush_delayproc_buffer (struct p2IoFilter_buffer *buffer)
{
  struct p2IoFilter_info *info = buffer->info;
  struct delayproc_info_s *dpinfo = info->dpinfo;
  struct rb_root *root = &buffer->buffer;
  struct rb_node *node = rb_first (root);
  unsigned long irq_flags = 0L;

  if (!node) PDEBUG("rb_tree is EMPTY!\n"); /* DEBUG�� */

  /* exec_fifo��å� */
  spin_lock_irqsave (&info->fifo_lock, irq_flags);

  while (node) {
    struct request *rq = rb_entry_rq (node);

    /* DIRENT�Τ�Sync���� */
    if (rq_is_dirent(rq)) {
      PDEBUG("Move rq[DIRENT] to exec_fifo(LBA=%lX)\n", (unsigned long)rq->sector);
      add_delayproc_req_waitlist (dpinfo, rq);
      p2IoFilter_prepare_exec (info, rq);

      /* �ǽ��request����� */
      node = rb_first (root);
    } else {
      /* ����request�˰�ư���� */
      node = rb_next (node);
    }
  }

  /* exec_fifo��å���� */
  spin_unlock_irqrestore (&info->fifo_lock, irq_flags);

  /* buffer�������ä���̵�������� */
  if (!rb_first (&buffer->buffer)) {
    /* �Хåե���̵�������� */
    PDEBUG(" In-Active buffer%d\n", buffer->id);
    if (!test_and_clear_bit (P2BF_ACTIVE, &buffer->flags)) {
      PDEBUG( " Already In-Active buffer%d\n", buffer->id );
    }

    /* �����ॢ���Ȥ�̵�������� */
    del_timer_sync (&buffer->unplug_timer);
  }

  PDEBUG("Exit while(node)[DIRENT].\n");
  return 1;
}


static int
p2IoFilter_flush_delayproc_buffer (struct p2IoFilter_buffer *buffer)
{
  /* �Хåե��μ�����ǧ */
  if (BUF_IS_DELAYPROC(buffer)) {

    /* SYS/USR1�ΰ�ν���(�ٱ����) */
    struct p2IoFilter_info *info = buffer->info;
    struct delayproc_info_s *dpinfo = info->dpinfo;
    unsigned char type = get_delayproc_type(dpinfo);
    unsigned char status = get_delayproc_status(dpinfo);

    /* RT_ON���֡��ٱ������α��ξ���flush���ʤ� */
    if ( (DELAYPROC_STATUS_RUN == status) && !current_is_delayprocd() ) {
      PDEBUG("NOT delayproc context!\n");
      return 1;
    }
    if ( (DELAYPROC_STATUS_STANDBY == status)
	 || (chk_rton(MAJOR(info->dev))
	     && (DELAYPROC_TYPE_SYSSYNC != type)) ) {
      PDEBUG("RT_ON or delayproc!\n");
      return 1;
    }

    PDEBUG("==== delayproc flush buf%d(type:%d) ====\n", buffer->id, type);

    /* �ٱ�����μ�����ǧ���Ƽ¹� */
    switch (type) {

    case DELAYPROC_TYPE_NORMAL:
      return __normal_flush_delayproc_buffer(buffer); /* ExecDelayProc */

    case DELAYPROC_TYPE_SYNC:
      return __sync_flush_delayproc_buffer(buffer); /* SyncDelayProc */

    case DELAYPROC_TYPE_SYSSYNC:
      return __sync_sys_flush_delayproc_buffer(buffer); /* SyncSystemDelayProc */

    default:
      {
	PDEBUG("NOT delayproc or invalid type = %d\n", type);
	break;
      }
    }
  }

  return 0;
}


/*
 * sysfs parts below -->
 */

static ssize_t
p2IoFilter_var_show (unsigned int var, char *page)
{
	return sprintf (page, "%d\n", var);
}

static ssize_t
p2IoFilter_var_store (unsigned int *var, const char *page, size_t count)
{
	char *p = (char *) page;

	*var = simple_strtoul (p, &p, 10);
	return count;
}

#define SHOW_FUNCTION(__FUNC, __VAR)				\
static ssize_t __FUNC(elevator_t *e, char *page)			\
{									\
	struct p2IoFilter_info *info = e->elevator_data;			\
	return p2IoFilter_var_show(__VAR, (page));				\
}
SHOW_FUNCTION(p2IoFilter_stat_show, get_delayproc_status(info->dpinfo));
SHOW_FUNCTION(p2IoFilter_type_show, get_delayproc_type(info->dpinfo));
SHOW_FUNCTION(p2IoFilter_bufcnt_show, get_delayproc_buf_cnt(info->dpinfo));
SHOW_FUNCTION(p2IoFilter_size_sys_show, info->dpinfo->params.size_sys);
SHOW_FUNCTION(p2IoFilter_size_usr_show, info->dpinfo->params.size_usr);
SHOW_FUNCTION(p2IoFilter_nr_sys_show, info->dpinfo->params.nr_sys);
SHOW_FUNCTION(p2IoFilter_nr_usr_show, info->dpinfo->params.nr_usr);
#undef SHOW_FUNCTION

#define STORE_FUNCTION(__FUNC, __PTR)			\
static ssize_t __FUNC(elevator_t *e, const char *page, size_t count)	\
{									\
	struct p2IoFilter_info *info = e->elevator_data;			\
	unsigned int __data = 0;						\
	int ret = p2IoFilter_var_store(&__data, (page), count);		\
	*(__PTR) = __data;					\
	return ret;							\
}
STORE_FUNCTION(p2IoFilter_size_sys_store, &info->dpinfo->params.size_sys);
STORE_FUNCTION(p2IoFilter_size_usr_store, &info->dpinfo->params.size_usr);
STORE_FUNCTION(p2IoFilter_nr_sys_store, &info->dpinfo->params.nr_sys);
STORE_FUNCTION(p2IoFilter_nr_usr_store, &info->dpinfo->params.nr_usr);
#undef STORE_FUNCTION

#define P2IOFILTER_ATTR(name) \
	__ATTR(name, S_IRUGO|S_IWUSR, p2IoFilter_##name##_show, p2IoFilter_##name##_store)
#define P2IOFILTER_ATTR_RO(name) \
	__ATTR(name, S_IRUGO, p2IoFilter_##name##_show, NULL)

static struct elv_fs_entry p2IoFilter_attrs[] = {
	P2IOFILTER_ATTR_RO(stat),
	P2IOFILTER_ATTR_RO(type),
	P2IOFILTER_ATTR_RO(bufcnt),
	P2IOFILTER_ATTR(size_sys),
	P2IOFILTER_ATTR(size_usr),
	P2IOFILTER_ATTR(nr_sys),
	P2IOFILTER_ATTR(nr_usr),
	__ATTR_NULL
};
/*
 * <-- sysfs parts end
 */


#else /* ! CONFIG_DELAYPROC */

# define BUF_IS_DELAYPROC(buf) (0)
# define RQ_IS_DELAYPROC(rq) (0)
# define BIO_IS_DELAYPROC(bio) (0)

static inline int
p2IoFilter_flush_delayproc_buffer(struct p2IoFilter_buffer *buffer) {return 0;}

#endif /* CONFIG_DELAYPROC */
/* ----------------------------------------------- [�ٱ����] */


static void
p2IoFilter_update_timelimit (struct p2IoFilter_buffer *buffer)
{
  PTRACE();
	
  BUG_ON (buffer == NULL);

  /* �Хåե��ؤκǽ������������֤Ȥ��ơ�
     ���ߤ�jiffes��Ͽ���� */
  buffer->last_access = jiffies;

  /* �����ॢ���Ƚ����λ��¤��Ĺ���� */
  mod_timer (&buffer->unplug_timer,
	     buffer->last_access + buffer->max_wait_time);

  return;
}

static int
p2IoFilter_check_total_size (struct p2IoFilter_buffer *buffer)
{
  struct rb_root *root = &buffer->buffer;
  struct rb_node *node = rb_first (root);
  unsigned long total_size = 0;
  PTRACE("%d", buffer->id);

#if defined(CONFIG_DELAYPROC)
  /* RT_ON���֡��ٱ������ϡ�SYS/USR1�ΰ�Хåե���flush�����ʤ��褦��0���֤� [�ٱ����] */
  if (BUF_IS_DELAYPROC(buffer))
  {
	  dev_t dev = buffer->info->dev;
	  
	  if (chk_rton(MAJOR(dev))
		  || is_delayproc_dev(dev)
		  || is_delayproc_run_dev(dev)) return 0;
  }
#endif /* CONFIG_DELAYPROC */
  
  while (node)
    {
      /* request��ž�����륵������­�����碌�Ƥ椯 */
      struct request *req = rb_entry_rq (node);
      total_size += req->nr_sectors;

      /* ����request�˰�ư���� */
      node = rb_next (node);
    }
  
  if (total_size >= buffer->block_size)
    {
      /* ���äѤ����ä��顢1���֤� */
      return 1;
    }

  /* ���äѤ��Ǥʤ��ʤ顢0���֤� */
  return 0;
}


static struct p2IoFilter_buffer *
p2IoFilter_find_area (struct p2IoFilter_info *info, unsigned long start, int is_dp)
{
  int i = 0;
  struct p2IoFilter_buffer *ret = NULL;
  PTRACE();

  BUG_ON (info == NULL);

  /* ���٤Ƥ���α�Хåե����������� */
  for (i = P2BF_MAX_BFS-1; i >= 0; i--)
    {
      struct p2IoFilter_buffer *buffer = &(info->buffers[i]);

      /* �ޤ��������ΰ褬ACTIVE���ɤ�����Ĵ������ */
      if (!test_bit (P2BF_ACTIVE, &buffer->flags))
	{
	  /* ACTIVE�Ǥʤ��ΰ�����Ф� */
	  continue;
	}

      /* ���äƤ����ΰ�ˡ��������Ƭ���������ޤޤ�Ƥ��뤫�����å� */
      if (buffer->start <= start
	  && start < (buffer->start + buffer->block_size))
	{
	  /* �ϰ���ʤ顢���դ��ä��Ȥ������Ȥǥ롼�פ�ȴ���� */
	  ret = buffer;
	  break;
	}
    }

#if defined(CONFIG_DELAYPROC)
  if (unlikely(ret && !BUF_IS_DELAYPROC(ret) && is_dp))
  {
	ret = NULL;
  }
#endif /* CONFIG_DELAYPROC */
  
  return ret;
}

static struct request *
elv_rb_find_tail (struct rb_root *root, sector_t sector)
{
  struct rb_node *n = root->rb_node;
  struct request *rq = NULL;
  PTRACE();
	
  while (n)
    {
      rq = rb_entry (n, struct request, rb_node);
      if (sector < (rq->sector + rq->nr_sectors))
	n = n->rb_left;
      else if (sector > (rq->sector + rq->nr_sectors))
	n = n->rb_right;
      else
	return rq;
    }

  return NULL;
}

static int
p2IoFilter_check_boundary (unsigned long start, unsigned long length,
			   unsigned long block_start,
			   unsigned long block_size)
{
  int ret = 0;
/*   PTRACE(); */

/*   printk( " start=%lX len=%lX blk_start=%lX blk_size=%lX\n", */
/* 		  start, length, block_start, block_size ); */
	
  /* ���⤽��֥�å��ˤϤޤ��Τ��ɤ��������å� */
/*   if ((length > 0) && (length < block_size) && (start >= block_start)) */
  if ((length > 0) && (length < block_size) && (start >= block_start))
    {
      /* ��Ƭ��������������������Ʊ��֥�å������äƤ��뤫�����å�
         �ʡᥢ������������ޤ����Ǥ��ʤ��������å��� */
      ret =
	((start - block_start) / block_size ==
	 (start + length - block_start - 1) / block_size) ? 1 : 0;
    }

/*   printk( " ret=%d (%s)\n", ret, (ret?"No-matagi/Dif. block":"Matagi/Dif. block") ); */
  return ret;
}

static inline void
p2IoFilter_prepare_exec (struct p2IoFilter_info *info, struct request *rq)
{
  struct p2IoFilter_buffer *buffer = rq->elevator_private;
  PTRACE();
	
  if (buffer != NULL)
    {
      /* ��������ȴ���ơ�exec_fifo���դ��ؤ��� */
      elv_rb_del (&buffer->buffer, rq);

#if defined(CONFIG_DELAYPROC)
	  /* SYS/USR1�ΰ�ξ�硢dirty������Ȥ�ǥ������ [�ٱ����] */
	  if (BUF_IS_DELAYPROC(buffer))
	  {
		  dec_delayproc_buf_cnt( info->dpinfo );
	  }
#endif /* CONFIG_DELAYPROC */
	  
      /* exec_fifo�����줿�顢private��NULL�ˤ��� */
      rq->elevator_private = NULL;
    }

  if(rq_data_dir(rq)==WRITE)
    {
      //printk("%lu-(%lu)>",info->exec_fifo_depth,rq->nr_sectors);
      info->exec_fifo_depth += rq->nr_sectors;
      //printk("%lu\n",info->exec_fifo_depth);
    }

  list_add_tail (&rq->queuelist, &info->exec_fifo);
  
  return;
}


static void
p2IoFilter_flush_buffered_rq (struct p2IoFilter_buffer *buffer)
{
  struct p2IoFilter_info *info = buffer->info;
  unsigned long irq_flags = 0L;
  PTRACE();

  /* ��α�Хåե����ٱ��оݤξ����̽�����Ԥʤ� */
  if (p2IoFilter_flush_delayproc_buffer(buffer))
  {
	  goto FIN;
  }
  
  /* ���٤��Ǥ��Ф��Τǡ����ΥХåե���̵�������� */
  if (!test_and_clear_bit (P2BF_ACTIVE, &buffer->flags))
    {
      PDEBUG( " Already In-Active buffer%d\n", buffer->id );
      goto FIN;
    }

  /* �����ॢ���Ȥ�̵�������� */
  del_timer_sync (&buffer->unplug_timer);

  PDEBUG( "==== flush buf%d ====\n", buffer->id );
  
  /* exec_fifo��å� */
  spin_lock_irqsave (&info->fifo_lock, irq_flags);
  
  while (rb_first (&buffer->buffer))
    {
      struct request *rq = rb_entry_rq (rb_first (&buffer->buffer));
      p2IoFilter_prepare_exec (info, rq);
    }

  /* exec_fifo��å���� */
  spin_unlock_irqrestore (&info->fifo_lock, irq_flags);
		  
  buffer->start = 0;
  buffer->last_access = jiffies;

FIN:

  return;
}

static inline void
p2IoFilter_set_block_params (struct p2IoFilter_info *info,
			     unsigned long rq_start,
			     unsigned long *block_start,
			     unsigned long *block_size)
{
  int is_sys = (rq_start < info->sys_boundary) ? 1 : 0;
  int area_offset = 0;
  PTRACE();
	
  area_offset = is_sys ? 0 : info->sys_boundary;
  *block_size = is_sys ? info->sys_block_sectors : info->usr_block_sectors;

  /* rq_start���顢��°����֥�å��ζ�����׻�����
     ��block_size�Ǥ�mod������Τ�Ʊ���� */
  *block_start = area_offset
    + (((rq_start - area_offset) / (*block_size)) * (*block_size));

  return;
}

static void
p2IoFilter_update_buffer (struct p2IoFilter_info *info,
			  struct p2IoFilter_buffer *buffer,
			  struct request *rq)
{
  struct request *_alias = NULL;
  PTRACE();
	
  /* ���Ǥ˽�°�Хåե���Ƚ�����Ƥ��ʤ����
     ��=��¸buffer���ɤ��Ф���ȯ��������ˤν����򤹤� */
  if (buffer == NULL)
    {
      /* FAT�ʤ顢META�ե饰��Ω�äƤ��� */
      if (rq_is_fat (rq))
	{
	  buffer = &info->buffers[P2BF_FAT];
	}
      else
	  {
		  /* �����ƥ��ΰ褫�桼���ΰ褫��Ƚ�� */
		  if (rq->sector < info->sys_boundary)
		  {
			  /* �����ƥ��ΰ��ѤΥХåե���������� */
			  buffer = &info->buffers[P2BF_SYS];
		  }
		  else
		  {
			  /* RT�ե饰��Ω�äƤ���С�USR2�ѥХåե���������� */
			  if (rq_is_rt (rq))
			  {
				  buffer = &info->buffers[P2BF_USR2];
			  }
			  else
			  {
				  buffer = &info->buffers[P2BF_USR1];
			  }
		  }
		  PDEBUG(" Select buf%d\n", buffer->id);
	  }

      /* �������褿�ʳ���rq�������buffer����ޤä��Τǡ�
         ��¸��buffer�����Ƥ��˴�����rq��������������򤹤� */
	  if (!BUF_IS_DELAYPROC(buffer))
		  p2IoFilter_flush_buffered_rq (buffer);

      /* ������buffer�Υѥ�᡼�������ꤷ�ʤ�����
         �����ޤ˴ؤ��Ƥϡ�flush_buffered_rq��̵��������Ƥ���Ϥ� */
      set_bit(P2BF_ACTIVE, &buffer->flags);
      buffer->last_access = jiffies;
      p2IoFilter_set_block_params (info, rq->sector, &buffer->start,
				   &buffer->block_size);
    }

  /* private�ΰ�˽�°�Хåե��ؤΥݥ��󥿤�Ͽ */
  rq->elevator_private = buffer;

RETRY:

  /* �Хåե���rq���ͤù��� */
  _alias = elv_rb_add (&buffer->buffer, rq);
  
  PDEBUG( " Add to buf%d: sector=%X\n", buffer->id, (unsigned int)rq->sector );

#if defined(CONFIG_DELAYPROC)
  /* SYS/USR1�ΰ�ξ�硢dirty������Ȥ򥤥󥯥���� [�ٱ����] */
  if (BUF_IS_DELAYPROC(buffer))
  {
	  inc_delayproc_buf_cnt( info->dpinfo );
  }
#endif /* CONFIG_DELAYPROC */
  
  if (unlikely (_alias != NULL))
    {
      /* Ʊ���������ֹ����Ҥ��錄�Τ�
         ��ö�����exec_fifo���ɤ��Ф� */
      p2IoFilter_prepare_exec (info, _alias);
      goto RETRY;
    }

  if(rq->cmd_flags & REQ_RW_SYNC)
    {
      /* �褿�Τ�SYNC�ξ��ϡ����ä��Ƚ񤭽Ф������򤹤� */
      set_bit(P2BF_TMOUT, &buffer->flags);
      del_timer_sync (&buffer->unplug_timer);
      queue_work (buffer->info->unplug_works, &buffer->unplug_work);      
    }
  else
    {
      /* �̾��request�ξ���buffer�ν񤭤��������ޤ����ꤷ�ʤ��� */
      p2IoFilter_update_timelimit (buffer);
    }
  return;
}

static int
p2IoFilter_merge (struct request_queue * q, struct request **req, struct bio *bio)
{
  struct p2IoFilter_info *info = q->elevator->elevator_data;
  struct p2IoFilter_buffer *buffer = NULL;
  struct request *_rq = NULL;
  PTRACE();
	
  BUG_ON (info == NULL);

  /* bio��READ�˴ؤ����Τʤ饹�롼���� */
  if (BIO_READ_DIR (bio))
    {
      goto FAIL;
    }

  /* ������ä�bio��°����֥�å��򸽺���α�椫�������� */
  buffer = p2IoFilter_find_area (info, bio->bi_sector, BIO_IS_DELAYPROC(bio));
  if (buffer == NULL)
    {
      /* ��α���Ƥ��ʤ���С��ޡ����оݤ�ʤ� */
      goto FAIL;
    }

  /* �����ޤ��褿�顢bio�˴�Ϣ����֥�å��ϻ��äƤ���Ȥ������� */
  /* �ޤ��ϡ������ޡ�����ǽ�ʸ����õ�� */
  _rq = elv_rb_find (&buffer->buffer, bio->bi_sector + bio_sectors (bio));
  if (_rq != NULL)
    {
      /* ���դ��ä������ޡ����������������
         ��elv_rq_merge_ok��ͳ�ǡ�allow_merge_fn��Ƥ�Ǥ���� */
      if (unlikely (!elv_rq_merge_ok (_rq, bio)))
	{
	  goto FAIL;
	}

      /* �����ޡ�����ǽ��request�����դ��ä����Ȥ����Τ��� */
      *req = _rq;
	  
      return ELEVATOR_FRONT_MERGE;
    }

  /* �����ޡ�����ǽ�ʸ����õ�� */
  /* �������������ޡ����ˤĤ��Ƥϡ������ʳ���������ˡ�
     ���ѥ֥�å�IO�ؤΥϥå���ơ��֥�Ǹ��դ��äƤ����礬¿�� */
  _rq = elv_rb_find_tail (&buffer->buffer, bio->bi_sector);
  if (_rq != NULL)
    {
      if (unlikely (!elv_rq_merge_ok (_rq, bio)))
	{
	  goto FAIL;
	}

      /* �����ޡ�����ǽ��request�����դ��ä����Ȥ����Τ��� */
      *req = _rq;

      return ELEVATOR_BACK_MERGE;
    }

FAIL:
  return ELEVATOR_NO_MERGE;
}

static void
p2IoFilter_merged_request (struct request_queue * q, struct request *req, int type)
{
  struct p2IoFilter_buffer *buffer = req->elevator_private;
/*   PTRACE(); */
	
  /* buffer��NULL�ʤ顢���Ǥ�exec_fifo�����äƤ��롣
     exec_fifo�����rq�Ϥ⤦������ʤ� */
  if (buffer == NULL)
    {
      return;
    }

  if (type == ELEVATOR_FRONT_MERGE)
    {
      /* FRONT MERGE�����顢��Ƭsector���Ѥ�ä��Ȥ������ȤʤΤǡ�
         ������¤����Ͽ���ʤ��� */
      elv_rb_del (&buffer->buffer, req);
      elv_rb_add (&buffer->buffer, req);
    }

  /* �ǽ������������֤򹹿����� */
  p2IoFilter_update_timelimit (buffer);

  /* �Хåե������äѤ��ˤʤäƤ��뤫
     �ޤ���SYNC°���Τ�Τ����ӹ�����褿��������å�����
     ���ˤ����н񤭤����������򥹥����塼�����Ͽ���� */
  if (p2IoFilter_check_total_size (buffer))
    {
	    //printk("%s: Buffer Full Ditected.\n",__FUNCTION__);
      del_timer_sync(&buffer->unplug_timer);
      queue_work (buffer->info->unplug_works, &buffer->unplug_work);
    }
  else if(req->cmd_flags & REQ_RW_SYNC)
    {
      set_bit(P2BF_TMOUT, &buffer->flags);
      del_timer_sync (&buffer->unplug_timer);
      queue_work (buffer->info->unplug_works, &buffer->unplug_work);
    }

  return;
}

static void
p2IoFilter_merged_requests (struct request_queue * q, struct request *req,
			    struct request *next)
{
  struct p2IoFilter_buffer *buffer = next->elevator_private;
/*   PTRACE(); */
	
  /* exec_fifo�����äƤ����Τ˴ؤ��ƸƤФ줿�餪������ */
  BUG_ON (buffer == NULL);

  /* �ޡ�������ƾä��Ƥ���request������оݤ���ä� */
  elv_rb_del (&buffer->buffer, next);
#if defined(CONFIG_DELAYPROC)
  /* SYS/USR1�ΰ�ξ�硢dirty������Ȥ�ǥ������ [�ٱ����] */
  if (BUF_IS_DELAYPROC(buffer))
  {
	  dec_delayproc_buf_cnt( buffer->info->dpinfo );
  }
#endif /* CONFIG_DELAYPROC */
  
  return;
}

static int
p2IoFilter_allow_merge (struct request_queue * q, struct request *req,
			struct bio *bio)
{
  struct p2IoFilter_buffer *buffer = req->elevator_private;
  unsigned long bi_sectors = bio_sectors(bio);
/*   PTRACE(); */

  /* DIRECT°����request��bio�ǿ�����äƤ����鿨��ʤ� */
  if (unlikely ((bio_drct (bio) && !(req->cmd_flags & REQ_DRCT))
		|| (!bio_drct (bio) && (req->cmd_flags & REQ_DRCT))))
    {
      //printk("%s: Failed - 2\n",__FUNCTION__);
      goto FAIL;
    }

  /* RT°����request��bio�ǿ�����äƤ����鿨��ʤ� */
  if (unlikely ((bio_rt (bio) && !rq_is_rt(req))
		|| (!bio_rt (bio) && rq_is_rt(req))))
    {
      //printk("%s: Failed - 2'\n",__FUNCTION__);
      goto FAIL;
    }
  
  /* bio��READ�˴ؤ����Τʤ饹�롼���� */
  if (BIO_READ_DIR (bio))
    {
      goto ALLOW;
    }

  /* ����exec_fifo�����äƤ����ΤˤĤ��ƤϿ���ʤ� */
  if (buffer == NULL)
    {
      //printk("%s: Failed - 3\n",__FUNCTION__);
      goto FAIL;
    }

  PDEBUG("  buf=%d\n", buffer->id);
  
  /* bio�����Ƥ���������������ޤ����Ǥ��ʤ��������å����� */
  if (!p2IoFilter_check_boundary (bio->bi_sector, bi_sectors,
				  buffer->start, buffer->block_size))
    {
      //printk("%s: Failed - 4 [%llu, %lu, %lu, %lu]\n",__FUNCTION__,
      //     bio->bi_sector, bi_sectors, buffer->start, buffer->block_size);
      goto FAIL;
    }

  /* �Хå��ޡ�����ǽ��Ĵ������ */
  if ((req->sector + req->nr_sectors) == bio->bi_sector)
    {
      if ((buffer->start + buffer->block_size) == bio->bi_sector)
	{
	  /* �Хå��ޡ������뤳�Ȥˤ�äƶ�����ޤ����Ǥ��ޤ���� */
	  //printk("%s: Failed - 5\n",__FUNCTION__);
	  goto FAIL;
	}
      else
	{
	  goto ALLOW;
	}
    }
  else
    {
      /* �ե��ȥޡ�����ǽ��Ĵ������ */
      if ((bio->bi_sector + bi_sectors) == req->sector)
	{
	  goto ALLOW;
	}
      else
	{
	  //printk("%s: Failed - 6\n",__FUNCTION__);
	  goto FAIL;
	}
    }

ALLOW:
/*   PDEBUG( "Allow\n" ); */
  return 1;

FAIL:
/*   PDEBUG( "Fail\n" ); */
  return 0;
}

static void
p2IoFilter_do_dispatch_request(struct request_queue *q)
{
  struct p2IoFilter_info *info = q->elevator->elevator_data;
  struct request *req = NULL;
  unsigned long irq_flags = 0L;

  /* exec_fifo��å� */
  spin_lock_irqsave (&info->fifo_lock, irq_flags);
  
  /* exec_fifo����Ƭ��������� */
  req = rq_entry_fifo (info->exec_fifo.next);

  PTRACE( " rq->sector = %08X %s", (int)req->sector,
		  (rq_data_dir(req)==WRITE?"WRITE":"READ") );
  //printk("%s: SCT:%llu (%lu)\n",__FUNCTION__,req->sector,req->nr_sectors);

  /* ��Ƭ��request��exec_fifo����ȴ�� */
  rq_fifo_clear (req);

  if(rq_data_dir(req)==WRITE)
    {
      //printk("%lu-(%lu)>",info->exec_fifo_depth, req->nr_sectors);
      info->exec_fifo_depth -= req->nr_sectors;
      //printk("%lu\n",info->exec_fifo_depth);
    }

  /* �ǥХɥ��queue���Ϥ� */
  elv_dispatch_add_tail (q, req);

  /* exec_fifo��å���� */
  spin_unlock_irqrestore (&info->fifo_lock, irq_flags);
}

static int
p2IoFilter_dispatch_requests (struct request_queue * q, int force)
{
  struct p2IoFilter_info *info = q->elevator->elevator_data;
  int i = 0;
  PTRACE();
	
  /* ���Ǥ�exec_fifo������Ƥ����Τ��ʤ��� */
  if (!list_empty (&info->exec_fifo))
    {
      goto DISPATCH;
    }

  /* ��α�Хåե����������ơ��񤭽Ф����Τ��ʤ��������å� */
  for (i = 0; i < P2BF_MAX_BFS; i++)
    {
      if (p2IoFilter_check_total_size (&info->buffers[i])	/* �Хåե����ե�ξ�� */
	  || force		/* �����񤭤���(sync)����ξ�� */
	  || test_and_clear_bit (P2BF_TMOUT,
				 &info->buffers[i].flags) /* �����ॢ���Ȥ�ȯ�����Ƥ������ */ )
	{
	  /* ��α�Хåե������Ƥ��Ǥ��Ф� */
	  p2IoFilter_flush_buffered_rq (&info->buffers[i]);
	}
    }

  /* �����ޤ����exec_fifo�����ʤ��뤳�ȤϤʤ� */
  if (list_empty (&info->exec_fifo))
    {
      return 0;
    }

DISPATCH:

  p2IoFilter_do_dispatch_request(q);
  
  return 1;
}

static void
p2IoFilter_add_request (struct request_queue *q, struct request *rq)
{
  struct p2IoFilter_info *info = q->elevator->elevator_data;
  struct p2IoFilter_buffer *buffer = NULL;
  unsigned long area_offset = 0;
  unsigned long area_block = 0;
  int check_boundary = 0;
#if defined(CONFIG_DELAYPROC)
  dev_t dev = info->dev;
#endif /* CONFIG_DELAYPROC */
  PTRACE();

  PDEBUG( " rq->sector = %08X %s\n", (int)rq->sector,
		  (rq_data_dir(rq)==WRITE?"WRITE":"READ") );

  /* rq��READ�˴ؤ����Τʤ�¨exec_fifo�ˤĤʤ� */
  if (rq_data_dir(rq)==READ)
    {
      goto PREPARE;
    }

#if defined(CONFIG_DELAYPROC)
  /* RT_ON���֡��ٱ������ϡ��ٱ��о�rq�ϤȤˤ���SYS/USR1�ΰ�ˤĤʤ� [�ٱ����] */

  /* rq��MI�˴ؤ����Τʤ�¨exec_fifo�ˤĤʤ� */
  if (rq_is_mi(rq))
    {
      PDEBUG(" Add MI to exec_fifo\n");
      goto PREPARE;
    }

  if (RQ_IS_DELAYPROC(rq))
  {
  if (chk_rton(MAJOR(dev))
	  || is_delayproc_dev(dev)
	  || is_delayproc_run_dev(dev))
  {
	  buffer = (rq->sector < info->sys_boundary)
		  ? &info->buffers[P2BF_SYS] /* SYS�ΰ�ʤΤǡ�SYS�ΰ�Хåե������� */
		  : (!rq_is_rt(rq)
			 ? &info->buffers[P2BF_USR1] /* RT�ե饰��Ω�äƤʤ��Τǡ�USR1�ΰ�Хåե������� */
			 : NULL);
	  
	  /* rq���ٱ��оݤǤ������SYS/USR1�ΰ�ˤĤʤ� */
	  if (NULL != buffer)
	  {
		  PDEBUG(" Add buf%d\n", buffer->id);

		  if (!test_bit(P2BF_ACTIVE, &buffer->flags))
		  {
			  set_bit(P2BF_ACTIVE, &buffer->flags);
			  p2IoFilter_set_block_params (info, rq->sector, &buffer->start,
										   &buffer->block_size);
		  }
		  buffer->last_access = jiffies;
		  p2IoFilter_update_buffer (info, buffer, rq);
		  return;
	  }
  }
  }
#endif /* CONFIG_DELAYPROC */
  
  /* �ΰ褬�Ҥä�����buffer�򸡺����� */
  buffer = p2IoFilter_find_area (info, rq->sector, RQ_IS_DELAYPROC(rq));
  
  area_offset = (buffer == NULL)
    ? ((rq->sector >= info->sys_boundary) ? info->sys_boundary : 0)
    : (buffer->start);

  area_block = (buffer == NULL)
    ? ((rq->sector >=
	info->sys_boundary) ? info->usr_block_sectors : info->
       sys_block_sectors) : (buffer->block_size);

  /* �֥�å�������ޤ�����ʤ���Ĵ�٤� */
  check_boundary =
    p2IoFilter_check_boundary (rq->sector, rq->nr_sectors, area_offset,
			       area_block);

  if (check_boundary != 0)
    {
      /* �Хåե���������硣Ŭ���ʥХåե����������롣 */
      p2IoFilter_update_buffer (info, buffer, rq);
      return;
    }

  /* ���Τޤ޼¹ԥ��塼��������� */
  if (buffer != NULL)
    {
      /* Ƭ���Ҥä����äƤ���Хåե������ä����ϡ�
         ����⴬��ź���ˤ��� */
      p2IoFilter_flush_buffered_rq (buffer);
    }

PREPARE:

  p2IoFilter_prepare_exec (info, rq);
  /* work_struct��������ä˰�̣�Ϥʤ� */
  queue_work (info->unplug_works, &info->buffers[0].unplug_work);

  return;
}

static int
p2IoFilter_queue_empty (struct request_queue * q)
{
  struct p2IoFilter_info *info = q->elevator->elevator_data;
  int i = 0;
  int ret = 0;
/*   PTRACE(); */
	
  BUG_ON (info == NULL);

  /* �ޤ�exec_fifo������Ĵ�١����ʤ�ƥХåե��ξ��֤򸫤� */
  if ((ret = list_empty (&info->exec_fifo)) == 1)
    {
      for (i = 0; i < P2BF_MAX_BFS; i++)
	{
	  struct p2IoFilter_buffer *buffer = &info->buffers[i];

	  /* ACTIVE�Ƕ��Ǥʤ��Хåե����ߤĤ��ä��顢�롼�פ�ȴ���� */
	  if (test_bit(P2BF_ACTIVE, &buffer->flags)
	      && (buffer->buffer.rb_node != NULL))
	    {
		  /* �ٱ�����Ե���ξ��϶��Ȥߤʤ�ɬ�פ��� [�ٱ����] */
			if (BUF_IS_DELAYPROC(buffer)
				&& is_delayproc_dev(info->dev)
				&& !get_delayproc_type(info->dpinfo))
			{
				PDEBUG( "!!! Fake !!!\n" );
				continue;
			}
			
	      ret = 0;
	      break;
	    }
	}
    }

/*   if (!ret) PDEBUG("fifo/buffer is NOT EMPTY!\n"); */
  return ret;
}

static struct request *
p2IoFilter_former_request (struct request_queue * q, struct request *rq)
{
  struct p2IoFilter_buffer *buffer = rq->elevator_private;
  struct request *_rq = NULL;
/*   PTRACE(); */

/*   PDEBUG( "rq->sector = %08X %s\n", (int)rq->sector, */
/* 		  (rq_data_dir(rq)==WRITE?"WRITE":"READ") ); */
  
  /* ���Ǥ�exec_fifo�����äƤ����ΤˤĤ��ƤϷ�����򤢤��ʤ� */
  if (buffer == NULL)
    {
      goto FIN;
    }

  /* Ʊ��buffer��Ǥ�������������� */
  _rq = elv_rb_former_request (q, rq);
  if (_rq != NULL)
    {
      /* DIRECT°�������פ��ʤ����Ϥ��� */
      if (((rq->cmd_flags & REQ_DRCT) && !(_rq->cmd_flags & REQ_DRCT))
	  || (!(rq->cmd_flags & REQ_DRCT) && (_rq->cmd_flags & REQ_DRCT)))
	{
	  _rq = NULL;
	}
    }

FIN:
  return _rq;
}

static struct request *
p2IoFilter_latter_request (struct request_queue * q, struct request *rq)
{
  struct p2IoFilter_buffer *buffer = rq->elevator_private;
  struct request *_rq = NULL;
/*   PTRACE(); */

/*   PDEBUG( "rq->sector = %08X %s nr_sectors = %ld\n", (int)rq->sector, */
/* 		  (rq_data_dir(rq)==WRITE?"WRITE":"READ"), */
/* 		  rq->nr_sectors ); */
  
  /* ���Ǥ�exec_fifo�����äƤ����ΤˤĤ��ƤϷ�����򤢤��ʤ� */
  if (buffer == NULL)
    {
      goto FIN;
    }

  /* Ʊ��buffer��Ǥ�������������� */
  _rq = elv_rb_latter_request (q, rq);
  if (_rq != NULL)
    {
/* 		PDEBUG( "_rq->sector = %08X %s\n", (int)_rq->sector, */
/* 				(rq_data_dir(_rq)==WRITE?"WRITE":"READ") ); */
		
      /* DIRECT°�������פ��ʤ����Ϥ��� */
      if (((rq->cmd_flags & REQ_DRCT) && !(_rq->cmd_flags & REQ_DRCT))
	  || (!(rq->cmd_flags & REQ_DRCT) && (_rq->cmd_flags & REQ_DRCT)))
	{
		PDEBUG( "rq->cmd_flags != _rq->cmd_flags\n" );
	  _rq = NULL;
	}
    }

FIN:
  return _rq;
}

static void
p2IoFilter_time_expired (unsigned long data)
{
  struct p2IoFilter_buffer *buffer = (struct p2IoFilter_buffer *) data;
  
  BUG_ON (buffer == NULL);
  PTRACE( " id = %d", buffer->id );

  /* �����鰷����α�Хåե���ͭ���ʤ�ΤʤΤ��� */
  if (likely (test_bit (P2BF_ACTIVE, &buffer->flags)))
    {
      PDEBUG( "Set TMOUT flag and execute unplug_work\n" );
		
      /* �����ॢ���Ȥ�ȯ���������Ȥ��Τ餻��ե饰��Ω�Ƥ� */
      set_bit (P2BF_TMOUT, &buffer->flags);


      if(buffer->id == P2BF_USR2)
      {
	      printk("USR2 : <TimeOut>\n");
      }

      //printk("%s: Timeout Occured.\n",__FUNCTION__);

      /* �����񤭤����Υ������򥹥����塼��󥰤��� */
      queue_work (buffer->info->unplug_works, &buffer->unplug_work);

      /* TODO �߷פǤϡ�������queue�ˤ�TMOUT�ե饰��Ω�Ƥ뤳�Ȥˤ��Ƥ��� */
      // set_bit(QUEUE_FLAG_TMOUT, &buffer->queue->flags);
    }

  return;
}

static void
p2IoFilter_unplug_device (struct work_struct *work)
{
  struct p2IoFilter_buffer *buffer =
    container_of (work, struct p2IoFilter_buffer, unplug_work);
  unsigned long flags = 0;
  PTRACE();
	
  BUG_ON (buffer == NULL);

  /* ���Τ�����Υ�å��ΰ������ϡ�¾��i/o scheduler�򻲹ͤˤ��Ƥ��� */
  spin_lock_irqsave (buffer->queue->queue_lock, flags);

  /* unplugư���¹� */
  blk_start_queueing (buffer->queue);
  spin_unlock_irqrestore (buffer->queue->queue_lock, flags);
  
  return;
}

/* �¹��Ԥ����塼�ˤޤ�AU�ʾ�Υǡ������������
   ������bio����������Τ��Ԥ����뤿����䤤��碌�������롣 */
static int
p2IoFilter_ready_queue (struct request_queue * q)
{
  struct p2IoFilter_info *info = q->elevator->elevator_data;
  unsigned long flags = 0;
  int locked = 0;
  int ret = 0;
  /* ���Υ����ɼ������ʳ���USR1�Хåե������Ȥ�ʤ��褦�ˤ����Τǡ�
     �ʰפʼ����Ǥ��ޤ��Ƥ��ޤ��ޤ���*/
  /* �ٱ������USR1����Ѥ���Τǡ�USR2���ѹ�! */
  struct p2IoFilter_buffer *buf_usr = &info->buffers[P2BF_USR2];
  PTRACE();
  
  locked = spin_trylock_irqsave (q->queue_lock, flags);

  /* USER�Хåե��ˤ��Ǥ�ï�����äƤ���ʤ顢
     ���줬������ˤʤ�ޤǤ�bio���������ƹ���ʤ� */
  if( test_bit(P2BF_ACTIVE, &buf_usr->flags)
      && (buf_usr->buffer.rb_node != NULL)
      && !(p2IoFilter_check_total_size(buf_usr)) )
    {
      ret = 1;
    }
  else
    {
      /* USER�Хåե�������äݡ�
	 �⤷����������ʻ��֤�������Ǥ��Ф����ˤξ��ϡ�
	 exec_fifo�ο����򸫤ơ����ޤ꿼���褦�ʤ��Ԥ����� */
      int max_depth = EXEC_FIFO_MAX_DEPTH;

      if(p2IoFilter_check_total_size(buf_usr) && max_depth!=0)
	{
	  max_depth--;
	}

      if( info->exec_fifo_depth < (max_depth * info->usr_block_sectors) )
	{
	  ret = 1;
	}
      else
	{
	  ret = 0;
	}
    }

  if(likely(locked))
    spin_unlock_irqrestore (q->queue_lock, flags);

  return ret;
}

static void
p2IoFilter_force_dispatch (struct request_queue * q, int area)
{
  struct p2IoFilter_info *info = q->elevator->elevator_data;
  unsigned long flags = 0;
  struct p2IoFilter_buffer *buffer;
  int *targets = NULL;
  int i=0;

  /* 0����ꤵ�줿���̾�����ΰ�����Ǥ��Ф� */
  if(area==0)
  {
	  targets = kmalloc(sizeof(int)*3,GFP_ATOMIC);
	  targets[0] = P2BF_SYS;
	  targets[1] = P2BF_USR1;
	  targets[2] = -1; /* α��ΰ� */
  }
  else if(area==1)  /* 1����ꤵ�줿���ȼ��ϤΤ��Ǥ��Ф� */
  {
	  targets = kmalloc(sizeof(int)*2,GFP_ATOMIC);
	  targets[0] = P2BF_USR2;
	  targets[1] = -1; /* α��ΰ� */	  
  }
  else if(area==-1) /* -1�ʤ����ΰ�ʤ����餯����ϻȤ��ʤ��� */
  {
	  targets = kmalloc(sizeof(int)*4,GFP_ATOMIC);
	  targets[0] = P2BF_SYS;
	  targets[1] = P2BF_USR1;
	  targets[2] = P2BF_USR2;
	  targets[3] = -1; /* α��ΰ� */	  
  }
  else
  {
	  return;
  }

  for(i=0; targets[i]!=-1; i++)
  {
	  buffer = &info->buffers[targets[i]];

	  spin_lock_irqsave (q->queue_lock, flags);

	  if(test_bit(P2BF_ACTIVE, &buffer->flags))
	  {
		  set_bit(P2BF_TMOUT, &buffer->flags);
		  del_timer_sync (&buffer->unplug_timer);
		  queue_work (info->unplug_works, &buffer->unplug_work);
	  }

	  spin_unlock_irqrestore (q->queue_lock, flags);
  }

  kfree(targets);

  return;
}

static int
p2IoFilter_init_buffers (struct request_queue * q,
			 struct p2IoFilter_info *info,
			 struct p2IoFilter_buffer *buffer)
{
  int i = 0;
  PTRACE();
	
  /* ǰ�Τ��ᡢ���줫�鿨������ΰ�Υ����å� */
  if (unlikely (buffer == NULL))
    {
      return -EINVAL;
    }

  /* �ƥХåե��ν���� */
  for (i = 0; i < P2BF_MAX_BFS; i++)
    {
      struct p2IoFilter_buffer *io_buffer = &(buffer[i]);

      /* �ΰ��ޤ���������� */
      memset (io_buffer, 0, sizeof (struct p2IoFilter_buffer));

      /* ���ޤ��̣�Ϥʤ������ǽ��������֤�����Ƥ��� */
      io_buffer->last_access = jiffies;

      /* �Хåե����Τν���� */
      io_buffer->buffer = RB_ROOT;

      /* �����ॢ�����ѤΥѥ�᡼������� */
      init_timer (&io_buffer->unplug_timer);
      io_buffer->unplug_timer.function = p2IoFilter_time_expired;
      io_buffer->unplug_timer.data = (unsigned long) io_buffer;

      /* �����ॢ���ȡ��������ν���� */
      INIT_WORK (&io_buffer->unplug_work, p2IoFilter_unplug_device);
      io_buffer->id = i;

      /* ���줾��Υ����ॢ���Ȼ��֤����� */
      switch (i)
	{
	case P2BF_FAT:
	  /* system�ΰ�Τ���P2�����ɤ��ɤ�����FAT�ؤΥ����������������ѹ����� */
	  io_buffer->block_size =
	    info->sys_block_sectors ? info->sys_block_sectors : info->
	    usr_block_sectors;
	  io_buffer->max_wait_time = FAT_TMOUT;
	  break;

	case P2BF_SYS:
		/* system�ΰ�Τ���P2�����ɤ��ɤ����ǥ����������������ѹ����� */
	  io_buffer->block_size =
	    info->sys_block_sectors ? info->sys_block_sectors : (unsigned long)-1;
	  io_buffer->max_wait_time = SYS_TMOUT;
	  break;

	case P2BF_USR1:
	  io_buffer->block_size = info->usr_block_sectors;
	  io_buffer->max_wait_time = SYS_TMOUT;  /* SYS�ΰ��Ʊ���ͤ����� */
	  break;
	  
	default:
	  io_buffer->block_size = info->usr_block_sectors;
	  io_buffer->max_wait_time = DEF_TMOUT;
	}

      /* lock�ν���� */
      spin_lock_init (&io_buffer->buffer_lock);

      io_buffer->queue = q;
      io_buffer->info = info;
    }

  return 0;
}

static void *
p2IoFilter_init_queue (struct request_queue * q)
{
  struct p2IoFilter_info *info = NULL;
  spd_dev_t *spd_dev = (spd_dev_t *) q->queuedata;
  int ret = 0;
  int i;
  PTRACE();
	
  /* �ɥ饤�Ф��ǡ���������Ƥ���Ƥ��뤫�����å� */
  if (unlikely (spd_dev == NULL))
    {
	  printk("[p2IoFilter] spd_dev is NULL!\n");
      return NULL;
    }

  /* ����scheduler���ȼ��ѥ�᡼���ΰ����ݤ��� */
  info =
    (struct p2IoFilter_info *) kmalloc (sizeof (struct p2IoFilter_info),
					GFP_KERNEL);
  if (unlikely (info == NULL))
    {
      return NULL;
    }

  /* �¹�FIFO�ν���� */
  INIT_LIST_HEAD (&info->exec_fifo);
  info->exec_fifo_depth = 0;
  spin_lock_init (&info->fifo_lock);
  
  /* �����ॢ�����Ѥ�workqueue�ν���� */
  info->unplug_works = create_workqueue ("p2IoFilter_Timeout");
  info->dev = spd_dev->devnum;
  PDEBUG("devnum = %d:%d\n", MAJOR(info->dev), MINOR(info->dev));
  
  PDEBUG("spd_dev->n_area = %d\n", spd_dev->n_area);
  for (i = 0 ; i < spd_dev->n_area; i++)
  {
	  PDEBUG("spd_dev->area[%d].start = %08X\n", i, spd_dev->area[i].start);
	  PDEBUG("spd_dev->area[%d].end   = %08X\n", i, spd_dev->area[i].end  );
	  PDEBUG("spd_dev->area[%d].wsize = %08X\n", i, spd_dev->area[i].wsize);
  }
  
  /* P2��Ϣ�ѥ�᡼���μ����� */
  if (spd_dev->n_area == 1)	/* ��P2�����ɤξ�� */
    {
      info->sys_boundary = 0;
      info->sys_block_sectors = 0;
      info->usr_block_sectors = spd_dev->area[0].wsize;
      info->sys_start_sector = 0;
      info->usr_start_sector = spd_dev->area[0].start;
    }
  else if (spd_dev->n_area == 2)	/* ��P2�����ɤξ�� */
    {
      info->sys_boundary = spd_dev->area[1].start;
      info->sys_block_sectors = spd_dev->area[0].wsize;
      info->usr_block_sectors = spd_dev->area[1].wsize;
      info->sys_start_sector = spd_dev->area[0].start;
      info->usr_start_sector = spd_dev->area[1].start;
    }
  else
    {
      printk ("Unsupported P2Card Version.\n");
      return NULL;
    }

#if defined(CONFIG_DELAYPROC)
  /* �ٱ�����ν���� [�ٱ����] */
  ret = init_delayproc_info (&info->dpinfo, spd_dev->devnum, dp_except_inode_list);
  if (ret)
  {
	  return NULL;
  }
  info->dpinfo->q = q;

  /* �ꥯ�����ȥ��塼�����ͤ��ѹ� [�ٱ����] */
  PDEBUG("Change unplug_thresh %d to %d\n", q->unplug_thresh, DELAYPROC_UNPLUG_THRESH);
  q->unplug_thresh = DELAYPROC_UNPLUG_THRESH;
  
  PDEBUG("Change num of requests thresh %ld to %d\n", q->nr_requests, DELAYPROC_NR_REQUESTS);
  q->nr_requests = DELAYPROC_NR_REQUESTS;
#endif /* CONFIG_DELAYPROC */
  
  /* ����α�Хåե��ν���� */
  ret = p2IoFilter_init_buffers (q, info, info->buffers);
  if (unlikely (ret < 0))
    {
      return NULL;
    }

  return info;
}

static void
p2IoFilter_exit_queue (elevator_t * e)
{
  struct p2IoFilter_info *info = e->elevator_data;
  int i = 0;
  PTRACE();
	
  for (i = 0; i < P2BF_MAX_BFS; i++)
    {
      struct p2IoFilter_buffer *buffer = &(info->buffers[i]);

      /* ���ƤΥ����ޡ�������������λ���� */
      del_timer_sync (&buffer->unplug_timer);

      /* ��α�Хåե��������ɤ������ǧ���� */
      BUG_ON (buffer->buffer.rb_node != NULL);
    }

  /* �����ॢ������workqueue�θ����դ� */
  destroy_workqueue (info->unplug_works);

  /* ���λ����Ǽ¹�FIFO�����Ǥʤ��Ȥ������� */
  BUG_ON (!list_empty (&info->exec_fifo));

#if defined(CONFIG_DELAYPROC)
  /* �ٱ�����θ����դ� [�ٱ����] */
  exit_delayproc_info (info->dpinfo);
  info->dpinfo = NULL;
#endif /* CONFIG_DELAYPROC */
  
  /* i/o scheduler�Ѥ��ȼ����ݤ��Ƥ��������ΰ��������� */
  kfree (info);

  return;
}


static struct elevator_type iosched_p2IoFilter = {
  .ops = {
	  .elevator_merge_fn = p2IoFilter_merge,
	  .elevator_merged_fn = p2IoFilter_merged_request,
	  .elevator_merge_req_fn = p2IoFilter_merged_requests,
	  .elevator_allow_merge_fn = p2IoFilter_allow_merge,
	  .elevator_dispatch_fn = p2IoFilter_dispatch_requests,
	  .elevator_add_req_fn = p2IoFilter_add_request,
	  .elevator_queue_empty_fn = p2IoFilter_queue_empty,
	  .elevator_former_req_fn = p2IoFilter_former_request,
	  .elevator_latter_req_fn = p2IoFilter_latter_request,
	  .elevator_init_fn = p2IoFilter_init_queue,
	  .elevator_exit_fn = p2IoFilter_exit_queue,
	  .elevator_ready_fn = p2IoFilter_ready_queue,
	  .elevator_force_dispatch_fn = p2IoFilter_force_dispatch,
#if defined(CONFIG_DELAYPROC)
	  .elevator_set_req_fn = p2IoFilter_set_req,
	  .elevator_put_req_fn = p2IoFilter_put_req,
	  .elevator_completed_req_fn = p2IoFilter_completed_req,
	  .elevator_force_delayproc_fn = p2IoFilter_force_delayproc,
#endif /* CONFIG_DELAYPROC */
	  },
#if defined(CONFIG_DELAYPROC)
  .elevator_attrs =	p2IoFilter_attrs,
#endif /* CONFIG_DELAYPROC */
  .elevator_name = "p2IoFilter",
  .elevator_owner = THIS_MODULE,
};

static int __init
p2IoFilter_init (void)
{
  elv_register (&iosched_p2IoFilter);
  printk(KERN_INFO " %s ver. %s\n", iosched_p2IoFilter.elevator_name, P2IOFILTER_VERSION);
  return 0;
}

static void __exit
p2IoFilter_exit (void)
{
  elv_unregister (&iosched_p2IoFilter);
}

module_init (p2IoFilter_init);
module_exit (p2IoFilter_exit);

MODULE_AUTHOR ("Seiji HORITA");
MODULE_LICENSE ("GPL");
MODULE_DESCRIPTION ("p2IOFilter");
