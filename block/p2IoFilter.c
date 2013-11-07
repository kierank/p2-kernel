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

/* バージョン番号 */
#define P2IOFILTER_VERSION "1.10"

/* デバッグ用メッセージ定義 */
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


/* バッファの種類 */
enum buffer_types
{
  P2BF_FAT,			/* FAT領域用(bioにMETAフラグが立っているもの) */
  P2BF_SYS,			/* SYS領域用（system領域） */
  P2BF_USR1,			/* USER領域（2面をLRUで使用） */
  P2BF_USR2,
  P2BF_MAX_BFS,			/* バッファの数 */
};

/* それぞれのバッファのタイムアウト時間 */
#define FAT_TMOUT (HZ >> 1)		/* 0.5sec(Windowsフィルタドライバと同じ) */
#define SYS_TMOUT (HZ >> 1)		/* 0.5sec(Windowsフィルタドライバと同じ) */
#define DEF_TMOUT (180*HZ)		/* ユーザ領域に使う */

#define EXEC_FIFO_MAX_DEPTH  (2)        /* AU何面ぶんまでexec_fifoにつなぐのを許すか */

/* p2IoFilter_buffer->flags のビット */
enum buffer_flags
{
  P2BF_ACTIVE,			/* このバッファは使用している */
  P2BF_TMOUT,			/* このバッファはタイムアウトした */
};

struct p2IoFilter_info;

/***
struct p2IoFilter_buffer
それぞれの保留キューに対して割り当てられる
管理用パラメータとキューの実体
***/
struct p2IoFilter_buffer
{

  unsigned long flags;		/* キューの状態を示すフラグ */
  unsigned long start;		/* 現在扱っている領域の先頭セクタ番号 */
  unsigned long block_size;	/* 現在扱っている領域のアクセスサイズ */
  struct rb_root buffer;	/* 保留キューの実体 */
  unsigned long last_access;	/* 最終アクセス時間(jiffies) */
  struct timer_list unplug_timer;	/* 強制書きだしタイマー */
  struct work_struct unplug_work;	/* 強制書きだし用タスク */
  unsigned long max_wait_time;	/* タイムアウト時間 */
  spinlock_t buffer_lock;	/* このバッファに関するlock */
  struct request_queue *queue;	/* このバッファが所属しているqueue */
  struct p2IoFilter_info *info;	/* このバッファが所属しているFilter */
  int id;					/* このバッファの種類(buffer_types) */
};

/***
struct p2IoFilter_info
各々のデバイスに対してひとつ割り当てられる構造体
***/
struct p2IoFilter_info
{

  struct p2IoFilter_buffer buffers[P2BF_MAX_BFS];	/* それぞれの領域を扱う保留キュー */
  struct list_head exec_fifo;	/* 実行の準備ができたrequestのFIFO */
  unsigned long exec_fifo_depth; /* 実行準備されているデータ総量（セクタ単位） */
  struct workqueue_struct *unplug_works;	/* タイムアウトタスク用のワークキュー */
  unsigned long sys_boundary;	/* P2のシステム領域とユーザー領域の境界セクタ */
  unsigned long sys_block_sectors;	/* P2のシステム領域のアクセスサイズ */
  unsigned long usr_block_sectors;	/* P2のユーザー領域のアクセスサイズ */
  unsigned long sys_start_sector;	/* P2のシステム領域の先頭セクタ番号 [遅延処理] */
  unsigned long usr_start_sector;	/* P2のユーザー領域の先頭セクタ番号 [遅延処理] */
  spinlock_t fifo_lock;	/* 実行FIFOに関するlock */
  struct delayproc_info_s *dpinfo; /* 遅延処理情報 [遅延処理] */
  dev_t dev; /* デバイス番号 [遅延処理] */
};

/* bioがread方向であったときには、BIO_RWビットが寝ている */
#define BIO_READ_DIR(bio) ((bio->bi_rw & (1UL << BIO_RW)) ? 0 : 1)


/* プロトタイプ宣言 */
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


/* -------------- 遅延処理関連定義・関数 -------------- */
#if defined(CONFIG_DELAYPROC)

/* SYS・USR1領域かどうかを判定 [遅延処理] */
# define BUF_IS_DELAYPROC(buf) ((buf->id == P2BF_SYS) || (buf->id == P2BF_USR1))
# define RQ_IS_DELAYPROC(rq) (rq_is_dirent(rq) || (!rq_is_rt(rq) && !rq_is_fat(rq)))
# define BIO_IS_DELAYPROC(bio) (bio_dirent(bio) || (!bio_rt(bio) && !bio_fat(bio)))

