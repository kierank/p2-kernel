#include <linux/module.h>
#include <linux/slab.h>
#include <linux/compiler.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/aio.h>
#include <linux/capability.h>
#include <linux/kernel_stat.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/mman.h>
#include <linux/pagemap.h>
#include <linux/file.h>
#include <linux/uio.h>
#include <linux/hash.h>
#include <linux/writeback.h>
#include <linux/backing-dev.h>
#include <linux/pagevec.h>
#include <linux/blkdev.h>
#include <linux/security.h>
#include <linux/syscalls.h>
#include <linux/cpuset.h>
#include <linux/hardirq.h> /* for BUG_ON(!in_atomic()) only */
#include <linux/memcontrol.h>

#include <linux/buffer_head.h> /* for generic_osync_inode */

#include <linux/sdfat_fs.h>

#include <asm/mman.h>

struct kiov_iter {
	const struct kvec *iov;
	unsigned long nr_segs;
	size_t iov_offset;
	size_t count;
};

static inline size_t kiov_iter_count(struct kiov_iter *i)
{
	return i->count;
}

static size_t __iovec_copy_from_kernel_inatomic(char *vaddr,
			const struct kvec *iov, size_t base, size_t bytes)
{
	void *ret;
	char *buf = iov->iov_base + base;
	int copy = min(bytes, iov->iov_len - base);

	ret = memcpy(vaddr, buf, copy);
	if(!ret){
		printk("__iovec_copy_from_kernel_inatomic failed \n");
		return 0;
	}

	return copy;
}

/*
 * Copy as much as we can into the page and return the number of bytes which
 * were sucessfully copied.  If a fault is encountered then return the number of
 * bytes which were copied.
 */
size_t iov_iter_copy_from_kernel_atomic(struct page *page,
		struct kiov_iter *i, unsigned long offset, size_t bytes)
{
	char *kaddr;
	size_t copied;

	BUG_ON(!in_atomic());
	kaddr = kmap_atomic(page, KM_USER0);
	if (likely(i->nr_segs == 1)) {
		char *buf = i->iov->iov_base + i->iov_offset;
		memcpy(kaddr + offset, buf, bytes);
		copied = bytes;
	} else {
		copied = __iovec_copy_from_kernel_inatomic(kaddr + offset,
						i->iov, i->iov_offset, bytes);
	}
	kunmap_atomic(kaddr, KM_USER0);

	return copied;
}

/*
 * This has the same sideeffects and return value as
 * iov_iter_copy_from_kernel_atomic().
 * The difference is that it attempts to resolve faults.
 * Page must not be locked.
 */
size_t iov_iter_copy_from_kernel(struct page *page,
		struct kiov_iter *i, unsigned long offset, size_t bytes)
{
	char *kaddr;
	size_t copied;

	kaddr = kmap(page);
	if (likely(i->nr_segs == 1)) {
		char *buf = i->iov->iov_base + i->iov_offset;
		memcpy(kaddr + offset, buf, bytes);
		copied = bytes;
	} else {
		copied = __iovec_copy_from_kernel_inatomic(kaddr + offset,
						i->iov, i->iov_offset, bytes);
	}
	kunmap(page);
	return copied;
}

void kiov_iter_advance(struct kiov_iter *i, size_t bytes)
{
	BUG_ON(i->count < bytes);

	if (likely(i->nr_segs == 1)) {
		i->iov_offset += bytes;
		i->count -= bytes;
	} else {
		const struct kvec *iov = i->iov;
		size_t base = i->iov_offset;

		/*
		 * The !iov->iov_len check ensures we skip over unlikely
		 * zero-length segments (without overruning the iovec).
		 */
		while (bytes || unlikely(i->count && !iov->iov_len)) {
			int copy;

			copy = min(bytes, iov->iov_len - base);
			BUG_ON(!i->count || i->count < copy);
			i->count -= copy;
			bytes -= copy;
			base += copy;
			if (iov->iov_len == base) {
				iov++;
				base = 0;
			}
		}
		i->iov = iov;
		i->iov_offset = base;
	}
}

