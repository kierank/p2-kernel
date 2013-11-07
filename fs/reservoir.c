#include <linux/capability.h>
#include <linux/module.h>
#include <linux/time.h>
#include <linux/smp_lock.h>
#include <linux/buffer_head.h>
#include <linux/writeback.h>
#include <linux/backing-dev.h>
#include <linux/blkdev.h>
#include <linux/fs.h>
#include <linux/swap.h>
#include <linux/pagevec.h>
#include <linux/reservoir_fs.h>
#include <linux/mpage.h>
#include <linux/dma-mapping.h>

char reservoir_fs_revision[] = "$Rev: 21273 $";

inline size_t
reservoir_filemap_copy_from_user_single(struct page *page, unsigned long offset,
					const char __user *buf, unsigned bytes)
{
  char *kaddr;
  struct drct_page *drct_page;
  unsigned long total_size = 0;

  /* Direct転送の指定だった場合 */

  /* HIGHMEM対応のためpageをマップ */
  kaddr = kmap(page);
  
  drct_page = (struct drct_page *)kaddr;

  total_size = drct_page->total_size;

  /* 1ページにおさまりきらなかったときのための
     チェーンをたどる。実機ではまず使われない */
  while( unlikely(drct_page->page_chain!=NULL) )
    {
      drct_page = (struct drct_page *)drct_page->page_chain;
      total_size += drct_page->total_size;
    }

  /* ページには追記しか許可しない */
  BUG_ON(total_size != offset);

  if(drct_page->nr_entries>=MAX_DRCT_ENTRIES)
    {
      /* 現pageではたりなかったので、
	 補助pageをkmallocしてリンクする */

      drct_page->page_chain = kmalloc(PAGE_CACHE_SIZE, GFP_KERNEL);
      if( unlikely(drct_page==NULL) )
	{
	  printk("%s-%d: Getting Sub Page in Direct Page Failed.\n",
		 __PRETTY_FUNCTION__, __LINE__);
	  kunmap(page);
	  return bytes;
	}

      drct_page = (struct drct_page *)(drct_page->page_chain);

      drct_page->flags = 0;
      drct_page->nr_entries = 0;
      drct_page->total_size = 0;
      drct_page->dummy_size = 0;
      drct_page->page_chain = NULL;
    }

  /* 新しいエントリを追加する */
  drct_page->entries[drct_page->nr_entries].pci_addr = (dma_addr_t)buf;
  drct_page->entries[drct_page->nr_entries].size = bytes;
  drct_page->nr_entries++;
  drct_page->total_size += bytes;

  /* kmapの後処理 */
  kunmap(page);

  return 0;
}

inline size_t
reservoir_filemap_copy_from_user_iovec(struct page *page, unsigned long offset,
				       const struct iovec *iov, size_t base, size_t bytes)
{
  char *kaddr;
  size_t copied = 0;
  struct drct_page *drct_page;
  unsigned long total_size = 0;

  /* DIRECT転送時の対応 */

  /* HIGHMEM対応 */
  kaddr = kmap(page);
  drct_page = (struct drct_page *)kaddr;

  total_size = drct_page->total_size;

  while( unlikely(drct_page->page_chain!=NULL) )
    {
      drct_page = (struct drct_page *)drct_page->page_chain;
      total_size += drct_page->total_size;
    }

  BUG_ON(total_size!=offset);

  while(bytes)
    {
      char __user *buf = iov->iov_base + base;
      int copy = min(bytes, iov->iov_len - base);

      if(drct_page->nr_entries>=MAX_DRCT_ENTRIES)
	{
	  /* 現pageではたりなかったので、
	     補助pageをkmallocしてリンクする */

	  drct_page->page_chain = kmalloc(PAGE_CACHE_SIZE, GFP_KERNEL);
	  if(drct_page==NULL)
	    {
	      printk("%s-%d: Getting Sub Page in Direct Page Failed.\n",
		     __PRETTY_FUNCTION__, __LINE__);
	      kunmap(page);
	      return 0;
	    }

	  drct_page = (struct drct_page *)(drct_page->page_chain);

	  drct_page->flags = 0;
	  drct_page->nr_entries = 0;
	  drct_page->total_size = 0;
	  drct_page->dummy_size = 0;
	  drct_page->page_chain = NULL;
	}

      base = 0;
      
      /* DIRECTエントリの追加 */
      drct_page->entries[drct_page->nr_entries].pci_addr = (dma_addr_t)buf;
      drct_page->entries[drct_page->nr_entries].size = copy;
      drct_page->nr_entries++;
      drct_page->total_size += copy;

      copied += copy;
      bytes -= copy;
      iov++;
    }

  /* kmap後処理 */
  kunmap(page);

  return copied;
}

size_t
reservoir_filemap_copy_from_user(struct page *page, struct iov_iter *iter, 
				 unsigned long offset, unsigned bytes, int drct)
{
  size_t copied = 0;

  if(unlikely(!drct))
    {
      return iov_iter_copy_from_user_atomic(page, iter, offset, bytes);
    }

  if(likely(iter->nr_segs == 1))
    {
      int left;
      char __user *buf = iter->iov->iov_base + iter->iov_offset;

      left = reservoir_filemap_copy_from_user_single(page, offset, buf, bytes);

      copied = bytes - left;
    }
  else
    {
      copied = reservoir_filemap_copy_from_user_iovec(page, offset, iter->iov,
						      iter->iov_offset, bytes);
    }

  return copied;
}

static inline int reservoir_clear_page_dirty(struct page *page)
{
  int ret;
  struct address_space *mapping = page_mapping(page);
  struct inode *inode = mapping->host; 

  ret = clear_page_dirty_for_io(page);
  
  if(ret && test_bit(RS_RT, &inode->i_rsrvr_flags))
    {
      write_lock_irq(&mapping->tree_lock);
      radix_tree_tag_clear(&mapping->page_tree,
			   page_index(page),
			   PAGECACHE_TAG_DIRTY);
      write_unlock_irq(&mapping->tree_lock);
    }

  return ret;
}

#ifndef CONFIG_KGDB
static inline int reservoir_set_page_dirty(struct page *page)
#else
static int reservoir_set_page_dirty(struct page *page)
#endif
{
  int ret = 0;
  struct address_space *mapping = page_mapping(page);
  struct inode *inode = mapping->host; 
  struct super_block *sb = inode->i_sb;

  if(test_bit(RS_RT, &inode->i_rsrvr_flags))
    {
      __set_page_dirty_nobuffers(page);  
    }
  else
    {
      unsigned block_start, block_end;
      struct buffer_head *bh, *head;
      sector_t block;

      if(page_has_buffers(page))
	create_empty_buffers(page, 1 << inode->i_blkbits, 0);

      head = page_buffers(page);

      block = (sector_t)page->index << ( PAGE_CACHE_SHIFT - (inode->i_blkbits) );

      for(bh=head, block_start=0;
	  bh!=head || !block_start;
	  block++, block_start=block_end, bh=bh->b_this_page)
	{
	  int err;

	  block_end = block_start + (1<<inode->i_blkbits);
	  clear_buffer_new(bh);
	  
	  if(!buffer_mapped(bh))
	    {
	      err = RS_SB(sb)->rs_ops->get_block(inode, block, bh, 1);
	      if( unlikely(err) )
		{
		  /* mmapを使って書いていたら、ここでエラーになるかも？
		     とりあえず現在はエラーとしておく */
		  printk("%s-%d: Unexpected Error.", __PRETTY_FUNCTION__, __LINE__);
		  break;
		}
	    }

	  set_buffer_uptodate(bh);
	  mark_buffer_dirty(bh);
	}
    }

  return ret;
}

static struct bio *reservoir_bio_alloc(struct super_block *sb,
				       struct inode *inode, sector_t first_sector, int rw)
{
  struct bio *bio = NULL;
  struct reservoir_operations *rs_ops = RS_SB(sb)->rs_ops;

  bio = mpage_alloc(sb->s_bdev, first_sector,
		    bio_get_nr_vecs(sb->s_bdev), GFP_NOFS|__GFP_HIGH);
  if(bio!=NULL)
    {
      set_bit(BIO_RW_RT, &bio->bi_rw);

      if( likely(rs_ops->alloc_bio_private!=NULL) )
	{
	  /* P2FSでは、実際にこのbioで転送するファイル情報と
	     データの量を記録しておく */
	  rs_ops->alloc_bio_private(bio, sb, inode, rw);
	}

      bio->bi_private2 = NULL;
    }

  return bio;
}

struct bio *reservoir_dummy_bio_alloc(struct super_block *sb)
{
  struct bio *bio = NULL;
  struct bio_vec *bvec = NULL;
  struct reservoir_operations *rs_ops = RS_SB(sb)->rs_ops;
  unsigned long dummy_addr = 0;

  bio = reservoir_bio_alloc(sb, NULL, 0, WRITE);
  if( unlikely(bio==NULL) )
    {
      printk("%s-%d: Getting BIO Failed.\n", __PRETTY_FUNCTION__, __LINE__);
      goto FAIL;
    }

  set_bit(BIO_RW_DUMMY, &bio->bi_rw);

  /* 非Direct系とDirect系のダミー、
     どっちを作るかは非常に判断の難しいところだが、
     get_addr_for_dummyの返り値が
     NULLかどうかで判断することにした */

  if( likely(rs_ops->get_addr_for_dummy_write!=NULL) )
    dummy_addr = rs_ops->get_addr_for_dummy_write(sb);

  BUG_ON(bio->bi_vcnt!=0);

  bvec = &bio->bi_io_vec[0];

  if(dummy_addr != 0)  // ダミーアドレスが定義されているから、システムはDirect対応している
    {
      /* write用dummy領域のサイズは128MB（ZION全面）。
	 その先頭からクラスタサイズぶんを使用 */
      bvec->bv_page = NULL;
      bvec->bv_private = dummy_addr;
      bvec->bv_len =  RS_SB(sb)->rs_block_size * PAGE_CACHE_SIZE;

      bvec++;
      bio->bi_vcnt++;
      bio->bi_phys_segments++;
      bio->bi_hw_segments++;

      set_bit(BIO_RW_DRCT, &bio->bi_rw);
    }
  else
    {
      int i=0;

      for(i=0; i<RS_SB(sb)->rs_block_size; i++)
	{
	  struct page *page = alloc_pages(GFP_KERNEL, 0);
	  if( unlikely(page==NULL) )
	    {
	      printk("%s-%d: Getting Dummy Page Failed.\n", __PRETTY_FUNCTION__, __LINE__);
	      /* でもどうしようもない？ */
	    }
	  else
	    {
	      SetPageUptodate(page);
	      TestSetPageWriteback(page);
	    }

	  bvec->bv_page = page;
	  bvec->bv_len = PAGE_CACHE_SIZE;
	  bio->bi_vcnt++;
	  bio->bi_phys_segments++;
	  bio->bi_hw_segments++;

	  bvec++;
	}
    }

  bio->bi_size = RS_SB(sb)->rs_block_size * PAGE_CACHE_SIZE;

  if( likely(rs_ops->set_bio_private!=NULL) )
    {
      /* privateな空間に、dummyクラスタであることを書き込む。
       コールバックでは、転送先のクラスタは開放する */
      rs_ops->set_bio_private(bio, NULL, WRITE, 1);
    }

 FAIL:
  return bio;
}

int reservoir_io_wait(void *word)
{
  io_schedule();
  return 0;
}

int reservoir_flush_reservoir(struct bio_reservoir *reservoir)
{
  struct bio *bio_walk = NULL;
  struct reservoir_operations *rs_ops = RS_SB(reservoir->sb)->rs_ops;
  int ret = -EINVAL;
  int i=0;

  for(i=0, bio_walk=reservoir->bio_head ;
      bio_walk!=NULL; i++)
    {
      struct bio *cur_bio = bio_walk;
      unsigned long sector = 0;

      if( likely(rs_ops->get_n_blocks!=NULL) )
	{
	  ret = rs_ops->get_n_blocks(reservoir->sb, 1, &sector);
	}

      if(ret)
	{
	  printk("%s-%d: No Space Found.\n",
		 __PRETTY_FUNCTION__, __LINE__);
	  goto FAIL;
	}

      cur_bio->bi_sector = sector;
      bio_walk = cur_bio->bi_next;
      cur_bio->bi_next = NULL;

      if( likely(rs_ops->set_bio_callback!=NULL) )
	{
	  /* bioが転送終了したときのコールバック関数を記録しておく */
	  rs_ops->set_bio_callback(cur_bio, reservoir->sb, WRITE, atomic_read(&reservoir->rt_count));
	}      

      BUG_ON(cur_bio->bi_end_io==NULL);
      submit_rt_bio(WRITE, reservoir->sb, cur_bio);

      reservoir->bio_head = bio_walk;
      reservoir->cur_length--;
    }

  reservoir->bio_tail = NULL;

 FAIL:

  reservoir->cls_ptr = 0;
  memset(reservoir->suspended_cls, 0,
	 sizeof(unsigned long)*RS_MAX_BIOS);
  reservoir->max_length = 1;  

  return ret;
}

static int reservoir_rt_flush_reservoir(struct bio_reservoir *reservoir,
					struct reservoir_operations *rs_ops)
{
  struct bio *bio_walk = reservoir->bio_head;
  int ret = 0;
  unsigned long max_length = rs_ops->get_max_bios(reservoir->sb);
  struct request_queue *q = NULL;
  unsigned long deadline_time = 0;

  /* 何もないならさっさと抜ける */
  if(bio_walk == NULL)
    {
      return 0;
    }

  /* i/o schedulerの実行キューが一定以下になるのを少し待ってみる */
  q = bdev_get_queue(bio_walk->bi_bdev);
  if( unlikely(q==NULL) )
    {
      printk("%s-%d : Unexpected Error.\n", __FUNCTION__, __LINE__);
      return -ENODEV;
    }

  if( likely(q->elevator->ops->elevator_ready_fn!=NULL) )
    {
      deadline_time = jiffies + RSFS_IO_WAIT_TIME;

      while(!q->elevator->ops->elevator_ready_fn(q))
	{
	  if( time_after(jiffies, deadline_time) )
	    {
	      //printk("[Warning] %s: Waiting Free Queue Failed.\n", __FUNCTION__);
	      break;
	    }

	  cond_resched();
	}
    }

  /* すでに確保しているクラスタがないかチェックする */
  if(reservoir->suspended_cls[reservoir->cls_ptr]==0)
    {
      reservoir->cls_ptr = 0;
      ret = rs_ops->get_n_blocks(reservoir->sb, reservoir->max_length,
				 reservoir->suspended_cls);
      if( unlikely(ret) )
	{
	  /* 連続空きがみつからなかったので、
	     ひとつずつ書き出していく */
	  return reservoir_flush_reservoir(reservoir);
	}
    }

