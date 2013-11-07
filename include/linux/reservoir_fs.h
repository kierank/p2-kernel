#ifndef _RESERVOIR_FS_H_
#define _RESERVOIR_FS_H_

/* REALTIMEファイル用フラグ(openで使用) */
#ifndef O_REALTIME
#define O_REALTIME  02000000
#endif
#ifndef O_PCIDRCT
#define O_PCIDRCT   04000000
#endif

#define RSFS_SET_GROUP     _IOW(0x82, 1, struct reservoir_file_ids)
#define RSFS_STRETCH_QUEUE _IOW(0x82, 2, unsigned long)
#define RSFS_SET_FILEID    _IOW(0x82, 3, struct reservoir_file_id)

struct reservoir_file_ids
{
  int file_group;
  int reservoir_id;
};

struct reservoir_file_id
{
  unsigned long notify_id;
  unsigned long file_id;
};

/* MI(管理情報)ファイル名 */
#define RSFS_MI_NAME_S "lastclip.txt"
#define RSFS_MI_NAME_C "LASTCLIP.TXT"

#ifdef __KERNEL__

#include <linux/fs.h>
#include <linux/bio.h>
#include <linux/drct_trans_page.h>
#include <linux/blkdev.h>
#include <linux/ioctl.h>

/* i/o schedulerが暇になるのを待つ最大時間 */
#define RSFS_IO_WAIT_TIME (2*HZ)
/* ユーザ空間のコールバック関数の処理終了を待つ最大時間 */
#define RSFS_IO_END_WAIT_TIME (10*HZ)

#define RS_SB(sb)  (&(sb->rsrvr_sb))

static inline void submit_rt_bio(int rw, struct super_block *sb, struct bio *bio)
{
  sector_t start_sector = bio->bi_sector;
  int bytes_done = 0;
  struct reservoir_operations *rs_ops = RS_SB(sb)->rs_ops;

  do
    {
      struct bio *cur_bio = bio;

      /* bioのbi_sectorを先頭bioからの
	 バイトオフセットを使って計算する。
	 通常のアプリではbioが複数になることはないので、
	 unlikelyで判定することにする */
      if(unlikely(bytes_done))
	{
	  set_bit(BIO_RW_SLAVE, &cur_bio->bi_rw);
	  cur_bio->bi_sector = start_sector + ( bytes_done / 512 );
	}

      /* 次のbioのためのバイトオフセットを計算 */
      bytes_done += cur_bio->bi_size;

      /* bioに入っている転送命令は
	 512byte単位でないとおかしい */
      BUG_ON(bytes_done % 512);

      /* bioのチェーンを次にたどり、submitする */
      bio = (struct bio *)cur_bio->bi_private2;
      cur_bio->bi_private2 = NULL;

      /* ローカルFSにsubmit直前にやりたいことがあるならやらせる */
      if(rs_ops->prepare_submit!=NULL)
	rs_ops->prepare_submit(cur_bio, sb, rw);

      submit_bio(rw, cur_bio);
    }
  while(bio!=NULL);

  return;
}

static inline void reservoir_sync_data(struct super_block *sb, int fgroup)
{
  struct reservoir_operations *rs_ops = RS_SB(sb)->rs_ops;
  struct request_queue *q = bdev_get_queue(sb->s_bdev);

  /* i/o schedulerに無理矢理吐き出させる */
  if(q->elevator->ops->elevator_force_dispatch_fn!=NULL)
    {
	    q->elevator->ops->elevator_force_dispatch_fn(q, 1);
    }

  /* LocalFSの後処理の終了を待つ */
  if(rs_ops->wait_on_localfs!=NULL)
    {
      rs_ops->wait_on_localfs(sb, fgroup);
    }

  return;
}

/* fs/reservoir.c */
extern int reservoir_file_open(struct inode *inode, struct file *filp);
extern int reservoir_file_release(struct inode *inode, struct file *filp);
extern int reservoir_file_ioctl(struct inode *inode, struct file *filp,
				unsigned int cmd, unsigned long arg);
extern ssize_t reservoir_file_aio_write(struct kiocb *iocb, const struct iovec *iov,
					unsigned long nr_segs, loff_t pos);
extern int reservoir_write_begin(struct file *file, struct address_space *mapping,
				 loff_t pos, unsigned len, unsigned flags,
				 struct page **pagep, void **fsdata,
				 get_block_t *get_block, loff_t *bytes);
extern int reservoir_write_end(struct file *file, struct address_space *mapping,
			       loff_t pos, unsigned len, unsigned copied,
			       struct page *page, void *fsdata);
extern int reservoir_writepages(struct address_space *mapping,
				struct writeback_control *wbc);
extern int reservoir_end_io_write(struct bio *bio, int err);
extern int reservoir_end_io_read(struct bio *bio, int err);
extern struct bio *reservoir_dummy_bio_alloc(struct super_block *sb);
extern int reservoir_fsync(struct file *filp, struct dentry *dent, int datasync);
extern loff_t reservoir_llseek(struct file *filp, loff_t offset, int whence);
extern int reservoir_submit_unasigned_pages(struct inode *inode,
					    unsigned long start, unsigned long length);
extern ssize_t generic_file_pci_direct_read(struct kiocb *iocb, const struct iovec *iov,
					    unsigned long nr_segs, loff_t pos);
extern int reservoir_io_wait(void *word);
extern ssize_t reservoir_file_aio_read(struct kiocb *iocb, const struct iovec *iov,
				       unsigned long nr_segs, loff_t pos);
extern int reservoir_clear_inodes(struct super_block *sb);
extern size_t reservoir_filemap_copy_from_user(struct page *page, struct iov_iter *iter, 
					       unsigned long offset, unsigned bytes, int drct);
#endif /* __KERNEL__ */

#endif /* _RESERVOIR_FS_H_ */
