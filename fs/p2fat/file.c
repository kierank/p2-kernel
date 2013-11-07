/*
 *  linux/fs/p2fat/file.c
 *
 *  Written 1992,1993 by Werner Almesberger
 *
 *  regular file handling primitives for fat-based filesystems
 */

#include <linux/capability.h>
#include <linux/module.h>
#include <linux/time.h>
#include <linux/p2fat_fs.h>
#include <linux/smp_lock.h>
#include <linux/buffer_head.h>
#include <linux/writeback.h>
#include <linux/backing-dev.h>
#include <linux/blkdev.h>
#include <linux/reservoir_sb.h>

/* Panasonic Original */
static int p2fat_file_repair(struct inode *inode, struct file *filp)
{
	int ret = 0;
	int curr, file_cluster;
	struct super_block *sb = inode->i_sb;
	loff_t fixed_size;
	loff_t maxbytes = sb->s_maxbytes - 3; // 'cause P2 contents are put on 4byte alignment

	lock_kernel();

	file_cluster = 0;

	curr = P2FAT_I(inode)->i_start;

	if(curr != 0){
		for(file_cluster = 0; curr && curr != FAT_ENT_EOF; file_cluster++){
			struct p2fat_entry fatent;

			p2fatent_init(&fatent);

			curr = p2fat_ent_read(inode, &fatent, curr);

			if(curr < 0) {
				break;
			}
			else if(curr == FAT_ENT_FREE) {
				if(p2fat_check_cont_space(sb, fatent.entry)){
					P2FAT_SB(sb)->cont_space.cont--;
				}
				// repair event
				ret = p2fat_ent_write(inode, &fatent, FAT_ENT_EOF, 0);
				break;
			}

			p2fatent_brelse(&fatent);

			//総クラスタ数を超えたら無限ループと見なし、エラー
			if(file_cluster > P2FAT_SB(sb)->max_cluster - FAT_START_ENT){
				printk("Infinite Loop.\n");

				// run away from here ! 
				unlock_kernel();
				return -EINVAL;
			}
		}
	}

	// repair the size of the file
	fixed_size = (loff_t)(file_cluster) << (P2FAT_SB(sb)->cluster_bits);
	inode->i_size = min(fixed_size, maxbytes);
	P2FAT_I(inode)->mmu_private = inode->i_size;
	inode->i_blocks = (inode->i_size + (sb->s_blocksize - 1)) >> sb->s_blocksize_bits;
        
	//P2FAT_I(inode)->i_touched_cluster.file_cluster=0;
	//P2FAT_I(inode)->i_touched_cluster.disk_cluster=P2FAT_I(inode)->i_start;
	memset(P2FAT_I(inode)->i_cluster_milestones, 0, sizeof(int)*(FAT_MILESTONES + 1));

	p2fat_cache_inval_inode(inode);

	// flush the repaired data
	mark_inode_dirty(inode);

	lock_rton(MAJOR(sb->s_dev));
	if(check_rt_status(sb)){
		if(likely(inode->i_sb->s_op->write_inode)){
			inode->i_sb->s_op->write_inode(inode, 1);
		}
		p2fat_sync(sb);
	}
	unlock_rton(MAJOR(sb->s_dev));

	unlock_kernel();
	return ret;
}
/*--------------------*/

/* Panasonic Original */
int p2fat_ioctl_check_region(struct inode *inode, struct file *filp,
			     struct fat_ioctl_chk_region *chk_region)
{
  struct super_block *sb = inode->i_sb;
  int nr = P2FAT_I(inode)->i_start;
  unsigned char p2_version = P2FAT_SB(sb)->p2_params.p2_version;
  unsigned long protect_start = P2FAT_SB(sb)->p2_params.p2_protect_start;
  unsigned long protect_sectors = P2FAT_SB(sb)->p2_params.p2_protect_sectors;
  unsigned long protect_boundary;
  struct p2fat_entry fatent;

  p2fatent_init(&fatent);