  /* 決まったクラスタをbioに付与してreservoirから吐き出す */
  bio_walk=reservoir->bio_head;

  while( bio_walk!=NULL )
    {
      struct bio *cur_bio = bio_walk;

      BUG_ON(reservoir->suspended_cls[reservoir->cls_ptr]==0);

      cur_bio->bi_sector = reservoir->suspended_cls[reservoir->cls_ptr];
      bio_walk = cur_bio->bi_next;
      reservoir->cls_ptr++;
      cur_bio->bi_next = NULL;

      /* Sequencialかどうかを判断する */
      if(test_bit(RT_SEQ, &RS_SB(reservoir->sb)->rt_flags))
	{
	  set_bit(BIO_RW_SEQ, &cur_bio->bi_rw);
	}

      if( likely(rs_ops->set_bio_callback!=NULL) )
	{
	  /* bioが転送終了したときのコールバック関数を記録しておく */
	  /* 同時に、必要であれば中でbio_privateの内容をいじって
	     転送サイズの記録をおこなう*/
	  rs_ops->set_bio_callback(cur_bio, reservoir->sb, WRITE, atomic_read(&reservoir->rt_count));
	}      

      BUG_ON(cur_bio->bi_end_io==NULL);
      submit_rt_bio(WRITE, reservoir->sb, cur_bio);
    }

  reservoir->bio_head = NULL;
  reservoir->bio_tail = NULL;
  reservoir->cur_length = 0;

  if(reservoir->cls_ptr == max_length)
    {
      /* きれいさっぱり吐き出した場合 */
      memset(reservoir->suspended_cls, 0,
	     sizeof(unsigned long)*RS_MAX_BIOS);
      reservoir->max_length = max_length;
      reservoir->cls_ptr = 0;
    }
  else
    {
      /* 確保したクラスタに残りがあるときは、
	 max_lengthをその残りの量に設定しなおしておく */
      BUG_ON(reservoir->cls_ptr > max_length);
      reservoir->max_length = max_length - reservoir->cls_ptr;
    }

  return ret;
}

static int reservoir_submit_bio(struct super_block *sb,
				struct bio *bio, struct bio_reservoir *reservoir,
				struct reservoir_operations *rs_ops)
{
  int ret = 0;

  /* ここくらいでしか調べるところがないので… */
  BUG_ON(RS_MAX_BIOS < reservoir->max_length);

  mutex_lock(&reservoir->reservoir_lock);

  if(reservoir->bio_head==NULL)
    {
      reservoir->bio_head = bio;
    }

  if(reservoir->bio_tail!=NULL)
    {
      reservoir->bio_tail->bi_next = bio;
    }

  reservoir->bio_tail = bio;
  reservoir->cur_length++;

  if(atomic_read(&reservoir->rt_count)
     && reservoir->cur_length >= reservoir->max_length)
    {
      ret = reservoir_rt_flush_reservoir(reservoir,rs_ops);
    }

  mutex_unlock(&reservoir->reservoir_lock);

  return ret;
}

static int reservoir_bio_add_page(struct bio *bio, struct page *page,
				  struct inode *inode, int rw)
{
  struct bio_vec *bvec = NULL;
  char *kaddr = NULL;
  struct drct_page *drct_page = NULL, *drct_page_walk = NULL;
  int total_size = 0;
  int entries = 0;
  int index_offset = 0;
  struct super_block *sb = inode->i_sb;
  struct reservoir_operations *rs_ops = RS_SB(sb)->rs_ops;
  int drct = test_bit(RS_PCIDRCT, &inode->i_rsrvr_flags);
  struct bio *first_bio = bio;
  unsigned long device_addr = RS_SB(sb)->rs_ops->get_device_address(sb);

  /* PCI Directでない場合は、標準のものを呼び出す */
  if(!drct)
    {
      total_size = bio_add_page(bio, page, PAGE_CACHE_SIZE, 0);
      goto FIN;
    }

  /* SLAVEのbioがある場合は、その末尾に移動する */
  while(bio->bi_private2!=NULL)
    {
      bio = (struct bio *)bio->bi_private2;
    }

  kaddr = kmap(page);
  drct_page = (struct drct_page *)kaddr;
  entries = drct_page->nr_entries;
  total_size = drct_page->total_size;

  /* 補助ページも走査する。ほとんど使われていないけれど。 */
  drct_page_walk = (struct drct_page *)drct_page->page_chain;
  while( unlikely(drct_page_walk!=NULL) )
    {
      entries += drct_page_walk->nr_entries;
      total_size += drct_page_walk->total_size;
      drct_page_walk = (struct drct_page *)drct_page_walk->page_chain;
    }

  BUG_ON(total_size!=PAGE_CACHE_SIZE);

  /* 問題なくエントリがbioひとつに収まる場合。
     これがほとんどの場合だといえる。
     実際の機器ではこれに反することはおこらない */
  if( likely( (bio->bi_vcnt + entries) <= bio->bi_max_vecs ) )
    {
      int i = 0;

      /* bioのベクター末尾を取得する */
      bvec = &bio->bi_io_vec[bio->bi_vcnt];

      /* BIOにDRCTフラグをたてる */
      set_bit(BIO_RW_DRCT, &bio->bi_rw);

      for(i=0; i<entries; i++)
	{
	  if(unlikely( (i!=0) && !(i%MAX_DRCT_ENTRIES) ))
	    {
	      drct_page = (struct drct_page *)drct_page->page_chain;
	      index_offset += MAX_DRCT_ENTRIES;
	    }

	  bvec->bv_page = (rw==READ && i==0) ? page : NULL;
	  bvec->bv_private = drct_page->entries[i-index_offset].pci_addr;
	  bvec->bv_len = drct_page->entries[i-index_offset].size;

	  /* ZION上以外からの転送の場合は、キャッシュの処理をしておく。
	     ダイレクト転送の場合はファイルシステムの役目 */
	  /* 実際にはほとんどがDirect転送だからunlikelyにしておく */
	  if(unlikely(bvec->bv_private < device_addr))
	    {
	      if(rw==WRITE)
		{
/* 		  dma_cache_wback( bus_to_virt(bvec->bv_private), bvec->bv_len ); */
		  dma_cache_sync(NULL, bus_to_virt(bvec->bv_private),
				 bvec->bv_len, DMA_TO_DEVICE);
		}
	      else
		{
/* 		  dma_cache_inv( bus_to_virt(bvec->bv_private), bvec->bv_len ); */
		  dma_cache_sync(NULL, bus_to_virt(bvec->bv_private),
				 bvec->bv_len, DMA_FROM_DEVICE);
		}
	    }

	  bio->bi_vcnt++;
	  bio->bi_phys_segments++;
	  bio->bi_hw_segments++;
	  
	  bvec++;
	}

      bio->bi_size += total_size;
    }
  /* bioに収まる最大エントリ数からはみでる場合。 */
  else
    {
      struct drct_entry *last_entry = &drct_page->entries[0];
      int entry_bytes_offset = 0;
      int page_size_remain = total_size;

      drct_page_walk = drct_page;

      /* まず起こることはないはずなので、超応急処置的。
	 エントリを512byteごとにちぎってbioひとつずつに入れる。
	 エントリの最大数128と、エントリ毎の最大転送サイズ4byteから
	 入ることが保証されているはず。 */
      while(page_size_remain)
	{
	  int sector_entries = 0;
	  int sum = 0;
	  int transfered = 0;
	  int i = 0;
	  struct drct_entry *entry_walk = last_entry;
	  struct drct_page *tmp = drct_page_walk;
 
     
	  /* まず、512byteに必要なエントリ数を数える */
	  while(sum<512)
	    {
	      sum += entry_walk->size - ((entry_walk==last_entry)?entry_bytes_offset:0);
	      sector_entries++;

	      if(unlikely(entry_walk==&tmp->entries[MAX_DRCT_ENTRIES-1]))
		{
		  tmp = (struct drct_page *)tmp->page_chain;
		  BUG_ON( (tmp==NULL) && (sum<512) );

		  entry_walk = &tmp->entries[0];
		}
	      else
		{
		  entry_walk++;
		}
	    }

	  BUG_ON(sector_entries > bio->bi_max_vecs);

	  /* もしbioにこれから入れる転送リストが
	     入りきらない場合は、新しいbioを確保して
	     private2にリストする */
	  if( (bio->bi_vcnt + sector_entries) > bio->bi_max_vecs )
	    {
	      struct bio *new_bio = NULL;
	      
	      /* セクタ番号はsubmit時に親から計算して振るので、
		 この段階ではゼロでも大丈夫 */
	      new_bio = reservoir_bio_alloc(inode->i_sb, inode, 0, WRITE);
	      if(new_bio == NULL)
		{
		  kunmap(page);
		  printk("%s-%d: Getting BIO Failed.\n", __PRETTY_FUNCTION__, __LINE__);
		  goto FIN;
		}
      
	      bio->bi_private2 = (void *)new_bio;
	      bio = new_bio;
	    }

	  /* bioのベクター末尾を取得する */
	  bvec = &bio->bi_io_vec[bio->bi_vcnt];

	  /* BIOにDRCTフラグをたてる */
	  set_bit(BIO_RW_DRCT, &bio->bi_rw);

	  while(transfered<512)
	    {
	      /* DIRECT転送としてのエントリを入れる */
	      unsigned long last_entry_size = last_entry->size - entry_bytes_offset;
	      dma_addr_t last_entry_addr = (dma_addr_t)( ((unsigned long)last_entry->pci_addr)
							 + entry_bytes_offset );
	      unsigned long size_remain =  (512 - transfered);
	      unsigned long size_transfer = min(size_remain, last_entry_size);

	      /* writeでは、転送終わるころには掃除されてpageはいなくなってます。 */
	      bvec->bv_page = (rw==READ && i==0) ? page : NULL;
	      bvec->bv_private = last_entry_addr;
	      bvec->bv_len = size_transfer;;

	      bio->bi_vcnt++;
	      bio->bi_phys_segments++;
	      bio->bi_hw_segments++;
	  
	      bvec++;

	      transfered += size_transfer;
	      entry_bytes_offset += size_transfer;

	      if(entry_bytes_offset==last_entry->size)
		{
		  entry_bytes_offset = 0;

		  if(unlikely(last_entry==&drct_page_walk->entries[MAX_DRCT_ENTRIES-1]))
		    {
		      drct_page_walk = (struct drct_page *)drct_page_walk->page_chain;
		      BUG_ON( (drct_page_walk==NULL) && (transfered<512) );

		      last_entry = &drct_page_walk->entries[0];
		    }
		  else
		    {
		      last_entry++;
		    }
		}
	    }

	  bio->bi_size += transfered;
	  page_size_remain -= transfered;
	}
    }

  kunmap(page);

 FIN:

  if( likely(rs_ops->set_bio_private!=NULL) )
    {
      /* P2FSでは、実際にこのbioで転送するファイル情報と
	 データの量を記録しておく
	 また、pageがbioの最初ならpage->indexの値も記録する。
	 基本的には、その値を使ってクラスタの更新を行う。
	 そうしないと、reservoir内にあるデータの上書きに対処できない。 */
      rs_ops->set_bio_private(first_bio, page, rw, drct);
    }

  /* Direct転送の場合、bioに追加したらpageは使い捨て */

  /* 補助ページがある場合は解放 */
  drct_page_walk = (struct drct_page *)drct_page->page_chain;
  while( unlikely(drct_page_walk!=NULL) )
    {
      struct drct_page *tmp = drct_page_walk;
      drct_page_walk = (struct drct_page *)drct_page_walk->page_chain;
      kfree((void *)tmp);
    }

  /* 親ページもクリアしておく */
  ClearPageUptodate(page);
  if(page_has_buffers(page))
    {
      try_to_free_buffers(page);
    }

  return total_size;
}

struct bio * __reservoir_writepage(struct bio *bio, struct page *page, 
				  get_block_t get_block,
				  sector_t *last_block_in_bio, int *ret,
				  struct writeback_control *wbc)
{
  struct inode *inode = page->mapping->host;
  const unsigned blkbits = inode->i_blkbits;
  const unsigned blocks_per_page = PAGE_CACHE_SIZE >> blkbits;
  sector_t block_in_file = 0;
  struct buffer_head mapped_bh;
  int ret_val = 0;

  /* 通常系の場合は、標準の関数を呼び出す */
  if(!test_bit(RS_RT, &inode->i_rsrvr_flags))
    {
      struct mpage_data mpd = {
	.bio = bio,
	.last_block_in_bio = *last_block_in_bio,
	.get_block = get_block,
	.use_writepage = 1,
      };

      /* MI(管理情報)フラグがOnなら、MIフラグをOnにする(bio!=NULLの場合) */
      /*  bio==NULLの場合は、__mpage_writepage()関数内でフラグをOnにする。 */
      if(test_bit(RS_MI, &inode->i_rsrvr_flags))
	{
	  if(bio)
	    {
	      set_bit(BIO_RW_MI, &bio->bi_rw);
	    }
	}

      *ret =  __mpage_writepage(page, wbc, &mpd);

      if(!(*ret))
	{
	  *last_block_in_bio = mpd.last_block_in_bio;
	}

      return mpd.bio;
    }
 
  /* RT系の場合は常にpage単位で扱っているので、
     bufferの部分的な扱いは不要。
     pageのuptodate確認だけでいい */
 
  BUG_ON(!PageUptodate(page));
  block_in_file = (sector_t)page->index << (PAGE_CACHE_SHIFT - blkbits);
  
  mapped_bh.b_page = page;
  mapped_bh.b_state = 0;
  mapped_bh.b_size = 1 << blkbits;

  ret_val = get_block(inode, block_in_file, &mapped_bh, 0);
  if(unlikely(ret_val))
    {
      if(bio!=NULL)
	{
	  submit_rt_bio(WRITE, inode->i_sb, bio);
	  bio = NULL;
	}

      return NULL;
    }

  /* RT系ではmappedなものしか
     ここにはこないはずです。
     基本的には、戻り書きでは
     その前にsyncを強要しているので */
  BUG_ON(!buffer_mapped(&mapped_bh));

  /* ここに引っかかる可能性が思い浮かばないが、
     とりあえず連続性をチェックしておく */
  if( unlikely(bio!=NULL && (mapped_bh.b_blocknr != ((*last_block_in_bio)+1)) ) )
    {
      struct reservoir_operations *rs_ops = RS_SB(inode->i_sb)->rs_ops;

      if( likely(rs_ops->set_bio_callback!=NULL) )
	{
	  struct bio_reservoir *reservoir = inode->i_reservoir;

	  /* bioが転送終了したときのコールバック関数を記録しておく */
	  rs_ops->set_bio_callback(bio, inode->i_sb, WRITE, atomic_read(&reservoir->rt_count));
	}
 
      BUG_ON(bio->bi_end_io==NULL);
      submit_rt_bio(WRITE, inode->i_sb, bio);
      bio = NULL;
    }