/* リクエストキューパラメータ [遅延処理] XXX 要調整 */
enum p2IoFilter_queue_thresh {
  DELAYPROC_UNPLUG_THRESH = 50,
  DELAYPROC_NR_REQUESTS = BLKDEV_MAX_RQ * 10,
};


/* 遅延処理対象外inode確認用関数 [遅延処理] */
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

/* 遅延処理対象外inode確認用関数リスト [遅延処理] */
static dp_except_inode_t dp_except_inode_list[] = {
  dp_inode_is_rt,
  dp_inode_is_fat,
  NULL,
};


/* --- 遅延処理write順番 関連 --- */
# if defined(CONFIG_DELAYPROC_WRITE_ORDER)

/* 遅延処理write順番→バッファ変換 */
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
  /* 念のため引数チェック */
  if (unlikely(NULL == info->dpinfo)) {
    printk("[p2IoFilter] dpinfo is NULL at %s!\n", __FUNCTION__);
    return (NULL);
  }
  dpinfo = info->dpinfo;

  if (order >= DELAYPROC_ORDER_MAXNR) {
    printk("[p2IoFilter] dpinfo->order is invalid(%d)!\n", order);
    return (NULL);
  }

  /* 遅延処理write順番からバッファを選択 */
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
/* ------------------------------ [遅延処理write順番] */

/* 遅延処理で必要になったI/O schedulerメソッドとsysfs [遅延処理] */
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
  
  /* 遅延処理からの呼び出しなので自前でロックをかける */
  spin_lock_irqsave (q->queue_lock, flags);
  
  /* 遅延処理のコンテキストなので、遅延処理ビットを立てる */
  set_task_delayprocd( current );

# if defined(CONFIG_DELAYPROC_WRITE_ORDER)

  /* 遅延処理write順番に応じてバッファを選択してflushする */

  /* 遅延処理write順番が正規の値か確認 */
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

  /* 引数に関係なく遅延処理用バッファすべてをflushする */
  p2IoFilter_flush_buffered_rq (&info->buffers[P2BF_SYS]);
  p2IoFilter_flush_buffered_rq (&info->buffers[P2BF_USR1]);

# endif /* CONFIG_DELAYPROC_WRITE_ORDER */

  /* dispatch実行 */
  if (!list_empty (&info->exec_fifo)) {
    p2IoFilter_do_dispatch_request (q);
  } else {
    PDEBUG("Empty exec_fifo\n");
  }
  
  /* 強制書き出し実行 */
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
  unsigned long before_quot = (unsigned long)-1; /* -1で初期化 */
  int nr_blk = 0;
  unsigned long irq_flags = 0L;

# if defined(CONFIG_DELAYPROC_WRITE_ORDER)
  struct rb_root *root = &buffer->buffer;
  struct rb_node *node = rb_first (root);
  unsigned char order = get_delayproc_order(dpinfo);
  PDEBUG(" == order:%d ==\n", order);
# endif /* CONFIG_DELAYPROC_WRITE_ORDER */

  /* 領域別の遅延処理I/Oブロックサイズとブロック数を取得 */
  if (buffer->id == P2BF_SYS) {
    /* システム領域 */
    blk_size = dpinfo->params.size_sys;
    nr_blk = dpinfo->params.nr_sys;
    offset = info->sys_start_sector;
  } else {
    /* ユーザ領域 */
    blk_size = dpinfo->params.size_usr;
    nr_blk = dpinfo->params.nr_usr;
    offset = info->usr_start_sector;
  }

  /* exec_fifoロック */
  spin_lock_irqsave (&info->fifo_lock, irq_flags);

  /* メイン処理 */
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

    /* 初回は同じ値を入れる */
    if ((unsigned long)-1 == before_quot) {
      before_quot = current_quot;
    }

    /* 実行量の調整 */			  
    if (before_quot != current_quot) { 
      nr_blk--;
    }

    /* 実行量を超えたら終了 */
    if (nr_blk < 1) {
      PDEBUG(" break!\n");
      break;
    }

    /* waitリストに追加、実行FIFOへ移動 */
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

    /* 初回は同じ値を入れる */
    if ((unsigned long)-1 == before_quot) {
      before_quot = current_quot;
    }

    /* 実行量の調整 */			  
    if (before_quot != current_quot) {
      nr_blk--;
    }

    /* 実行量を超えたら終了 */
    if (nr_blk < 1) {
      PDEBUG(" break!\n");
      break;
    }

    /* waitリストに追加、実行FIFOへ移動 */
    add_delayproc_req_waitlist (dpinfo, rq);
    p2IoFilter_prepare_exec (info, rq);

    before_quot = current_quot;
  }

