#include <linux/p2fat_fs.h>
#include <linux/reservoir_sb.h>
#include <linux/bio.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/pagevec.h>
#include <linux/swap.h>
#include <linux/writeback.h>
#include <linux/module.h>

#define P2FAT_ASIGN_SIZE     (512*1024)  // 512KB単位で扱う
#define P2FAT_ASIGN_CLUSTERS(sb) (P2FAT_ASIGN_SIZE >> P2FAT_SB(sb)->cluster_bits)

char reservoir_fat_revision[] = "$Rev: 18335 $";
extern char reservoir_fs_revision[];

static unsigned long neo_address = 0UL;
static unsigned long neo_trash_can_address = 0UL;

/* コールバック用の外部変数 */
static struct rb_root done_list;     //コールバック実施用記憶領域
static struct mutex done_lock;         //上記変数用ロック
static wait_queue_head_t done_event;
static unsigned long done_flags;     // fat_notify_end のフラグと同じ

enum{P2FAT_DUMMY_CLUSTER, P2FAT_SLAVE_BIO, P2FAT_SYNC_BIO};  //p2fat_reservoir_private.flag用

struct p2fat_reservoir_private
{
  unsigned long flags;
  struct super_block *sb;
  unsigned long disk_cluster;
  unsigned long file_cluster;
  struct inode *inode;
  ssize_t size;
  int errno;
  struct list_head cluster_list;
  int reservoir_idx;
};

struct p2fat_done_list_entry
{
  int errno;
  unsigned long file_id;
  unsigned long notify_id;
  unsigned long size;
  struct rb_node rb_node;
};

static int fat_access(struct super_block *sb, int nr, int new_value)
{
  struct p2fat_entry fatent;
  int ret;
  struct p2fat_sb_info *sbi = P2FAT_SB(sb);
  
  p2fatent_init(&fatent);
  ret = __p2fat_ent_read(sb, &fatent, nr);

  if(ret >= 0 && new_value!=-1)
    {
      if(ret == 0 && new_value != 0)
        {
	  if(P2FAT_SB(sb)->free_clusters != -1)
            {
              P2FAT_SB(sb)->free_clusters--;  //空きを一つへらす
            }

          if(sbi->show_inval_log)
            {
              if(sbi->free_clusters != -1)
                {
                  if(sbi->cont_space.cont * sbi->cont_space.n > sbi->free_clusters)
                    {
                      printk("[%s:%d] Cont space(%lu) is bigger than fs info(%u)\n", 
                        __FILE__, __LINE__, sbi->cont_space.cont * sbi->cont_space.n, sbi->free_clusters);
                      sbi->show_inval_log = 0;
                    }
                }
            }

          if(p2fat_check_cont_space(sb, fatent.entry))
            {
              P2FAT_SB(sb)->cont_space.cont--;
            }

        }

      __p2fat_ent_write(sb, &fatent, new_value, 0);

      if(ret != 0 && new_value == 0)
        {
	  if(P2FAT_SB(sb)->free_clusters != -1)
            {
              P2FAT_SB(sb)->free_clusters++; //空きを一つ増やす
            }

          if(p2fat_check_cont_space(sb, fatent.entry))
            {
              P2FAT_SB(sb)->cont_space.cont++;
            }
        }
    }

  p2fatent_brelse(&fatent);

  return ret;
}

static struct p2fat_done_list_entry *p2fat_find_done_list(struct super_block *sb,
						    unsigned long file_id)
{
  struct rb_node *n = done_list.rb_node;
  struct p2fat_done_list_entry *done_list_entry = NULL;

  while(n)
    {
      done_list_entry = rb_entry(n, struct p2fat_done_list_entry, rb_node);

      if(file_id < done_list_entry->file_id)
	{
	  n = n->rb_left;
	}
      else if(file_id > done_list_entry->file_id)
	{
	  n = n->rb_right;
	}
      else
	{
	  return done_list_entry;
	}
    }

  return NULL;
}

static void p2fat_add_done_list(struct super_block *sb,
				struct p2fat_done_list_entry *entry)
{
  struct rb_node **p = &(done_list.rb_node);
  struct rb_node *parent = NULL;
  struct p2fat_done_list_entry *__entry;

  while(*p)
    {
      parent = *p;
      __entry = rb_entry(parent, struct p2fat_done_list_entry, rb_node);

      BUG_ON(entry->file_id == __entry->file_id);

      if(entry->file_id < __entry->file_id)
	{
	  p = &(*p)->rb_left;
	}
      else if(entry->file_id > __entry->file_id)
	{
	  p = &(*p)->rb_right;
	}
    }

  rb_link_node(&entry->rb_node, parent, p);
  rb_insert_color(&entry->rb_node, &done_list);

  return;
}