  /* bioが確保されていなかったら取得する */
  if (bio == NULL) {
    bio = reservoir_bio_alloc(inode->i_sb, inode, mapped_bh.b_blocknr, WRITE);
    if (bio == NULL)
      {
	printk("%s-%d: Getting BIO Failed.\n", __PRETTY_FUNCTION__, __LINE__);
	return NULL;
      }
  }
  
  ret_val = reservoir_bio_add_page(bio, page, inode, WRITE);
  if( unlikely(ret_val!=PAGE_CACHE_SIZE) )
    {
      struct reservoir_operations *rs_ops = RS_SB(inode->i_sb)->rs_ops;

      if( likely(rs_ops->set_bio_callback!=NULL) )
	{
	  struct bio_reservoir *reservoir = inode->i_reservoir;

	  /* bioが転送終了したときのコールバック関数を記録しておく */
	  rs_ops->set_bio_callback(bio, inode->i_sb, WRITE, atomic_read(&reservoir->rt_count));
	}
 
      printk("%s-%d: Unexpected Error Occured.\n",
	     __PRETTY_FUNCTION__, __LINE__);
      BUG_ON(bio->bi_end_io==NULL);
      submit_rt_bio(WRITE, inode->i_sb, bio);
      bio = NULL;

      return NULL;
    }

  if(!test_bit(RS_PCIDRCT, &inode->i_rsrvr_flags))
    {
      BUG_ON(PageWriteback(page));
      set_page_writeback(page);
    }

  unlock_page(page);

  *last_block_in_bio = mapped_bh.b_blocknr + blocks_per_page - 1;

  return bio;
}

static int reservoir_submit_pages(struct inode *inode, unsigned long start,
				  unsigned long length, unsigned long *next,
				  struct writeback_control *wbc)
{
  /* nextは、処理した次のブロック先頭indexを返す。
   その値はクラスタアラインメントにあわせておく。 */

  struct address_space *mapping = inode->i_mapping;
  struct super_block *sb = inode->i_sb;
  struct pagevec pvec;
  pgoff_t end = start + length - 1;
  int done = 0;
  int nr_pages = 0;
  unsigned long rs_block_size = RS_SB(sb)->rs_block_size;
  struct bio *bio = NULL;
  int ret = 0;
  struct reservoir_operations *rs_ops = RS_SB(sb)->rs_ops;
  sector_t last_block_in_bio = 0;
  struct backing_dev_info *bdi = mapping->backing_dev_info;

  if(!test_bit(RS_RT, &inode->i_rsrvr_flags))
    {
	    lock_rton(MAJOR(sb->s_dev));

	    if(!check_rt_status(sb)&&current_is_pdflush())
	    {
		    unlock_rton(MAJOR(sb->s_dev));
		    return 0;
	    }

	    unlock_rton(MAJOR(sb->s_dev));

      if(wbc->nonblocking && bdi_write_congested(bdi))
	{
	  wbc->encountered_congestion = 1;
	  return 0;
	}

      if (wbc->range_cyclic)
	{
	  start = mapping->writeback_index; /* Start from prev offset */
	  if(wbc->nr_to_write)
	    {
	      end = start + wbc->nr_to_write - 1;
	    }
	  else
	    {
	      end = ~(0LL);
	    }
	}
      else
	{
	  start = wbc->range_start >> PAGE_CACHE_SHIFT;
	  end = wbc->range_end >> PAGE_CACHE_SHIFT;
	}

    }

  *next = 0;

  pagevec_init(&pvec, 0);

  while(!done && (start<=end))
    {
      unsigned i;
      unsigned max_pages = rs_block_size - (start % rs_block_size);
      unsigned long prev_index = 0;
      unsigned long prev_start = start;
 
      nr_pages = pagevec_lookup_tag(&pvec, mapping, &start,
				    PAGECACHE_TAG_DIRTY,
				    max_pages);
      /* 釣れたpageがなかった場合 */
      if(!nr_pages)
	{
	  break;
	}

      for(i=0; i<nr_pages; i++)
	{
	  struct page *page = pvec.pages[i];

	  lock_page(page);

	  if(!page_has_buffers(page)
	     || page->index > end)
	    {
	      /* bufferの割り当てられていないpageを
		 みつけた段階で処理をやめる */
	      done = 1;
	      start = page->index;
	      unlock_page(page);
	      break;
	    }

	  /* 連続性をチェックする */
	  if(i!=0)
	    {
	      if( (prev_index+1) != page->index 
		  || !(page->index % rs_block_size) ) /* 連続していても、境界をまたいだらアウト */
		{
		  start = page->index;
		  unlock_page(page);
		  break;
		}
	    }

	  prev_index = page->index;

	  if(!test_bit(RS_RT, &inode->i_rsrvr_flags))
	    {
	      if (unlikely(page->mapping != mapping))
		{
		  start = page->index;
		  unlock_page(page);
		  break;
		}

	      if (!wbc->range_cyclic && page->index > end)
		{
		  done = 1;
		  unlock_page(page);
		  break;
	      }

	      if (wbc->sync_mode != WB_SYNC_NONE)
		{
		  wait_on_page_writeback(page);
		}
	    }

	  /* すでにBIO化されてi/o schedulerに渡っていたら、
	     こいつはあきらめよう */
	  if(PageWriteback(page) || !reservoir_clear_page_dirty(page))
	    {
	      start = page->index + 1;
	      unlock_page(page);
	      break;
	    }

	  bio = __reservoir_writepage(bio, page, rs_ops->get_block,
				     &last_block_in_bio, &ret, wbc);

	  if(!test_bit(RS_RT, &inode->i_rsrvr_flags))
	    {
	      /* 通常系関連のエラー処理 */
	      if (unlikely(ret == AOP_WRITEPAGE_ACTIVATE))
		unlock_page(page);
	      if (ret || (--(wbc->nr_to_write) <= 0))
		{
		  done = 1;
		}
	      if (wbc->nonblocking && bdi_write_congested(bdi))
		{
		  wbc->encountered_congestion = 1;
		  done = 1;
		}
	    }
	  else
	    {
	      /* RT系のエラー処理 */
	      if(bio==NULL)
		{
		  *next = page->index + 1;
		  unlock_page(page);
		  break;
		}
	    }
	}

      if(bio)
	{
	  if(!test_bit(RS_RT, &inode->i_rsrvr_flags))
	    {
	      mpage_bio_submit(WRITE, bio);
	    }
	  else
	    {
	      if( likely(rs_ops->set_bio_callback!=NULL) )
		{
		  struct bio_reservoir *reservoir = inode->i_reservoir;

		  /* bioが転送終了したときのコールバック関数を記録しておく */
		  rs_ops->set_bio_callback(bio, sb, WRITE, atomic_read(&reservoir->rt_count));
		}
	      BUG_ON(bio->bi_end_io==NULL);

	      submit_rt_bio(WRITE, sb, bio);
	    }
	}

      pagevec_release(&pvec);

      invalidate_mapping_pages(mapping, prev_start, start-1);

      bio = NULL;
      last_block_in_bio = 0;
      *next = start;

      cond_resched();
    }

  if(bio)
    {
      if(!test_bit(RS_RT, &inode->i_rsrvr_flags))
	{
	  mpage_bio_submit(WRITE, bio);
	}
      else
	{
	  if( likely(rs_ops->set_bio_callback!=NULL) )
	    {
	      struct bio_reservoir *reservoir = inode->i_reservoir;

	      /* bioが転送終了したときのコールバック関数を記録しておく */
	      rs_ops->set_bio_callback(bio, sb, WRITE, atomic_read(&reservoir->rt_count));
	    }
	  BUG_ON(bio->bi_end_io==NULL);

	  submit_rt_bio(WRITE, sb, bio);
	}
    }

  /* nextをクラスタアラインメントにあわせておく */
  *next = ( (*next) / rs_block_size) * rs_block_size;

  if(!test_bit(RS_RT, &inode->i_rsrvr_flags))
    {
      mapping->writeback_index = *next;
    }

 return 0;
}

int reservoir_submit_unasigned_pages(struct inode *inode,
				     unsigned long start, unsigned long length)
{
  struct super_block *sb = inode->i_sb;
  struct bio_reservoir *reservoir = inode->i_reservoir;
  struct address_space *mapping = inode->i_mapping;
  int submit_done = 0;
  struct pagevec pvec;
  int nr_pages = 0;
  unsigned long rs_block_size = RS_SB(sb)->rs_block_size;
  int drct = test_bit(RS_PCIDRCT, &inode->i_rsrvr_flags);
  int done = 0;
  int ret = 0;

  /* あくまでクラスタ単位でしか書かないので、
     それ以下の依頼を受けても困ります */
  if( unlikely(length % rs_block_size) )
    {
      printk("%s-%d: Invalid Call.\n", __PRETTY_FUNCTION__, __LINE__);
      return 0;
    }

  /* startをクラスタアラインメントにあわせる */
  start = (( start + rs_block_size - 1 ) / rs_block_size) * rs_block_size;

  /* pagevecの初期化 */
  pagevec_init(&pvec, 0);

  while(length >= rs_block_size && !done)
    {
      struct bio *bio = NULL;
      unsigned long prev_start = start;
      int i = 0;

      nr_pages = pagevec_lookup_tag(&pvec, mapping, &start,
				    PAGECACHE_TAG_DIRTY, rs_block_size);

      /* クラスタ全体が釣れなかったときと、
       釣れたページが連続でなかった場合は処理をしない*/
      if(nr_pages!=rs_block_size
	 || pvec.pages[nr_pages-1]->index != (prev_start + rs_block_size -1) )
	{
	  done = 1;
	  goto PAGE_RELEASE;
	}

      /* 既存ブロックの書き込みはこの関数の仕事ではないので飛ばす。
	 逐次 reservoir_submit_pages() で処理されているはず。
	 そもそもここで釣れることは想定していないので
	 BUG_ON 扱いにしてもいいかもしれない */
      if(page_has_buffers(pvec.pages[0]))
	{
	  length -= rs_block_size;
	  pagevec_release(&pvec);
	  continue;
	}

      for(i=0; i<nr_pages; i++)
	{
	  struct page *page = pvec.pages[i];

	  lock_page(page);

	  BUG_ON(page->mapping != mapping);
	  BUG_ON(PageWriteback(page));
	  BUG_ON(!reservoir_clear_page_dirty(page));

	  /* BIOの確保 */
	  if(bio==NULL)
	    {
	      bio = reservoir_bio_alloc(sb, inode, 0, WRITE);
	      if( unlikely(bio==NULL) )
		{
		  printk("%s-%d: Getting BIO Failed.\n",
			 __PRETTY_FUNCTION__, __LINE__);
		  unlock_page(page);
		  submit_done = -ENOMEM;
		  done = 1;
		  break;
		}
	    }

	  ret = reservoir_bio_add_page(bio, page, inode, WRITE);
	  if( unlikely(ret!=PAGE_CACHE_SIZE) )
	    {
	      printk("%s-%d: Unexpected Error Occured.\n",
		     __PRETTY_FUNCTION__, __LINE__);
	      submit_done = -EIO;
	      unlock_page(page);
	      done = 1;
	      break;
	    }

	  if(!drct)
	    {
	      set_page_writeback(page);
	    }

	  unlock_page(page);
	}
      
      if(bio)
	{
	  int ret = 0;

	  ret = reservoir_submit_bio(sb, bio, reservoir, RS_SB(sb)->rs_ops);

	  if( unlikely(ret) )
	    {
	      if(submit_done==0)
		submit_done = ret;

	      done = 1;
	      goto PAGE_RELEASE;
	    }
	  else
	    {
	      if(submit_done==0)
		submit_done = 1;
	    }
	}

      length -= rs_block_size;


      /* 転送処理の終わったpageはすぐに消してしまう */
      invalidate_mapping_pages(mapping, pvec.pages[0]->index,
			       pvec.pages[0]->index + rs_block_size - 1);

    PAGE_RELEASE:

      pagevec_release(&pvec);

      cond_resched();
    }

  return submit_done;
}

int reservoir_clear_inodes(struct super_block *sb)
{
  struct inode *inode=NULL, *next=NULL;
  struct list_head *dirty_inodes = &RS_SB(sb)->rt_dirty_inodes;

  lock_rton(MAJOR(sb->s_dev));

  mutex_lock(&RS_SB(sb)->rt_dirty_inodes_lock);

  list_for_each_entry_safe(inode, next, dirty_inodes, i_reservoir_list)
  {
	  list_del_init(&inode->i_reservoir_list);
	  clear_bit(RS_SUSPENDED, &inode->i_rsrvr_flags);
     
	  /* sync付きでinodeを書き出す */
	  /* TODO: sb->s_ops->write_inode() で
	     充分かどうかは要検討 */
	  if(inode->i_sb->s_op->write_inode && !is_bad_inode(inode))
	  {
		  if(check_rt_status(sb)){
			  inode->i_sb->s_op->write_inode(inode, 1);
		  }else{
			  inode->i_sb->s_op->write_inode(inode, 0);
		  }
	  }

	  iput(inode);
  }

  mutex_unlock(&RS_SB(sb)->rt_dirty_inodes_lock);

  unlock_rton(MAJOR(sb->s_dev));

  return 0;
}

static int reservoir_sync_and_clear_inodes(struct super_block *sb)
{
  struct list_head *dirty_inodes = &RS_SB(sb)->rt_dirty_inodes;
  int empty = 0;

  mutex_lock(&RS_SB(sb)->rt_dirty_inodes_lock);
  empty = list_empty(dirty_inodes);
  mutex_unlock(&RS_SB(sb)->rt_dirty_inodes_lock);

  /* デバイス全体の管理情報を書き出す */
  if(!empty)
    {
	  lock_rton(MAJOR(sb->s_dev));
      if (check_rt_status(sb))
	{
	  if(sb->s_op->write_super)
	    sb->s_op->write_super(sb);
	}
	  unlock_rton(MAJOR(sb->s_dev));
    }

  return reservoir_clear_inodes(sb);
}