  chk_region->version = p2_version;
  chk_region->continuance = 0;
  chk_region->first = (unsigned long)nr;
  chk_region->last = (unsigned long )nr;
  chk_region->result = 0;

  if(!nr)
    {
      chk_region->result = -1;
      goto FIN;
    }

  protect_boundary
    = (((protect_start + protect_sectors
       - P2FAT_SB(sb)->data_start) <<  sb->s_blocksize_bits)
       + P2FAT_SB(sb)->cluster_size -1)
    / P2FAT_SB(sb)->cluster_size + 2;

  while(nr != FAT_ENT_EOF)
    {
      if(p2_version>=3 && nr>=protect_boundary)
	{
	  chk_region->result = -1;
	}

      chk_region->continuance++;

      if(nr > chk_region->last)
	{
	  chk_region->last = nr;
	}

      nr = __p2fat_ent_read(sb, &fatent, nr);
      if(nr==0)
	{
	  p2fat_fs_panic(sb, "fat_chk_region: EOF not found\n");
	  chk_region->result = -1;
	  break;
	}
      else if(nr<0)
	{
	  p2fat_fs_panic(sb, "fat_chk_region: Reading FAT failed\n");
	  chk_region->result = -1;
	  break;
	}

      if(nr < chk_region->first)
	{
	  chk_region->first = nr;
	}
    }

 FIN:

  p2fatent_brelse(&fatent);

  return 0;
}

/*--------------------*/

/* Panasonic Original */

static int do_p2fat_file_release(struct inode *inode, struct file *filp)
{
	/* Panasonic Original */
	int ret = 0;
	int rt = (filp->f_flags & O_REALTIME) ? 1 : 0;   /* rtかどうかをフラグを見て確認 */
	struct super_block *sb = inode->i_sb;

	mutex_lock(&P2FAT_SB(sb)->rt_inode_dirty_lock);
	if(test_bit(FAT_SUSPENDED_INODE, &P2FAT_I(inode)->i_flags)
			&& list_empty(&P2FAT_I(inode)->i_rt_dirty)){
		__iget(inode);
		list_add_tail(&P2FAT_I(inode)->i_rt_dirty, &P2FAT_SB(sb)->rt_inode_dirty_list);
	}
	mutex_unlock(&P2FAT_SB(sb)->rt_inode_dirty_lock);
	/*--------------------*/

	if ((filp->f_mode & FMODE_WRITE) &&
	     P2FAT_SB(sb)->options.flush) {
		p2fat_flush_inodes(sb, inode, NULL);
		congestion_wait(WRITE, HZ/10);
	}

	ret = reservoir_file_release(inode, filp);

	/* 以降、ローカルFSが行なう管理情報の更新処理 */
	if (!(sb->s_flags & MS_RDONLY))
		p2fat_clusters_flush(sb, 1);

	if(rt){
		lock_rton(MAJOR(sb->s_dev));
		if((atomic_read(&(RS_SB(sb)->rt_total_files)))==0
		   && check_rt_status(sb))
		{
			p2fat_sync(sb);
			/* inodeの書き出しは、
			   reservoir_file_release内で
			   している */
		}
		unlock_rton(MAJOR(sb->s_dev));
	} else {
		lock_rton(MAJOR(sb->s_dev));
		if(check_rt_status(sb)){
			p2fat_sync(sb);
			filemap_write_and_wait(inode->i_mapping);
			if(sb->s_op->write_inode && !is_bad_inode(inode)){
				sb->s_op->write_inode(inode, 1);
			}
		} else {
			/* RT優先状態ではdirty化だけ行なっておく。
			 * ファイルサイズが中途半端のままになる可能性があるため。*/
			mark_inode_dirty(inode);
		}
		unlock_rton(MAJOR(sb->s_dev));
	}
	
	return ret;
}

/*-------------------*/