static void p2fat_update_each_cluster(struct p2fat_reservoir_private *bi_private)
{
  struct inode *inode = bi_private->inode;
  struct super_block *sb = bi_private->sb;
  unsigned long size = bi_private->size;
  struct p2fat_done_list_entry *done_list_entry = NULL;

  //printk("D:%lu\n", bi_private->disk_cluster);

  if(test_bit(P2FAT_DUMMY_CLUSTER, &bi_private->flags))
    {
      /* 無駄に確保した領域の解放 */
      fat_access(sb, bi_private->disk_cluster, FAT_ENT_FREE);
    }
  else
    {
      unsigned long file_id = bi_private->inode->i_file_id;
      unsigned long notify_id = bi_private->inode->i_notify_id;

      /* 転送実体のあるbioの場合は、
	 クラスタチェーンの更新と
	 コールバックの準備をおこなう */

      /* slaveフラグが立っているときは、1クラスタを1bioで転送しきれなかったときの
	 補助bioだから、クラスタ処理をすることはない */

      if(!test_and_clear_bit(P2FAT_SLAVE_BIO, &bi_private->flags) && P2FAT_I(inode)->i_pos!=0)
	{
	  if( (inode->i_blocks >> (P2FAT_SB(sb)->cluster_bits - 9))
	      == bi_private->file_cluster )  // ファイル末尾に関するクラスタかを調査
	    {
	      /* FAT Chain をつなぐ */
	      p2fat_chain_add(inode, bi_private->disk_cluster, 1);
	      P2FAT_I(inode)->mmu_private += P2FAT_SB(sb)->cluster_size;
	    }
	  else if(bi_private->file_cluster==0 && P2FAT_I(inode)->i_start!=0
		  && ( bi_private->disk_cluster != P2FAT_I(inode)->i_start )
		  && ( size==P2FAT_SB(sb)->cluster_size || size==inode->i_size ) )
	    {
	      int old_cluster = P2FAT_I(inode)->i_start;
	      int next = 0;

	      /* クラスタの入れ換え処理をおこなう */
	      next = fat_access(sb, old_cluster, -1);
	      fat_access(sb, bi_private->disk_cluster, next);
	      P2FAT_I(inode)->i_start = bi_private->disk_cluster;
	      P2FAT_I(inode)->i_logstart = bi_private->disk_cluster;

	      /** ここから下のブロックに関しては、次のifブロックと
		  　処理が重複するので実行をやめました **/

	      p2fat_reserve_fat_free(inode, old_cluster);
	    }

	  /* 最初のクラスタだった場合、FATを確実に書き出す.
	     以前はDirEntも書き出していたが、RT優先との
	     兼ね合いで意味がなくなったのでやらない 
	     (2011/3/17)完全にやらないようにした */
	  if(bi_private->file_cluster==0)
	    {
/* 			lock_rton(MAJOR(sb->s_dev)); */
/* 			if(check_delay_status(sb)){ */
/* 				if(check_rt_status(sb)){ */
/* 					p2fat_sync(sb); */
/* 					if(likely(sb->s_op->write_inode)) */
/* 						sb->s_op->write_inode(inode, 1); */
/* 				} */
/* 			} else { */
				mutex_lock(&P2FAT_SB(sb)->rt_inode_dirty_lock);
				set_bit(FAT_NEWDIR_INODE, &P2FAT_I(inode)->i_flags);
				if(list_empty(&P2FAT_I(inode)->i_rt_dirty)){
					__iget(inode);
					list_add_tail(&P2FAT_I(inode)->i_rt_dirty, &P2FAT_SB(sb)->rt_inode_dirty_list);
				}
				mutex_unlock(&P2FAT_SB(sb)->rt_inode_dirty_lock);
/* 			} */
/* 			unlock_rton(MAJOR(sb->s_dev)); */
	    }
	}

      /* ここから下は、コールバックのための準備 */

      if(size && file_id!=0)  //サイズが入っていない場合（メモリ経由時）はなにもしない
	{
	  mutex_lock(&done_lock);

	  done_list_entry = p2fat_find_done_list(sb, file_id);
	  if(!done_list_entry)
	    {
	      /* file_idに関するコールバック情報が未登録なので
		 新しく作成して登録する */
	      done_list_entry = (struct p2fat_done_list_entry *)
		kmalloc(sizeof(struct p2fat_done_list_entry), GFP_KERNEL);
	      if(done_list_entry==NULL)
		{
		  printk("%s-%d: Getting Memory Space Failed.\n",
			 __PRETTY_FUNCTION__, __LINE__);
		  goto FIN;
		}

	      done_list_entry->file_id = file_id;
	      done_list_entry->notify_id = notify_id;
	      done_list_entry->size = 0;
	      done_list_entry->errno = 0;
	      RB_CLEAR_NODE(&done_list_entry->rb_node);

	      p2fat_add_done_list(sb, done_list_entry);
	    }

	  /* 転送終了したサイズを記録 */
	  done_list_entry->size += size;
	  
	  if(bi_private->errno)
	    {
	      done_list_entry->errno = bi_private->errno;
	    }

	FIN:
	  mutex_unlock(&done_lock);
	}

    }

  return;
}