static inline void kiov_iter_init(struct kiov_iter *i,
			const struct kvec *iov, unsigned long nr_segs,
			size_t count, size_t written)
{
	i->iov = iov;
	i->nr_segs = nr_segs;
	i->iov_offset = 0;
	i->count = count + written;

	kiov_iter_advance(i, written);
}

static int kiov_iter_fault_in_readable(struct kiov_iter *i, size_t bytes)
{
	char *buf = i->iov->iov_base + i->iov_offset;
	bytes = min(bytes, i->iov->iov_len - i->iov_offset);
	return fault_in_pages_readable(buf, bytes);
}

/*
 * Return the count of just the current kiov_iter segment.
 */
size_t kiov_iter_single_seg_count(struct kiov_iter *i)
{
	const struct kvec *iov = i->iov;
	if (i->nr_segs == 1)
		return i->count;
	else
		return min(i->count, iov->iov_len - i->iov_offset);
}

/*
 * Performs necessary checks before doing a write
 *
 * Can adjust writing position or amount of bytes to write.
 * Returns appropriate error code that caller should return or
 * zero in case that write should be allowed.
 */
inline int kernel_write_checks(struct file *file, loff_t *pos, size_t *count, int isblk)
{
	struct inode *inode = file->f_mapping->host;
	unsigned long limit = current->signal->rlim[RLIMIT_FSIZE].rlim_cur;

	if (unlikely(*pos < 0))
		return -EINVAL;

	if (!isblk) {
		/* FIXME: this is for backwards compatibility with 2.4 */
		if (file->f_flags & O_APPEND)
			*pos = i_size_read(inode);

		if (limit != RLIM_INFINITY) {
			if (*pos >= limit) {
				send_sig(SIGXFSZ, current, 0);
				return -EFBIG;
			}
			if (*count > limit - (typeof(limit))*pos) {
				*count = limit - (typeof(limit))*pos;
			}
		}
	}

	/*
	 * LFS rule
	 */
	if (unlikely(*pos + *count > MAX_NON_LFS &&
				!(file->f_flags & O_LARGEFILE))) {
		if (*pos >= MAX_NON_LFS) {
			return -EFBIG;
		}
		if (*count > MAX_NON_LFS - (unsigned long)*pos) {
			*count = MAX_NON_LFS - (unsigned long)*pos;
		}
	}

	/*
	 * Are we about to exceed the fs block limit ?
	 *
	 * If we have written data it becomes a short write.  If we have
	 * exceeded without writing data we send a signal and return EFBIG.
	 * Linus frestrict idea will clean these up nicely..
	 */
	if (likely(!isblk)) {
		if (unlikely(*pos >= inode->i_sb->s_maxbytes)) {
			if (*count || *pos > inode->i_sb->s_maxbytes) {
				return -EFBIG;
			}
			/* zero-length writes at ->s_maxbytes are OK */
		}

		if (unlikely(*pos + *count > inode->i_sb->s_maxbytes))
			*count = inode->i_sb->s_maxbytes - *pos;
	} else {
#ifdef CONFIG_BLOCK
		loff_t isize;
		if (bdev_read_only(I_BDEV(inode)))
			return -EPERM;
		isize = i_size_read(inode);
		if (*pos >= isize) {
			if (*count || *pos > isize)
				return -ENOSPC;
		}

		if (*pos + *count > isize)
			*count = isize - *pos;
#else
		return -EPERM;
#endif
	}
	return 0;
}

static ssize_t kernel_perform_write_2copy(struct file *file,
				struct kiov_iter *i, loff_t pos)
{
	struct address_space *mapping = file->f_mapping;
	const struct address_space_operations *a_ops = mapping->a_ops;
	struct inode *inode = mapping->host;
	long status = 0;
	ssize_t written = 0;