static int reservoir_remove_inode_member(struct inode *inode, int rt)
{
  struct bio_reservoir *reservoir = inode->i_reservoir;
  struct super_block *sb = inode->i_sb;
  int ret = 0;

  /* どこにも所属していないinodeに呼ばれるのはおかしい */
  BUG_ON(reservoir==NULL);

  list_del_init(&inode->i_reservoir_list);
  reservoir->active_inodes--;
  inode->i_reservoir = NULL;

  /* RT記録中はinodeの書き出しを一時保留するため、
     いったん、reservoirの独自リストにうつす */
  atomic_inc(&inode->i_count);
  set_bit(RS_SUSPENDED, &inode->i_rsrvr_flags);

  mutex_lock(&RS_SB(sb)->rt_dirty_inodes_lock);
  list_add_tail(&inode->i_reservoir_list,
		&RS_SB(sb)->rt_dirty_inodes);
  mutex_unlock(&RS_SB(sb)->rt_dirty_inodes_lock);

  if(rt)
    {
      if( atomic_read(&reservoir->rt_count) == 1 )
	{
	  /* 所属するRTファイルが誰もいなくなった場合の処理をおこなう */
	  /* ダミーメンバを埋めてsubmitする。ダミーにあたる場所をFAT上で解放する */

	  //printk("%s: Last Operation (%d)...\n", __FUNCTION__, reservoir->file_group_idx);

	  if( (reservoir->cur_length!=0)
	      ||(reservoir->suspended_cls[reservoir->cls_ptr]!=0) )
	    {
	      int i = 0;
	      int bio_wanted = reservoir->max_length - reservoir->cur_length;

	      for(i=0; i<bio_wanted; i++)
		{
		  struct bio *bio = NULL;

		  bio = reservoir_dummy_bio_alloc(sb);
		  if( unlikely(bio==NULL) )
		    {
		      printk("%s-%d: Getting BIO Failed.\n",
		           __PRETTY_FUNCTION__, __LINE__);
		      ret = -ENOMEM;
		      break;
		    }
		  //printk(".");

		  reservoir_submit_bio(sb, bio, reservoir, RS_SB(sb)->rs_ops);
		}

	      //printk("  done (max=%d)\n", (int)reservoir->max_length);
	    }

	  /* キューの深さを標準に戻す */
	  reservoir->max_length = RS_SB(sb)->rs_ops->get_max_bios(sb);
	}

      atomic_dec(&reservoir->rt_count);

      if( atomic_dec_return(&(RS_SB(sb)->file_groups[reservoir->file_group_idx].rt_files)) == 0 )
	{
	  //printk("[%s] Last File closed (%d)...", __FUNCTION__, reservoir->file_group_idx);

	  //int i = 0;

	  // printk("%s: Last Operation - 2\n", __FUNCTION__);
	  
	  /* 余分な確保ぶんのdummy bio埋め */

	  if( RS_SB(sb)->rs_ops->prepare_end_rt_writing!=NULL )
	    {
	      /* 最後のファイルなら、prepare_end_rt_writeメソッドを呼ぶ */
	      RS_SB(sb)->rs_ops->prepare_end_rt_writing(sb, reservoir->file_group_idx);	      
	    }

	  /* シーケンシャルwriteフラグを下げる */
	  clear_bit(RT_SEQ, &RS_SB(sb)->rt_flags);

	  /* reservoirの深さをもとにもどす
	   将来的に、通常系もreservoir経由にするなら
	   いるかもしれないが、現状では不必要なので
	   コメントアウトしておく */
	  //for(i=0; i<MAX_RESERVOIRS; i++)
	  //  {
	  //    RS_SB(sb)->reservoir[i].max_length = 1;
	  //  }

	  /* i/o scheduler に強制書きだし指令 */
	  lock_rton(MAJOR(sb->s_dev));
	  if (check_rt_status(sb))
	  {
		  if(inode->i_sb->s_op->write_inode)
			  inode->i_sb->s_op->write_inode(inode, 1);
	  }
	  unlock_rton(MAJOR(sb->s_dev));
	  
	  //printk("  ...done\n");
	}

      atomic_dec(&(RS_SB(sb)->rt_total_files));
    }

  /* RT記録中でないことを確認してinodeの内容を書き出す */
  if(atomic_read(&(RS_SB(sb)->file_groups[reservoir->file_group_idx].rt_files))==0)
    {
      if(rt)
	{
	  if( RS_SB(sb)->rs_ops->end_rt_writing!=NULL )
	    {
	      /* 最後のファイルなら、end_rt_writeメソッドを呼ぶ */
	      RS_SB(sb)->rs_ops->end_rt_writing(sb);
	    }
	}
      
      /* これまで関わってきた全inodeを書き出す */
      reservoir_sync_and_clear_inodes(sb);
    }
      
  return ret;
}

static int reservoir_add_inode_member(struct bio_reservoir *reservoir,
				      struct inode *inode, int rt)
{
  struct super_block *sb = reservoir->sb;

  /* すでにsuspend化されている場合は、そこから抜く */
  if(test_and_clear_bit(RS_SUSPENDED, &inode->i_rsrvr_flags))
    {
      mutex_lock(&RS_SB(sb)->rt_dirty_inodes_lock);
      list_del_init(&inode->i_reservoir_list);
      mutex_unlock(&RS_SB(sb)->rt_dirty_inodes_lock);

      atomic_dec(&inode->i_count);
    }

  /* すでにどこかに所属しているなら、そこから抜く */
  if(inode->i_reservoir!=NULL)
    {
      reservoir_remove_inode_member(inode, rt);
    }

  /* inodeを登録する */  
  list_add_tail(&inode->i_reservoir_list, &reservoir->inode_list);
  inode->i_reservoir = reservoir;

  reservoir->active_inodes++;
  if(rt)
    {
      atomic_inc(&reservoir->rt_count);

      //printk("[%s] (%d) -> %d\n", __FUNCTION__, reservoir->file_group_idx,
      //     atomic_read(&(RS_SB(sb)->file_groups[reservoir->file_group_idx].rt_files)));

      if(atomic_inc_return(&(RS_SB(sb)->file_groups[reservoir->file_group_idx].rt_files)) == 1)
	{
	  int i = 0;

	  //printk("[%s] Setting (%d)...\n", __FUNCTION__, reservoir->file_group_idx);

	  /* reservoirの深さを設定する。
	   ほとんどの場合しなくてもいいと思うが、一応この段階でも設定。 */
	  for(i=0; i<MAX_RESERVOIRS; i++)
	    {
	      RS_SB(sb)->file_groups[reservoir->file_group_idx]
		.reservoir[i].max_length
		= RS_SB(sb)->rs_ops->get_max_bios(sb);
	    }

	}

      /* デバイス内で最初のRT登録かを確認 */
      if( atomic_inc_return(&(RS_SB(sb)->rt_total_files)) == 1 )
	{
	  if( likely( (RS_SB(sb)->rs_ops->begin_rt_writing!=NULL) ) )
	    {
	      /* 最初の登録なら、begin_rt_writeメソッドを呼ぶ */
	      RS_SB(sb)->rs_ops->begin_rt_writing(sb);
	    }
	}
    }

  return 0;
}

static int reservoir_fill_dummy_entry_for_page(struct page *page,
						struct super_block *sb)
{
  char *kaddr = NULL;
  struct drct_page *drct = NULL;
  unsigned long dummy_addr = 0;
  unsigned long total_size = 0;

  /* ダミー用のアドレスを用意する */
  if( (RS_SB(sb)->rs_ops!=NULL)
      && (RS_SB(sb)->rs_ops->get_addr_for_dummy_write!=NULL) )
    {
      dummy_addr = RS_SB(sb)->rs_ops->get_addr_for_dummy_write(sb);
    }

  /* 最後にエントリを埋める必要があるか調べ、
     必要であれば書き込む*/
  kaddr = kmap(page);
  drct = (struct drct_page *)kaddr;

  total_size = drct->total_size;

  while( unlikely(drct->page_chain!=NULL) )
    {
      drct = (struct drct_page *)drct->page_chain;
      total_size += drct->total_size;
    }

  if(total_size != PAGE_CACHE_SIZE)
    {
      __u16 fill_size = PAGE_CACHE_SIZE - total_size;

      /* 1ページに収まらない場合は補助ページを割り当てる */
      if( unlikely(drct->nr_entries==MAX_DRCT_ENTRIES) )
	{
	  drct->page_chain = kmalloc(PAGE_CACHE_SIZE, GFP_KERNEL);
	  if( unlikely(drct==NULL) )
	    {
	      printk("%s-%d: Getting Sub Page in Direct Page Failed.\n",
		     __PRETTY_FUNCTION__, __LINE__);
	      kunmap(page);
	      return -ENOMEM;
	    }

	  drct = (struct drct_page *)(drct->page_chain);
	      
	  drct->flags = 0;
	  drct->nr_entries = 0;
	  drct->total_size = 0;
	  drct->dummy_size = 0;
	  drct->page_chain = NULL;
	}

      drct->entries[drct->nr_entries].pci_addr = dummy_addr;
      drct->entries[drct->nr_entries].size = fill_size;
      drct->nr_entries++;

      drct->total_size += fill_size;
      drct->dummy_size += fill_size;
    }

  kunmap(page);
  flush_dcache_page(page);

  return 0;
}

static int __reservoir_pad_and_commit_tail(struct inode *inode, int commit)
{
  struct super_block *sb = inode->i_sb;
  struct address_space *mapping = inode->i_mapping;
  unsigned long rs_block_size = RS_SB(sb)->rs_block_size;
  unsigned long last_index = ( (inode->i_size - 1) >> PAGE_CACHE_SHIFT );
  unsigned long aligned_index = (last_index/rs_block_size) * rs_block_size;
  unsigned long aligned_index_copy = aligned_index;
  unsigned long page_wanted = last_index - aligned_index + 1;
  int nr_pages = 0;
  int drct = test_bit(RS_PCIDRCT, &inode->i_rsrvr_flags);
  int padding_done = 0;
  int i = 0;
  struct pagevec pvec;
  struct page *page = NULL;
  int ret = 0;
  int overwrite = 0;

  /* まだなにもしてないファイルには
     やることがないはずなので返る */
  if( unlikely(inode->i_size==0) )
    {
      return 0;
    }

  pagevec_init(&pvec, 0);

  nr_pages = pagevec_lookup_tag(&pvec, mapping, &aligned_index,
				PAGECACHE_TAG_DIRTY,
				page_wanted);

  if(nr_pages > 0 && page_has_buffers(pvec.pages[0]))
    {
      overwrite = 1;
    }

  if(nr_pages!=page_wanted && overwrite!=1)
    {
      /* 末尾に対する書き込みは
	 どうやらなかったらしいので
	 なにもせずに返る。*/
      goto FIN;
    }

  /* PCIDRCTの場合、最後のpageを埋める */
  if(drct)
    {
      ret = reservoir_fill_dummy_entry_for_page(pvec.pages[nr_pages-1], sb);
      if(ret)
	{
	  goto FIN;
	}
    }

  if(overwrite)
    {
      goto FIN;
    }

  /* ブロックを完成させるのに必要なページ数を計算する */
  /* ダミーのpageを作る。通常系とPTで内容を変える */
  for(i=(last_index+1) ; (i%rs_block_size); i++)
    {
      page = __grab_cache_page(mapping, i);
      if( unlikely(!page) )
	{
	  printk("%s-%d: Getting Page Failed.\n",
		 __PRETTY_FUNCTION__, __LINE__);
	  break;
	}

      /* syncが複数回呼ばれたときなど、
	 Uptodateなものがつれるときがある */
      if(PageUptodate(page))
	{
	  goto PG_UPTODATE;
	}

      /* drctの場合はダミー転送で内容を初期化 */
      if(drct)
	{
	  char *kaddr = NULL;
	  struct drct_page *drct = NULL;
	  unsigned long dummy_addr = 0;

	  /* ダミー用のアドレスを用意する */
	  if( (RS_SB(sb)->rs_ops!=NULL)
	  && (RS_SB(sb)->rs_ops->get_addr_for_dummy_write!=NULL) )
	    {
	      dummy_addr = RS_SB(sb)->rs_ops->get_addr_for_dummy_write(sb);
	    }

	  kaddr = kmap(page);
	  drct = (struct drct_page *)kaddr;

	  /* ダミー領域を加える */
	  drct->entries[0].pci_addr = dummy_addr;
	  drct->entries[0].size = PAGE_CACHE_SIZE;

	  drct->total_size = PAGE_CACHE_SIZE;
	  drct->dummy_size = PAGE_CACHE_SIZE;
	  drct->nr_entries = 1;
	  drct->page_chain = NULL;

	  kunmap(page);
	  flush_dcache_page(page);
	}

      SetPageUptodate(page);

    PG_UPTODATE:

      /* PageをDirtyにする */
      reservoir_set_page_dirty(page);

      /* pageの後処理 */
      unlock_page(page);
      mark_page_accessed(page);
      page_cache_release(page);

      cond_resched();
    }

  padding_done = 1;

 FIN:

  pagevec_release(&pvec);

  if(commit)
    {
      if(overwrite)
	{
	  unsigned long next = 0;
	  struct writeback_control wbc
	    ={.bdi=NULL, .sync_mode=WB_SYNC_NONE, .older_than_this=NULL,
	      .nr_to_write= 0, .nonblocking=1, .range_cyclic=1,};
      
	  ret = reservoir_submit_pages(inode, aligned_index_copy,
				       rs_block_size, &next, &wbc);
	  if(ret)
	    {
	      printk("%s-%d: File Tail Padding Done. But Error Occured ...\n",
		     __PRETTY_FUNCTION__, __LINE__);
	    }
	}
      else
	{
	  ret = reservoir_submit_unasigned_pages(inode, aligned_index_copy, rs_block_size);
	  if( unlikely(ret<=0 && padding_done) )
	    {
	      printk("%s-%d: File Tail Padding Done. But Nothing wrote ...\n",
		     __PRETTY_FUNCTION__, __LINE__);
	    }
	}
    }

  return ret;
}

static inline int reservoir_pad_and_commit_tail(struct inode *inode)
{
  return __reservoir_pad_and_commit_tail(inode, 1);
}

static inline int reservoir_pad_file_tail(struct inode *inode)
{
  // return __reservoir_pad_and_commit_tail(inode, 0);

  /* 通常ファイルも絶対に i/o schedulerにはクラスタ単位の
     bioが渡ることを保証するには、ファイルのcloseやsyncの
     タイミングでファイル末尾をクラスタ単位で埋める必要がある。
     しかし、現時点では「bioに所属する一連のpageがクラスタ境界を
     またいでいない」ということしか保証しないので、
     ファイルの末尾埋めはとりあえず無効化しておく。 */

  return 0;
}