void p2fat_update_cluster_chain(struct work_struct *work)
{
  struct super_block *sb = container_of(work, struct p2fat_sb_info, rt_chain_updater)->sb;
  struct p2fat_reservoir_private *bi_private;
  unsigned long flags = 0;
  int i = 0;

  spin_lock_irqsave(&P2FAT_SB(sb)->rt_updated_clusters_lock, flags);

  while(!list_empty(&P2FAT_SB(sb)->rt_updated_clusters))
    {
      bi_private = list_entry(P2FAT_SB(sb)->rt_updated_clusters.next,
			      struct p2fat_reservoir_private, cluster_list);
      list_del(&bi_private->cluster_list);

      /* lockを握ったままだと、この後に呼ぶ write_inode などが
	 動かないので、一旦はなす */
      spin_unlock_irqrestore(&P2FAT_SB(sb)->rt_updated_clusters_lock, flags);

      /* FATの更新とコールバックのための準備をおこなう */
      p2fat_update_each_cluster(bi_private);

      /* 適度な周期で待っているタスクを起こす。
	 128に特に根拠はない。チューニング要素。*/
      i++;
      if(unlikely(i==128))
	{
	  wake_up(&done_event);
	  cond_resched();
	  i=0;
	}

      /* emptyチェックのために再度取得必要 */
      spin_lock_irqsave(&P2FAT_SB(sb)->rt_updated_clusters_lock, flags);

      /* i/o schedulerに渡したデータが処理し終わったのなら
	 カウンタを減らしておく */
      if(bi_private->reservoir_idx>=0)
	{
	  P2FAT_SB(sb)->rt_private_count[bi_private->reservoir_idx]--;
	}

      kfree(bi_private);      
    }

  spin_unlock_irqrestore(&P2FAT_SB(sb)->rt_updated_clusters_lock, flags);

  /* 最後に確実に、待っているタスクを起こす。 */
  wake_up(&done_event);

  return;
}

inline static int queue_p2fat_chain_updater(struct super_block *sb)
{
  return queue_work(P2FAT_SB(sb)->rt_chain_updater_wq, &P2FAT_SB(sb)->rt_chain_updater);
}

static void p2fat_callback_write(struct bio *bio, int err)
{
  struct p2fat_reservoir_private *bi_private = bio->bi_private;
  struct super_block *sb = NULL;
  unsigned long flags = 0;

  /* Private入ってないのはおかしいですね */
  BUG_ON(bi_private==NULL);

  sb = bi_private->sb;

  if(test_bit(BIO_RW_DUMMY, &bio->bi_rw))
    {
      set_bit(P2FAT_DUMMY_CLUSTER, &bi_private->flags);
    }

  if(test_bit(BIO_RW_SLAVE, &bio->bi_rw))
    {
      set_bit(P2FAT_SLAVE_BIO, &bi_private->flags);
    }

  if(!test_bit(BIO_UPTODATE, &bio->bi_flags))
    {
      bi_private->errno = -EIO;
    }
  else
    {
      bi_private->errno = err;
    }

  /* クラスタチェーンのアップデートのためにリストに登録する。
     チェーンの更新は、workqueueでおこなう。
     なぜなら、これが呼ばれる区間は割り込み禁止になっているので
     IOを伴う処理を呼ぶことができないから。*/
  spin_lock_irqsave(&P2FAT_SB(sb)->rt_updated_clusters_lock, flags);
  list_add_tail(&bi_private->cluster_list, &P2FAT_SB(sb)->rt_updated_clusters);
  spin_unlock_irqrestore(&P2FAT_SB(sb)->rt_updated_clusters_lock, flags);

  /* FATチェーンの更新タスクをスケジュールする */
  queue_p2fat_chain_updater(sb);

  bio->bi_private = NULL;

  reservoir_end_io_write(bio, err);

  return;
}

static void p2fat_callback_read(struct bio *bio, int err)
{
  /* 読み込みでは、一般的な処理以外特にすることはないはず */

  if( likely(bio->bi_private) )
    {
      kfree(bio->bi_private);
      bio->bi_private = NULL;
    }

  reservoir_end_io_read(bio, err);

  return;
}

static void p2fat_prepare_submit(struct bio *bio, struct super_block *sb, int rw)
{
  struct p2fat_reservoir_private *bi_private = bio->bi_private;
  unsigned long flags = 0;

  if(rw==READ)
    return;

  /* 書き込みのために i/o schedulerに渡した最後のデータを記録しておく */
  spin_lock_irqsave(&P2FAT_SB(sb)->rt_updated_clusters_lock, flags);

  if(bi_private->reservoir_idx != -1)
    {
      P2FAT_SB(sb)->rt_private_count[bi_private->reservoir_idx]++;
    }

  spin_unlock_irqrestore(&P2FAT_SB(sb)->rt_updated_clusters_lock, flags);

  return;
}