# endif /* CONFIG_DELAYPROC_WRITE_ORDER */

  /* exec_fifoロック解除 */
  spin_unlock_irqrestore (&info->fifo_lock, irq_flags);

  /* bufferが空だったら無効化する */
  if (!rb_first (&buffer->buffer)) {
    /* バッファを無効化する */
    PDEBUG(" In-Active buffer%d\n", buffer->id);
    if (!test_and_clear_bit (P2BF_ACTIVE, &buffer->flags)) {
      PDEBUG(" Already In-Active buffer%d\n", buffer->id);
    }

    /* タイムアウトを無効化する */
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

  /* exec_fifoロック */
  spin_lock_irqsave (&info->fifo_lock, irq_flags);

  /* メイン処理 */
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

  /* exec_fifoロック解除 */
  spin_unlock_irqrestore (&info->fifo_lock, irq_flags);

  /* バッファを無効化する */
  PDEBUG(" In-Active buffer%d\n", buffer->id);
  if (!test_and_clear_bit (P2BF_ACTIVE, &buffer->flags)) {
    PDEBUG( " Already In-Active buffer%d\n", buffer->id );
  }

  /* タイムアウトを無効化する */
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

  if (!node) PDEBUG("rb_tree is EMPTY!\n"); /* DEBUG用 */

  /* exec_fifoロック */
  spin_lock_irqsave (&info->fifo_lock, irq_flags);

  while (node) {
    struct request *rq = rb_entry_rq (node);

    /* DIRENTのみSyncする */
    if (rq_is_dirent(rq)) {
      PDEBUG("Move rq[DIRENT] to exec_fifo(LBA=%lX)\n", (unsigned long)rq->sector);
      add_delayproc_req_waitlist (dpinfo, rq);
      p2IoFilter_prepare_exec (info, rq);

      /* 最初のrequestへ戻る */
      node = rb_first (root);
    } else {
      /* 次のrequestに移動する */
      node = rb_next (node);
    }
  }

  /* exec_fifoロック解除 */
  spin_unlock_irqrestore (&info->fifo_lock, irq_flags);

  /* bufferが空だったら無効化する */
  if (!rb_first (&buffer->buffer)) {
    /* バッファを無効化する */
    PDEBUG(" In-Active buffer%d\n", buffer->id);
    if (!test_and_clear_bit (P2BF_ACTIVE, &buffer->flags)) {
      PDEBUG( " Already In-Active buffer%d\n", buffer->id );
    }

    /* タイムアウトを無効化する */
    del_timer_sync (&buffer->unplug_timer);
  }

  PDEBUG("Exit while(node)[DIRENT].\n");
  return 1;
}