int p2fat_generic_ioctl(struct inode *inode, struct file *filp,
		      unsigned int cmd, unsigned long arg)
{
	struct p2fat_sb_info *sbi = P2FAT_SB(inode->i_sb);
	u32 __user *user_attr = (u32 __user *)arg;

	switch (cmd) {
	case FAT_IOCTL_GET_ATTRIBUTES:
	{
		u32 attr;

		if (inode->i_ino == P2FAT_ROOT_INO)
			attr = ATTR_DIR;
		else
			attr = fat_attr(inode);

		return put_user(attr, user_attr);
	}
	case FAT_IOCTL_SET_ATTRIBUTES:
	{
		u32 attr, oldattr;
		int err, is_dir = S_ISDIR(inode->i_mode);
		struct iattr ia;

		err = get_user(attr, user_attr);
		if (err)
			return err;

		mutex_lock(&inode->i_mutex);

		if (IS_RDONLY(inode)) {
			err = -EROFS;
			goto up;
		}

		/*
		 * ATTR_VOLUME and ATTR_DIR cannot be changed; this also
		 * prevents the user from turning us into a VFAT
		 * longname entry.  Also, we obviously can't set
		 * any of the NTFS attributes in the high 24 bits.
		 */
		attr &= 0xff & ~(ATTR_VOLUME | ATTR_DIR);
		/* Merge in ATTR_VOLUME and ATTR_DIR */
		attr |= (P2FAT_I(inode)->i_attrs & ATTR_VOLUME) |
			(is_dir ? ATTR_DIR : 0);
		oldattr = fat_attr(inode);

		/* Equivalent to a chmod() */
		ia.ia_valid = ATTR_MODE | ATTR_CTIME;
		if (is_dir) {
			ia.ia_mode = MSDOS_MKMODE(attr,
				S_IRWXUGO & ~sbi->options.fs_dmask)
				| S_IFDIR;
		} else {
			ia.ia_mode = MSDOS_MKMODE(attr,
				(S_IRUGO | S_IWUGO | (inode->i_mode & S_IXUGO))
				& ~sbi->options.fs_fmask)
				| S_IFREG;
		}

		/* The root directory has no attributes */
		if (inode->i_ino == P2FAT_ROOT_INO && attr != ATTR_DIR) {
			err = -EINVAL;
			goto up;
		}

		if (sbi->options.sys_immutable) {
			if ((attr | oldattr) & ATTR_SYS) {
				if (!capable(CAP_LINUX_IMMUTABLE)) {
					err = -EPERM;
					goto up;
				}
			}
		}

		/* This MUST be done before doing anything irreversible... */
		err = notify_change(filp->f_path.dentry, &ia);
		if (err)
			goto up;

		if (sbi->options.sys_immutable) {
			if (attr & ATTR_SYS)
				inode->i_flags |= S_IMMUTABLE;
			else
				inode->i_flags &= S_IMMUTABLE;
		}

		P2FAT_I(inode)->i_attrs = attr & ATTR_UNUSED;
		mark_inode_dirty(inode);
	up:
		mutex_unlock(&inode->i_mutex);
		return err;
	}

	/* Panasonic Original */
	case FAT_IOCTL_FILE_PRERELEASE:
		return do_p2fat_file_release(inode, filp);

	/* Panasonic Original */
	case FAT_IOCTL_FAT_SYNC:
		return p2fat_sync(inode->i_sb);
	/*--------------------*/
	  
	/* Panasonic Original */
 	case FAT_IOCTL_FILE_REPAIR:
		return p2fat_file_repair(inode, filp);
	/*--------------------*/

	case FAT_IOCTL_GET_FS_TYPE:
	  {
	    enum fat_fs_type fs_type = KRNL_P2FAT;
	    if(copy_to_user((void *)arg, (void *)&fs_type, sizeof(enum fat_fs_type)))
	      {
		return -EINVAL;
	      }
	    return 0;
	  }

	/* Panasonic Original */
	case FAT_IOCTL_CHECK_REGION:
	  {
	    struct fat_ioctl_chk_region chk_region;
 
	    int ret = p2fat_ioctl_check_region(inode, filp, &chk_region);
	    if(copy_to_user((void *)arg, (void *)&chk_region, sizeof(struct fat_ioctl_chk_region)))
	      {
		return -EINVAL;
	      }

	    return ret;
	  }
	/*--------------------*/

	default:
	  return reservoir_file_ioctl(inode, filp, cmd, arg);
	}
}