static int reservoir_stretch_queue(struct inode *inode, struct file *filp, u32 stretch)
{
  struct super_block *sb = inode->i_sb;
  struct bio_reservoir *reservoir = inode->i_reservoir;
  int rt = (filp->f_flags & O_REALTIME) ? 1 : 0;
  int normal_depth = RS_SB(sb)->rs_ops->get_max_bios(sb);
  int ret = 0;

  if( unlikely(!rt) )
    {
      printk("%s-%d: You can't stretch queue with normal file.\n",
	     __PRETTY_FUNCTION__, __LINE__);
      return -EINVAL;
    }

  if( unlikely(normal_depth * stretch > RS_MAX_BIOS) )
    {
      printk("%s-%d: Too Large Stretch Param (%u)\n", __PRETTY_FUNCTION__, __LINE__, stretch);    
      return -EINVAL;
    }

  mutex_lock(&inode->i_mutex);

  /* READONLYでないか調査する */
  if( (filp->f_flags & O_ACCMODE)==O_RDONLY )
    {
      printk("%s-%d : Invalid Call.\n", __FUNCTION__, __LINE__);
      /* READONLYならなにもしない */
      ret = -EINVAL;
      goto UNLOCK_FIN;
    }

  mutex_lock(&RS_SB(sb)->rt_files_lock);

  reservoir->max_length = normal_depth * stretch;

  mutex_unlock(&RS_SB(sb)->rt_files_lock);
  
 UNLOCK_FIN:

  mutex_unlock(&inode->i_mutex);

  return ret;
}

static int reservoir_set_group(struct inode *inode, struct file *filp,
			       struct reservoir_file_ids *ids)
{
  struct super_block *sb = inode->i_sb;
  int rt = (filp->f_flags & O_REALTIME) ? 1 : 0;
  int ret = 0, i = 0;
  int file_group = ids->file_group;
  int reservoir_id = ids->reservoir_id;
  int group_idx = -1, first_empty = -1, prev_group = 0;


  if( unlikely(reservoir_id >= MAX_RESERVOIRS) )
    {
      printk("%s-%d: Too Large Group ID (%u)\n", __PRETTY_FUNCTION__, __LINE__, reservoir_id);
      return -EINVAL;
    }

  mutex_lock(&inode->i_mutex);

  /* READONLYでないか調査する */
  if( (filp->f_flags & O_ACCMODE)==O_RDONLY )
    {
      printk("%s-%d : Invalid Call.\n", __FUNCTION__, __LINE__);
      /* READONLYならなにもしない */
      ret = -EINVAL;
      goto UNLOCK_FIN;
    }

  mutex_lock(&RS_SB(sb)->rt_files_lock);

  /* Search file_group */
  if( likely(group_idx!=DEFAULT_GROUP))
    {
      for(i=0; i<MAX_GROUPS; i++)
	{
	  if(i==DEFAULT_GROUP)
	    {
	      continue;
	    }
	  
	  if(RS_SB(sb)->file_groups[i].file_group==file_group)
	    {
	      //printk("[%s] Found (%d)...\n", __FUNCTION__, file_group);
	      group_idx = i;
	      break;
	    }

	  if(atomic_read(&RS_SB(sb)->file_groups[i].rt_files)==0 && first_empty==-1)
	    {
	      first_empty = i;
	    }

	}
    }
  else
    {
      group_idx = DEFAULT_GROUP;
    }


  if(group_idx==-1)
    {
      if( unlikely(first_empty==-1) )
	{
	  printk("%s-%d Too many Clips on the device.\n", __FUNCTION__, __LINE__);
	  goto UNLOCK_RT_FIN; 
	}
      else
	{
	  int j;

	  RS_SB(sb)->file_groups[first_empty].file_group = file_group;

	  for(j=0;j<MAX_RESERVOIRS;j++)
	    {
	      RS_SB(sb)->file_groups[first_empty].reservoir[j].file_group_idx = first_empty;
	    }

	  group_idx = first_empty;

	  //printk("[%s] New Clip (%d idx:%d)...\n", __FUNCTION__, file_group, first_empty);
	}
     
    }

  /* 既に所属しているのとおなじresrvoirに
     登録しようとしている場合は処理を飛ばす */
  if(&RS_SB(sb)->file_groups[group_idx].reservoir[reservoir_id]==inode->i_reservoir)
    {
      goto UNLOCK_RT_FIN;
    }

  /* 所属するreservoirをセットしなおす */
  /* remove_inode_menber内でREC終了と勘違いしてinodeのflushをしてしまわないように
     　擬似的にカウントをあげる */
  prev_group = inode->i_reservoir->file_group_idx;
  atomic_inc(&(RS_SB(sb)->file_groups[prev_group].rt_files));

  reservoir_remove_inode_member(inode, rt);
  reservoir_add_inode_member(&RS_SB(sb)->file_groups[group_idx].reservoir[reservoir_id],
			     inode, rt);

  /* 擬似的にあげていたカウントをおろす */
  atomic_dec(&(RS_SB(sb)->file_groups[prev_group].rt_files));

 UNLOCK_RT_FIN:

  mutex_unlock(&RS_SB(sb)->rt_files_lock);
  
 UNLOCK_FIN:

  mutex_unlock(&inode->i_mutex);

  return ret;
}

static int reservoir_set_fileid(struct inode *inode, struct file *filp, struct reservoir_file_id *id)
{
  int ret = 0;

  mutex_lock(&inode->i_mutex);

  /* READONLYでないか調査する */
  if( (filp->f_flags & O_ACCMODE)==O_RDONLY )
    {
      printk("%s-%d : Invalid Call.\n", __FUNCTION__, __LINE__);
      /* READONLYならなにもしない */
      ret = -EINVAL;
      goto UNLOCK_FIN;
    }

  /* ファイルIDをinodeに記録する */
  inode->i_file_id = id->file_id;

  /* 通知用IDをinodeに記録する */
  inode->i_notify_id = id->notify_id;
  
 UNLOCK_FIN:

  mutex_unlock(&inode->i_mutex);

  return ret;
  
}

int reservoir_file_open(struct inode *inode, struct file *filp)
{
  struct super_block *sb = inode->i_sb;
  int rt = (filp->f_flags & O_REALTIME) ? 1 : 0;   /* rtかどうかをフラグを見て確認 */
  int ret = 0;
  unsigned char *iname = filp->f_dentry->d_iname;

  /* inodeのmutexを握る */
  mutex_lock(&inode->i_mutex);

  //printk( "%s: Open %s\n", __FUNCTION__, iname);
  
  /* PCIDRCTはRTとセットで使いましょう */
  if(filp->f_flags & O_PCIDRCT)
    {
      if(!rt)
	{
	  printk("%s-%d: Check O_REALTIME when you set O_PCIDRCT\n",
		 __PRETTY_FUNCTION__, __LINE__);
	  ret = -EINVAL;
	  goto UNLOCK_FIN;
	}
    }

  /* READONLYでないか調査する */
  if( (filp->f_flags & O_ACCMODE)==O_RDONLY )
    {
      /* READONLYならなにもしない */
      goto DRCT_CHECK_FIN;
    }

  /* 2重以上のopenをされている場合 */
  if(inode->i_rsrvr_count)
    {
      int cur_drct = 0;
      int drct = 0;

      cur_drct = test_bit(RS_PCIDRCT, &inode->i_rsrvr_flags) ? 1 : 0;
      drct = filp->f_flags & O_PCIDRCT ? 1 : 0;

      if(cur_drct!=drct)
	{
	  printk("%s-%d: You can't open a file with both O_PCIDRCT and NonDirect\n",
		 __PRETTY_FUNCTION__, __LINE__);
	  ret = -EINVAL;
	  goto UNLOCK_FIN;
	}
    }

  if(rt)
    {
      /* 初めてのopen処理だった場合 */
      if(inode->i_reservoir == NULL)
	{
	  /* ファイルをデフォルトのreservoirに所属させる */
	  mutex_lock(&RS_SB(sb)->rt_files_lock);
	  ret = reservoir_add_inode_member(&(RS_SB(sb)->file_groups[DEFAULT_GROUP]
					     .reservoir[DEFAULT_RESERVOIR]),
					   inode, rt);
	  mutex_unlock(&RS_SB(sb)->rt_files_lock);

	  if(ret)
	    {
	      goto UNLOCK_FIN;
	    }
	}
    }

 DRCT_CHECK_FIN:

  if(rt)
  {	  
	  /* RS_RT フラグをOn */
	  set_bit(RS_RT, &inode->i_rsrvr_flags);
	  inode->i_rsrvr_rt_count++;
  }

  if( filp->f_flags & O_PCIDRCT )
    {
      /* PCI_DIRECTかどうか確認して、inodeにマークをつける */
      if(inode->i_rsrvr_drct_count==0)
	{
	  /* 通常系の書き込みが残っていないかチェック */
	  /* 一時的に RS_RTフラグをOffにする */
	  clear_bit(RS_RT, &inode->i_rsrvr_flags);
		
	  generic_osync_inode(inode, inode->i_mapping, OSYNC_DATA);
	  invalidate_mapping_pages(inode->i_mapping, 0, ~0UL);

	  set_bit(RS_PCIDRCT, &inode->i_rsrvr_flags);

	  set_bit(RS_RT, &inode->i_rsrvr_flags);
	}

      inode->i_rsrvr_drct_count++;
    }

  /* inode->i_countは、suspendしてる
     可能性があるから信用できないのよ。 */
  inode->i_rsrvr_count++;

  /* MI(管理情報)であれば、RS_MIフラグをOn */
  if (!strcmp(iname, RSFS_MI_NAME_S) || !strcmp(iname, RSFS_MI_NAME_C))
    {
      set_bit(RS_MI, &inode->i_rsrvr_flags);
    }

 UNLOCK_FIN:

  mutex_unlock(&inode->i_mutex);

  return ret;
}

int reservoir_file_release(struct inode *inode, struct file *filp)
{
  struct super_block *sb = inode->i_sb;
  int rt = (filp->f_flags & O_REALTIME) ? 1 : 0;   /* rtかどうかをフラグを見て確認 */
  int ret = 0;

  //printk("%s: Closing %s\n", __FUNCTION__, filp->f_dentry->d_iname);

  mutex_lock(&inode->i_mutex);

  /* READONLYでないか調査する */
  if( (filp->f_flags & O_ACCMODE)==O_RDONLY )
    {
      /* READONLYならなにもしない */
      goto UNLOCK_FIN;
    }

  mutex_lock(&RS_SB(sb)->io_serialize);

  /* 反映していない戻り書きの反映 ＆
     クラスタに対し中途半端なぶんのページを埋める */
  if(rt)
    {
      unsigned long next = 0;
      struct writeback_control wbc
	={.bdi=NULL, .sync_mode=WB_SYNC_NONE, .older_than_this=NULL,
	  .nr_to_write= 0, .nonblocking=1, .range_cyclic=1,};
      unsigned long pages = inode->i_size >> PAGE_CACHE_SHIFT;

      ret = reservoir_submit_pages(inode, 0, pages, &next, &wbc);
      if(ret)
	{
	  printk("%s-%d: File Tail Padding Done. But Error Occured ...\n",
		 __PRETTY_FUNCTION__, __LINE__);
	}

      reservoir_pad_and_commit_tail(inode);
 
      if(inode->i_rsrvr_rt_count==1)
	{
	  /* 最後のcloseだった場合、所属しているreservoirから抜く */
	  mutex_lock(&RS_SB(inode->i_sb)->rt_files_lock);
	  reservoir_remove_inode_member(inode, rt);
	  mutex_unlock(&RS_SB(inode->i_sb)->rt_files_lock);
	}
    }
  else
    {
      /* 非RTファイルも必ずクラスタ単位で
	 書き出すようにする場合は必要だが、
	 現在はそうはしていないので、
	 ファイルの末尾埋めはやらないことにする
	 （関数の中身は無効化してある） */
      reservoir_pad_file_tail(inode);

      /*******************************************************
       以下の処理は当面しないことに決めました。
       reservoir_writepages()で、クラスタに達していないデータの
       書き出しを許可しても、P2機器では
       大きなパフォーマンスの差はでないと判断しました。
       ひとつのページの書き出しが複数回おこなわれることで
       Read-Modify-Writeが頻発する状態が
       発生する可能性も否定できませんが、
       実装の複雑さと天秤にかけて、単純なやりかたを取ることにします。
       なお、将来的にSDカードへのRU単位書きをReservoir Filesystem経由で
       おこなおうとする日が訪れた場合は、ここと、reservoir_writepages()の処理を
       復活させる必要があるでしょう。
      *******************************************************/
      /* ファイル末尾まで書き出す */
      /* TODO: RTが誰もいないときはi/o schedulerを叩きたいが、
	 osync_inodeの中で待ってしまう。これをなんとかせねば。
	 結局、filemap_fdatawait()でwaitしている直前に
	 i/o schedulerを叩くしかないだろう。 */

      //filemap_fdatawrite(inode->i_mapping);

      /*******************************************************/


	  /* 管理情報更新処理は、その順番などがローカルFSに依存するので、
	   * ここでは行なわず、ローカルFSに任せるように変更した。 */
    }

  mutex_unlock(&RS_SB(sb)->io_serialize);

 UNLOCK_FIN:

  if(rt)
  {
	  inode->i_rsrvr_rt_count--;
	  
	  if(inode->i_rsrvr_rt_count==0)
	  {
		  clear_bit(RS_RT, &inode->i_rsrvr_flags);
	  }
  }

  /* PCI_DIRECTのカウンタをさげ、ゼロになったらフラグを下げる */
  if( filp->f_flags & O_PCIDRCT )
    {
      inode->i_rsrvr_drct_count--;
      
      if(inode->i_rsrvr_drct_count==0)
	{
	  /* Direct系のpageが残っていたら全部消す */
	  clear_bit(RS_PCIDRCT, &inode->i_rsrvr_flags);
	  invalidate_mapping_pages(inode->i_mapping, 0, ~0UL);
	}
    }

  inode->i_rsrvr_count--;

  mutex_unlock(&inode->i_mutex);

  //printk("%s: Exit Close %s\n", __FUNCTION__, filp->f_dentry->d_iname);

  return ret;
}

int reservoir_file_ioctl(struct inode *inode, struct file *filp,
			 unsigned int cmd, unsigned long arg)
{
  int ret = 0;

  switch(cmd)
    {
    case RSFS_SET_GROUP:
      {
	struct reservoir_file_ids __user *user_ids = (struct reservoir_file_ids __user *)arg;
	struct reservoir_file_ids ids;

	if(copy_from_user(&ids, user_ids, sizeof(struct reservoir_file_ids)))
	  {
	    printk("%s-%d : Copy_from_User Error.\n", __FUNCTION__, __LINE__);
	    return -EFAULT;
	  }

	/* ファイルグループの設定を呼び出す */
	ret = reservoir_set_group(inode, filp, &ids);
	break;
      }
    case RSFS_STRETCH_QUEUE:
      {
	/* reservoirキューの深さを変える */
	ret = reservoir_stretch_queue(inode, filp, arg);
	break;
      }
    case RSFS_SET_FILEID:
      {
	struct reservoir_file_id __user *user_id = (struct reservoir_file_id __user *)arg;
	struct reservoir_file_id id;

	if(copy_from_user(&id, user_id, sizeof(struct reservoir_file_id)))
	  {
	    printk("%s-%d : Copy_from_User Error.\n", __FUNCTION__, __LINE__);
	    return -EFAULT;
	  }

	/* ファイルIDの設定を呼び出す */
	ret =  reservoir_set_fileid(inode, filp, &id);
	break;
      }
    default:
      {
	printk("%s-%d : Invalid Call.\n", __FUNCTION__, __LINE__);
	/* Inappropriate ioctl for device */
	ret = -ENOTTY;
      }
    }

  return ret;
}