	do {
		struct page *src_page;
		struct page *page;
		pgoff_t index;		/* Pagecache index for current page */
		unsigned long offset;	/* Offset into pagecache page */
		unsigned long bytes;	/* Bytes to write to page */
		size_t copied;		/* Bytes copied from user */

		offset = (pos & (PAGE_CACHE_SIZE - 1));
		index = pos >> PAGE_CACHE_SHIFT;
		bytes = min_t(unsigned long, PAGE_CACHE_SIZE - offset,
						kiov_iter_count(i));

		/*
		 * a non-NULL src_page indicates that we're doing the
		 * copy via get_user_pages and kmap.
		 */
		src_page = NULL;

		/*
		 * Bring in the user page that we will copy from _first_.
		 * Otherwise there's a nasty deadlock on copying from the
		 * same page as we're writing to, without it being marked
		 * up-to-date.
		 *
		 * Not only is this an optimisation, but it is also required
		 * to check that the address is actually valid, when atomic
		 * usercopies are used, below.
		 */
		if (unlikely(kiov_iter_fault_in_readable(i, bytes))) {
			status = -EFAULT;
			break;
		}

		page = __grab_cache_page(mapping, index);
		if (!page) {
			status = -ENOMEM;
			break;
		}

		/*
		 * non-uptodate pages cannot cope with short copies, and we
		 * cannot take a pagefault with the destination page locked.
		 * So pin the source page to copy it.
		 */
		if (!PageUptodate(page) && !segment_eq(get_fs(), KERNEL_DS)) {
			unlock_page(page);

			src_page = alloc_page(GFP_KERNEL);
			if (!src_page) {
				page_cache_release(page);
				status = -ENOMEM;
				break;
			}

			/*
			 * Cannot get_user_pages with a page locked for the
			 * same reason as we can't take a page fault with a
			 * page locked (as explained below).
			 */
			copied = iov_iter_copy_from_kernel(src_page, i,
								offset, bytes);
			if (unlikely(copied == 0)) {
				status = -EFAULT;
				page_cache_release(page);
				page_cache_release(src_page);
				break;
			}
			bytes = copied;

			lock_page(page);
			/*
			 * Can't handle the page going uptodate here, because
			 * that means we would use non-atomic usercopies, which
			 * zero out the tail of the page, which can cause
			 * zeroes to become transiently visible. We could just
			 * use a non-zeroing copy, but the APIs aren't too
			 * consistent.
			 */
			if (unlikely(!page->mapping || PageUptodate(page))) {
				unlock_page(page);
				page_cache_release(page);
				page_cache_release(src_page);
				continue;
			}
		}

		status = a_ops->prepare_write(file, page, offset, offset+bytes);
		if (unlikely(status))
			goto fs_write_aop_error;

		if (!src_page) {
			/*
			 * Must not enter the pagefault handler here, because
			 * we hold the page lock, so we might recursively
			 * deadlock on the same lock, or get an ABBA deadlock
			 * against a different lock, or against the mmap_sem
			 * (which nests outside the page lock).  So increment
			 * preempt count, and use _atomic usercopies.
			 *
			 * The page is uptodate so we are OK to encounter a
			 * short copy: if unmodified parts of the page are
			 * marked dirty and written out to disk, it doesn't
			 * really matter.
			 */
			pagefault_disable();
			copied = iov_iter_copy_from_kernel_atomic(page, i,
								offset, bytes);
			pagefault_enable();
		} else {
			void *src, *dst;
			src = kmap_atomic(src_page, KM_USER0);
			dst = kmap_atomic(page, KM_USER1);
			memcpy(dst + offset, src + offset, bytes);
			kunmap_atomic(dst, KM_USER1);
			kunmap_atomic(src, KM_USER0);
			copied = bytes;
		}
		flush_dcache_page(page);

		status = a_ops->commit_write(file, page, offset, offset+bytes);
		if (unlikely(status < 0))
			goto fs_write_aop_error;
		if (unlikely(status > 0)) /* filesystem did partial write */
			copied = min_t(size_t, copied, status);

		unlock_page(page);
		mark_page_accessed(page);
		page_cache_release(page);
		if (src_page)
			page_cache_release(src_page);

		kiov_iter_advance(i, copied);
		pos += copied;
		written += copied;

		balance_dirty_pages_ratelimited(mapping);
		cond_resched();
		continue;

fs_write_aop_error:
		unlock_page(page);
		page_cache_release(page);
		if (src_page)
			page_cache_release(src_page);

		/*
		 * prepare_write() may have instantiated a few blocks
		 * outside i_size.  Trim these off again. Don't need
		 * i_size_read because we hold i_mutex.
		 */
		if (pos + bytes > inode->i_size)
			vmtruncate(inode, inode->i_size);
		break;
	} while (kiov_iter_count(i));