static int p2fat_file_release(struct inode *inode, struct file *filp)
{
	int rt = (filp->f_flags & O_REALTIME) ? 1 : 0;   /* rtかどうかをフラグを見て確認 */
	int ret = 0;

	if(!rt)
	{
		ret = do_p2fat_file_release(inode, filp);
	}

	return ret;
}

static int p2fat_fsync(struct file *filp, struct dentry *dent, int datasync)
{
	struct super_block *sb = dent->d_inode->i_sb;
	int ret = reservoir_fsync(filp, dent, datasync);

	/* 以降、ローカルFSが行なう管理情報更新処理 */
	lock_rton(MAJOR(sb->s_dev));
	if(check_rt_status(sb)){
		p2fat_sync(sb);
	}
	unlock_rton(MAJOR(sb->s_dev));
	return ret;
}

const struct file_operations p2fat_file_operations = {
	.llseek		= reservoir_llseek,
	.read		= do_sync_read,
	.write		= do_sync_write,
	.aio_read	= reservoir_file_aio_read,
	.aio_write	= reservoir_file_aio_write,
	.mmap		= generic_file_mmap,
	.open           = reservoir_file_open,
	.release	= p2fat_file_release,
	.ioctl		= p2fat_generic_ioctl,
	.fsync		= p2fat_fsync,
	.splice_read	= generic_file_splice_read,
};

static int fat_cont_expand(struct inode *inode, loff_t size)
{
	struct address_space *mapping = inode->i_mapping;
	loff_t start = inode->i_size, count = size - inode->i_size;
	int err;

	err = generic_cont_expand_simple(inode, size);
	if (err)
		goto out;

	inode->i_ctime = inode->i_mtime = CURRENT_TIME_SEC;
	mark_inode_dirty(inode);
	if (IS_SYNC(inode))
		err = sync_page_range_nolock(inode, mapping, start, count);
out:
	return err;
}

int p2fat_notify_change(struct dentry *dentry, struct iattr *attr)
{
	struct p2fat_sb_info *sbi = P2FAT_SB(dentry->d_sb);
	struct inode *inode = dentry->d_inode;
	int error = 0;

	lock_kernel();

	/*
	 * Expand the file. Since inode_setattr() updates ->i_size
	 * before calling the ->truncate(), but FAT needs to fill the
	 * hole before it.
	 */
	if (attr->ia_valid & ATTR_SIZE) {
		if (attr->ia_size > inode->i_size) {
			error = fat_cont_expand(inode, attr->ia_size);
			if (error || attr->ia_valid == ATTR_SIZE)
				goto out;
			attr->ia_valid &= ~ATTR_SIZE;
		}
	}

	error = inode_change_ok(inode, attr);
	if (error) {
		if (sbi->options.quiet)
			error = 0;
		goto out;
	}
	if (((attr->ia_valid & ATTR_UID) &&
	     (attr->ia_uid != sbi->options.fs_uid)) ||
	    ((attr->ia_valid & ATTR_GID) &&
	     (attr->ia_gid != sbi->options.fs_gid)) ||
	    ((attr->ia_valid & ATTR_MODE) &&
	     (attr->ia_mode & ~MSDOS_VALID_MODE)))
		error = -EPERM;

	if (error) {
		if (sbi->options.quiet)
			error = 0;
		goto out;
	}
	error = inode_setattr(inode, attr);
	if (error)
		goto out;

#ifdef CONFIG_MK_RDONLY_FILE
	inode->i_mode = (inode->i_mode & S_IFMT) | S_IRWXUGO;
#else
        {
          int mask;

	  if (S_ISDIR(inode->i_mode))
		  mask = sbi->options.fs_dmask;
	  else
		  mask = sbi->options.fs_fmask;
	  inode->i_mode &= S_IFMT | (S_IRWXUGO & ~mask);
        }
#endif

out:
	unlock_kernel();
	return error;
}