static void p2fat_wait_on_sync(struct super_block *sb, int fgroup)
{
  unsigned long flags = 0;
  unsigned long time_limit = jiffies + RSFS_IO_END_WAIT_TIME; 
  
  while(1)
    {
      int count;

      spin_lock_irqsave(&P2FAT_SB(sb)->rt_updated_clusters_lock, flags);
      count = P2FAT_SB(sb)->rt_private_count[fgroup];
      spin_unlock_irqrestore(&P2FAT_SB(sb)->rt_updated_clusters_lock, flags);

      if(count==0)
	{
	  break;
	}

      if(time_after(jiffies, time_limit))
	{
	  printk("%s: Time Expired.\n",__FUNCTION__);
	  break;
	}

      cond_resched();
    }

  return;
}

static void p2fat_alloc_bio_private(struct bio *bio, struct super_block *sb, struct inode *inode, int rw)
{
  /* ダミー転送のbioのときはpageにNULLが入ってくるので注意 */

  struct p2fat_reservoir_private *bi_private;

  bi_private = (struct p2fat_reservoir_private *)
    kmalloc(sizeof(struct p2fat_reservoir_private), GFP_KERNEL);

  if( unlikely(bi_private==NULL) )
    {
      printk("%s-%d: Getting Private Space Failed.\n",
	     __PRETTY_FUNCTION__, __LINE__);
      return;
    }

  bi_private->flags = 0;


  bi_private->inode = inode;

  if(inode && inode->i_reservoir)
    {
      bi_private->reservoir_idx = inode->i_reservoir->file_group_idx;
    }
  else
    {
      bi_private->reservoir_idx = -1;
    }

  bi_private->size = 0;
 
  bi_private->sb = sb;
  bi_private->disk_cluster = 0;
  bi_private->file_cluster = 0;
  INIT_LIST_HEAD(&bi_private->cluster_list);
  bi_private->errno = 0;

  bio->bi_private = bi_private;

  return;
}

static void p2fat_set_bio_private(struct bio *bio, struct page *page, int rw, int rt)
{
  struct inode *inode;
  struct p2fat_reservoir_private *bi_private = bio->bi_private;
  struct drct_page *drct_page = NULL;
  char *kaddr = NULL;
  int first_time = (bi_private->size==0) ? 1 : 0;

  /* dummyについては触れない */
  if( page==NULL )
    {
      return;
    }

  inode = page->mapping->host;

  if(test_bit(RS_PCIDRCT, &inode->i_rsrvr_flags))
    {
      kaddr = kmap(page);
      drct_page = (struct drct_page *)kaddr;

      do
	{
	  bi_private->size += (drct_page->total_size - drct_page->dummy_size);
	  drct_page = (struct drct_page *)drct_page->page_chain;
	}
      while(drct_page!=NULL);

      kunmap(page);
    }

  /* 最初のpage登録だったとき
     bioがslaveの場合はでたらめな値が入るが、
     コールバックで無視するので問題ない。 */

  if(first_time)
    {
      bi_private->file_cluster = (page->index
				  >> (P2FAT_SB(inode->i_sb)->cluster_bits - PAGE_CACHE_SHIFT));
    }

  return;
}

static int p2fat_get_n_blocks(struct super_block *sb,
			      unsigned long length, unsigned long *blocks)
{
  int head_cluster = 0;
  unsigned long head_sector = 0;
  int i = 0;

  /* 基本的には、ファイルシステムの知っているAUサイズごとに
     確保する。その中から、nブロックを渡す。
     nがAUの約数かチェックしておく。1か16以外来ないと思ってもよい。
     blockで通知する単位はsectorであることを忘れない。 */

  if( unlikely(length!=P2FAT_ASIGN_CLUSTERS(sb) && length!=1) )
    {
      printk("%s-%d: Unsupported Length (%lu)\n",
	     __PRETTY_FUNCTION__, __LINE__, length);
      return -EINVAL;
    }

  if( unlikely(length==1) )
    {
      /* ここが1で呼ばれるのは、残り容量僅少のときなど
	 よほどの事態の場合のみ。 */
      
      clear_bit(RT_SEQ, &RS_SB(sb)->rt_flags);

      head_cluster = p2fat_alloc_cont_clusters(sb, 1);
      blocks[0] = ( (head_cluster-2) * P2FAT_SB(sb)->sec_per_clus ) + P2FAT_SB(sb)->data_start;

      printk("%s-%d: Disk Full for Recording.\n", __PRETTY_FUNCTION__, __LINE__);
      return (head_cluster > 0) ? 0 : head_cluster;
    }

  mutex_lock(&P2FAT_SB(sb)->reserved_cluster_lock);

  /* すでに確保済みのものがないかチェック */
  if(P2FAT_SB(sb)->reserved_cluster_count==0)
    {
      /* AU_sizeは512KBごとに数えている */
      int nr_clusters = ( P2FAT_SB(sb)->options.AU_size << 10 ) / P2FAT_SB(sb)->sec_per_clus;

      head_cluster = p2fat_alloc_cont_clusters(sb, nr_clusters);
      if( unlikely(head_cluster<0) )
	{
	  printk("%s-%d: Disk Full for RT Recording.\n",
		 __PRETTY_FUNCTION__, __LINE__);
	  mutex_unlock(&P2FAT_SB(sb)->reserved_cluster_lock);
	  return head_cluster;
	}

      P2FAT_SB(sb)->reserved_cluster_head  = head_cluster;
      P2FAT_SB(sb)->reserved_cluster_count = nr_clusters;

      /* 配置制御が新しいAUへと入ったので、
	 ここからはsequencialになると判断する */
      set_bit(RT_SEQ, &RS_SB(sb)->rt_flags);
    }
  else
    {
      head_cluster = P2FAT_SB(sb)->reserved_cluster_head;
    }

  /* 変な取り方されると困るので */
  BUG_ON(P2FAT_SB(sb)->reserved_cluster_count % length);
  BUG_ON(P2FAT_SB(sb)->reserved_cluster_count < length);

  head_sector = ((head_cluster-2)*P2FAT_SB(sb)->sec_per_clus) + P2FAT_SB(sb)->data_start;

  for(i=0; i<length; i++)
    {
      blocks[i] = head_sector;
      head_sector += P2FAT_SB(sb)->sec_per_clus;
      P2FAT_SB(sb)->reserved_cluster_head++;
      P2FAT_SB(sb)->reserved_cluster_count--;
    }

  mutex_unlock(&P2FAT_SB(sb)->reserved_cluster_lock);

  return 0;
}