	return written ? written : status;
}

static ssize_t kernel_perform_write(struct file *file,
				struct kiov_iter *i, loff_t pos)
{
	struct address_space *mapping = file->f_mapping;
	const struct address_space_operations *a_ops = mapping->a_ops;
	long status = 0;
	ssize_t written = 0;
	unsigned int flags = 0;

	/*
	 * Copies from kernel address space cannot fail (NFSD is a big user).
	 */
	if (segment_eq(get_fs(), KERNEL_DS))
		flags |= AOP_FLAG_UNINTERRUPTIBLE;

	do {
		struct page *page;
		pgoff_t index;		/* Pagecache index for current page */
		unsigned long offset;	/* Offset into pagecache page */
		unsigned long bytes;	/* Bytes to write to page */
		size_t copied;		/* Bytes copied from user */
		void *fsdata;

		offset = (pos & (PAGE_CACHE_SIZE - 1));
		index = pos >> PAGE_CACHE_SHIFT;
		bytes = min_t(unsigned long, PAGE_CACHE_SIZE - offset,
						kiov_iter_count(i));

again:

		/*
		 * Bring in the user page that we will copy from _first_.
		 * Otherwise there's a nasty deadlock on copying from the
		 * same page as we're writing to, without it being marked
		 * up-to-date.
		 *
		 * Not only is this an optimisation, but it is also required
		 * to check that the address is actually valid, when atomic
		 * usercopies are used, below.
		 */
		if (unlikely(kiov_iter_fault_in_readable(i, bytes))) {
			status = -EFAULT;
			break;
		}

		status = a_ops->write_begin(file, mapping, pos, bytes, flags,
						&page, &fsdata);
		if (unlikely(status))
			break;

		pagefault_disable();
		copied = iov_iter_copy_from_kernel_atomic(page, i, offset, bytes);
		pagefault_enable();
		flush_dcache_page(page);

		status = a_ops->write_end(file, mapping, pos, bytes, copied,
						page, fsdata);
		if (unlikely(status < 0))
			break;
		copied = status;

		cond_resched();

		kiov_iter_advance(i, copied);
		if (unlikely(copied == 0)) {
			/*
			 * If we were unable to copy any data at all, we must
			 * fall back to a single segment length write.
			 *
			 * If we didn't fallback here, we could livelock
			 * because not all segments in the iov can be copied at
			 * once without a pagefault.
			 */
			bytes = min_t(unsigned long, PAGE_CACHE_SIZE - offset,
						kiov_iter_single_seg_count(i));
			goto again;
		}
		pos += copied;
		written += copied;

		balance_dirty_pages_ratelimited(mapping);

	} while (kiov_iter_count(i));

	return written ? written : status;
}

ssize_t
kernel_file_buffered_write(struct kiocb *iocb, const struct kvec *iov,
		unsigned long nr_segs, loff_t pos, loff_t *ppos,
		size_t count, ssize_t written)
{
	struct file *file = iocb->ki_filp;
	struct address_space *mapping = file->f_mapping;
	const struct address_space_operations *a_ops = mapping->a_ops;
	struct inode *inode = mapping->host;
	ssize_t status;
	struct kiov_iter i;

	kiov_iter_init(&i, iov, nr_segs, count, written);
	if (a_ops->write_begin)
		status = kernel_perform_write(file, &i, pos);
	else
		status = kernel_perform_write_2copy(file, &i, pos);

	if (likely(status >= 0)) {
		written += status;
		*ppos = pos + status;

		/*
		 * For now, when the user asks for O_SYNC, we'll actually give
		 * O_DSYNC
		 */
		if (unlikely((file->f_flags & O_SYNC) || IS_SYNC(inode))) {
			if (!a_ops->writepage || !is_sync_kiocb(iocb))
				status = generic_osync_inode(inode, mapping,
						OSYNC_METADATA|OSYNC_DATA);
		}
  	}
	
	/*
	 * If we get here for O_DIRECT writes then we must have fallen through
	 * to buffered writes (block instantiation inside i_size).  So we sync
	 * the file data here, to try to honour O_DIRECT expectations.
	 */
	if (unlikely(file->f_flags & O_DIRECT) && written)
		status = filemap_write_and_wait(mapping);

	return written ? written : status;
}