static int
p2IoFilter_flush_delayproc_buffer (struct p2IoFilter_buffer *buffer)
{
  /* バッファの種類を確認 */
  if (BUF_IS_DELAYPROC(buffer)) {

    /* SYS/USR1領域の処理(遅延処理) */
    struct p2IoFilter_info *info = buffer->info;
    struct delayproc_info_s *dpinfo = info->dpinfo;
    unsigned char type = get_delayproc_type(dpinfo);
    unsigned char status = get_delayproc_status(dpinfo);

    /* RT_ON状態・遅延処理保留中の場合はflushしない */
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

    /* 遅延処理の種類を確認して実行 */
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
/* ----------------------------------------------- [遅延処理] */


static void
p2IoFilter_update_timelimit (struct p2IoFilter_buffer *buffer)
{
  PTRACE();
	
  BUG_ON (buffer == NULL);

  /* バッファへの最終アクセス時間として、
     現在のjiffesを記録する */
  buffer->last_access = jiffies;

  /* タイムアウト処理の時限を延長する */
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
  /* RT_ON状態・遅延処理中は、SYS/USR1領域バッファをflushさせないように0を返す [遅延処理] */
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
      /* requestが転送するサイズを足しあわせてゆく */
      struct request *req = rb_entry_rq (node);
      total_size += req->nr_sectors;

      /* 次のrequestに移動する */
      node = rb_next (node);
    }
  
  if (total_size >= buffer->block_size)
    {
      /* いっぱいだったら、1を返す */
      return 1;
    }

  /* いっぱいでないなら、0を返す */
  return 0;
}


static struct p2IoFilter_buffer *
p2IoFilter_find_area (struct p2IoFilter_info *info, unsigned long start, int is_dp)
{
  int i = 0;
  struct p2IoFilter_buffer *ret = NULL;
  PTRACE();

  BUG_ON (info == NULL);

  /* すべての保留バッファを走査する */
  for (i = P2BF_MAX_BFS-1; i >= 0; i--)
    {
      struct p2IoFilter_buffer *buffer = &(info->buffers[i]);

      /* まず、その領域がACTIVEかどうかを調査する */
      if (!test_bit (P2BF_ACTIVE, &buffer->flags))
	{
	  /* ACTIVEでない領域は飛ばす */
	  continue;
	}

      /* 扱っている領域に、今回の先頭セクタが含まれているかチェック */
      if (buffer->start <= start
	  && start < (buffer->start + buffer->block_size))
	{
	  /* 範囲内なら、見付かったということでループを抜ける */
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
	
  /* そもそもブロックにはまるものかどうかチェック */
/*   if ((length > 0) && (length < block_size) && (start >= block_start)) */
  if ((length > 0) && (length < block_size) && (start >= block_start))
    {
      /* 先頭セクタと末尾セクタが同一ブロックに入っているかチェック
         （＝アクセス境界をまたいでいないかチェック） */
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
      /* 管理から抜いて、exec_fifoに付け替える */
      elv_rb_del (&buffer->buffer, rq);

#if defined(CONFIG_DELAYPROC)
	  /* SYS/USR1領域の場合、dirtyカウントをデクリメント [遅延処理] */
	  if (BUF_IS_DELAYPROC(buffer))
	  {
		  dec_delayproc_buf_cnt( info->dpinfo );
	  }
#endif /* CONFIG_DELAYPROC */
	  
      /* exec_fifoに入れたら、privateはNULLにする */
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

  /* 保留バッファが遅延対象の場合は別処理を行なう */
  if (p2IoFilter_flush_delayproc_buffer(buffer))
  {
	  goto FIN;
  }
  
  /* すべて吐き出すので、このバッファは無効化する */
  if (!test_and_clear_bit (P2BF_ACTIVE, &buffer->flags))
    {
      PDEBUG( " Already In-Active buffer%d\n", buffer->id );
      goto FIN;
    }

  /* タイムアウトを無効化する */
  del_timer_sync (&buffer->unplug_timer);

  PDEBUG( "==== flush buf%d ====\n", buffer->id );
  
  /* exec_fifoロック */
  spin_lock_irqsave (&info->fifo_lock, irq_flags);
  
  while (rb_first (&buffer->buffer))
    {
      struct request *rq = rb_entry_rq (rb_first (&buffer->buffer));
      p2IoFilter_prepare_exec (info, rq);
    }

  /* exec_fifoロック解除 */
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

  /* rq_startから、所属するブロックの境界を計算する
     （block_sizeでのmodを引くのと同等） */
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
	
  /* すでに所属バッファが判明していない場合
     （=既存bufferの追い出しが発生する場合）の処理をする */
  if (buffer == NULL)
    {
      /* FATなら、METAフラグが立っている */
      if (rq_is_fat (rq))
	{
	  buffer = &info->buffers[P2BF_FAT];
	}
      else
	  {
		  /* システム領域かユーザ領域かを判定 */
		  if (rq->sector < info->sys_boundary)
		  {
			  /* システム領域用のバッファを取得する */
			  buffer = &info->buffers[P2BF_SYS];
		  }
		  else
		  {
			  /* RTフラグが立っていれば、USR2用バッファを取得する */
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

      /* ここに来た段階でrqを入れるbufferが決まったので、
         既存のbufferの内容を破棄してrqを挿入する準備をする */
	  if (!BUF_IS_DELAYPROC(buffer))
		  p2IoFilter_flush_buffered_rq (buffer);

      /* 新しくbufferのパラメータを設定しなおす。
         タイマに関しては、flush_buffered_rqで無効化されているはず */
      set_bit(P2BF_ACTIVE, &buffer->flags);
      buffer->last_access = jiffies;
      p2IoFilter_set_block_params (info, rq->sector, &buffer->start,
				   &buffer->block_size);
    }

  /* private領域に所属バッファへのポインタを記録 */
  rq->elevator_private = buffer;

RETRY:

  /* バッファにrqを突っ込む */
  _alias = elv_rb_add (&buffer->buffer, rq);
  
  PDEBUG( " Add to buf%d: sector=%X\n", buffer->id, (unsigned int)rq->sector );

#if defined(CONFIG_DELAYPROC)
  /* SYS/USR1領域の場合、dirtyカウントをインクリメント [遅延処理] */
  if (BUF_IS_DELAYPROC(buffer))
  {
	  inc_delayproc_buf_cnt( info->dpinfo );
  }
#endif /* CONFIG_DELAYPROC */
  
  if (unlikely (_alias != NULL))
    {
      /* 同じセクタ番号で先客が居たので
         一旦それをexec_fifoに追い出す */
      p2IoFilter_prepare_exec (info, _alias);
      goto RETRY;
    }

  if(rq->cmd_flags & REQ_RW_SYNC)
    {
      /* 来たのがSYNCの場合は、さっさと書き出す準備をする */
      set_bit(P2BF_TMOUT, &buffer->flags);
      del_timer_sync (&buffer->unplug_timer);
      queue_work (buffer->info->unplug_works, &buffer->unplug_work);      
    }
  else
    {
      /* 通常のrequestの場合はbufferの書きだしタイマを設定しなおす */
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

  /* bioがREADに関するものならスルーする */
  if (BIO_READ_DIR (bio))
    {
      goto FAIL;
    }

  /* 受け取ったbioが属するブロックを現在保留中か検索する */
  buffer = p2IoFilter_find_area (info, bio->bi_sector, BIO_IS_DELAYPROC(bio));
  if (buffer == NULL)
    {
      /* 保留していなければ、マージ対象もない */
      goto FAIL;
    }

  /* ここまで来たら、bioに関連するブロックは持っているということ */
  /* まずは、前方マージ可能な候補を探す */
  _rq = elv_rb_find (&buffer->buffer, bio->bi_sector + bio_sectors (bio));
  if (_rq != NULL)
    {
      /* 見付かった前方マージ候補の妥当性を試す
         （elv_rq_merge_ok経由で、allow_merge_fnを呼んでいる） */
      if (unlikely (!elv_rq_merge_ok (_rq, bio)))
	{
	  goto FAIL;
	}

      /* 前方マージ可能なrequestが見付かったことを通知する */
      *req = _rq;
	  
      return ELEVATOR_FRONT_MERGE;
    }

  /* 後方マージ可能な候補を探す */
  /* ただし、後方マージについては、この段階に来る前に、
     汎用ブロックIO層のハッシュテーブルで見付かっている場合が多い */
  _rq = elv_rb_find_tail (&buffer->buffer, bio->bi_sector);
  if (_rq != NULL)
    {
      if (unlikely (!elv_rq_merge_ok (_rq, bio)))
	{
	  goto FAIL;
	}

      /* 後方マージ可能なrequestが見付かったことを通知する */
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
	
  /* bufferがNULLなら、すでにexec_fifoに入っている。
     exec_fifoの中のrqはもういじらない */
  if (buffer == NULL)
    {
      return;
    }

  if (type == ELEVATOR_FRONT_MERGE)
    {
      /* FRONT MERGEしたら、先頭sectorが変わったということなので、
         管理構造に登録しなおす */
      elv_rb_del (&buffer->buffer, req);
      elv_rb_add (&buffer->buffer, req);
    }

  /* 最終アクセス時間を更新する */
  p2IoFilter_update_timelimit (buffer);

  /* バッファがいっぱいになっているか
     またはSYNC属性のものが飛び込んで来たかをチェックし、
     条件にあえば書きだしタスクをスケジューラに登録する */
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
	
  /* exec_fifoに入っているものに関して呼ばれたらおかしい */
  BUG_ON (buffer == NULL);

  /* マージされて消えていくrequestを管理対象から消す */
  elv_rb_del (&buffer->buffer, next);
#if defined(CONFIG_DELAYPROC)
  /* SYS/USR1領域の場合、dirtyカウントをデクリメント [遅延処理] */
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

  /* DIRECT属性がrequestとbioで食い違っていたら触らない */
  if (unlikely ((bio_drct (bio) && !(req->cmd_flags & REQ_DRCT))
		|| (!bio_drct (bio) && (req->cmd_flags & REQ_DRCT))))
    {
      //printk("%s: Failed - 2\n",__FUNCTION__);
      goto FAIL;
    }

  /* RT属性がrequestとbioで食い違っていたら触らない */
  if (unlikely ((bio_rt (bio) && !rq_is_rt(req))
		|| (!bio_rt (bio) && rq_is_rt(req))))
    {
      //printk("%s: Failed - 2'\n",__FUNCTION__);
      goto FAIL;
    }
  
  /* bioがREADに関するものならスルーする */
  if (BIO_READ_DIR (bio))
    {
      goto ALLOW;
    }

  /* 既にexec_fifoに入っているものについては触らない */
  if (buffer == NULL)
    {
      //printk("%s: Failed - 3\n",__FUNCTION__);
      goto FAIL;
    }

  PDEBUG("  buf=%d\n", buffer->id);
  
  /* bioの内容がアクセス境界をまたいでいないかチェックする */
  if (!p2IoFilter_check_boundary (bio->bi_sector, bi_sectors,
				  buffer->start, buffer->block_size))
    {
      //printk("%s: Failed - 4 [%llu, %lu, %lu, %lu]\n",__FUNCTION__,
      //     bio->bi_sector, bi_sectors, buffer->start, buffer->block_size);
      goto FAIL;
    }

  /* バックマージ可能か調査する */
  if ((req->sector + req->nr_sectors) == bio->bi_sector)
    {
      if ((buffer->start + buffer->block_size) == bio->bi_sector)
	{
	  /* バックマージすることによって境界をまたいでしまう場合 */
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
      /* フロントマージ可能か調査する */
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

  /* exec_fifoロック */
  spin_lock_irqsave (&info->fifo_lock, irq_flags);
  
  /* exec_fifoの先頭を取得する */
  req = rq_entry_fifo (info->exec_fifo.next);

  PTRACE( " rq->sector = %08X %s", (int)req->sector,
		  (rq_data_dir(req)==WRITE?"WRITE":"READ") );
  //printk("%s: SCT:%llu (%lu)\n",__FUNCTION__,req->sector,req->nr_sectors);

  /* 先頭のrequestをexec_fifoから抜く */
  rq_fifo_clear (req);

  if(rq_data_dir(req)==WRITE)
    {
      //printk("%lu-(%lu)>",info->exec_fifo_depth, req->nr_sectors);
      info->exec_fifo_depth -= req->nr_sectors;
      //printk("%lu\n",info->exec_fifo_depth);
    }

  /* デバドラのqueueに渡す */
  elv_dispatch_add_tail (q, req);

  /* exec_fifoロック解除 */
  spin_unlock_irqrestore (&info->fifo_lock, irq_flags);
}

static int
p2IoFilter_dispatch_requests (struct request_queue * q, int force)
{
  struct p2IoFilter_info *info = q->elevator->elevator_data;
  int i = 0;
  PTRACE();
	
  /* すでにexec_fifoに入れているものがないか */
  if (!list_empty (&info->exec_fifo))
    {
      goto DISPATCH;
    }

  /* 保留バッファを走査して、書き出せるものがないかチェック */
  for (i = 0; i < P2BF_MAX_BFS; i++)
    {
      if (p2IoFilter_check_total_size (&info->buffers[i])	/* バッファがフルの場合 */
	  || force		/* 強制書きだし(sync)指定の場合 */
	  || test_and_clear_bit (P2BF_TMOUT,
				 &info->buffers[i].flags) /* タイムアウトが発生していた場合 */ )
	{
	  /* 保留バッファの内容を吐き出す */
	  p2IoFilter_flush_buffered_rq (&info->buffers[i]);
	}
    }

  /* ここまで来てexec_fifoが空ならやることはない */
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

  /* rqがREADに関するものなら即exec_fifoにつなぐ */
  if (rq_data_dir(rq)==READ)
    {
      goto PREPARE;
    }

#if defined(CONFIG_DELAYPROC)
  /* RT_ON状態・遅延処理中は、遅延対象rqはとにかくSYS/USR1領域につなぐ [遅延処理] */

  /* rqがMIに関するものなら即exec_fifoにつなぐ */
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
		  ? &info->buffers[P2BF_SYS] /* SYS領域なので、SYS領域バッファを選択 */
		  : (!rq_is_rt(rq)
			 ? &info->buffers[P2BF_USR1] /* RTフラグが立ってないので、USR1領域バッファを選択 */
			 : NULL);
	  
	  /* rqが遅延対象である場合はSYS/USR1領域につなぐ */
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
  
  /* 領域がひっかかるbufferを検索する */
  buffer = p2IoFilter_find_area (info, rq->sector, RQ_IS_DELAYPROC(rq));
  
  area_offset = (buffer == NULL)
    ? ((rq->sector >= info->sys_boundary) ? info->sys_boundary : 0)
    : (buffer->start);

  area_block = (buffer == NULL)
    ? ((rq->sector >=
	info->sys_boundary) ? info->usr_block_sectors : info->
       sys_block_sectors) : (buffer->block_size);

  /* ブロック境界をまたがらないか調べる */
  check_boundary =
    p2IoFilter_check_boundary (rq->sector, rq->nr_sectors, area_offset,
			       area_block);

  if (check_boundary != 0)
    {
      /* バッファに入れる場合。適当なバッファに挿入する。 */
      p2IoFilter_update_buffer (info, buffer, rq);
      return;
    }

  /* そのまま実行キューに入れる場合 */
  if (buffer != NULL)
    {
      /* 頭がひっかかっているバッファがあった場合は、
         それも巻き添えにする */
      p2IoFilter_flush_buffered_rq (buffer);
    }

PREPARE:

  p2IoFilter_prepare_exec (info, rq);
  /* work_structの選択に特に意味はない */
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

  /* まずexec_fifoが空か調べ、空なら各バッファの状態を見る */
  if ((ret = list_empty (&info->exec_fifo)) == 1)
    {
      for (i = 0; i < P2BF_MAX_BFS; i++)
	{
	  struct p2IoFilter_buffer *buffer = &info->buffers[i];

	  /* ACTIVEで空でないバッファがみつかったら、ループを抜ける */
	  if (test_bit(P2BF_ACTIVE, &buffer->flags)
	      && (buffer->buffer.rb_node != NULL))
	    {
		  /* 遅延処理待機中の場合は空とみなす必要あり [遅延処理] */
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
  
  /* すでにexec_fifoに入っているものについては結合候補をあげない */
  if (buffer == NULL)
    {
      goto FIN;
    }

  /* 同一buffer内での前方を取得する */
  _rq = elv_rb_former_request (q, rq);
  if (_rq != NULL)
    {
      /* DIRECT属性が一致しない場合はやめる */
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
  
  /* すでにexec_fifoに入っているものについては結合候補をあげない */
  if (buffer == NULL)
    {
      goto FIN;
    }

  /* 同一buffer内での前方を取得する */
  _rq = elv_rb_latter_request (q, rq);
  if (_rq != NULL)
    {
/* 		PDEBUG( "_rq->sector = %08X %s\n", (int)_rq->sector, */
/* 				(rq_data_dir(_rq)==WRITE?"WRITE":"READ") ); */
		
      /* DIRECT属性が一致しない場合はやめる */
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

  /* 今から扱う保留バッファは有効なものなのか？ */
  if (likely (test_bit (P2BF_ACTIVE, &buffer->flags)))
    {
      PDEBUG( "Set TMOUT flag and execute unplug_work\n" );
		
      /* タイムアウトが発生したことを知らせるフラグを立てる */
      set_bit (P2BF_TMOUT, &buffer->flags);


      if(buffer->id == P2BF_USR2)
      {
	      printk("USR2 : <TimeOut>\n");
      }

      //printk("%s: Timeout Occured.\n",__FUNCTION__);

      /* 強制書きだしのタスクをスケジューリングする */
      queue_work (buffer->info->unplug_works, &buffer->unplug_work);

      /* TODO 設計では、ここでqueueにもTMOUTフラグを立てることにしている */
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

  /* このあたりのロックの握り方は、他のi/o schedulerを参考にしている */
  spin_lock_irqsave (buffer->queue->queue_lock, flags);

  /* unplug動作を実行 */
  blk_start_queueing (buffer->queue);
  spin_unlock_irqrestore (buffer->queue->queue_lock, flags);
  
  return;
}

/* 実行待ちキューにまだAU以上のデータがある場合は
   新しいbioを投入するのを待たせるための問い合わせに答える。 */
static int
p2IoFilter_ready_queue (struct request_queue * q)
{
  struct p2IoFilter_info *info = q->elevator->elevator_data;
  unsigned long flags = 0;
  int locked = 0;
  int ret = 0;
  /* このコード実装の段階でUSR1バッファしか使わないようにしたので、
     簡易な実装ですませてしまいます。*/
  /* 遅延処理でUSR1を使用するので、USR2へ変更! */
  struct p2IoFilter_buffer *buf_usr = &info->buffers[P2BF_USR2];
  PTRACE();
  
  locked = spin_trylock_irqsave (q->queue_lock, flags);

  /* USERバッファにすでに誰か入っているなら、
     それが満タンになるまではbioを受け入れて構わない */
  if( test_bit(P2BF_ACTIVE, &buf_usr->flags)
      && (buf_usr->buffer.rb_node != NULL)
      && !(p2IoFilter_check_total_size(buf_usr)) )
    {
      ret = 1;
    }
  else
    {
      /* USERバッファがからっぽ、
	 もしくは満タン（時間の問題で吐き出される）の場合は、
	 exec_fifoの深さを見て、あまり深いようなら待たせる */
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

  /* 0を指定されたら通常系全領域を強制吐き出し */
  if(area==0)
  {
	  targets = kmalloc(sizeof(int)*3,GFP_ATOMIC);
	  targets[0] = P2BF_SYS;
	  targets[1] = P2BF_USR1;
	  targets[2] = -1; /* 留めの印 */
  }
  else if(area==1)  /* 1を指定されたら独自系のみ吐き出し */
  {
	  targets = kmalloc(sizeof(int)*2,GFP_ATOMIC);
	  targets[0] = P2BF_USR2;
	  targets[1] = -1; /* 留めの印 */	  
  }
  else if(area==-1) /* -1なら全領域（おそらくこれは使われない） */
  {
	  targets = kmalloc(sizeof(int)*4,GFP_ATOMIC);
	  targets[0] = P2BF_SYS;
	  targets[1] = P2BF_USR1;
	  targets[2] = P2BF_USR2;
	  targets[3] = -1; /* 留めの印 */	  
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
	
  /* 念のため、これから触るメモリ領域のチェック */
  if (unlikely (buffer == NULL))
    {
      return -EINVAL;
    }

  /* 各バッファの初期化 */
  for (i = 0; i < P2BF_MAX_BFS; i++)
    {
      struct p2IoFilter_buffer *io_buffer = &(buffer[i]);

      /* 領域をまずゼロで埋める */
      memset (io_buffer, 0, sizeof (struct p2IoFilter_buffer));

      /* あまり意味はないが、最終更新時間を入れておく */
      io_buffer->last_access = jiffies;

      /* バッファ実体の初期化 */
      io_buffer->buffer = RB_ROOT;

      /* タイムアウト用のパラメータ初期化 */
      init_timer (&io_buffer->unplug_timer);
      io_buffer->unplug_timer.function = p2IoFilter_time_expired;
      io_buffer->unplug_timer.data = (unsigned long) io_buffer;

      /* タイムアウト・タスクの初期化 */
      INIT_WORK (&io_buffer->unplug_work, p2IoFilter_unplug_device);
      io_buffer->id = i;

      /* それぞれのタイムアウト時間の設定 */
      switch (i)
	{
	case P2BF_FAT:
	  /* system領域のあるP2カードかどうかでFATへのアクセスサイズを変更する */
	  io_buffer->block_size =
	    info->sys_block_sectors ? info->sys_block_sectors : info->
	    usr_block_sectors;
	  io_buffer->max_wait_time = FAT_TMOUT;
	  break;

	case P2BF_SYS:
		/* system領域のあるP2カードかどうかでアクセスサイズを変更する */
	  io_buffer->block_size =
	    info->sys_block_sectors ? info->sys_block_sectors : (unsigned long)-1;
	  io_buffer->max_wait_time = SYS_TMOUT;
	  break;

	case P2BF_USR1:
	  io_buffer->block_size = info->usr_block_sectors;
	  io_buffer->max_wait_time = SYS_TMOUT;  /* SYS領域と同じ値を設定 */
	  break;
	  
	default:
	  io_buffer->block_size = info->usr_block_sectors;
	  io_buffer->max_wait_time = DEF_TMOUT;
	}

      /* lockの初期化 */
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
	
  /* ドライバがデータを入れてくれているかチェック */
  if (unlikely (spd_dev == NULL))
    {
	  printk("[p2IoFilter] spd_dev is NULL!\n");
      return NULL;
    }

  /* このschedulerの独自パラメータ領域を確保する */
  info =
    (struct p2IoFilter_info *) kmalloc (sizeof (struct p2IoFilter_info),
					GFP_KERNEL);
  if (unlikely (info == NULL))
    {
      return NULL;
    }

  /* 実行FIFOの初期化 */
  INIT_LIST_HEAD (&info->exec_fifo);
  info->exec_fifo_depth = 0;
  spin_lock_init (&info->fifo_lock);
  
  /* タイムアウト用のworkqueueの初期化 */
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
  
  /* P2関連パラメータの取りだし */
  if (spd_dev->n_area == 1)	/* 旧P2カードの場合 */
    {
      info->sys_boundary = 0;
      info->sys_block_sectors = 0;
      info->usr_block_sectors = spd_dev->area[0].wsize;
      info->sys_start_sector = 0;
      info->usr_start_sector = spd_dev->area[0].start;
    }
  else if (spd_dev->n_area == 2)	/* 新P2カードの場合 */
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
  /* 遅延処理の初期化 [遅延処理] */
  ret = init_delayproc_info (&info->dpinfo, spd_dev->devnum, dp_except_inode_list);
  if (ret)
  {
	  return NULL;
  }
  info->dpinfo->q = q;

  /* リクエストキューの閾値を変更 [遅延処理] */
  PDEBUG("Change unplug_thresh %d to %d\n", q->unplug_thresh, DELAYPROC_UNPLUG_THRESH);
  q->unplug_thresh = DELAYPROC_UNPLUG_THRESH;
  
  PDEBUG("Change num of requests thresh %ld to %d\n", q->nr_requests, DELAYPROC_NR_REQUESTS);
  q->nr_requests = DELAYPROC_NR_REQUESTS;
#endif /* CONFIG_DELAYPROC */
  
  /* 各保留バッファの初期化 */
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

      /* 全てのタイマー処理を削除・終了する */
      del_timer_sync (&buffer->unplug_timer);

      /* 保留バッファが空かどうかを確認する */
      BUG_ON (buffer->buffer.rb_node != NULL);
    }

  /* タイムアウト用workqueueの後片付け */
  destroy_workqueue (info->unplug_works);

  /* この時点で実行FIFOが空でないとおかしい */
  BUG_ON (!list_empty (&info->exec_fifo));

#if defined(CONFIG_DELAYPROC)
  /* 遅延処理の後片付け [遅延処理] */
  exit_delayproc_info (info->dpinfo);
  info->dpinfo = NULL;
#endif /* CONFIG_DELAYPROC */
  
  /* i/o scheduler用に独自確保していたメモリ領域を解放する */
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