EXPORT_SYMBOL_GPL(p2fat_notify_change);

/* Free all clusters after the skip'th cluster. */
static int fat_free(struct inode *inode, int skip)
{
	struct super_block *sb = inode->i_sb;
	int err, wait, free_start, i_start, i_logstart;

	if (P2FAT_I(inode)->i_start == 0)
		return 0;

	p2fat_cache_inval_inode(inode);

	wait = IS_DIRSYNC(inode);
	i_start = free_start = P2FAT_I(inode)->i_start;
	i_logstart = P2FAT_I(inode)->i_logstart;

	/* First, we write the new file size. */
	if (!skip) {
		P2FAT_I(inode)->i_start = 0;
		P2FAT_I(inode)->i_logstart = 0;
	}
	P2FAT_I(inode)->i_attrs |= ATTR_ARCH;
	inode->i_ctime = inode->i_mtime = CURRENT_TIME_SEC;
	if (wait) {
		err = p2fat_sync_inode(inode);
		if (err) {
			P2FAT_I(inode)->i_start = i_start;
			P2FAT_I(inode)->i_logstart = i_logstart;
			return err;
		}
	} else
		mark_inode_dirty(inode);

	/* Write a new EOF, and get the remaining cluster chain for freeing. */
	if (skip) {
		struct p2fat_entry fatent;
		int ret, fclus, dclus;

		ret = p2fat_get_cluster(inode, skip - 1, &fclus, &dclus, 0);
		if (ret < 0)
			return ret;
		else if (ret == FAT_ENT_EOF)
			return 0;

		p2fatent_init(&fatent);
	
		ret = p2fat_ent_read(inode, &fatent, dclus);
		if (ret == FAT_ENT_EOF) {
			p2fatent_brelse(&fatent);
			return 0;
		} else if (ret == FAT_ENT_FREE) {
			p2fat_fs_panic(sb,
				     "%s: invalid cluster chain (i_pos %lld)",
				     __FUNCTION__, P2FAT_I(inode)->i_pos);
			ret = -EIO;
		} else if (ret > 0) {
			err = p2fat_ent_write(inode, &fatent, FAT_ENT_EOF, wait);
			if (err)
				ret = err;
		}

		p2fatent_brelse(&fatent);
		if (ret < 0)
			return ret;

		free_start = ret;
	}
	inode->i_blocks = skip << (P2FAT_SB(sb)->cluster_bits - 9);

	/* Freeing the remained cluster chain */
	return p2fat_free_clusters(inode, free_start);
}

void p2fat_truncate(struct inode *inode)
{
	struct p2fat_sb_info *sbi = P2FAT_SB(inode->i_sb);
	const unsigned int cluster_size = sbi->cluster_size;
	int nr_clusters;

	/*
	 * This protects against truncating a file bigger than it was then
	 * trying to write into the hole.
	 */
	if (P2FAT_I(inode)->mmu_private > inode->i_size)
		P2FAT_I(inode)->mmu_private = inode->i_size;

	nr_clusters = (inode->i_size + (cluster_size - 1)) >> sbi->cluster_bits;

	lock_kernel();
	fat_free(inode, nr_clusters);
	unlock_kernel();
	p2fat_flush_inodes(inode->i_sb, inode, NULL);
}

int p2fat_getattr(struct vfsmount *mnt, struct dentry *dentry, struct kstat *stat)
{
	struct inode *inode = dentry->d_inode;
	generic_fillattr(inode, stat);
	stat->blksize = P2FAT_SB(inode->i_sb)->cluster_size;
	return 0;
}
EXPORT_SYMBOL_GPL(p2fat_getattr);

const struct inode_operations p2fat_file_inode_operations = {
	.truncate	= p2fat_truncate,
	.setattr	= p2fat_notify_change,
	.getattr	= p2fat_getattr,
};
