#ifndef RESERVOIR_SB_H
#define RESERVOIR_SB_H

#include <linux/list.h>
#include <linux/spinlock.h>
#include <asm/atomic.h>
#include <linux/types.h>
#include <linux/mutex.h>

#include <linux/drct_trans_page.h>

#define MAX_GROUPS        (8)
#define MAX_RESERVOIRS    (8)
#define DEFAULT_GROUP     (0)
#define DEFAULT_RESERVOIR (0)

struct super_block;
struct bio;
struct file;
struct inode;
struct buffer_head;
struct page;
struct iovec;
struct kiocb;

struct reservoir_operations
{
  void (*begin_rt_writing)(struct super_block *);
  void (*prepare_end_rt_writing)(struct super_block *, int);
  void (*end_rt_writing)(struct super_block *);
  int (*get_n_blocks)(struct super_block *, unsigned long, unsigned long *);
  int (*get_block)(struct inode *inode, sector_t iblock, struct buffer_head *bh, int create);
  void (*set_bio_callback)(struct bio *, struct super_block *sb, int rw, int rt);
  void (*alloc_bio_private)(struct bio*, struct super_block *sb, struct inode *, int rw);
  void (*set_bio_private)(struct bio *, struct page *page, int rw, int rt);
  void (*prepare_submit)(struct bio *, struct super_block *sb, int rw);
  void (*wait_on_localfs)(struct super_block *sb, int);
  unsigned long (*get_device_address)(struct super_block *);
  unsigned long (*get_addr_for_dummy_write)(struct super_block *);
  unsigned long (*get_addr_for_dummy_read)(struct super_block *);
  int (*write_check)(struct kiocb *iocb, const struct iovec *iov, unsigned long nr_segs,
		     loff_t pos, loff_t *ppos, size_t *count, ssize_t *written);
  int (*get_max_bios)(struct super_block *sb);
};

/* reservoirに入れる最大のbio数 */
#define RS_MAX_BIOS (32)

struct bio_reservoir
{
  unsigned long reservoir_flags;

  struct bio *bio_head;
  struct bio *bio_tail;

  unsigned long suspended_cls[RS_MAX_BIOS];
  unsigned cls_ptr;

  struct list_head inode_list;
  unsigned long active_inodes;
  atomic_t rt_count;

  unsigned long max_length;
  unsigned long cur_length;

  struct super_block *sb;
  int file_group_idx;

  struct mutex reservoir_lock;
};

struct reservoir_file_group
{
  int file_group;

  struct bio_reservoir reservoir[MAX_RESERVOIRS]; 
  atomic_t rt_files;
};

struct reservoir_sb
{
  unsigned long rt_flags;

  struct reservoir_file_group file_groups[MAX_GROUPS];

  struct list_head rt_dirty_inodes;
  struct mutex rt_dirty_inodes_lock;
  struct mutex rt_files_lock;
  struct mutex io_serialize;
  struct mutex meta_serialize;
  unsigned long  rs_block_size;

  atomic_t rt_total_files;

  struct reservoir_operations *rs_ops; 
};

/* for rt_flags */
enum {RT_SEQ}; // Sequencial Flag 付加用

#endif  /* RESERVOIR_SB_H */