int reservoir_rt_fsync(struct inode *inode)
{
  struct super_block *sb = inode->i_sb;
  struct inode *member_inode = NULL;
  int ret = 0;
  int i = 0;
  int group = 0;

  /* RTの場合は、全てのRTファイルを巻き添えにして動く */

  /* 横から書き込んでくるやつがいないようにブロックする */
  mutex_lock(&RS_SB(sb)->io_serialize);

  /* reservoirを走査して、
     最後のbioを供給するものの処理でsyncを実行させる */
  for(group=0; group<MAX_GROUPS; group++)
    {
      if( &RS_SB(sb)->file_groups[group].rt_files==0)
	{
	  continue;
	}

      for(i=0; i<MAX_RESERVOIRS; i++)
	{
	  struct bio_reservoir *reservoir = &RS_SB(sb)->file_groups[group].reservoir[i];
	  int ret_val = 0;
	  
	  /* 現在アクティブなRTファイル全てに関して
	     すでに配置が決まっている要素を書き出す */
	  list_for_each_entry(member_inode, &reservoir->inode_list, i_reservoir_list)
	    {
	      unsigned long next = 0;
	      struct writeback_control wbc
		={.bdi=NULL, .sync_mode=WB_SYNC_NONE, .older_than_this=NULL,
		  .nr_to_write= 0, .nonblocking=1, .range_cyclic=1,};
	      unsigned long pages = member_inode->i_size >> PAGE_CACHE_SHIFT;

	      ret = reservoir_submit_pages(member_inode, 0, pages, &next, &wbc);
	      if(ret)
		{
		  printk("%s-%d: File Tail Padding Done. But Error Occured ...\n",
			 __PRETTY_FUNCTION__, __LINE__);
		}

	      //reservoir_pad_and_commit_tail(member_inode);
	    }

	  reservoir_pad_and_commit_tail(inode);

	  /* Reservoirの内容を全て書き出す */
	  ret_val = reservoir_rt_flush_reservoir(reservoir, RS_SB(sb)->rs_ops);
	  if(ret_val)
	    {
	      ret = ret_val;
	    }
	}

      /* i/o schedulerへの強制syncとその後処理終了待ち */
      reservoir_sync_data(sb, group);
    }

  /* シーケンシャルwriteフラグを下げる */
  clear_bit(RT_SEQ, &RS_SB(sb)->rt_flags);

  mutex_unlock(&RS_SB(sb)->io_serialize);

  /* 管理情報の書き出し */
  if(sb->s_op->write_super)
    sb->s_op->write_super(sb);
  
  /* TODO: i/o schedulerに対して強制書き出しを指示する */
  return ret;
}

static ssize_t __reservoir_file_aio_write(struct kiocb *iocb,
					  const struct iovec *iov,
					  unsigned long nr_segs,
					  loff_t pos, loff_t *ppos,
					  size_t count)
{
  struct file *filp = iocb->ki_filp;
  struct address_space *mapping = filp->f_mapping;
  struct inode *inode = mapping->host;
  const struct address_space_operations *a_ops = mapping->a_ops;
  long status = 0;
  ssize_t written = 0;
  int drct = test_bit(RS_PCIDRCT, &inode->i_rsrvr_flags);
  int overwrite = 0;
  unsigned long rs_block_size = RS_SB(inode->i_sb)->rs_block_size;
  struct iov_iter iter;

  iov_iter_init(&iter, iov, nr_segs, count, 0);

  do
    {
      unsigned long index = pos >> PAGE_CACHE_SHIFT;
      unsigned long offset = (pos & (PAGE_CACHE_SIZE - 1));
      size_t bytes = PAGE_CACHE_SIZE - offset;
      size_t copied = 0;
      unsigned int flags = 0;
      struct page *page = NULL;
      void *fsdata = NULL;

      /* 書き込み末尾の場合を考えてbytesを丸める */
      bytes = min(bytes, iov_iter_count(&iter));

      status = a_ops->write_begin(filp, mapping, pos, bytes, flags, &page, &fsdata);
      if(unlikely(status))
	{
	  printk("%s-%d: Unexpected Error.\n", __FUNCTION__, __LINE__);
	  break;
	}

      /* 既存のブロックに対する書き込みが発生したかをチェック */
      if(page_has_buffers(page))
	{
	  overwrite = 1;
	}

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
      written += copied;

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
	  if(overwrite)
	    {
	      unsigned long next = 0; /* ここでは使わない。処理した末端が入る。 */
	      struct writeback_control wbc
		= {.bdi=NULL, .sync_mode=WB_SYNC_NONE, .older_than_this=NULL,
		   .nr_to_write=0, .nonblocking=1, .range_cyclic=1,};
	      
	      reservoir_submit_pages(inode, (index + 1 - rs_block_size),
				     rs_block_size, &next, &wbc);
	      overwrite = 0;
	    }
	  else
	    {
	      /* クラスタ先頭からページをさらってreservoirに入れてゆく */
	      status = reservoir_submit_unasigned_pages(inode,
							(index + 1 - rs_block_size),
							rs_block_size);
	    }
	}

      balance_dirty_pages_ratelimited(mapping);
      cond_resched();
    }
  while(iov_iter_count(&iter));

  /* ファイルポインタの更新 */
  *ppos = pos;

  /* overwriteが発生していたなら、書き出し処理を行う
   (asignedなものだけを吐き出す処理)*/
  /*
  if(overwrite)
    {
      unsigned long next = 0; // ここでは使わない。処理した末端が入る。
      struct writeback_control wbc
	= {.bdi=NULL, .sync_mode=WB_SYNC_NONE, .older_than_this=NULL,
	   .nr_to_write=0, .nonblocking=1, .range_cyclic=1,};
     
      reservoir_submit_pages(inode, first_index, treat_length, &next, &wbc);
    }
  */

  return written ? written : status;
}

static inline int has_dirty_page(struct inode *inode)
{
  struct pagevec pvec;
  struct address_space *mapping = inode->i_mapping;
  unsigned long start = 0;
  int nr_pages = 0;

  pagevec_init(&pvec, 0);

  nr_pages = pagevec_lookup_tag(&pvec, mapping, &start,
				PAGECACHE_TAG_DIRTY, 1);

  pagevec_release(&pvec);

  return nr_pages;
}

ssize_t reservoir_file_aio_write(struct kiocb *iocb,
				 const struct iovec *iov,
				 unsigned long nr_segs,
				 loff_t pos)
{
  struct file *filp = iocb->ki_filp;
  struct address_space *mapping = filp->f_mapping;
  struct inode *inode = mapping->host;
  struct super_block *sb = inode->i_sb;
  ssize_t written = 0;
  loff_t *ppos = &iocb->ki_pos;
  ssize_t err = 0;
  size_t count = 0;
  unsigned long seg = 0;
  int rt = test_bit(RS_RT, &inode->i_rsrvr_flags);
  int drct = test_bit(RS_PCIDRCT, &inode->i_rsrvr_flags);

  /* 非RTもreservoir経由にしてブロック書き込みの精度を
     高める方法も考えられるが、現時点では非RTはgeneric経由にする。
     非RTの書き込み高速化は、i/o schedulerと、
     reservoir_submit_pages() で i/o schedulerに
     クラスタ境界をまたがないbioを渡すことを
     保証して実現する。それ以上の大きさへのマージは、
     i/o scheduler層で期待する。 */
  if(!rt)
    {
      return generic_file_aio_write(iocb, iov, nr_segs, pos);
    }

  mutex_lock(&inode->i_mutex);

  for(seg = 0; seg < nr_segs; seg++)
    {
      const struct iovec *iv = &iov[seg]; /* Discard static status ! */

      if(unlikely( iv->iov_len <= 0  /* サイズが負の場合 */
		   || ( drct && (iv->iov_len & (4-1)) && (seg!=(nr_segs-1)) ) /* 最後でないのに4byte単位でない */
		   ) )
	{
	  printk("%s-%d: Invalid Transfer Entries. (%u)\n",
		 __PRETTY_FUNCTION__, __LINE__, iv->iov_len);
	  err = -EINVAL;
	  goto OUT;
	}

      /* segが指定する転送サイズを合計してゆく */
      count += iv->iov_len;
    }

  /* According to generic_file_aio_write ... */
  vfs_check_frozen(sb, SB_FREEZE_WRITE);
  current->backing_dev_info = mapping->backing_dev_info;


  /* ファイルサイズ上限のチェックなど、
     ファイルシステム固有のチェックをおこなう */
  if( likely( RS_SB(sb)->rs_ops->write_check!=NULL ) )
    {
      if( unlikely (RS_SB(sb)->rs_ops->write_check(iocb, iov, nr_segs, pos, ppos,  &count, &written)) )
	{
	  printk("%s-%d : Write Check Error.\n", __FUNCTION__, __LINE__);
	  goto OUT;
	}
    }

  /* 書き込むものがなくなった場合はすぐに抜ける。
     p2fatの場合、先頭クラスタの入れ換え時にこうなる */
  if( unlikely(count==0) )
    {
      goto OUT;
    }

  /* まだ書き終わっていない部分に関して
     ファイル途中への書き込みが来たときの全sync */
  if( unlikely(*ppos != inode->i_size) 
      && unlikely(has_dirty_page(inode)))
    {
      /**
      printk("[[[ %s ]]]]\n  Automatic Fsync %lld@%llu (Writing %u)\n",
	     __FUNCTION__, *ppos, inode->i_size, count);
      printk("%lu - %lu\n", cur_block, asigned_block - 1);
      **/

      reservoir_rt_fsync(inode);
    }

  /* According to generic_file_aio_write ... */
  err = file_remove_suid(filp);
  if(err)
    {
      printk("%s-%d : SUID Error.\n", __FUNCTION__, __LINE__);
      goto OUT;
    }

  /* 書き込んでいる途中にinodeを汚したくないので、
     いまのところここはコメントアウトしておく */
  //file_update_time(file);

  mutex_lock(&RS_SB(sb)->io_serialize);
  /* Write動作の本体を呼び出す */
  written = __reservoir_file_aio_write(iocb, iov, nr_segs,
				       pos, ppos, count);
  mutex_unlock(&RS_SB(sb)->io_serialize);

 OUT:

  /* According to generic_aio_write ... */
  current->backing_dev_info =NULL;

  mutex_unlock(&inode->i_mutex);

  return written ? written : err;
}