static void p2fat_begin_rt_writing(struct super_block *sb)
{
  /* RT用のprev_freeをアラインメントにリセットする
     基本的にはendの段階で中途半端な分をうめているので
     配置関連でやることはないはず */

  /* 取り損なわれているファイル書き出し情報を破棄する */

  if( unlikely((done_list.rb_node != NULL)) )
    {
      printk("%s: Warning: Found Unknown Callback Entries. Discarding...\n",
	     __FUNCTION__);

      while( done_list.rb_node != NULL )
	{
	  struct rb_node *node;
	  struct p2fat_done_list_entry *entry;
	  
	  node = rb_first(&done_list);
	  entry = rb_entry(node, struct p2fat_done_list_entry, rb_node);

	  rb_erase(node, &done_list);
	  kfree(entry);
	}
    }

  return;
}

static void p2fat_prepare_end_rt_writing(struct super_block *sb, int fgroup)
{
  struct reservoir_operations *rs_ops = RS_SB(sb)->rs_ops;

  /* 余分に確保していたぶんをdummy bioで発行する。
     その領域解放はコールバック内でおこなわれる。 */
  mutex_lock(&P2FAT_SB(sb)->reserved_cluster_lock);

  if(P2FAT_SB(sb)->reserved_cluster_count)
    {
      while(P2FAT_SB(sb)->reserved_cluster_count)
	{
	  struct bio *bio;
	  unsigned long sector = ( (P2FAT_SB(sb)->reserved_cluster_head-2)
				   *P2FAT_SB(sb)->sec_per_clus
				   + P2FAT_SB(sb)->data_start ); 

	  bio = reservoir_dummy_bio_alloc(sb);
	  bio->bi_sector = sector;

	  if( likely(rs_ops->set_bio_callback!=NULL) )
	    {
	      /* bioが転送終了したときのコールバック関数を記録しておく */
	      /* 同時に、必要であれば中でbio_privateの内容をいじって
		 転送サイズの記録をおこなう*/
	      rs_ops->set_bio_callback(bio, sb, WRITE, 1);
	    }      

	  BUG_ON(bio->bi_end_io==NULL);
	  submit_rt_bio(WRITE, sb, bio);

	  P2FAT_SB(sb)->reserved_cluster_head++; 
	  P2FAT_SB(sb)->reserved_cluster_count--;
	}
    }

  mutex_unlock(&P2FAT_SB(sb)->reserved_cluster_lock);

  reservoir_sync_data(sb, fgroup);

  return;
}

static void p2fat_end_rt_writing(struct super_block *sb)
{
  unsigned long deadline_time = 0;

  /* FATチェーン処理の終了を確認する。
     ほとんど「念のため」というレベル。 */
  flush_workqueue(P2FAT_SB(sb)->rt_chain_updater_wq);

#ifndef P2FAT_CALLBACK_OMISSION

  /* Callback Waiting... */

  deadline_time = jiffies + RSFS_IO_END_WAIT_TIME;

  while( !( (done_list.rb_node == NULL) && (waitqueue_active(&done_event)) ) )
    {
      if( time_after(jiffies, deadline_time) )
	{
	  printk("[Warning] %s: Callback Operation may NOT be treated properly...\n", __FUNCTION__);
	  break;
	}

      cond_resched();
    }

#endif

  return;
}

static int p2fat_get_max_bios(struct super_block *sb)
{
  return P2FAT_ASIGN_CLUSTERS(sb);
}