static ssize_t
__kernel_file_aio_write_nolock(struct kiocb *iocb, const struct kvec *iov,
				unsigned long nr_segs, loff_t *ppos)
{
	struct file *file = iocb->ki_filp;
	struct address_space * mapping = file->f_mapping;
	size_t ocount;		/* original count */
	size_t count;		/* after file limit checks */
	unsigned long	seg;
	struct inode 	*inode = mapping->host;
	loff_t		pos;
	ssize_t		written;
	ssize_t		err;

	ocount = 0;
	//セグメント数分繰り返す（参：do_sync_writeからの分はセグメント１つ）
	for (seg = 0; seg < nr_segs; seg++) {
		const struct kvec *iv = &iov[seg];

		/*
		 * If any segment has a negative length, or the cumulative
		 * length ever wraps negative then return -EINVAL.
		 */
		ocount += iv->iov_len;
		if (unlikely((ssize_t)(ocount|iv->iov_len) < 0)) //?
			return -EINVAL;
	}

	count = ocount;
	pos = *ppos;

	vfs_check_frozen(inode->i_sb, SB_FREEZE_WRITE);

	/* We can write back this queue in page reclaim */
	current->backing_dev_info = mapping->backing_dev_info;
	written = 0;

	err = kernel_write_checks(file, &pos, &count, S_ISBLK(inode->i_mode));
	if (err)
		goto out;

	if (count == 0)
		goto out;

	err = file_remove_suid(file);
	if (err)
		goto out;

	file_update_time(file);

	/* coalesce the kvecs and go direct-to-BIO for O_DIRECT */
	if (unlikely(file->f_flags & O_DIRECT)) {
		err = -EINVAL;
		printk("Not support O_DIRECT.\n");
	} else {
		written = kernel_file_buffered_write(iocb, iov, nr_segs,
				pos, ppos, count, written);
	}
out:
	current->backing_dev_info = NULL;
	return written ? written : err;
}

ssize_t kernel_file_aio_write_nolock(struct kiocb *iocb,
		const struct kvec *iov, unsigned long nr_segs, loff_t pos)
{
	struct file *file = iocb->ki_filp;
	struct address_space *mapping = file->f_mapping;
	struct inode *inode = mapping->host;
	ssize_t ret;

	BUG_ON(iocb->ki_pos != pos);

	ret = __kernel_file_aio_write_nolock(iocb, iov, nr_segs,
			&iocb->ki_pos);

	if (ret > 0 && ((file->f_flags & O_SYNC) || IS_SYNC(inode))) {
		ssize_t err;

		err = sync_page_range_nolock(inode, mapping, pos, ret);
		if (err < 0)
			ret = err;
	}
	return ret;
}
EXPORT_SYMBOL(kernel_file_aio_write_nolock);

ssize_t kernel_file_aio_write(struct kiocb *iocb, const struct kvec *iov,
		unsigned long nr_segs, loff_t pos)
{
	struct file *file = iocb->ki_filp;
	struct address_space *mapping = file->f_mapping;
	struct inode *inode = mapping->host;
	ssize_t ret;

	BUG_ON(iocb->ki_pos != pos);

	mutex_lock(&inode->i_mutex);
	ret = __kernel_file_aio_write_nolock(iocb, iov, nr_segs,
			&iocb->ki_pos);
	mutex_unlock(&inode->i_mutex);

	if (ret > 0 && ((file->f_flags & O_SYNC) || IS_SYNC(inode))) {
		ssize_t err;

		err = sync_page_range(inode, mapping, pos, ret);
		if (err < 0)
			ret = err;
	}
	return ret;
}
EXPORT_SYMBOL(kernel_file_aio_write);