static int reservoir_prepare_write(struct file *filp, struct page *page,
				   unsigned from, unsigned to)
{
  struct inode *inode = page->mapping->host;
  struct super_block *sb = inode->i_sb;
  /* 既存の割り当て済みpage数を調べる */
  unsigned long asigned_index = inode->i_blocks >> (PAGE_CACHE_SHIFT - sb->s_blocksize_bits);
  unsigned long page_index = page->index;
  char *kaddr = NULL;
  struct drct_page *drct_page = NULL;
  struct reservoir_operations *rs_ops = RS_SB(sb)->rs_ops;

  unsigned long block_start, block_end;
  sector_t block;
  int err = 0;
  unsigned blocksize, bbits;
  struct buffer_head *bh, *head;

  /* これから扱う部分は割り当て済みかどうか */
  if(page_index >= asigned_index)
    {
      /* まだ割り当てられていない場合 */

      /* そのページを触るのが初めてなら、
       初期化してUptodateにしておく。*/
      if(!PageUptodate(page))
	{
	  if(test_bit(RS_PCIDRCT, &inode->i_rsrvr_flags))
	    {
	      /* ページの途中から書き込もうとしているのに、
		 pageが初見なのはおかしいですよね。
		 これは、syncしたのにアラインメントを守らず
		 書こうとしたときに発生する。*/
	      if( unlikely(from & ~PAGE_CACHE_MASK) )
		{
		  printk("%s-%d: Bad Write Position.\n", __FUNCTION__, __LINE__);
		  printk("  filp=%lld\n", filp->f_pos);
		  return -EINVAL;
		}

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
	}

      return 0;
    }

  /* ここから先は、既存ブロックの処理だった場合 */

  /* according to __block_prepare_write() ... */
  BUG_ON(!PageLocked(page));
  BUG_ON(from > PAGE_CACHE_SIZE);
  BUG_ON(to > PAGE_CACHE_SIZE);
  BUG_ON(from > to);

  /* 既存pageへの追記はサポートしない */
  // BUG_ON(page_has_buffers(page));

  if(test_bit(RS_PCIDRCT, &inode->i_rsrvr_flags))
    {
      /* Sync後にアラインメントを
	 守らず書き出したときの対策 */
      /*
      if( unlikely(from & ~PAGE_CACHE_MASK) )
	{
	  printk("%s-%d: Bad Write Position.\n", __FUNCTION__, __LINE__);
	  printk("  filp=%lld\n", filp->f_pos);

	  return -EINVAL;
	}
      */

      if(!PageUptodate(page))
	{
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
    }
  else
    {
      /* ページ全体に書くわけでないときは、
	 page全体の下地を全部読み出す*/
      if( !PageUptodate(page) && (from!=0 || to!=PAGE_CACHE_SIZE) )
	{
	  block_read_full_page(page, rs_ops->get_block);
	  if(!PageError(page))
	    {
	      struct buffer_head *head_bh = page_buffers(page);
	      struct buffer_head *wait_bh = head_bh;

	      /* 読み込みの完了を待つ */
	      do {
		wait_on_buffer(wait_bh);
		wait_bh = head_bh->b_this_page;
	      }while(wait_bh != head_bh);
	    }
	}
    }
  
  blocksize = 1 << inode->i_blkbits;
  bbits = inode->i_blkbits;
  block = (sector_t)page->index << (PAGE_CACHE_SHIFT - bbits);
  
  if(!page_has_buffers(page))
    {
      create_empty_buffers(page, blocksize, 0);
      head = page_buffers(page);

      for(bh = head, block_start = 0; bh != head || !block_start;
	  block++, block_start=block_end, bh = bh->b_this_page)
	{
	  block_end = block_start + blocksize;
	  if(buffer_new(bh))
	    {
	      clear_buffer_new(bh);
	    }
	  
	  if(!buffer_mapped(bh))
	    {
	      /* 既存ブロックのはずなので、
		 最後の引数はゼロで呼び出す。
		 ただし、まだ前に書いたデータが
		 reservoirの中である可能性もあるので、
		 必ずしも成功するとは限らない */
	      
	      err = rs_ops->get_block(inode, block, bh, 0);
	      if(err)
		{
		  /* reservoirの中だったと考えられる。
		     とりあえず放置しておく。*/
		}
	    }

	  set_buffer_uptodate(bh);
	}
    }

  SetPageUptodate(page);

  return 0;
}

int reservoir_write_begin(struct file *file, struct address_space *mapping,
			  loff_t pos, unsigned len, unsigned flags,
			  struct page **pagep, void **fsdata,
			  get_block_t *get_block, loff_t *bytes)
{
  struct inode *inode = mapping->host;
  unsigned long index = pos >> PAGE_CACHE_SHIFT;
  unsigned long offset = (pos & (PAGE_CACHE_SIZE - 1));
  unsigned long end = offset + len;
  struct page *page = *pagep;
  int ownpage = 0, status = 0;

  if(!test_bit(RS_RT, &inode->i_rsrvr_flags))
    {
      return cont_write_begin(file, mapping, pos, len,
			      flags, pagep, fsdata, get_block, bytes);
    }


  if(page==NULL)
    {
      ownpage=1;
      page = __grab_cache_page(mapping, index);
      if(!page)
	{
	  printk("%s-%d No Memory\n", __FUNCTION__, __LINE__);
	  status = -ENOMEM;
	  goto out;
	}
      *pagep = page;
    }
  else
    {
      BUG_ON(!PageLocked(page));
    }

  status = reservoir_prepare_write(file, page, offset, end);
  if(unlikely(status))
    {
      ClearPageUptodate(page);
      if(ownpage)
	{
	  unlock_page(page);
	  page_cache_release(page);
	  *pagep = NULL;
	  
	  if(pos + len > inode->i_size)
	    {
	      vmtruncate(inode, inode->i_size);
	    }
	  goto out;
	}
    }

 out:

  return status;
}

static int reservoir_commit_write(struct inode *inode, struct page *page,
				  unsigned from, unsigned to)
{
  unsigned long pos = (page->index << PAGE_CACHE_SHIFT) + to;
  int ret = 0;
  //struct reservoir_operations *rs_ops = RS_SB(inode->i_sb)->rs_ops;

/*   if(!test_bit(RS_RT, &inode->i_rsrvr_flags)) */
/*     { */
/*       return generic_commit_write(filp, page, from, to); */
/*     } */

  /* 既存ブロックへの上書きでページが中途半端な場合、
     最後の部分をダミー埋めする（このあと即書きするため） */
  /*
  if( page_has_buffers(page)
      && test_bit(RS_PCIDRCT, &inode->i_rsrvr_flags)
      && (to!=PAGE_CACHE_SIZE) )
    {
      ret = reservoir_fill_dummy_entry_for_page(page, inode->i_sb);
      if(ret)
	{
	  return ret;
	}
    }
  */

  if(pos > inode->i_size)
    {
      i_size_write(inode, pos);
    }

  reservoir_set_page_dirty(page);

  return ret;
}

int reservoir_write_end(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct page *page, void *fsdata)
{
  struct inode *inode = mapping->host;
  unsigned long start = pos & (PAGE_CACHE_SIZE - 1);

  if(!test_bit(RS_RT, &inode->i_rsrvr_flags))
    {
      return generic_write_end(file, mapping, pos, len,
			       copied, page, fsdata);
    }

  reservoir_commit_write(inode, page, start, start+copied);
  
  unlock_page(page);
  page_cache_release(page);

  return copied;
}

int reservoir_writepages(struct address_space *mapping,
			 struct writeback_control *wbc)
{
  /* もし将来的に、非RT系のものも
     reservoir経由で書き出すようにする場合は、
     標準の書き出しのシーケンスの中に、
     reservoirをさらえる処理を入れる必要がある。
     ここで、その前に通常系のファイルの
     pageをreservoirに移動させればよい */

  struct inode *inode = mapping->host;
  unsigned long next = 0;
  /*
  struct super_block *sb = inode->i_sb;
  unsigned long last_page = inode->i_size >> PAGE_CACHE_SHIFT;
  unsigned long rs_block_size = RS_SB(sb)->rs_block_size;
  */
  
  if(test_bit(RS_RT, &inode->i_rsrvr_flags))
    {
      return 0;
    }

  /* 非RT系で書き込んだものを書き込む。
     非RT系で書いたら、page_has_buffersなことが大前提 */

  /* syncでない場合は、wbcのレンジを
     i_sizeから計算できるブロックアラインメントに丸める。
     ただ、それはclose時にsyncで必ず呼ばれることが前提。 */

#if defined(CONFIG_DELAYPROC)
  if(wbc->for_delayproc)
	  goto SUBMIT_PAGES;
#endif /* CONFIG_DELAYPROC */
 
  /**************************************
   // ここの処理をコメントアウトした理由は
   // reservoir_file_release() 内のコメントを読んでください

  if(wbc->sync_mode!=WB_SYNC_ALL)
    {
      long long aligned_end = ( (last_page / rs_block_size) * rs_block_size ) << PAGE_CACHE_SHIFT;

      if(aligned_end==0)
	{
	  return 0;
	}

      wbc->range_end = wbc->range_end < aligned_end ? wbc->range_end : aligned_end;
    }
  ***************************************/
  
#if defined(CONFIG_DELAYPROC)
 SUBMIT_PAGES:
#endif /* CONFIG_DELAYPROC */
  reservoir_submit_pages(inode, 0, ~(0L), &next, wbc);

  return 0;
}

int reservoir_end_io_write(struct bio *bio, int err)
{
  if(bio->bi_size)
    return 1;

  if(test_and_clear_bit(BIO_RW_DUMMY, &bio->bi_rw))
    {
      /* ダミークラスタの場合は、pageがあったら単純解放 */

      struct bio_vec *bvec = &bio->bi_io_vec[0];

      while(bvec->bv_page!=NULL)
	{
	  end_page_writeback(bvec->bv_page);
	  put_page(bvec->bv_page);
	  bvec++;
	}
    }
  /* DIRECTのときは、さっさとpageは
     掃除されているので、いじっちゃだめ。 */
  else if(!test_and_clear_bit(BIO_RW_DRCT, &bio->bi_rw))
    {
      struct bio_vec *bvec = bio->bi_io_vec + bio->bi_vcnt - 1 ;
      const int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);

      do
	{
	  struct page *page = bvec->bv_page;

	  BUG_ON(page==NULL);

	  if(--bvec >= bio->bi_io_vec)
	    {
	      prefetchw(&bvec->bv_page->flags);
	    }

	  if(!uptodate)
	    {
	      SetPageError(page);
	      if(page->mapping)
		set_bit(AS_EIO, &page->mapping->flags);
	    }
	  end_page_writeback(page);

	}while(bvec >= bio->bi_io_vec);
    }

  bio_put(bio);

  return 0;
}

int reservoir_end_io_read(struct bio *bio, int err)
{
  struct bio_vec *bvec = bio->bi_io_vec + bio->bi_vcnt - 1 ;
  const int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);

  if(bio->bi_size)
    return 1;

  /* Direct系の場合、bio_putの呼出はreadの関数内で行うことにする。
     readの完了待ちをbioに対する wait_on_bit() でおこなうため */
  if(test_and_clear_bit(BIO_RW_DRCT, &bio->bi_rw))
    {
      smp_mb__after_clear_bit();
      wake_up_bit(&bio->bi_rw, BIO_RW_DRCT);
    }
  else
    {
      do
	{
	  struct page *page = bvec->bv_page;
	  
	  if(page)
	    {
	      if( (--bvec >= bio->bi_io_vec)
		  && (bvec->bv_page != NULL) )
		{
		  prefetchw(&bvec->bv_page->flags);
		}

	      if(uptodate)
		{
		  SetPageUptodate(page);
		}
	      else
		{
		  ClearPageUptodate(page);
		  SetPageError(page);
		}

	      unlock_page(page);
	    }

	}while(bvec >= bio->bi_io_vec);

      bio_put(bio);
    }

  return 0;
}

int reservoir_fsync(struct file *filp, struct dentry *dent, int datasync)
{
  struct inode *inode = dent->d_inode;
  int rt = (filp->f_flags & O_REALTIME) ? 1 : 0;   /* rtかどうかをフラグを見て確認 */
  int ret = 0;

  if(rt)
    {
      ret = reservoir_rt_fsync(inode);
    }
  else
    {
	  struct super_block *sb = inode->i_sb;
		
       /* 非RTファイルも必ずクラスタ単位で
	 書き出すようにする場合は必要だが、
	 現在はそうはしていないので、
	 ファイルの末尾埋めはやらないことにする
	 （関数の中身は無効化してある） */
      reservoir_pad_file_tail(inode);
	  
      /* 通常のfsyncを呼び出す */
	  lock_rton(MAJOR(sb->s_dev));
	  if (check_rt_status(sb))
	  {
		  ret = file_fsync(filp, dent, datasync);
	  }
	  unlock_rton(MAJOR(sb->s_dev));
    }

  return ret;
}

loff_t reservoir_llseek(struct file *filp, loff_t offset, int whence)
{
  int rt = (filp->f_flags & O_REALTIME) ? 1 : 0;   /* rtかどうかをフラグを見て確認 */
  loff_t ret = 0;

  if( !rt || ( (filp->f_flags & O_ACCMODE)==O_RDONLY ) )
    {
      /* 非RTおよびReadOnlyの場合は通常のllseekを呼び出す */
      ret = generic_file_llseek(filp, offset, whence);
    }
  else
    {
      struct inode *inode = filp->f_mapping->host; 

      /* TODO: PCI Directでない場合はもう少し柔軟に
	 SEEKの条件を設定できるはず。
	 （下地読み出しをすることが前提） */

      mutex_lock(&inode->i_mutex);

      switch(whence)
	{
	case 2:
	  offset += inode->i_size;
	  break;
	case 1:
	  offset += filp->f_pos;
	}

      ret = offset;

      /* RTでは、ファイル末尾よりも先のSEEKは許さないことにする */
      if(offset > inode->i_size)
	{
	  printk("%s-%d: Seek beyond the file end.\n",
		 __PRETTY_FUNCTION__, __LINE__);
	  ret = -EINVAL;
	  goto FIN;
	}

      /* ファイルサイズ上限を越えていないかチェック */
      if(offset<0 || offset > inode->i_sb->s_maxbytes)
	{
	  printk("%s-%d: Invalid Seek.\n",
		 __PRETTY_FUNCTION__, __LINE__);
	  ret = -EINVAL;
	  goto FIN;
	}
      
      /* Seekがf_posの移動を伴わない場合はなにもしない */
      if(filp->f_pos != offset)
	{
	  struct super_block *sb = inode->i_sb;

	  /* page境界からしか書き込ませないために考えたが、
	     不便なのでチェックしないことにする。
	     write_check内で調べることに。*/
	  /**
	  if(offset & (PAGE_CACHE_SIZE-1))
	    {
	      printk("%s-%d: Invalid File Seek. (%s@%lld\n",
		     __PRETTY_FUNCTION__, __LINE__,
		     filp->f_dentry->d_iname, offset);
	      goto FIN;
	    }
	  **/

	  /* まだcommitしていないぶんを書き出してしまう。
	     書き出さず、既存Pageへの再編集を許すようにすれば
	     簡単になる可能性もあるが、Kernel2.4との
	     互換性のためにこうする。 */
	  mutex_lock(&RS_SB(sb)->io_serialize);
	  reservoir_pad_and_commit_tail(inode);
	  mutex_unlock(&RS_SB(sb)->io_serialize);

	  /* ファイルのポジションを動かす */
	  filp->f_pos = offset;
	  filp->f_version = 0;
	}

    FIN:
      mutex_unlock(&inode->i_mutex);
    }
 
  return ret;
} 

/******************* Direct系 Read の関数 *******************/

static int submit_and_list_read_bio(struct bio *bio, struct super_block *sb,
				    struct bio **last_bio)
{
  struct reservoir_operations *rs_ops = RS_SB(sb)->rs_ops;

  /* 次のBIOへのポインタ */
  if(*last_bio)
    (*last_bio)->bi_private2 = (void *)bio;

  *last_bio = bio;

  if( likely(rs_ops->set_bio_callback!=NULL) )
    {
      /* bioが転送終了したときのコールバック関数を記録しておく */
      rs_ops->set_bio_callback(bio, sb, READ, 1);
    }      

  /* i/o schedulerに渡す */
  BUG_ON(bio->bi_end_io==NULL);
  BUG_ON(bio->bi_private2!=NULL);
  submit_rt_bio(READ, sb, bio);

  return 0;
}