static void p2fat_set_bio_callback(struct bio *bio, struct super_block *sb, int rw, int rt)
{
  unsigned long sector;
  unsigned long disk_cluster;
  struct p2fat_reservoir_private *bi_private;

  if( unlikely(bio==NULL) )
    goto FIN;

  /* bio->bi_sector からクラスタ番号を計算して
     bio->bi_private->disk_cluster に代入。
     コールバック関数の中で使う。 */

  bi_private = bio->bi_private;

  sector = bio->bi_sector;
  disk_cluster = ( ( sector - P2FAT_SB(sb)->data_start ) / P2FAT_SB(sb)->sec_per_clus ) + 2;
  bi_private->disk_cluster = disk_cluster;

  /* Read/Writeそれぞれに対応したコールバック関数を登録する
     SLAVEに関しても再帰的に登録 */
  do
    {
      if(rw==WRITE)
	{
	  bio->bi_end_io = p2fat_callback_write;
	}
      else
	{
	  bio->bi_end_io = p2fat_callback_read;
	}

      bio = (struct bio *)bio->bi_private2;
    }
  while( unlikely(bio!=NULL) );

 FIN:
  return;
}

static unsigned long p2fat_get_device_address(struct super_block *sb)
{
  /* zionの先頭アドレスを返す */
  return neo_address;
}

static unsigned long p2fat_get_addr_for_dummy_write(struct super_block *sb)
{
  /* write用なのでデータは壊されないため、zionの先頭アドレスを返す */
  return neo_address;
}

static unsigned long p2fat_get_addr_for_dummy_read(struct super_block *sb)
{
  /* p2fatにioctlで通知されているゴミ箱領域のアドレスを返す */
  return neo_trash_can_address;
}

static int p2fat_exchange_cluster(struct kiocb *iocb,
				  const struct iovec *iov,
				  unsigned long nr_segs,
				  loff_t pos, loff_t *ppos,
				  size_t *count, ssize_t *written)
{
  struct file *filp = iocb->ki_filp;
  struct address_space *mapping = filp->f_mapping;
  struct inode *inode = mapping->host;
  const struct address_space_operations *a_ops = mapping->a_ops;
  long status = 0;
  int drct = test_bit(RS_PCIDRCT, &inode->i_rsrvr_flags);
  unsigned long rs_block_size = RS_SB(inode->i_sb)->rs_block_size;
  struct iov_iter iter;

  *written = 0;

  iov_iter_init(&iter, iov, nr_segs, *count, 0);

  do
    {
      unsigned long index = pos >> PAGE_CACHE_SHIFT;
      unsigned long offset = (pos & (PAGE_CACHE_SIZE - 1));
      size_t bytes = PAGE_CACHE_SIZE - offset;
      size_t copied = 0;
      int retry = 0;
      struct page *page = NULL;
      void *fsdata = NULL;

      /* 書き込み末尾の場合を考えてbytesを丸める */
      bytes = min(bytes, iov_iter_count(&iter));

    RETRY:

      /* 該当するpageを探して握る */
      page = __grab_cache_page(mapping, index);
      if( unlikely(!page) )
	{
	  printk("%s-%d : Page Getting Error.\n", __FUNCTION__, __LINE__);
	  status = -ENOMEM;
	  break;
	}

      /* writeback状態たった場合は、解消されるまで待つ */
      /* ここに引き込まれてきた段階では
	 i_start!=0 だから、pageが書き出し待ちに
	 なっていることはないはずだが、念のため */

      if( unlikely(PageWriteback(page)) )
	{
	  unlock_page(page);
	  mark_page_accessed(page);
	  page_cache_release(page);

	  cond_resched();

	  retry++;
	  if( unlikely(retry > 100) )
	    {
	      printk("%s-%d: Unexpected Error.\n", __PRETTY_FUNCTION__, __LINE__);
	      status = -EIO;
	      break;
	    }

	  goto RETRY;
	}

      /* 下地の準備などをおこなう */
      if(page_has_buffers(page))
	try_to_free_buffers(page);

      if(test_bit(RS_PCIDRCT, &inode->i_rsrvr_flags))
	{
	  char *kaddr = NULL;
	  struct drct_page *drct_page = NULL;

	  BUG_ON(PageUptodate(page));
	  /* DirectPageのための初期化 */
	  kaddr = kmap(page);
	  drct_page = (struct drct_page *)kaddr;
	  drct_page->flags = 0;
	  drct_page->nr_entries = 0;
	  drct_page->total_size = 0;
	  drct_page->dummy_size = 0;
	  drct_page->page_chain = NULL;
	  kunmap(page);
	  flush_dcache_page(page);
	}

      SetPageUptodate(page);

      pagefault_disable();
      copied = reservoir_filemap_copy_from_user(page, &iter, offset, bytes, drct);
      pagefault_enable();

      flush_dcache_page(page);

      status = a_ops->write_end(filp, mapping, pos, bytes, copied, page, fsdata);

      /* どこかでエラーが起こっていたらループを抜ける */
      if(status < 0)
	break;

      iov_iter_advance(&iter, copied);

      pos += copied;
      *written += copied;
      *count -= copied;

      if( unlikely(copied != bytes) )
	{
	  printk("%s-%d: Unexpected Error Occured. (%u-%u)\n",
		 __FUNCTION__, __LINE__, copied, bytes);
	  status = -EFAULT;
	  break;
	}

      /* ブロックの末尾ページの尻まで書き込みだった場合 */
      if( ( (index+1) % rs_block_size == 0 )
	  && ( (offset+bytes) == PAGE_CACHE_SIZE) )
	{
	  /* クラスタ先頭からページをさらってreservoirに入れてゆく */
	  reservoir_submit_unasigned_pages(inode,
					   (index + 1 - rs_block_size),
					   rs_block_size);
	}

      /* どこかでエラーが起こっていたらループを抜ける */
      if( unlikely(status < 0) )
	break;

      balance_dirty_pages_ratelimited(mapping);
      cond_resched();
    }
  while(iov_iter_count(&iter));

  /* ファイルポインタの更新 */
  *ppos = pos;

  return *written ? 0 : status;
}

