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

  /* Directž���λ�����ä���� */

  /* HIGHMEM�б��Τ���page��ޥå� */
  kaddr = kmap(page);
  
  drct_page = (struct drct_page *)kaddr;

  total_size = drct_page->total_size;

  /* 1�ڡ����ˤ����ޤ꤭��ʤ��ä��Ȥ��Τ����
     ��������򤿤ɤ롣�µ��ǤϤޤ��Ȥ��ʤ� */
  while( unlikely(drct_page->page_chain!=NULL) )
    {
      drct_page = (struct drct_page *)drct_page->page_chain;
      total_size += drct_page->total_size;
    }

  /* �ڡ����ˤ��ɵ��������Ĥ��ʤ� */
  BUG_ON(total_size != offset);

  if(drct_page->nr_entries>=MAX_DRCT_ENTRIES)
    {
      /* ��page�ǤϤ���ʤ��ä��Τǡ�
	 ���page��kmalloc���ƥ�󥯤��� */

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

  /* ����������ȥ���ɲä��� */
  drct_page->entries[drct_page->nr_entries].pci_addr = (dma_addr_t)buf;
  drct_page->entries[drct_page->nr_entries].size = bytes;
  drct_page->nr_entries++;
  drct_page->total_size += bytes;

  /* kmap�θ���� */
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

  /* DIRECTž�������б� */

  /* HIGHMEM�б� */
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
	  /* ��page�ǤϤ���ʤ��ä��Τǡ�
	     ���page��kmalloc���ƥ�󥯤��� */

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
      
      /* DIRECT����ȥ���ɲ� */
      drct_page->entries[drct_page->nr_entries].pci_addr = (dma_addr_t)buf;
      drct_page->entries[drct_page->nr_entries].size = copy;
      drct_page->nr_entries++;
      drct_page->total_size += copy;

      copied += copy;
      bytes -= copy;
      iov++;
    }

  /* kmap����� */
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
		  /* mmap��Ȥäƽ񤤤Ƥ����顢�����ǥ��顼�ˤʤ뤫�⡩
		     �Ȥꤢ�������ߤϥ��顼�Ȥ��Ƥ��� */
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
	  /* P2FS�Ǥϡ��ºݤˤ���bio��ž������ե���������
	     �ǡ������̤�Ͽ���Ƥ��� */
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

  /* ��Direct�Ϥ�Direct�ϤΥ��ߡ���
     �ɤä����뤫������Ƚ�Ǥ��񤷤��Ȥ��������
     get_addr_for_dummy���֤��ͤ�
     NULL���ɤ�����Ƚ�Ǥ��뤳�Ȥˤ��� */

  if( likely(rs_ops->get_addr_for_dummy_write!=NULL) )
    dummy_addr = rs_ops->get_addr_for_dummy_write(sb);

  BUG_ON(bio->bi_vcnt!=0);

  bvec = &bio->bi_io_vec[0];

  if(dummy_addr != 0)  // ���ߡ����ɥ쥹���������Ƥ��뤫�顢�����ƥ��Direct�б����Ƥ���
    {
      /* write��dummy�ΰ�Υ�������128MB��ZION���̡ˡ�
	 ������Ƭ���饯�饹���������֤����� */
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
	      /* �Ǥ�ɤ����褦��ʤ��� */
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
      /* private�ʶ��֤ˡ�dummy���饹���Ǥ��뤳�Ȥ�񤭹��ࡣ
       ������Хå��Ǥϡ�ž����Υ��饹���ϳ������� */
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
	  /* bio��ž����λ�����Ȥ��Υ�����Хå��ؿ���Ͽ���Ƥ��� */
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

  /* ����ʤ��ʤ餵�ä���ȴ���� */
  if(bio_walk == NULL)
    {
      return 0;
    }

  /* i/o scheduler�μ¹ԥ��塼������ʲ��ˤʤ�Τ򾯤��ԤäƤߤ� */
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

  /* ���Ǥ˳��ݤ��Ƥ��륯�饹�����ʤ��������å����� */
  if(reservoir->suspended_cls[reservoir->cls_ptr]==0)
    {
      reservoir->cls_ptr = 0;
      ret = rs_ops->get_n_blocks(reservoir->sb, reservoir->max_length,
				 reservoir->suspended_cls);
      if( unlikely(ret) )
	{
	  /* Ϣ³�������ߤĤ���ʤ��ä��Τǡ�
	     �ҤȤĤ��Ľ񤭽Ф��Ƥ��� */
	  return reservoir_flush_reservoir(reservoir);
	}
    }

  /* ��ޤä����饹����bio����Ϳ����reservoir�����Ǥ��Ф� */
  bio_walk=reservoir->bio_head;

  while( bio_walk!=NULL )
    {
      struct bio *cur_bio = bio_walk;

      BUG_ON(reservoir->suspended_cls[reservoir->cls_ptr]==0);

      cur_bio->bi_sector = reservoir->suspended_cls[reservoir->cls_ptr];
      bio_walk = cur_bio->bi_next;
      reservoir->cls_ptr++;
      cur_bio->bi_next = NULL;

      /* Sequencial���ɤ�����Ƚ�Ǥ��� */
      if(test_bit(RT_SEQ, &RS_SB(reservoir->sb)->rt_flags))
	{
	  set_bit(BIO_RW_SEQ, &cur_bio->bi_rw);
	}

      if( likely(rs_ops->set_bio_callback!=NULL) )
	{
	  /* bio��ž����λ�����Ȥ��Υ�����Хå��ؿ���Ͽ���Ƥ��� */
	  /* Ʊ���ˡ�ɬ�פǤ�������bio_private�����Ƥ򤤤��ä�
	     ž���������ε�Ͽ�򤪤��ʤ�*/
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
      /* ���줤���äѤ��Ǥ��Ф������ */
      memset(reservoir->suspended_cls, 0,
	     sizeof(unsigned long)*RS_MAX_BIOS);
      reservoir->max_length = max_length;
      reservoir->cls_ptr = 0;
    }
  else
    {
      /* ���ݤ������饹���˻Ĥ꤬����Ȥ��ϡ�
	 max_length�򤽤λĤ���̤����ꤷ�ʤ����Ƥ��� */
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

  /* �������餤�Ǥ���Ĵ�٤�Ȥ����ʤ��Τǡ� */
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

  /* PCI Direct�Ǥʤ����ϡ�ɸ��Τ�Τ�ƤӽФ� */
  if(!drct)
    {
      total_size = bio_add_page(bio, page, PAGE_CACHE_SIZE, 0);
      goto FIN;
    }

  /* SLAVE��bio��������ϡ����������˰�ư���� */
  while(bio->bi_private2!=NULL)
    {
      bio = (struct bio *)bio->bi_private2;
    }

  kaddr = kmap(page);
  drct_page = (struct drct_page *)kaddr;
  entries = drct_page->nr_entries;
  total_size = drct_page->total_size;

  /* ����ڡ������������롣�ۤȤ�ɻȤ��Ƥ��ʤ�����ɡ� */
  drct_page_walk = (struct drct_page *)drct_page->page_chain;
  while( unlikely(drct_page_walk!=NULL) )
    {
      entries += drct_page_walk->nr_entries;
      total_size += drct_page_walk->total_size;
      drct_page_walk = (struct drct_page *)drct_page_walk->page_chain;
    }

  BUG_ON(total_size!=PAGE_CACHE_SIZE);

  /* ����ʤ�����ȥ꤬bio�ҤȤĤ˼��ޤ��硣
     ���줬�ۤȤ�ɤξ����Ȥ����롣
     �ºݤε���ǤϤ����ȿ���뤳�ȤϤ�����ʤ� */
  if( likely( (bio->bi_vcnt + entries) <= bio->bi_max_vecs ) )
    {
      int i = 0;

      /* bio�Υ٥������������������ */
      bvec = &bio->bi_io_vec[bio->bi_vcnt];

      /* BIO��DRCT�ե饰�򤿤Ƥ� */
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

	  /* ZION��ʳ������ž���ξ��ϡ�����å���ν����򤷤Ƥ�����
	     �����쥯��ž���ξ��ϥե����륷���ƥ������ */
	  /* �ºݤˤϤۤȤ�ɤ�Directž��������unlikely�ˤ��Ƥ��� */
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
  /* bio�˼��ޤ���票��ȥ������ϤߤǤ��硣 */
  else
    {
      struct drct_entry *last_entry = &drct_page->entries[0];
      int entry_bytes_offset = 0;
      int page_size_remain = total_size;

      drct_page_walk = drct_page;

      /* �ޤ������뤳�ȤϤʤ��Ϥ��ʤΤǡ�Ķ���޽���Ū��
	 ����ȥ��512byte���Ȥˤ����ä�bio�ҤȤĤ��Ĥ�����롣
	 ����ȥ�κ����128�ȡ�����ȥ���κ���ž��������4byte����
	 ���뤳�Ȥ��ݾڤ���Ƥ���Ϥ��� */
      while(page_size_remain)
	{
	  int sector_entries = 0;
	  int sum = 0;
	  int transfered = 0;
	  int i = 0;
	  struct drct_entry *entry_walk = last_entry;
	  struct drct_page *tmp = drct_page_walk;
 
     
	  /* �ޤ���512byte��ɬ�פʥ���ȥ��������� */
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

	  /* �⤷bio�ˤ��줫�������ž���ꥹ�Ȥ�
	     ���꤭��ʤ����ϡ�������bio����ݤ���
	     private2�˥ꥹ�Ȥ��� */
	  if( (bio->bi_vcnt + sector_entries) > bio->bi_max_vecs )
	    {
	      struct bio *new_bio = NULL;
	      
	      /* �������ֹ��submit���˿Ƥ���׻����ƿ���Τǡ�
		 �����ʳ��Ǥϥ���Ǥ������ */
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

	  /* bio�Υ٥������������������ */
	  bvec = &bio->bi_io_vec[bio->bi_vcnt];

	  /* BIO��DRCT�ե饰�򤿤Ƥ� */
	  set_bit(BIO_RW_DRCT, &bio->bi_rw);

	  while(transfered<512)
	    {
	      /* DIRECTž���Ȥ��ƤΥ���ȥ������� */
	      unsigned long last_entry_size = last_entry->size - entry_bytes_offset;
	      dma_addr_t last_entry_addr = (dma_addr_t)( ((unsigned long)last_entry->pci_addr)
							 + entry_bytes_offset );
	      unsigned long size_remain =  (512 - transfered);
	      unsigned long size_transfer = min(size_remain, last_entry_size);

	      /* write�Ǥϡ�ž������뤳��ˤ��ݽ������page�Ϥ��ʤ��ʤäƤޤ��� */
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
      /* P2FS�Ǥϡ��ºݤˤ���bio��ž������ե���������
	 �ǡ������̤�Ͽ���Ƥ���
	 �ޤ���page��bio�κǽ�ʤ�page->index���ͤ⵭Ͽ���롣
	 ����Ū�ˤϡ������ͤ�Ȥäƥ��饹���ι�����Ԥ���
	 �������ʤ��ȡ�reservoir��ˤ���ǡ����ξ�񤭤��н�Ǥ��ʤ��� */
      rs_ops->set_bio_private(first_bio, page, rw, drct);
    }

  /* Directž���ξ�硢bio���ɲä�����page�ϻȤ��Τ� */

  /* ����ڡ�����������ϲ��� */
  drct_page_walk = (struct drct_page *)drct_page->page_chain;
  while( unlikely(drct_page_walk!=NULL) )
    {
      struct drct_page *tmp = drct_page_walk;
      drct_page_walk = (struct drct_page *)drct_page_walk->page_chain;
      kfree((void *)tmp);
    }

  /* �ƥڡ����⥯�ꥢ���Ƥ��� */
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

  /* �̾�Ϥξ��ϡ�ɸ��δؿ���ƤӽФ� */
  if(!test_bit(RS_RT, &inode->i_rsrvr_flags))
    {
      struct mpage_data mpd = {
	.bio = bio,
	.last_block_in_bio = *last_block_in_bio,
	.get_block = get_block,
	.use_writepage = 1,
      };

      /* MI(��������)�ե饰��On�ʤ顢MI�ե饰��On�ˤ���(bio!=NULL�ξ��) */
      /*  bio==NULL�ξ��ϡ�__mpage_writepage()�ؿ���ǥե饰��On�ˤ��롣 */
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
 
  /* RT�Ϥξ��Ͼ��pageñ�̤ǰ��äƤ���Τǡ�
     buffer����ʬŪ�ʰ��������ס�
     page��uptodate��ǧ�����Ǥ��� */
 
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

  /* RT�ϤǤ�mapped�ʤ�Τ���
     �����ˤϤ��ʤ��Ϥ��Ǥ���
     ����Ū�ˤϡ����񤭤Ǥ�
     ��������sync���פ��Ƥ���Τ� */
  BUG_ON(!buffer_mapped(&mapped_bh));

  /* �����˰��ä������ǽ�����פ��⤫�Фʤ�����
     �Ȥꤢ����Ϣ³��������å����Ƥ��� */
  if( unlikely(bio!=NULL && (mapped_bh.b_blocknr != ((*last_block_in_bio)+1)) ) )
    {
      struct reservoir_operations *rs_ops = RS_SB(inode->i_sb)->rs_ops;

      if( likely(rs_ops->set_bio_callback!=NULL) )
	{
	  struct bio_reservoir *reservoir = inode->i_reservoir;

	  /* bio��ž����λ�����Ȥ��Υ�����Хå��ؿ���Ͽ���Ƥ��� */
	  rs_ops->set_bio_callback(bio, inode->i_sb, WRITE, atomic_read(&reservoir->rt_count));
	}
 
      BUG_ON(bio->bi_end_io==NULL);
      submit_rt_bio(WRITE, inode->i_sb, bio);
      bio = NULL;
    }

  /* bio�����ݤ���Ƥ��ʤ��ä���������� */
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

	  /* bio��ž����λ�����Ȥ��Υ�����Хå��ؿ���Ͽ���Ƥ��� */
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
  /* next�ϡ������������Υ֥�å���Ƭindex���֤���
   �����ͤϥ��饹�����饤����Ȥˤ��碌�Ƥ����� */

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
      /* ��줿page���ʤ��ä���� */
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
	      /* buffer�γ�����Ƥ��Ƥ��ʤ�page��
		 �ߤĤ����ʳ��ǽ�������� */
	      done = 1;
	      start = page->index;
	      unlock_page(page);
	      break;
	    }

	  /* Ϣ³��������å����� */
	  if(i!=0)
	    {
	      if( (prev_index+1) != page->index 
		  || !(page->index % rs_block_size) ) /* Ϣ³���Ƥ��Ƥ⡢������ޤ������饢���� */
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

	  /* ���Ǥ�BIO�������i/o scheduler���ϤäƤ����顢
	     �����ĤϤ������褦 */
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
	      /* �̾�ϴ�Ϣ�Υ��顼���� */
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
	      /* RT�ϤΥ��顼���� */
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

		  /* bio��ž����λ�����Ȥ��Υ�����Хå��ؿ���Ͽ���Ƥ��� */
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

	      /* bio��ž����λ�����Ȥ��Υ�����Хå��ؿ���Ͽ���Ƥ��� */
	      rs_ops->set_bio_callback(bio, sb, WRITE, atomic_read(&reservoir->rt_count));
	    }
	  BUG_ON(bio->bi_end_io==NULL);

	  submit_rt_bio(WRITE, sb, bio);
	}
    }

  /* next�򥯥饹�����饤����Ȥˤ��碌�Ƥ��� */
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

  /* �����ޤǥ��饹��ñ�̤Ǥ����񤫤ʤ��Τǡ�
     ����ʲ��ΰ��������Ƥ⺤��ޤ� */
  if( unlikely(length % rs_block_size) )
    {
      printk("%s-%d: Invalid Call.\n", __PRETTY_FUNCTION__, __LINE__);
      return 0;
    }

  /* start�򥯥饹�����饤����Ȥˤ��碌�� */
  start = (( start + rs_block_size - 1 ) / rs_block_size) * rs_block_size;

  /* pagevec�ν���� */
  pagevec_init(&pvec, 0);

  while(length >= rs_block_size && !done)
    {
      struct bio *bio = NULL;
      unsigned long prev_start = start;
      int i = 0;

      nr_pages = pagevec_lookup_tag(&pvec, mapping, &start,
				    PAGECACHE_TAG_DIRTY, rs_block_size);

      /* ���饹�����Τ����ʤ��ä��Ȥ��ȡ�
       ��줿�ڡ�����Ϣ³�Ǥʤ��ä����Ͻ����򤷤ʤ�*/
      if(nr_pages!=rs_block_size
	 || pvec.pages[nr_pages-1]->index != (prev_start + rs_block_size -1) )
	{
	  done = 1;
	  goto PAGE_RELEASE;
	}

      /* ��¸�֥�å��ν񤭹��ߤϤ��δؿ��λŻ��ǤϤʤ��Τ����Ф���
	 �༡ reservoir_submit_pages() �ǽ�������Ƥ���Ϥ���
	 ���⤽�⤳�������뤳�Ȥ����ꤷ�Ƥ��ʤ��Τ�
	 BUG_ON �����ˤ��Ƥ⤤�����⤷��ʤ� */
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

	  /* BIO�γ��� */
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


      /* ž�������ν���ä�page�Ϥ����˾ä��Ƥ��ޤ� */
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
     
	  /* sync�դ���inode��񤭽Ф� */
	  /* TODO: sb->s_ops->write_inode() ��
	     ��ʬ���ɤ������׸�Ƥ */
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

  /* �ǥХ������Τδ��������񤭽Ф� */
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

  /* �ɤ��ˤ��°���Ƥ��ʤ�inode�˸ƤФ��ΤϤ������� */
  BUG_ON(reservoir==NULL);

  list_del_init(&inode->i_reservoir_list);
  reservoir->active_inodes--;
  inode->i_reservoir = NULL;

  /* RT��Ͽ���inode�ν񤭽Ф�������α���뤿�ᡢ
     ���ä���reservoir���ȼ��ꥹ�Ȥˤ��Ĥ� */
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
	  /* ��°����RT�ե����뤬ï�⤤�ʤ��ʤä����ν����򤪤��ʤ� */
	  /* ���ߡ����Ф�����submit���롣���ߡ��ˤ��������FAT��ǲ������� */

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

	  /* ���塼�ο�����ɸ����᤹ */
	  reservoir->max_length = RS_SB(sb)->rs_ops->get_max_bios(sb);
	}

      atomic_dec(&reservoir->rt_count);

      if( atomic_dec_return(&(RS_SB(sb)->file_groups[reservoir->file_group_idx].rt_files)) == 0 )
	{
	  //printk("[%s] Last File closed (%d)...", __FUNCTION__, reservoir->file_group_idx);

	  //int i = 0;

	  // printk("%s: Last Operation - 2\n", __FUNCTION__);
	  
	  /* ;ʬ�ʳ��ݤ֤��dummy bio��� */

	  if( RS_SB(sb)->rs_ops->prepare_end_rt_writing!=NULL )
	    {
	      /* �Ǹ�Υե�����ʤ顢prepare_end_rt_write�᥽�åɤ�Ƥ� */
	      RS_SB(sb)->rs_ops->prepare_end_rt_writing(sb, reservoir->file_group_idx);	      
	    }

	  /* �������󥷥��write�ե饰�򲼤��� */
	  clear_bit(RT_SEQ, &RS_SB(sb)->rt_flags);

	  /* reservoir�ο������Ȥˤ�ɤ�
	   ����Ū�ˡ��̾�Ϥ�reservoir��ͳ�ˤ���ʤ�
	   ���뤫�⤷��ʤ����������Ǥ���ɬ�פʤΤ�
	   �����ȥ����Ȥ��Ƥ��� */
	  //for(i=0; i<MAX_RESERVOIRS; i++)
	  //  {
	  //    RS_SB(sb)->reservoir[i].max_length = 1;
	  //  }

	  /* i/o scheduler �˶����񤭤������� */
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

  /* RT��Ͽ��Ǥʤ����Ȥ��ǧ����inode�����Ƥ�񤭽Ф� */
  if(atomic_read(&(RS_SB(sb)->file_groups[reservoir->file_group_idx].rt_files))==0)
    {
      if(rt)
	{
	  if( RS_SB(sb)->rs_ops->end_rt_writing!=NULL )
	    {
	      /* �Ǹ�Υե�����ʤ顢end_rt_write�᥽�åɤ�Ƥ� */
	      RS_SB(sb)->rs_ops->end_rt_writing(sb);
	    }
	}
      
      /* ����ޤǴؤ�äƤ�����inode��񤭽Ф� */
      reservoir_sync_and_clear_inodes(sb);
    }
      
  return ret;
}

static int reservoir_add_inode_member(struct bio_reservoir *reservoir,
				      struct inode *inode, int rt)
{
  struct super_block *sb = reservoir->sb;

  /* ���Ǥ�suspend������Ƥ�����ϡ���������ȴ�� */
  if(test_and_clear_bit(RS_SUSPENDED, &inode->i_rsrvr_flags))
    {
      mutex_lock(&RS_SB(sb)->rt_dirty_inodes_lock);
      list_del_init(&inode->i_reservoir_list);
      mutex_unlock(&RS_SB(sb)->rt_dirty_inodes_lock);

      atomic_dec(&inode->i_count);
    }

  /* ���Ǥˤɤ����˽�°���Ƥ���ʤ顢��������ȴ�� */
  if(inode->i_reservoir!=NULL)
    {
      reservoir_remove_inode_member(inode, rt);
    }

  /* inode����Ͽ���� */  
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

	  /* reservoir�ο��������ꤹ�롣
	   �ۤȤ�ɤξ�礷�ʤ��Ƥ⤤���Ȼפ�������������ʳ��Ǥ����ꡣ */
	  for(i=0; i<MAX_RESERVOIRS; i++)
	    {
	      RS_SB(sb)->file_groups[reservoir->file_group_idx]
		.reservoir[i].max_length
		= RS_SB(sb)->rs_ops->get_max_bios(sb);
	    }

	}

      /* �ǥХ�����Ǻǽ��RT��Ͽ�����ǧ */
      if( atomic_inc_return(&(RS_SB(sb)->rt_total_files)) == 1 )
	{
	  if( likely( (RS_SB(sb)->rs_ops->begin_rt_writing!=NULL) ) )
	    {
	      /* �ǽ����Ͽ�ʤ顢begin_rt_write�᥽�åɤ�Ƥ� */
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

  /* ���ߡ��ѤΥ��ɥ쥹���Ѱդ��� */
  if( (RS_SB(sb)->rs_ops!=NULL)
      && (RS_SB(sb)->rs_ops->get_addr_for_dummy_write!=NULL) )
    {
      dummy_addr = RS_SB(sb)->rs_ops->get_addr_for_dummy_write(sb);
    }

  /* �Ǹ�˥���ȥ������ɬ�פ����뤫Ĵ�١�
     ɬ�פǤ���н񤭹���*/
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

      /* 1�ڡ����˼��ޤ�ʤ���������ڡ����������Ƥ� */
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

  /* �ޤ��ʤˤ⤷�Ƥʤ��ե�����ˤ�
     ��뤳�Ȥ��ʤ��Ϥ��ʤΤ��֤� */
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
      /* �������Ф���񤭹��ߤ�
	 �ɤ����ʤ��ä��餷���Τ�
	 �ʤˤ⤻�����֤롣*/
      goto FIN;
    }

  /* PCIDRCT�ξ�硢�Ǹ��page������ */
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

  /* �֥�å�����������Τ�ɬ�פʥڡ�������׻����� */
  /* ���ߡ���page���롣�̾�Ϥ�PT�����Ƥ��Ѥ��� */
  for(i=(last_index+1) ; (i%rs_block_size); i++)
    {
      page = __grab_cache_page(mapping, i);
      if( unlikely(!page) )
	{
	  printk("%s-%d: Getting Page Failed.\n",
		 __PRETTY_FUNCTION__, __LINE__);
	  break;
	}

      /* sync��ʣ����ƤФ줿�Ȥ��ʤɡ�
	 Uptodate�ʤ�Τ��Ĥ��Ȥ������� */
      if(PageUptodate(page))
	{
	  goto PG_UPTODATE;
	}

      /* drct�ξ��ϥ��ߡ�ž�������Ƥ����� */
      if(drct)
	{
	  char *kaddr = NULL;
	  struct drct_page *drct = NULL;
	  unsigned long dummy_addr = 0;

	  /* ���ߡ��ѤΥ��ɥ쥹���Ѱդ��� */
	  if( (RS_SB(sb)->rs_ops!=NULL)
	  && (RS_SB(sb)->rs_ops->get_addr_for_dummy_write!=NULL) )
	    {
	      dummy_addr = RS_SB(sb)->rs_ops->get_addr_for_dummy_write(sb);
	    }

	  kaddr = kmap(page);
	  drct = (struct drct_page *)kaddr;

	  /* ���ߡ��ΰ��ä��� */
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

      /* Page��Dirty�ˤ��� */
      reservoir_set_page_dirty(page);

      /* page�θ���� */
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

  /* �̾�ե���������Ф� i/o scheduler�ˤϥ��饹��ñ�̤�
     bio���Ϥ뤳�Ȥ��ݾڤ���ˤϡ��ե������close��sync��
     �����ߥ󥰤ǥե����������򥯥饹��ñ�̤�����ɬ�פ����롣
     ���������������Ǥϡ�bio�˽�°�����Ϣ��page�����饹��������
     �ޤ����Ǥ��ʤ��פȤ������Ȥ����ݾڤ��ʤ��Τǡ�
     �ե�������������ϤȤꤢ����̵�������Ƥ����� */

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

  /* READONLY�Ǥʤ���Ĵ������ */
  if( (filp->f_flags & O_ACCMODE)==O_RDONLY )
    {
      printk("%s-%d : Invalid Call.\n", __FUNCTION__, __LINE__);
      /* READONLY�ʤ�ʤˤ⤷�ʤ� */
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

  /* READONLY�Ǥʤ���Ĵ������ */
  if( (filp->f_flags & O_ACCMODE)==O_RDONLY )
    {
      printk("%s-%d : Invalid Call.\n", __FUNCTION__, __LINE__);
      /* READONLY�ʤ�ʤˤ⤷�ʤ� */
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

  /* ���˽�°���Ƥ���ΤȤ��ʤ�resrvoir��
     ��Ͽ���褦�Ȥ��Ƥ�����Ͻ��������Ф� */
  if(&RS_SB(sb)->file_groups[group_idx].reservoir[reservoir_id]==inode->i_reservoir)
    {
      goto UNLOCK_RT_FIN;
    }

  /* ��°����reservoir�򥻥åȤ��ʤ��� */
  /* remove_inode_menber���REC��λ�ȴ��㤤����inode��flush�򤷤Ƥ��ޤ�ʤ��褦��
     ������Ū�˥�����Ȥ򤢤��� */
  prev_group = inode->i_reservoir->file_group_idx;
  atomic_inc(&(RS_SB(sb)->file_groups[prev_group].rt_files));

  reservoir_remove_inode_member(inode, rt);
  reservoir_add_inode_member(&RS_SB(sb)->file_groups[group_idx].reservoir[reservoir_id],
			     inode, rt);

  /* ����Ū�ˤ����Ƥ���������Ȥ򤪤� */
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

  /* READONLY�Ǥʤ���Ĵ������ */
  if( (filp->f_flags & O_ACCMODE)==O_RDONLY )
    {
      printk("%s-%d : Invalid Call.\n", __FUNCTION__, __LINE__);
      /* READONLY�ʤ�ʤˤ⤷�ʤ� */
      ret = -EINVAL;
      goto UNLOCK_FIN;
    }

  /* �ե�����ID��inode�˵�Ͽ���� */
  inode->i_file_id = id->file_id;

  /* ������ID��inode�˵�Ͽ���� */
  inode->i_notify_id = id->notify_id;
  
 UNLOCK_FIN:

  mutex_unlock(&inode->i_mutex);

  return ret;
  
}

int reservoir_file_open(struct inode *inode, struct file *filp)
{
  struct super_block *sb = inode->i_sb;
  int rt = (filp->f_flags & O_REALTIME) ? 1 : 0;   /* rt���ɤ�����ե饰�򸫤Ƴ�ǧ */
  int ret = 0;
  unsigned char *iname = filp->f_dentry->d_iname;

  /* inode��mutex�򰮤� */
  mutex_lock(&inode->i_mutex);

  //printk( "%s: Open %s\n", __FUNCTION__, iname);
  
  /* PCIDRCT��RT�ȥ��åȤǻȤ��ޤ��礦 */
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

  /* READONLY�Ǥʤ���Ĵ������ */
  if( (filp->f_flags & O_ACCMODE)==O_RDONLY )
    {
      /* READONLY�ʤ�ʤˤ⤷�ʤ� */
      goto DRCT_CHECK_FIN;
    }

  /* 2�Űʾ��open�򤵤�Ƥ����� */
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
      /* ���Ƥ�open�������ä���� */
      if(inode->i_reservoir == NULL)
	{
	  /* �ե������ǥե���Ȥ�reservoir�˽�°������ */
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
	  /* RS_RT �ե饰��On */
	  set_bit(RS_RT, &inode->i_rsrvr_flags);
	  inode->i_rsrvr_rt_count++;
  }

  if( filp->f_flags & O_PCIDRCT )
    {
      /* PCI_DIRECT���ɤ�����ǧ���ơ�inode�˥ޡ�����Ĥ��� */
      if(inode->i_rsrvr_drct_count==0)
	{
	  /* �̾�Ϥν񤭹��ߤ��ĤäƤ��ʤ��������å� */
	  /* ���Ū�� RS_RT�ե饰��Off�ˤ��� */
	  clear_bit(RS_RT, &inode->i_rsrvr_flags);
		
	  generic_osync_inode(inode, inode->i_mapping, OSYNC_DATA);
	  invalidate_mapping_pages(inode->i_mapping, 0, ~0UL);

	  set_bit(RS_PCIDRCT, &inode->i_rsrvr_flags);

	  set_bit(RS_RT, &inode->i_rsrvr_flags);
	}

      inode->i_rsrvr_drct_count++;
    }

  /* inode->i_count�ϡ�suspend���Ƥ�
     ��ǽ�������뤫�鿮�ѤǤ��ʤ��Τ衣 */
  inode->i_rsrvr_count++;

  /* MI(��������)�Ǥ���С�RS_MI�ե饰��On */
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
  int rt = (filp->f_flags & O_REALTIME) ? 1 : 0;   /* rt���ɤ�����ե饰�򸫤Ƴ�ǧ */
  int ret = 0;

  //printk("%s: Closing %s\n", __FUNCTION__, filp->f_dentry->d_iname);

  mutex_lock(&inode->i_mutex);

  /* READONLY�Ǥʤ���Ĵ������ */
  if( (filp->f_flags & O_ACCMODE)==O_RDONLY )
    {
      /* READONLY�ʤ�ʤˤ⤷�ʤ� */
      goto UNLOCK_FIN;
    }

  mutex_lock(&RS_SB(sb)->io_serialize);

  /* ȿ�Ǥ��Ƥ��ʤ����񤭤�ȿ�� ��
     ���饹�����Ф�����Ⱦü�ʤ֤�Υڡ��������� */
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
	  /* �Ǹ��close���ä���硢��°���Ƥ���reservoir����ȴ�� */
	  mutex_lock(&RS_SB(inode->i_sb)->rt_files_lock);
	  reservoir_remove_inode_member(inode, rt);
	  mutex_unlock(&RS_SB(inode->i_sb)->rt_files_lock);
	}
    }
  else
    {
      /* ��RT�ե������ɬ�����饹��ñ�̤�
	 �񤭽Ф��褦�ˤ������ɬ�פ�����
	 ���ߤϤ����Ϥ��Ƥ��ʤ��Τǡ�
	 �ե�������������Ϥ��ʤ����Ȥˤ���
	 �ʴؿ�����Ȥ�̵�������Ƥ���� */
      reservoir_pad_file_tail(inode);

      /*******************************************************
       �ʲ��ν��������̤��ʤ����Ȥ˷��ޤ�����
       reservoir_writepages()�ǡ����饹����ã���Ƥ��ʤ��ǡ�����
       �񤭽Ф�����Ĥ��Ƥ⡢P2����Ǥ�
       �礭�ʥѥե����ޥ󥹤κ��ϤǤʤ���Ƚ�Ǥ��ޤ�����
       �ҤȤĤΥڡ����ν񤭽Ф���ʣ���󤪤��ʤ��뤳�Ȥ�
       Read-Modify-Write����ȯ������֤�
       ȯ�������ǽ��������Ǥ��ޤ��󤬡�
       ������ʣ������ŷ��ˤ����ơ�ñ��ʤ�꤫�����뤳�Ȥˤ��ޤ���
       �ʤ�������Ū��SD�����ɤؤ�RUñ�̽񤭤�Reservoir Filesystem��ͳ��
       �����ʤ����Ȥ�������ˬ�줿���ϡ������ȡ�reservoir_writepages()�ν�����
       ���褵����ɬ�פ�����Ǥ��礦��
      *******************************************************/
      /* �ե����������ޤǽ񤭽Ф� */
      /* TODO: RT��ï�⤤�ʤ��Ȥ���i/o scheduler��á����������
	 osync_inode������ԤäƤ��ޤ��������ʤ�Ȥ����ͤС�
	 ��ɡ�filemap_fdatawait()��wait���Ƥ���ľ����
	 i/o scheduler��á�������ʤ������� */

      //filemap_fdatawrite(inode->i_mapping);

      /*******************************************************/


	  /* �������󹹿������ϡ����ν��֤ʤɤ�������FS�˰�¸����Τǡ�
	   * �����ǤϹԤʤ鷺��������FS��Ǥ����褦���ѹ������� */
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

  /* PCI_DIRECT�Υ����󥿤򤵤�������ˤʤä���ե饰�򲼤��� */
  if( filp->f_flags & O_PCIDRCT )
    {
      inode->i_rsrvr_drct_count--;
      
      if(inode->i_rsrvr_drct_count==0)
	{
	  /* Direct�Ϥ�page���ĤäƤ����������ä� */
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

	/* �ե����륰�롼�פ������ƤӽФ� */
	ret = reservoir_set_group(inode, filp, &ids);
	break;
      }
    case RSFS_STRETCH_QUEUE:
      {
	/* reservoir���塼�ο������Ѥ��� */
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

	/* �ե�����ID�������ƤӽФ� */
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

  /* RT�ξ��ϡ����Ƥ�RT�ե�����򴬤�ź���ˤ���ư�� */

  /* ������񤭹���Ǥ����Ĥ����ʤ��褦�˥֥�å����� */
  mutex_lock(&RS_SB(sb)->io_serialize);

  /* reservoir���������ơ�
     �Ǹ��bio�򶡵뤹���Τν�����sync��¹Ԥ����� */
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
	  
	  /* ���ߥ����ƥ��֤�RT�ե��������Ƥ˴ؤ���
	     ���Ǥ����֤���ޤäƤ������Ǥ�񤭽Ф� */
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

	  /* Reservoir�����Ƥ����ƽ񤭽Ф� */
	  ret_val = reservoir_rt_flush_reservoir(reservoir, RS_SB(sb)->rs_ops);
	  if(ret_val)
	    {
	      ret = ret_val;
	    }
	}

      /* i/o scheduler�ؤζ���sync�Ȥ��θ������λ�Ԥ� */
      reservoir_sync_data(sb, group);
    }

  /* �������󥷥��write�ե饰�򲼤��� */
  clear_bit(RT_SEQ, &RS_SB(sb)->rt_flags);

  mutex_unlock(&RS_SB(sb)->io_serialize);

  /* ��������ν񤭽Ф� */
  if(sb->s_op->write_super)
    sb->s_op->write_super(sb);
  
  /* TODO: i/o scheduler���Ф��ƶ����񤭽Ф���ؼ����� */
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

      /* �񤭹��������ξ���ͤ���bytes��ݤ�� */
      bytes = min(bytes, iov_iter_count(&iter));

      status = a_ops->write_begin(filp, mapping, pos, bytes, flags, &page, &fsdata);
      if(unlikely(status))
	{
	  printk("%s-%d: Unexpected Error.\n", __FUNCTION__, __LINE__);
	  break;
	}

      /* ��¸�Υ֥�å����Ф���񤭹��ߤ�ȯ��������������å� */
      if(page_has_buffers(page))
	{
	  overwrite = 1;
	}

      pagefault_disable();
      copied = reservoir_filemap_copy_from_user(page, &iter, offset, bytes, drct);
      pagefault_enable();

      flush_dcache_page(page);

      status = a_ops->write_end(filp, mapping, pos, bytes, copied, page, fsdata);

      /* �ɤ����ǥ��顼�������äƤ�����롼�פ�ȴ���� */
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

      /* �֥�å��������ڡ����ο��ޤǽ񤭹��ߤ��ä���� */
      if( ( (index+1) % rs_block_size == 0 )
	  && ( (offset+bytes) == PAGE_CACHE_SIZE) )
	{
	  if(overwrite)
	    {
	      unsigned long next = 0; /* �����ǤϻȤ�ʤ�������������ü�����롣 */
	      struct writeback_control wbc
		= {.bdi=NULL, .sync_mode=WB_SYNC_NONE, .older_than_this=NULL,
		   .nr_to_write=0, .nonblocking=1, .range_cyclic=1,};
	      
	      reservoir_submit_pages(inode, (index + 1 - rs_block_size),
				     rs_block_size, &next, &wbc);
	      overwrite = 0;
	    }
	  else
	    {
	      /* ���饹����Ƭ����ڡ����򤵤�ä�reservoir������Ƥ椯 */
	      status = reservoir_submit_unasigned_pages(inode,
							(index + 1 - rs_block_size),
							rs_block_size);
	    }
	}

      balance_dirty_pages_ratelimited(mapping);
      cond_resched();
    }
  while(iov_iter_count(&iter));

  /* �ե�����ݥ��󥿤ι��� */
  *ppos = pos;

  /* overwrite��ȯ�����Ƥ����ʤ顢�񤭽Ф�������Ԥ�
   (asigned�ʤ�Τ������Ǥ��Ф�����)*/
  /*
  if(overwrite)
    {
      unsigned long next = 0; // �����ǤϻȤ�ʤ�������������ü�����롣
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

  /* ��RT��reservoir��ͳ�ˤ��ƥ֥�å��񤭹��ߤ����٤�
     ������ˡ��ͤ����뤬���������Ǥ���RT��generic��ͳ�ˤ��롣
     ��RT�ν񤭹��߹�®���ϡ�i/o scheduler�ȡ�
     reservoir_submit_pages() �� i/o scheduler��
     ���饹��������ޤ����ʤ�bio���Ϥ����Ȥ�
     �ݾڤ��Ƽ¸����롣����ʾ���礭���ؤΥޡ����ϡ�
     i/o scheduler�ؤǴ��Ԥ��롣 */
  if(!rt)
    {
      return generic_file_aio_write(iocb, iov, nr_segs, pos);
    }

  mutex_lock(&inode->i_mutex);

  for(seg = 0; seg < nr_segs; seg++)
    {
      const struct iovec *iv = &iov[seg]; /* Discard static status ! */

      if(unlikely( iv->iov_len <= 0  /* ����������ξ�� */
		   || ( drct && (iv->iov_len & (4-1)) && (seg!=(nr_segs-1)) ) /* �Ǹ�Ǥʤ��Τ�4byteñ�̤Ǥʤ� */
		   ) )
	{
	  printk("%s-%d: Invalid Transfer Entries. (%u)\n",
		 __PRETTY_FUNCTION__, __LINE__, iv->iov_len);
	  err = -EINVAL;
	  goto OUT;
	}

      /* seg�����ꤹ��ž�����������פ��Ƥ椯 */
      count += iv->iov_len;
    }

  /* According to generic_file_aio_write ... */
  vfs_check_frozen(sb, SB_FREEZE_WRITE);
  current->backing_dev_info = mapping->backing_dev_info;


  /* �ե����륵������¤Υ����å��ʤɡ�
     �ե����륷���ƥ��ͭ�Υ����å��򤪤��ʤ� */
  if( likely( RS_SB(sb)->rs_ops->write_check!=NULL ) )
    {
      if( unlikely (RS_SB(sb)->rs_ops->write_check(iocb, iov, nr_segs, pos, ppos,  &count, &written)) )
	{
	  printk("%s-%d : Write Check Error.\n", __FUNCTION__, __LINE__);
	  goto OUT;
	}
    }

  /* �񤭹����Τ��ʤ��ʤä����Ϥ�����ȴ���롣
     p2fat�ξ�硢��Ƭ���饹�������촹�����ˤ����ʤ� */
  if( unlikely(count==0) )
    {
      goto OUT;
    }

  /* �ޤ��񤭽���äƤ��ʤ���ʬ�˴ؤ���
     �ե���������ؤν񤭹��ߤ��褿�Ȥ�����sync */
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

  /* �񤭹���Ǥ��������inode����������ʤ��Τǡ�
     ���ޤΤȤ������ϥ����ȥ����Ȥ��Ƥ��� */
  //file_update_time(file);

  mutex_lock(&RS_SB(sb)->io_serialize);
  /* Writeư������Τ�ƤӽФ� */
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
  /* ��¸�γ�����ƺѤ�page����Ĵ�٤� */
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

  /* ���줫�鰷����ʬ�ϳ�����ƺѤߤ��ɤ��� */
  if(page_index >= asigned_index)
    {
      /* �ޤ�������Ƥ��Ƥ��ʤ���� */

      /* ���Υڡ����򿨤�Τ����Ƥʤ顢
       ���������Uptodate�ˤ��Ƥ�����*/
      if(!PageUptodate(page))
	{
	  if(test_bit(RS_PCIDRCT, &inode->i_rsrvr_flags))
	    {
	      /* �ڡ��������椫��񤭹��⤦�Ȥ��Ƥ���Τˡ�
		 page���鸫�ʤΤϤ��������Ǥ���͡�
		 ����ϡ�sync�����Τ˥��饤����Ȥ��餺
		 �񤳤��Ȥ����Ȥ���ȯ�����롣*/
	      if( unlikely(from & ~PAGE_CACHE_MASK) )
		{
		  printk("%s-%d: Bad Write Position.\n", __FUNCTION__, __LINE__);
		  printk("  filp=%lld\n", filp->f_pos);
		  return -EINVAL;
		}

	      /* DirectPage�Τ���ν���� */
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

  /* ����������ϡ���¸�֥�å��ν������ä���� */

  /* according to __block_prepare_write() ... */
  BUG_ON(!PageLocked(page));
  BUG_ON(from > PAGE_CACHE_SIZE);
  BUG_ON(to > PAGE_CACHE_SIZE);
  BUG_ON(from > to);

  /* ��¸page�ؤ��ɵ��ϥ��ݡ��Ȥ��ʤ� */
  // BUG_ON(page_has_buffers(page));

  if(test_bit(RS_PCIDRCT, &inode->i_rsrvr_flags))
    {
      /* Sync��˥��饤����Ȥ�
	 ��餺�񤭽Ф����Ȥ����к� */
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
	  /* DirectPage�Τ���ν���� */
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
      /* �ڡ������Τ˽񤯤櫓�Ǥʤ��Ȥ��ϡ�
	 page���Τβ��Ϥ������ɤ߽Ф�*/
      if( !PageUptodate(page) && (from!=0 || to!=PAGE_CACHE_SIZE) )
	{
	  block_read_full_page(page, rs_ops->get_block);
	  if(!PageError(page))
	    {
	      struct buffer_head *head_bh = page_buffers(page);
	      struct buffer_head *wait_bh = head_bh;

	      /* �ɤ߹��ߤδ�λ���Ԥ� */
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
	      /* ��¸�֥�å��ΤϤ��ʤΤǡ�
		 �Ǹ�ΰ����ϥ���ǸƤӽФ���
		 ���������ޤ����˽񤤤��ǡ�����
		 reservoir����Ǥ����ǽ���⤢��Τǡ�
		 ɬ��������������Ȥϸ¤�ʤ� */
	      
	      err = rs_ops->get_block(inode, block, bh, 0);
	      if(err)
		{
		  /* reservoir������ä��ȹͤ����롣
		     �Ȥꤢ�������֤��Ƥ�����*/
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

  /* ��¸�֥�å��ؤξ�񤭤ǥڡ���������Ⱦü�ʾ�硢
     �Ǹ����ʬ����ߡ���᤹��ʤ��Τ���¨�񤭤��뤿��� */
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
  /* �⤷����Ū�ˡ���RT�ϤΤ�Τ�
     reservoir��ͳ�ǽ񤭽Ф��褦�ˤ�����ϡ�
     ɸ��ν񤭽Ф��Υ������󥹤���ˡ�
     reservoir�򤵤館������������ɬ�פ����롣
     �����ǡ����������̾�ϤΥե������
     page��reservoir�˰�ư������Ф褤 */

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

  /* ��RT�Ϥǽ񤭹������Τ�񤭹��ࡣ
     ��RT�Ϥǽ񤤤��顢page_has_buffers�ʤ��Ȥ������� */

  /* sync�Ǥʤ����ϡ�wbc�Υ�󥸤�
     i_size����׻��Ǥ���֥�å����饤����Ȥ˴ݤ�롣
     �����������close����sync��ɬ���ƤФ�뤳�Ȥ����� */

#if defined(CONFIG_DELAYPROC)
  if(wbc->for_delayproc)
	  goto SUBMIT_PAGES;
#endif /* CONFIG_DELAYPROC */
 
  /**************************************
   // �����ν����򥳥��ȥ����Ȥ�����ͳ��
   // reservoir_file_release() ��Υ����Ȥ��ɤ�Ǥ�������

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
      /* ���ߡ����饹���ξ��ϡ�page�����ä���ñ����� */

      struct bio_vec *bvec = &bio->bi_io_vec[0];

      while(bvec->bv_page!=NULL)
	{
	  end_page_writeback(bvec->bv_page);
	  put_page(bvec->bv_page);
	  bvec++;
	}
    }
  /* DIRECT�ΤȤ��ϡ����ä���page��
     �ݽ�����Ƥ���Τǡ������ä�����ᡣ */
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

  /* Direct�Ϥξ�硢bio_put�θƽФ�read�δؿ���ǹԤ����Ȥˤ��롣
     read�δ�λ�Ԥ���bio���Ф��� wait_on_bit() �Ǥ����ʤ����� */
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
  int rt = (filp->f_flags & O_REALTIME) ? 1 : 0;   /* rt���ɤ�����ե饰�򸫤Ƴ�ǧ */
  int ret = 0;

  if(rt)
    {
      ret = reservoir_rt_fsync(inode);
    }
  else
    {
	  struct super_block *sb = inode->i_sb;
		
       /* ��RT�ե������ɬ�����饹��ñ�̤�
	 �񤭽Ф��褦�ˤ������ɬ�פ�����
	 ���ߤϤ����Ϥ��Ƥ��ʤ��Τǡ�
	 �ե�������������Ϥ��ʤ����Ȥˤ���
	 �ʴؿ�����Ȥ�̵�������Ƥ���� */
      reservoir_pad_file_tail(inode);
	  
      /* �̾��fsync��ƤӽФ� */
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
  int rt = (filp->f_flags & O_REALTIME) ? 1 : 0;   /* rt���ɤ�����ե饰�򸫤Ƴ�ǧ */
  loff_t ret = 0;

  if( !rt || ( (filp->f_flags & O_ACCMODE)==O_RDONLY ) )
    {
      /* ��RT�����ReadOnly�ξ����̾��llseek��ƤӽФ� */
      ret = generic_file_llseek(filp, offset, whence);
    }
  else
    {
      struct inode *inode = filp->f_mapping->host; 

      /* TODO: PCI Direct�Ǥʤ����Ϥ⤦���������
	 SEEK�ξ�������Ǥ���Ϥ���
	 �ʲ����ɤ߽Ф��򤹤뤳�Ȥ������ */

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

      /* RT�Ǥϡ��ե����������������SEEK�ϵ����ʤ����Ȥˤ��� */
      if(offset > inode->i_size)
	{
	  printk("%s-%d: Seek beyond the file end.\n",
		 __PRETTY_FUNCTION__, __LINE__);
	  ret = -EINVAL;
	  goto FIN;
	}

      /* �ե����륵������¤�ۤ��Ƥ��ʤ��������å� */
      if(offset<0 || offset > inode->i_sb->s_maxbytes)
	{
	  printk("%s-%d: Invalid Seek.\n",
		 __PRETTY_FUNCTION__, __LINE__);
	  ret = -EINVAL;
	  goto FIN;
	}
      
      /* Seek��f_pos�ΰ�ư��ȼ��ʤ����Ϥʤˤ⤷�ʤ� */
      if(filp->f_pos != offset)
	{
	  struct super_block *sb = inode->i_sb;

	  /* page�������餷���񤭹��ޤ��ʤ�����˹ͤ�������
	     ���ؤʤΤǥ����å����ʤ����Ȥˤ��롣
	     write_check���Ĵ�٤뤳�Ȥˡ�*/
	  /**
	  if(offset & (PAGE_CACHE_SIZE-1))
	    {
	      printk("%s-%d: Invalid File Seek. (%s@%lld\n",
		     __PRETTY_FUNCTION__, __LINE__,
		     filp->f_dentry->d_iname, offset);
	      goto FIN;
	    }
	  **/

	  /* �ޤ�commit���Ƥ��ʤ��֤��񤭽Ф��Ƥ��ޤ���
	     �񤭽Ф�������¸Page�ؤκ��Խ�������褦�ˤ����
	     ��ñ�ˤʤ��ǽ���⤢�뤬��Kernel2.4�Ȥ�
	     �ߴ����Τ���ˤ������롣 */
	  mutex_lock(&RS_SB(sb)->io_serialize);
	  reservoir_pad_and_commit_tail(inode);
	  mutex_unlock(&RS_SB(sb)->io_serialize);

	  /* �ե�����Υݥ�������ư���� */
	  filp->f_pos = offset;
	  filp->f_version = 0;
	}

    FIN:
      mutex_unlock(&inode->i_mutex);
    }
 
  return ret;
} 

/******************* Direct�� Read �δؿ� *******************/

static int submit_and_list_read_bio(struct bio *bio, struct super_block *sb,
				    struct bio **last_bio)
{
  struct reservoir_operations *rs_ops = RS_SB(sb)->rs_ops;

  /* ����BIO�ؤΥݥ��� */
  if(*last_bio)
    (*last_bio)->bi_private2 = (void *)bio;

  *last_bio = bio;

  if( likely(rs_ops->set_bio_callback!=NULL) )
    {
      /* bio��ž����λ�����Ȥ��Υ�����Хå��ؿ���Ͽ���Ƥ��� */
      rs_ops->set_bio_callback(bio, sb, READ, 1);
    }      

  /* i/o scheduler���Ϥ� */
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
  size_t dummy_head =  0; /* bio��Ƭ������٤����ߡ�ž�������� */
  size_t dummy_tail = 0;  /* bio����������٤����ߡ�ž�������� */
  loff_t cluster_offset = 0; /* ž�����ϰ��֤Υ��饹���⥪�ե��å� */
  loff_t sector_offset = 0; /* ž�����ϰ��֤Υ������⥪�ե��å� */
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

  /* �ե�����������ۤ����ɤ߹��⤦�Ȥ��Ƥ����顢
     �ʤˤ⤻�����֤� */
  if( unlikely(pos >= inode->i_size) )
    {
      return 0;
    }

  for(seg = 0; seg < nr_segs; seg++)
    {
      const struct iovec *iv = &iov[seg]; /* Discard static status ! */

      if(unlikely( iv->iov_len <= 0  /* ����������ξ�� */
		   || (iv->iov_len & (2-1)) /* 2byteñ�̤Ǥʤ� */
		   ) )
	{
	  printk("%s-%d: Invalid Transfer Entries. (%u)\n",
		 __PRETTY_FUNCTION__, __LINE__, iv->iov_len);
	  err = -EINVAL;
	  goto OUT;
	}

      /* seg�����ꤹ��ž�����������פ��Ƥ椯 */
      count += iv->iov_len;
    }

  /* iov��ź����ꥻ�åȤ��Ƥ��� */
  seg = 0;

  if( unlikely( (inode->i_size - pos) < count ) )
    {
      /* P2�����ƥ�Υߥɥ�ϡ������쥯��ž���ΤȤ�
	 �ɤ߹��ߥ�������4byte���ܿ��˴ݤ�Ƥ���
	 ��ǽ��������Τǡ�����������
	 �ɤ����ɤ߹������DM��ZION�ʤΤǡ�
	 �������3byte�ʲ��Υ��ߥǡ��������äĤ��Ƥ��Ƥ�
	 ����ˤ��ޤ�ʤ��ΤǤ� */
      if(count - (inode->i_size - pos) > 3)
	{
	  count = inode->i_size - pos;
	  if(count & (2-1))
	    {
	      /* 2byte���ܿ��ˤϤ��Ƥ����ޤ��礦 */
	      count += 1;
	    }
	}

      /* �տ�Ū�˲���ɤ߽Ф�����֤��Ф��Ƥ���
	 ���֤��ͤ�Ĵ�����뤿��� */
      verbous_read = count - (inode->i_size - pos);
    }

  if( unlikely( count==0 ) )
    {
      goto OUT;
    }

  if( likely(rs_ops->get_addr_for_dummy_read!=NULL) )
    dummy_addr = rs_ops->get_addr_for_dummy_read(sb);

  /* ��Ƭ�������ֹ����� */
  iblock = pos >> sb->s_blocksize_bits;
  err = rs_ops->get_block(inode, iblock, &bh, 0);
  if( unlikely(err) )
    {
      printk("%s-%d: Getting Sector Number Failed.\n", __PRETTY_FUNCTION__, __LINE__);
      goto OUT;
    }
  cur_sector = bh.b_blocknr;

  /* �񤭹��߰��֤Υ������⥪�ե��åȤ���� */
  sector_offset = pos & ( sb->s_blocksize - 1 );
  dummy_head = sector_offset;

  /* �񤭹��߰��֤Υ��饹���⥪�ե��åȤ���� */
  cluster_offset = pos & ( cluster_size  - 1 );

  /* ������������ߡ����ǤΥ���������� */
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


  /* �ɤ߽Ф��������������椫��Ϥޤ��硢
     �������ޤǤΥ��ե��åȤ֤����ߡ�ž���Ȥ��ư��� */
  if(dummy_head)
    {
      size_t size_remain = dummy_head;

      /* 512byte�ʾ�ˤʤ뤳�ȤϤʤ�����ǰ�Τ���롼�� */
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

  while(count && !done)  /* ž������ǡ���������¤�BIO��������� */
    {
      size_t transfer_size = 0;
      size_t transfer_done = 0;

    RETRY:

      /* ���Τޤ�bio���ɲä��Ƥ�����Τ�Ĵ�٤� */
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

      /* ���饹�����ž������٤���������׻����롣
	 ���ߤΥե�����ݥ��󥿤��顢�ɤ�����Υǡ����̤�
	 ���ȥ졼�����ʪ��Ū��Ϣ³���Ƥ�����ݾڤ���뤫
	 �׻����Ƥ��뤳�Ȥˤʤ롣
	 �⤷SD���������Ǥ⤳�δؿ������Ѥ������Τʤ顢
	 ������rs_block_size�򥯥饹���������Ȥ���
	 �ȤäƤ���Ȥ���򤢤餿�ᡢ�ե����륷���ƥ��
	 ���Τʥ��饹������Ȥ������ߤ�ɬ�פǤ��롣
	 rs_block_size��ñ�̤�page�ʤΤǡ�512Byte���饹���ʤɤ�ɽ���Ǥ��ʤ�*/
      transfer_size = cluster_size - cluster_offset;
      transfer_size = min(transfer_size, count);

      cluster_offset = 0;

      while(transfer_size) /* ���饹����Υǡ���ž����bio������ */
	{
	  size_t sector_transfer_size = sb->s_blocksize - sector_offset;
	  size_t byte_count = 0;
	  unsigned long seg_walk = seg;
	  int i = 0;
	  size_t sector_transfer_done = 0;

	  sector_offset = 0;

	  /* iov����֤˸��Ƥ��äơ�512byte���������ˤ�
	   �ɤ�����Υ���ȥ����ɬ�פ�������å����롣
	   �ʤ��ʤ顢bio�ˤ�512byteñ�̤��׵ᤷ��
	   ���줿���ʤ����� */
	  for(i=0, seg_walk=seg;
	      (byte_count < sector_transfer_size) && (seg_walk < nr_segs);
	      seg_walk++, i++)
	    {
	      byte_count += ( iov[seg_walk].iov_len - (i==0 ? iv_offset : 0) ); 
	    }

	  byte_count = min(byte_count, transfer_size);

	  /* ��Ƭ�Υ���ȥ꤬��bio�˴������äƤ��륨��ȥ��
	     ���Ǥ��뤫������å� */
	  if( ( bio->bi_vcnt!=0 ) &&
	      ( bio->bi_io_vec[bio->bi_vcnt-1].bv_private
		+ bio->bi_io_vec[bio->bi_vcnt-1].bv_len
		== ( ((unsigned long)iov[seg].iov_base) + iv_offset ) ) )
	    {
	      i--;
	    }

	  /* �Ǹ�˥��ߡ�ž���ѤΥ���ȥ��
	     �����ɬ�פ����뤫�ɤ�����Ƚ�Ǥ��� */
	  if( unlikely( (seg_walk==nr_segs) && (byte_count<sector_transfer_size) ) )
	    {
	      i++;
	      need_dummy = 1;
	    }

	  /* bio���ɲä��Ƥ⥨��ȥ꤬���դ�ʤ���������å� */
	  if( unlikely( ( ( bio->bi_vcnt + i ) > bio->bi_max_vecs) ||
			( bio_max_size < (bio->bi_size + sector_transfer_size) ) ) )
	    {
	      /* ������ϡ�bio��������ʤ��� */
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

	  /* �ºݤ�bio�˥���ȥ���ɲä��� */
	  sector_transfer_size = min(sector_transfer_size, transfer_size);
	  byte_count = min(byte_count, sector_transfer_size);
	  while(byte_count)
	    {
	      unsigned int entry_size = iov[seg].iov_len - iv_offset;
	      unsigned long add_size = 0;

	      /* iov�θ��ߤΥ���ȥ꤫��bio�˲ä���٤������� */
	      add_size = min(entry_size, byte_count);

	      if( ( bio->bi_vcnt!=0 ) &&
		  ( bio->bi_io_vec[bio->bi_vcnt-1].bv_private
		    + bio->bi_io_vec[bio->bi_vcnt-1].bv_len
		    == ( ((unsigned long)iov[seg].iov_base) + iv_offset) ) )
		{
		  /* ���˲ä�������ȥ�ȥޡ�����ǽ�ʾ�� */
		  bio->bi_io_vec[bio->bi_vcnt-1].bv_len += add_size;

		}
	      else
		{
		  /* ����������ȥ��ɲä�ɬ�פʾ�� */
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
		  /* Read�ǤϤ��ޤΤȤ���private�򤤤���ͽ��Ϥʤ� */
		  rs_ops->set_bio_private(bio, NULL, READ, 1);
		}

	      /* ����å���ν����򤷤Ƥ��� */
	      if( unlikely( (((unsigned long)iov[seg].iov_base) + iv_offset) < device_addr) )
		{
/* 		  dma_cache_inv( bus_to_virt(((unsigned long)iov[seg].iov_base) + iv_offset), add_size ); */
		  dma_cache_sync(NULL, bus_to_virt(((unsigned long)iov[seg].iov_base) + iv_offset),
				 add_size, DMA_FROM_DEVICE);
		}

	      bio->bi_size += add_size;

	      iv_offset += add_size;

	      /* ���Υ���ȥ��Ȥ����ä��鼡�Υ���ȥ�� */
	      if( iov[seg].iov_len == iv_offset )
		{
		  seg++;
		  iv_offset = 0;
		}

	      byte_count -= add_size;
	      sector_transfer_done += add_size;
	    }

	  /* ž�����������Х��ȿ��ε�Ͽ */
	  transfer_size -= sector_transfer_done;
	  transfer_done += sector_transfer_done;
	  cur_sector++;

	  /* ����Ū�˥ե�����ݥ��󥿤�ʤ�� */
	  pos += sector_transfer_done;
	}

      count -= transfer_done;
      read_done += transfer_done;

      /* ���ޤˤ�¾�Ԥˤ�פ���� */
      cond_resched();
    }

  /* �����������ǥ��顼�������äƤ����Ȥ���̵�̤ʤ��Ȥ򤷤ʤ��褦�� */
  if(unlikely(err))
  {
	  goto OUT;
  }

  /* �����Υ��ߡ�ž����᤬ɬ�פ��ä���� */
  if(need_dummy && bio)
    {
      /* ����������ȥ��ɲä�ɬ�פʾ�� */
      /* ����������ϥ롼�פǤ���ɬ�פϤʤ���ǰ�Τ��� */
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
	  /* Read�ǤϤ��ޤΤȤ���private�򤤤���ͽ��Ϥʤ� */
	  rs_ops->set_bio_private(bio, NULL, READ, 1);
	}
    }

  /* �Ǹ�ΤҤȤĤޤ� i/o scheduler ���Ϥ� */
  if( likely(bio) )
    {
      submit_and_list_read_bio(bio, sb, &last_bio);
    }

  /* ���Ƥ��Ȥϡ�i/o �ν�λ���Ԥ��ޤ��礦 */
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

  /* �ե�����ݥ��󥿤ΰ�ư��ȿ�Ǥ��� */
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

  /* �ޤ����������ɤ߹��߲�ǽ�ʾ��ʤΤ�������å����� */
  for(seg = 0; seg < nr_segs; seg++)
    {
      const struct iovec *iv = &iov[seg]; /* Discard static status ! */
      /* seg�����ꤹ��ž�����������פ��Ƥ椯 */
      count += iv->iov_len;
    }

  if(count==0)
    {
      return 0;
    }

  mutex_lock(&RS_SB(sb)->io_serialize);

  /* �������ե���������� */
  tail = pos + count;

  /* i_blocks��i_size���԰��פξ��
     �� reservoir�ϤǤ����äƤ������Υե�����ξ�� */
  if( inode->i_blocks != ((inode->i_size + (RS_SB(sb)->rs_block_size*PAGE_CACHE_SIZE -1))
			  & ~((loff_t)RS_SB(sb)->rs_block_size*PAGE_CACHE_SIZE -1)) >> 9 )
    {
      /* �񤭹��ޤ줿��꤬���ꤷ�Ƥ���֥�å�����׻�����
	 ���줫���ɤ߽Ф����Ȥ��Ƥ���Ȥ����ϰ��⤫������å� */
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
      /* Ʊ����DRCT�ǳ�����Ƥ�����ϡ�
	 ReadAhead�ǥ���å����������Ⱥ���Τ����ɤ߶ػ� */
      if(test_bit(RS_PCIDRCT, &inode->i_rsrvr_flags))
	{
	  filp->f_ra.ra_pages = 0;
	}

      ret = generic_file_aio_read(iocb, iov, nr_segs, pos);

      /* Ʊ����DRCT�ǳ�����Ƥ�����ϡ�
	 ����ˤʤ��礬����Τ�cache��ä��Ƥ��� */
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