ssize_t generic_file_pci_direct_read(struct kiocb *iocb, const struct iovec *iov,
			       unsigned long nr_segs, loff_t pos)
{
  struct inode *inode = iocb->ki_filp->f_dentry->d_inode;
  struct super_block *sb = inode->i_sb;
  struct reservoir_operations *rs_ops = RS_SB(sb)->rs_ops;
  unsigned long seg = 0;
  size_t count = 0;
  ssize_t read_done = 0;
  ssize_t err = 0;
  struct bio *first_bio = NULL;
  struct bio *last_bio = NULL;
  unsigned long dummy_addr = 0;
  sector_t cur_sector = 0;
  size_t dummy_head =  0; /* bio先頭に埋めるべきダミー転送サイズ */
  size_t dummy_tail = 0;  /* bio末尾に埋めるべきダミー転送サイズ */
  loff_t cluster_offset = 0; /* 転送開始位置のクラスタ内オフセット */
  loff_t sector_offset = 0; /* 転送開始位置のセクタ内オフセット */
  struct buffer_head bh;
  sector_t iblock = 0;
  struct bio *bio = NULL;
  unsigned long cluster_size = RS_SB(sb)->rs_block_size * PAGE_CACHE_SIZE;
  loff_t *ppos = &iocb->ki_pos;
  int need_dummy = 0;
  int done = 0;
  unsigned long iv_offset = 0;
  size_t bio_max_size = bdev_get_queue(sb->s_bdev)->max_sectors << sb->s_blocksize_bits;
  unsigned long device_addr = RS_SB(sb)->rs_ops->get_device_address(sb);
  unsigned long verbous_read = 0;

  /* ファイル末尾を越えて読み込もうとしていたら、
     なにもせずに返す */
  if( unlikely(pos >= inode->i_size) )
    {
      return 0;
    }

  for(seg = 0; seg < nr_segs; seg++)
    {
      const struct iovec *iv = &iov[seg]; /* Discard static status ! */

      if(unlikely( iv->iov_len <= 0  /* サイズが負の場合 */
		   || (iv->iov_len & (2-1)) /* 2byte単位でない */
		   ) )
	{
	  printk("%s-%d: Invalid Transfer Entries. (%u)\n",
		 __PRETTY_FUNCTION__, __LINE__, iv->iov_len);
	  err = -EINVAL;
	  goto OUT;
	}

      /* segが指定する転送サイズを合計してゆく */
      count += iv->iov_len;
    }

  /* iovの添数をリセットしておく */
  seg = 0;

  if( unlikely( (inode->i_size - pos) < count ) )
    {
      /* P2システムのミドルは、ダイレクト転送のとき
	 読み込みサイズを4byteの倍数に丸めてくる
	 可能性があるので、それを許す。
	 どうせ読み込み先はDMかZIONなので、
	 うしろに3byte以下のゴミデータがくっついてきても
	 一向にかまわないのです */
      if(count - (inode->i_size - pos) > 3)
	{
	  count = inode->i_size - pos;
	  if(count & (2-1))
	    {
	      /* 2byteの倍数にはしておきましょう */
	      count += 1;
	    }
	}

      /* 意図的に過剰読み出しするぶんを覚えておく
	 （返り値を調整するため） */
      verbous_read = count - (inode->i_size - pos);
    }

  if( unlikely( count==0 ) )
    {
      goto OUT;
    }

  if( likely(rs_ops->get_addr_for_dummy_read!=NULL) )
    dummy_addr = rs_ops->get_addr_for_dummy_read(sb);

  /* 先頭セクタ番号を求める */
  iblock = pos >> sb->s_blocksize_bits;
  err = rs_ops->get_block(inode, iblock, &bh, 0);
  if( unlikely(err) )
    {
      printk("%s-%d: Getting Sector Number Failed.\n", __PRETTY_FUNCTION__, __LINE__);
      goto OUT;
    }
  cur_sector = bh.b_blocknr;

  /* 書き込み位置のセクタ内オフセットを求める */
  sector_offset = pos & ( sb->s_blocksize - 1 );
  dummy_head = sector_offset;

  /* 書き込み位置のクラスタ内オフセットを求める */
  cluster_offset = pos & ( cluster_size  - 1 );

  /* 末尾に埋めるダミー要素のサイズを求める */
  dummy_tail =  sb->s_blocksize - ( (pos + count) & (sb->s_blocksize - 1) );
  if( unlikely(dummy_tail == sb->s_blocksize) )
    {
      dummy_tail = 0;
    }

  bio = first_bio = reservoir_bio_alloc(sb, inode, cur_sector, READ);
  if( unlikely(bio == NULL) )
    {
      printk("%s-%d: Getting BIO Failed.\n", __PRETTY_FUNCTION__, __LINE__);
      err = -ENOMEM;
      goto OUT;
    }

  set_bit(BIO_RW_DRCT, &bio->bi_rw);


  /* 読み出しがセクタの途中から始まる場合、
     開始点までのオフセットぶんをダミー転送として扱う */
  if(dummy_head)
    {
      size_t size_remain = dummy_head;

      /* 512byte以上になることはないが、念のためループ */
      while(size_remain)
	{
	  struct bio_vec *bvec = &bio->bi_io_vec[bio->bi_vcnt];
	  size_t size = min(size_remain, (size_t)512);

	  bvec->bv_page = NULL;
	  bvec->bv_private = dummy_addr;
	  bvec->bv_len = size;
	  bio->bi_vcnt++;
	  bio->bi_phys_segments++;
	  bio->bi_hw_segments++;
	  bio->bi_size += size;
	  size_remain -= size;
	}
    }

  while(count && !done)  /* 転送するデータがある限りBIOを作成する */
    {
      size_t transfer_size = 0;
      size_t transfer_done = 0;

    RETRY:

      /* そのままbioに追加していいものか調べる */
      iblock = pos >> sb->s_blocksize_bits;
      err = rs_ops->get_block(inode, iblock, &bh, 0);
      if( unlikely(err) )
	{
	  printk("%s-%d: Getting Sector Number Failed.\n", __PRETTY_FUNCTION__, __LINE__);
	  break;
	}
      cur_sector = bh.b_blocknr;

      if( ( bio->bi_sector + (bio->bi_size >> sb->s_blocksize_bits) ) != cur_sector )
	{
	  submit_and_list_read_bio(bio, sb, &last_bio);
	  bio = reservoir_bio_alloc(sb, inode, cur_sector, READ);
	  if( unlikely(bio==NULL) )
	    {
	      printk("%s-%d: Getting BIO Failed.\n", __PRETTY_FUNCTION__, __LINE__);
	      err = -ENOMEM;
	      break;
	    }

	  set_bit(BIO_RW_DRCT, &bio->bi_rw);

	  goto RETRY;
	}

      /* クラスタ内で転送するべきサイズを計算する。
	 現在のファイルポインタから、どれだけのデータ量が
	 ストレージ上で物理的に連続していると保証されるか
	 計算していることになる。
	 もしSDカード等でもこの関数を利用したいのなら、
	 ここでrs_block_sizeをクラスタサイズとして
	 使っているところをあらため、ファイルシステムの
	 正確なクラスタ数を使うしくみが必要である。
	 rs_block_sizeの単位はpageなので、512Byteクラスタなどを表現できない*/
      transfer_size = cluster_size - cluster_offset;
      transfer_size = min(transfer_size, count);

      cluster_offset = 0;

      while(transfer_size) /* クラスタ内のデータ転送をbio化する */
	{
	  size_t sector_transfer_size = sb->s_blocksize - sector_offset;
	  size_t byte_count = 0;
	  unsigned long seg_walk = seg;
	  int i = 0;
	  size_t sector_transfer_done = 0;

	  sector_offset = 0;

	  /* iovを順番に見ていって、512byteを取得するには
	   どれだけのエントリ数が必要かをチェックする。
	   なぜなら、bioには512byte単位の要求しか
	   入れたくないから */
	  for(i=0, seg_walk=seg;
	      (byte_count < sector_transfer_size) && (seg_walk < nr_segs);
	      seg_walk++, i++)
	    {
	      byte_count += ( iov[seg_walk].iov_len - (i==0 ? iv_offset : 0) ); 
	    }

	  byte_count = min(byte_count, transfer_size);

	  /* 先頭のエントリが、bioに既に入っているエントリと
	     結合できるかをチェック */
	  if( ( bio->bi_vcnt!=0 ) &&
	      ( bio->bi_io_vec[bio->bi_vcnt-1].bv_private
		+ bio->bi_io_vec[bio->bi_vcnt-1].bv_len
		== ( ((unsigned long)iov[seg].iov_base) + iv_offset ) ) )
	    {
	      i--;
	    }

	  /* 最後にダミー転送用のエントリを
	     入れる必要があるかどうかを判断する */
	  if( unlikely( (seg_walk==nr_segs) && (byte_count<sector_transfer_size) ) )
	    {
	      i++;
	      need_dummy = 1;
	    }

	  /* bioに追加してもエントリがあふれないかをチェック */
	  if( unlikely( ( ( bio->bi_vcnt + i ) > bio->bi_max_vecs) ||
			( bio_max_size < (bio->bi_size + sector_transfer_size) ) ) )
	    {
	      /* 溢れる場合は、bioを取得しなおす */
	      submit_and_list_read_bio(bio, sb, &last_bio);
	      bio = reservoir_bio_alloc(sb, inode, cur_sector, READ);
	      if(bio==NULL)
		{
		  printk("%s-%d: Getting BIO Failed.\n", __PRETTY_FUNCTION__, __LINE__);
		  err = -ENOMEM;
		  done = 1;
		  break;
		}

	      set_bit(BIO_RW_DRCT, &bio->bi_rw);
	    }

	  /* 実際にbioにエントリを追加する */
	  sector_transfer_size = min(sector_transfer_size, transfer_size);
	  byte_count = min(byte_count, sector_transfer_size);
	  while(byte_count)
	    {
	      unsigned int entry_size = iov[seg].iov_len - iv_offset;
	      unsigned long add_size = 0;

	      /* iovの現在のエントリからbioに加えるべきサイズ */
	      add_size = min(entry_size, byte_count);

	      if( ( bio->bi_vcnt!=0 ) &&
		  ( bio->bi_io_vec[bio->bi_vcnt-1].bv_private
		    + bio->bi_io_vec[bio->bi_vcnt-1].bv_len
		    == ( ((unsigned long)iov[seg].iov_base) + iv_offset) ) )
		{
		  /* 前に加えたエントリとマージ可能な場合 */
		  bio->bi_io_vec[bio->bi_vcnt-1].bv_len += add_size;

		}
	      else
		{
		  /* 新しいエントリ追加が必要な場合 */
		  bio->bi_io_vec[bio->bi_vcnt].bv_page = NULL;
		  bio->bi_io_vec[bio->bi_vcnt].bv_private
		    = ((unsigned long)iov[seg].iov_base) + iv_offset;
		  bio->bi_io_vec[bio->bi_vcnt].bv_len = add_size;

		  bio->bi_vcnt++;
		  bio->bi_phys_segments++;
		  bio->bi_hw_segments++;
		}

	      if( likely(rs_ops->set_bio_private!=NULL) )
		{
		  /* Readではいまのところprivateをいじる予定はない */
		  rs_ops->set_bio_private(bio, NULL, READ, 1);
		}

	      /* キャッシュの処理をしておく */
	      if( unlikely( (((unsigned long)iov[seg].iov_base) + iv_offset) < device_addr) )
		{
/* 		  dma_cache_inv( bus_to_virt(((unsigned long)iov[seg].iov_base) + iv_offset), add_size ); */
		  dma_cache_sync(NULL, bus_to_virt(((unsigned long)iov[seg].iov_base) + iv_offset),
				 add_size, DMA_FROM_DEVICE);
		}

	      bio->bi_size += add_size;

	      iv_offset += add_size;

	      /* そのエントリを使いきったら次のエントリへ */
	      if( iov[seg].iov_len == iv_offset )
		{
		  seg++;
		  iv_offset = 0;
		}

	      byte_count -= add_size;
	      sector_transfer_done += add_size;
	    }

	  /* 転送処理したバイト数の記録 */
	  transfer_size -= sector_transfer_done;
	  transfer_done += sector_transfer_done;
	  cur_sector++;

	  /* 内部的にファイルポインタを進める */
	  pos += sector_transfer_done;
	}

      count -= transfer_done;
      read_done += transfer_done;

      /* たまには他者にも思いやり */
      cond_resched();
    }

  /* 処理の内部でエラーが起こっていたときは無駄なことをしないように */
  if(unlikely(err))
  {
	  goto OUT;
  }

  /* 末尾のダミー転送埋めが必要だった場合 */
  if(need_dummy && bio)
    {
      /* 新しいエントリ追加が必要な場合 */
      /* ここも本来はループである必要はないが念のため */
      while(dummy_tail)
	{
	  size_t size = min(dummy_tail, (size_t)512);

	  bio->bi_io_vec[bio->bi_vcnt].bv_page = NULL;
	  bio->bi_io_vec[bio->bi_vcnt].bv_private = dummy_addr;
	  bio->bi_io_vec[bio->bi_vcnt].bv_len = size;
      
	  bio->bi_vcnt++;
	  bio->bi_phys_segments++;
	  bio->bi_hw_segments++;

	  bio->bi_size += size;
	  dummy_tail -= size;
	}    

      if( likely(rs_ops->set_bio_private!=NULL) )
	{
	  /* Readではいまのところprivateをいじる予定はない */
	  rs_ops->set_bio_private(bio, NULL, READ, 1);
	}
    }

  /* 最後のひとつまで i/o scheduler に渡す */
  if( likely(bio) )
    {
      submit_and_list_read_bio(bio, sb, &last_bio);
    }

  /* さてあとは、i/o の終了を待ちましょう */
  bio = first_bio;
  while(bio)
    {
      struct bio *bio_wait = bio;

      wait_on_bit(&bio->bi_rw, BIO_RW_DRCT,
		  reservoir_io_wait, TASK_UNINTERRUPTIBLE);
      bio = (struct bio *)bio_wait->bi_private2;
      bio_wait->bi_private2 = NULL;

      if(!test_bit(BIO_UPTODATE, &bio_wait->bi_flags))
	{
	  printk("%s-%d: Invalid BIO State\n", __FUNCTION__, __LINE__);
	  err = -EIO;
	}

      bio_put(bio_wait);
    }

 OUT:

  /* ファイルポインタの移動を反映する */
  *ppos = pos;

  return (err==0) ? (read_done - verbous_read) : err;
}

ssize_t reservoir_file_aio_read(struct kiocb *iocb, const struct iovec *iov,
				unsigned long nr_segs, loff_t pos)
{
  struct file *filp = iocb->ki_filp;
  struct inode *inode = iocb->ki_filp->f_dentry->d_inode;
  struct super_block *sb = inode->i_sb;
  size_t count = 0;
  loff_t tail = 0; 
  ssize_t ret = 0;
  int seg = 0;

  /* まず、本当に読み込み可能な場所なのかをチェックする */
  for(seg = 0; seg < nr_segs; seg++)
    {
      const struct iovec *iv = &iov[seg]; /* Discard static status ! */
      /* segが指定する転送サイズを合計してゆく */
      count += iv->iov_len;
    }

  if(count==0)
    {
      return 0;
    }

  mutex_lock(&RS_SB(sb)->io_serialize);

  /* ここがファイルの末尾 */
  tail = pos + count;

  /* i_blocksとi_sizeが不一致の場合
     ＝ reservoir系でいじっている最中のファイルの場合 */
  if( inode->i_blocks != ((inode->i_size + (RS_SB(sb)->rs_block_size*PAGE_CACHE_SIZE -1))
			  & ~((loff_t)RS_SB(sb)->rs_block_size*PAGE_CACHE_SIZE -1)) >> 9 )
    {
      /* 書き込まれた場所が確定しているブロック数を計算し、
	 これから読み出そうとしているところが範囲内かをチェック */
      if( ((tail-1) >> 9) + 1 > inode->i_blocks )
	{
	  goto UNLOCK_OUT;
	}
    }

  if(filp->f_flags & O_PCIDRCT)
    {
      ret = generic_file_pci_direct_read(iocb, iov, nr_segs, pos);
    }
  else
    {
      /* 同時にDRCTで開かれている場合は、
	 ReadAheadでキャッシュを汚されると困るので先読み禁止 */
      if(test_bit(RS_PCIDRCT, &inode->i_rsrvr_flags))
	{
	  filp->f_ra.ra_pages = 0;
	}

      ret = generic_file_aio_read(iocb, iov, nr_segs, pos);

      /* 同時にDRCTで開かれている場合は、
	 邪魔になる場合があるのでcacheを消しておく */
      if(test_bit(RS_PCIDRCT, &inode->i_rsrvr_flags))
	{
	  pgoff_t start = pos >> PAGE_CACHE_SHIFT;
	  pgoff_t end = iocb->ki_pos >> PAGE_CACHE_SHIFT;
	  invalidate_mapping_pages(inode->i_mapping, start, end);
	}
    }

 UNLOCK_OUT:
  mutex_unlock(&RS_SB(sb)->io_serialize);

  return ret;
}