static int p2fat_write_check(struct kiocb *iocb,
			     const struct iovec *iov,
			     unsigned long nr_segs,
			     loff_t pos, loff_t *ppos,
			     size_t *count, ssize_t *written)
{
  struct file *filp = iocb->ki_filp;
  struct inode *inode = filp->f_mapping->host;
  struct super_block *sb = inode->i_sb;

  /* 残りが1ブロック以下のときは受け取らない */
  /* 通常、実機では連続空き容量がないのに
     Writeが来ることは発生しえないうえ、free clustersの誤った
     カードが用いられたときにパニックになるので、見るのはやめる。
  if( unlikely( P2FAT_SB(sb)->free_clusters
		< ( P2FAT_SB(sb)->options.AU_size << 10) / P2FAT_SB(sb)->sec_per_clus ) )
    {
      printk("%s-%d : No Enough Space in the Device. (%d clusters)\n",
	     __FUNCTION__, __LINE__, P2FAT_SB(sb)->free_clusters);
      return -ENOSPC;
    }
  */

  /* FATとしての最大サイズを越えていないか */
  if( unlikely( (*ppos + *count) > sb->s_maxbytes ) )
    {
      printk("%s-%d: File Max Size Exceeded.\n",
	     __FUNCTION__, __LINE__);
      return -EINVAL;
    }

  if(inode->i_size != *ppos)
    {
      if( unlikely(*ppos % PAGE_CACHE_SIZE) )
	{
	  printk("%s-%d: Bad File Pointer (%lld@%lld)\n",
		 __FUNCTION__, __LINE__, *ppos, inode->i_size);
	  return -EINVAL;
	}

      if( unlikely( (*count%PAGE_CACHE_SIZE) && ((*ppos + *count)<inode->i_size) ) )
	{
	  printk("%s-%d: Invalid Write Size\n", __FUNCTION__, __LINE__);
	  return -EINVAL;
	}

      /* ここで、先頭クラスタの入れ換え処理を実行してしまう。
	 実行後は *count をゼロに設定しておくことで
	 この関数を抜けて以降の処理をすっ飛ばすことにする。
	 （いろいろこの処理を入れる場所を考えたのだが、
	 やはりFAT特有の処理なので、ここにした） */
      if(*ppos==0
	 && (*count==P2FAT_SB(sb)->cluster_size
	     || (*count < P2FAT_SB(sb)->cluster_size && *count >= inode->i_size )) )
	{
	  return p2fat_exchange_cluster(iocb, iov, nr_segs, pos, ppos, count, written);
	}
    }

  return 0;
}

struct reservoir_operations p2fat_rs_ops =
  {
    .begin_rt_writing = p2fat_begin_rt_writing,
    .prepare_end_rt_writing = p2fat_prepare_end_rt_writing,
    .end_rt_writing = p2fat_end_rt_writing,
    .get_n_blocks = p2fat_get_n_blocks,
    .get_block = p2fat_get_block,
    .set_bio_callback = p2fat_set_bio_callback,
    .alloc_bio_private = p2fat_alloc_bio_private,
    .set_bio_private = p2fat_set_bio_private,
    .prepare_submit = p2fat_prepare_submit,
    .wait_on_localfs = p2fat_wait_on_sync,
    .get_device_address = p2fat_get_device_address,
    .get_addr_for_dummy_write = p2fat_get_addr_for_dummy_write,
    .get_addr_for_dummy_read = p2fat_get_addr_for_dummy_read,
    .write_check = p2fat_write_check,
    .get_max_bios = p2fat_get_max_bios,
  };

/*********** ここから下は、コールバック用の Char Dev 関係 **************/

#define P2FAT_NOTIFY_WAKEUP_INTERVAL (60*HZ)

static int p2fat_notify_end(struct inode *inode, struct file *filp, struct fat_end_notify *notify)
{
  notify->flags = 0;
  notify->entries = 0;
  notify->ret = 0;
  memset(notify->file_ids, 0, sizeof(unsigned long)*FAT_MAX_NOTIFY_END);
  memset(notify->sizes, 0, sizeof(unsigned long)*FAT_MAX_NOTIFY_END);

  if(notify->wait)
    {
      /* イベントが起きるか、強制起床されるまで寝る */
      while((done_list.rb_node == NULL)
	    && !test_bit(P2FAT_FORCE_WAKEUP, &done_flags))
	{
	  int ret = wait_event_interruptible_timeout(done_event,
						     (done_list.rb_node != NULL)
						     ||test_bit(P2FAT_FORCE_WAKEUP, &done_flags),
						     P2FAT_NOTIFY_WAKEUP_INTERVAL);
	  if(ret==-ERESTARTSYS)
	    {
	      /* Interrupted by signal */
	      return 0;
	    }
	}

      if(done_list.rb_node == NULL
	 && test_and_clear_bit(P2FAT_FORCE_WAKEUP, &done_flags))
	{
	  /* 強制起床されたことをフラグで教える */
	  set_bit(P2FAT_FORCE_WAKEUP, &notify->flags);
	}
    }

  mutex_lock(&done_lock);

  /* コールバック用の情報を走査する */
  while( done_list.rb_node != NULL )
    {
      struct rb_node *node;
      struct p2fat_done_list_entry *entry;

      node = rb_first(&done_list);
      entry = rb_entry(node, struct p2fat_done_list_entry, rb_node);

      notify->file_ids[notify->entries] = entry->file_id;
      notify->notify_ids[notify->entries] = entry->notify_id;
      notify->sizes[notify->entries] = entry->size;
      notify->entries++;
      if(entry->errno)
	{
	  notify->ret = entry->errno;
	}

      rb_erase(node, &done_list);
      kfree(entry);

      if(notify->entries==FAT_MAX_NOTIFY_END)
	{
	  /* 一回に通知するエントリ上限を越えたらやめる */
	  break;
	}
    }

  mutex_unlock(&done_lock);

  return 0;
}


static int p2fat_kick_notify_up(void)
{
  /* 強制起床フラグを立ててタスクを起こす */
  set_bit(P2FAT_FORCE_WAKEUP, &done_flags);
  wake_up(&done_event);

  return 0;
}

int p2fat_char_ioctl(struct inode *inode, struct file *filp,
		     unsigned int cmd, unsigned long arg)
{
  struct fat_end_notify notify;
  struct p2fat_device_info device_info;
  int ret = 0;

  switch(cmd)
    {
    case FAT_IOCTL_GET_NOTIFY_END:
      if(copy_from_user(&notify, (struct fat_end_notify *)arg, sizeof(notify)))
	{
	  printk("%s-%d : Copy_from_User Error.\n", __FUNCTION__, __LINE__);
	  return -EINVAL;
	}
      ret = p2fat_notify_end(inode, filp, &notify);
      if(copy_to_user((struct fat_end_notify *)arg, &notify, sizeof(notify)))
	{
	  printk("%s-%d : Copy_to_User Error.\n", __FUNCTION__, __LINE__);
	  return -EINVAL;
	}
      break;

    case FAT_IOCTL_KICK_NOTIFY_UP:
      ret = p2fat_kick_notify_up();
      break;

    case FAT_IOCTL_SET_DEVICE_INFO:
      if(copy_from_user(&device_info, (struct p2fat_device_info *)arg, sizeof(device_info)))
	{
	  printk("%s-%d : Copy_from_User Error.\n", __FUNCTION__, __LINE__);
	  return -EINVAL;
	}
      neo_address = device_info.device_addr;
      neo_trash_can_address = neo_address + device_info.trash_can_offset;
      break;

    default:
      {
	printk("%s-%d : Invalid Call.\n", __FUNCTION__, __LINE__);    
	ret = -EINVAL;
      }
    }

  return ret;
}

static int p2fat_char_open(struct inode *inode, struct file *file)
{
#ifdef RSFS_DEBUG
  //printk("%s Called.\n", __PRETTY_FUNCTION__);
#endif
  return 0;
}

static int p2fat_char_release(struct inode *indoe, struct file *file)
{
#ifdef RSFS_DEBUG
  //printk("%s Called.\n", __PRETTY_FUNCTION__);
#endif
  return 0;
}

static struct file_operations p2fat_char_ops = {
  .owner = THIS_MODULE,
  .open  = p2fat_char_open,
  .ioctl = p2fat_char_ioctl,
  .release = p2fat_char_release
}; 

int init_p2fat_callback_module(void)
{
  printk("*** Initializing P2FAT Filesytem ... ***\n");
  printk(" P2FAT Filesytem : %s\n", reservoir_fat_revision);
  printk(" Built on Reservoir Filesytem : %s\n", reservoir_fs_revision);

  done_list = RB_ROOT;
  mutex_init(&done_lock);
  init_waitqueue_head(&done_event);
  done_flags = 0;

  return register_chrdev(P2FAT_CHARDEV_MAJOR, P2FAT_CHARDEV_NAME, &p2fat_char_ops);
}

void exit_p2fat_callback_module(void)
{
  if(waitqueue_active(&done_event))
    {
      p2fat_kick_notify_up();
    }

 unregister_chrdev(P2FAT_CHARDEV_MAJOR, P2FAT_CHARDEV_NAME);

 return;
}
