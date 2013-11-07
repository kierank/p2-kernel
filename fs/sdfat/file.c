/*
 *  linux/fs/fat/file.c
 *
 *  Written 1992,1993 by Werner Almesberger
 *
 *  regular file handling primitives for fat-based filesystems
 */

#include <linux/capability.h>
#include <linux/module.h>
#include <linux/time.h>
#include <linux/sdfat_fs.h>
#include <linux/smp_lock.h>
#include <linux/buffer_head.h>
#include <linux/writeback.h>
#include <linux/backing-dev.h>
#include <linux/blkdev.h>

/* Modified by Panasonic (SAV), 2009-oct-5 */
static int sdfat_file_repair(struct inode *inode, struct file *filp)
{
	int ret = 0;
	int curr, file_cluster;
	struct super_block *sb = inode->i_sb;
	loff_t fixed_size;

	lock_kernel();

	file_cluster = 0;

	curr = SDFAT_I(inode)->i_start;
	if(curr != 0){
		for(file_cluster = 1; curr && curr != FAT_ENT_EOF; file_cluster++){
			struct fat_entry fatent;

			fatent_init(&fatent);

			curr = sdfat_ent_read(inode, &fatent, curr);
			if(curr < 0) {
				break;
			}
			else if(curr == FAT_ENT_EOF) {
				break;
			}
			else if(curr == FAT_ENT_FREE) {
				// repair event
				ret = sdfat_ent_write(inode, &fatent, FAT_ENT_EOF, 0);
				break;
			}

			fatent_brelse(&fatent);

			//総クラスタ数を超えたら無限ループと見なし、エラー
			if(file_cluster > SDFAT_SB(sb)->max_cluster - FAT_START_ENT){
				printk("Infinite Loop.\n");

				// run away from here ! 
				unlock_kernel();
				return -EINVAL;
			}
		}
	}

	// repair the size of the file
	fixed_size = (loff_t)(file_cluster) << (SDFAT_SB(sb)->cluster_bits);
	inode->i_size = min(fixed_size, (loff_t)sb->s_maxbytes);
	SDFAT_I(inode)->mmu_private = inode->i_size;
	inode->i_blocks = (inode->i_size + (sb->s_blocksize - 1)) >> sb->s_blocksize_bits;

#if defined(CONFIG_SDFAT_USE_SM331)
	memset(SDFAT_I(inode)->i_cluster_milestones, 0, sizeof(int)*(FAT_MILESTONES + 1));
#endif //defined(CONFIG_SDFAT_USE_SM331)

	sdfat_cache_inval_inode(inode);

	// flush the repaired data
	mark_inode_dirty(inode);
	fsync_super(sb);

	unlock_kernel();
	return ret;
}
/*-----------------------------------------*/

/* Modified by Panasonic (SAV), 2009-oct-5 */
static int sdfat_set_normal_io(struct inode *inode, struct file *filp)
{
	if((SDFAT_SB(inode->i_sb)->sd_info_sb.param.au_size > 0)
			&& SDFAT_SB(inode->i_sb)->sd_info_sb.proxy_mode){

		down(&SDFAT_I(inode)->sd_info_i.buf_lock);
		if(!SDFAT_I(inode)->sd_info_i.normal_io){
			if(!SDFAT_I(inode)->sd_info_i.dirty){
				if(SDFAT_I(inode)->sd_info_i.sd_open == 1){
					if(SDFAT_I(inode)->sd_info_i.buf != NULL){
						free_pages(
							(unsigned long)SDFAT_I(inode)->sd_info_i.buf,
							FAT_SD_RU_BITS
						);
					}
					SDFAT_I(inode)->sd_info_i.sd_open--;
					up(&SDFAT_I(inode)->sd_info_i.buf_lock);
				}
				else{
					printk("Already opened.\n");
					up(&SDFAT_I(inode)->sd_info_i.buf_lock);
					return -EINVAL;
				}
			}
			else{
				printk("Another process is writing.\n");
				up(&SDFAT_I(inode)->sd_info_i.buf_lock);
				return -EINVAL;
			}
			SDFAT_I(inode)->sd_info_i.normal_io = 1;
		}
		up(&SDFAT_I(inode)->sd_info_i.buf_lock);
	}
	return 0;
}
/*-----------------------------------------*/

int sdfat_generic_ioctl(struct inode *inode, struct file *filp,
		      unsigned int cmd, unsigned long arg)
{
	struct sdfat_sb_info *sbi = SDFAT_SB(inode->i_sb);
	u32 __user *user_attr = (u32 __user *)arg;

	switch (cmd) {
	case FAT_IOCTL_GET_ATTRIBUTES:
	{
		u32 attr;

		if (inode->i_ino == MSDOS_ROOT_INO)
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
		attr |= (SDFAT_I(inode)->i_attrs & ATTR_VOLUME) |
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
		if (inode->i_ino == MSDOS_ROOT_INO && attr != ATTR_DIR) {
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

		SDFAT_I(inode)->i_attrs = attr & ATTR_UNUSED;
		mark_inode_dirty(inode);
	up:
		mutex_unlock(&inode->i_mutex);
		return err;
	}

	/* Modified by Panasonic (SAV), 2009-oct-5 */
	case FAT_IOCTL_FILE_REPAIR:
		return sdfat_file_repair(inode, filp);
	/*-----------------------------------------*/

	/* Modified by Panasonic (SAV), 2009-oct-5 */
	case FAT_IOCTL_FILE_NORMAL_IO:
		return sdfat_set_normal_io(inode, filp);
	/*-----------------------------------------*/

	default:
		return -ENOTTY;	/* Inappropriate ioctl for device */
	}
}

/* Modified by Panasonic (SAV), 2009-oct-5 */
static int fat_file_open(struct inode *inode, struct file *filp)
{
	if((SDFAT_SB(inode->i_sb)->sd_info_sb.param.au_size > 0)
		&& SDFAT_SB(inode->i_sb)->sd_info_sb.proxy_mode){

		down(&SDFAT_I(inode)->sd_info_i.buf_lock);
		if(!SDFAT_I(inode)->sd_info_i.normal_io && (filp->f_mode & FMODE_WRITE)){

			if(!(filp->f_flags & O_SYNC))
				filp->f_flags |= O_SYNC;

			if(SDFAT_I(inode)->sd_info_i.sd_open == 0){
				SDFAT_I(inode)->sd_info_i.offset = 0;
				SDFAT_I(inode)->sd_info_i.start = 0;
				SDFAT_I(inode)->sd_info_i.end = 0;
				SDFAT_I(inode)->sd_info_i.dirty = 0;
				SDFAT_I(inode)->sd_info_i.buf = (void *)__get_free_pages(GFP_KERNEL, FAT_SD_RU_BITS);
				if(SDFAT_I(inode)->sd_info_i.buf == NULL){
					up(&SDFAT_I(inode)->sd_info_i.buf_lock);
					return -ENOMEM;
				}

#if defined(CONFIG_SDFAT_USE_SM331)
				mutex_lock(&SDFAT_SB(inode->i_sb)->open_lru_lock);

				list_add_tail(&SDFAT_I(inode)->open_lru.lru,
					&SDFAT_SB(inode->i_sb)->open_list);

				mutex_unlock(&SDFAT_SB(inode->i_sb)->open_lru_lock);
#endif //CONFIG_SDFAT_USE_SM331
			}
			SDFAT_I(inode)->sd_info_i.sd_open++;
		}
		up(&SDFAT_I(inode)->sd_info_i.buf_lock);
	}

	return 0;
}
/*-----------------------------------------*/

/* Modified by Panasonic (SAV), 2009-mar-24 */
#if defined(CONFIG_SDFAT_USE_SM331)
static int sdop_check_open_count(struct inode *inode)
{
	int i = 0;
	struct list_head *walk;
	struct sdfat_open_list_head *open_list;

	list_for_each(walk, &SDFAT_SB(inode->i_sb)->open_list){
		open_list = list_entry(walk, struct sdfat_open_list_head, lru);
		i++;
	}

	return i;
}
#endif //CONFIG_SDFAT_USE_SM331
/*-----------------------------------------*/

/* Modified by Panasonic (SAV), 2009-mar-24 */
#if defined(CONFIG_SDFAT_USE_SM331)
int sdfat_fix_last_file_chain(struct inode *inode)
{
	int writting_file = 0;
	struct list_head *walk;
	struct sdfat_open_list_head *open_list;
	struct inode *next_inode = NULL;

	list_for_each(walk, &SDFAT_SB(inode->i_sb)->open_list){
		open_list = list_entry(walk, struct sdfat_open_list_head, lru);

		//データがかかれてない場合は対象外
		if(SDFAT_I(open_list->inode)->i_start == 0)
			continue;

		if(SDFAT_I(open_list->inode)->sd_info_i.last_cluster == -1){
			writting_file = 1;
			continue;
		}

		next_inode = open_list->inode;
		break;
	}

	return  sdfat_replace_chain(inode, next_inode, writting_file);
}
#endif //CONFIG_SDFAT_USE_SM331
/*------------------------------------------*/

/* Modified by Panasonic (SAV), 2009-oct-5 */
#if defined(CONFIG_SDFAT_USE_SM331)
static int sdfat_fix_fat_chain(struct inode *inode)
{
	int ret = 0;
	struct sdfat_sb_info *sbi = SDFAT_SB(inode->i_sb);

	lock_fat(sbi);
	mutex_lock(&sbi->open_lru_lock);

	if(list_empty(&SDFAT_I(inode)->open_lru.lru)){
		mutex_unlock(&sbi->open_lru_lock);
		unlock_fat(sbi);
		return 0;
	}

	list_del(&SDFAT_I(inode)->open_lru.lru);
	INIT_LIST_HEAD(&SDFAT_I(inode)->open_lru.lru);

	if(sdop_check_open_count(inode) > 0){
		mutex_unlock(&sbi->open_lru_lock);
		unlock_fat(sbi);
		return 0;
	}

	if(sbi->use_continuously){
		mutex_unlock(&sbi->open_lru_lock);
		unlock_fat(sbi);
		return 0;
	}

	if(sbi->sd_info_sb.write_pos.inode != inode){
		if(sbi->sd_info_sb.write_pos.inode == NULL){
			mutex_unlock(&sbi->open_lru_lock);
			unlock_fat(sbi);
			return 0;
		}
		inode = sbi->sd_info_sb.write_pos.inode;
	}

	ret = sdfat_fix_last_file_chain(inode);

	mutex_unlock(&sbi->open_lru_lock);
	unlock_fat(sbi);
	return ret;
}
#endif //CONFIG_SDFAT_USE_SM331
/*-----------------------------------------*/

static int fat_file_release(struct inode *inode, struct file *filp)
{
	/* Modified by Panasonic (SAV), 2009-oct-5 */
	int ret = 0, err = 0;

	if((SDFAT_SB(inode->i_sb)->sd_info_sb.param.au_size > 0)
		&& SDFAT_SB(inode->i_sb)->sd_info_sb.proxy_mode){

		down(&SDFAT_I(inode)->sd_info_i.buf_lock);
		if(!SDFAT_I(inode)->sd_info_i.normal_io && (filp->f_mode & FMODE_WRITE)){
			if(SDFAT_I(inode)->sd_info_i.sd_open == 1){

#if defined(CONFIG_SDFAT_USE_SM331)
				ret = sdfat_fix_fat_chain(inode);
#endif //CONFIG_SDFAT_USE_SM331

#if defined(CONFIG_SDFAT_USE_SM331)
				if(filp->f_mode & FMODE_WRITE){
					SDFAT_I(inode)->i_dummy_size = inode->i_size;
					mark_inode_dirty(inode);
				}
#endif //CONFIG_SDFAT_USE_SM331

				if(SDFAT_I(inode)->sd_info_i.buf != NULL){
					free_pages((unsigned long)SDFAT_I(inode)->sd_info_i.buf, FAT_SD_RU_BITS);
				}
			}
			SDFAT_I(inode)->sd_info_i.sd_open--;
		}
		up(&SDFAT_I(inode)->sd_info_i.buf_lock);
	}
	/*-----------------------------------------*/

	if ((filp->f_mode & FMODE_WRITE) &&
	     SDFAT_SB(inode->i_sb)->options.flush) {
		sdfat_flush_inodes(inode->i_sb, inode, NULL);
		congestion_wait(WRITE, HZ/10);
	}

	/* Modified by Panasonic (SAV), 2009-oct-5 */
#if defined(CONFIG_SDFAT_USE_SM331)
	if(test_bit(FAT_SUSPENDED_INODE, &SDFAT_I(inode)->i_flags)){
		clear_bit(FAT_SUSPENDED_INODE, &SDFAT_I(inode)->i_flags);
		sdfat_sync_inode(inode);
	}
#else
	sdfat_sync_inode(inode);
#endif
	/*-----------------------------------------*/

	return (ret == 0 ? err : ret);
}

/* Modified by Panasonic (SAV), 2009-oct-5 */
ssize_t fat_sd_file_read(struct kiocb *iocb, const struct iovec *iov,
		unsigned long nr_segs, loff_t pos)
{
	struct inode *inode = iocb->ki_filp->f_dentry->d_inode;
	size_t count = iocb->ki_left;
	ssize_t retval;
	loff_t buf_pos;
	size_t buf_len;

	if(SDFAT_SB(inode->i_sb)->sd_info_sb.param.au_size == 0){
		printk("[%s-%d] AU size is 0.\n", __FUNCTION__, __LINE__);
		return -EINVAL;
	}

	retval = generic_file_aio_read(iocb, iov, nr_segs, pos);
	if(retval < 0)
		return retval;

	down(&SDFAT_I(inode)->sd_info_i.buf_lock);

	if(SDFAT_I(inode)->sd_info_i.dirty){
		buf_pos = SDFAT_I(inode)->sd_info_i.offset * FAT_SD_RU_SIZE
				+ SDFAT_I(inode)->sd_info_i.start;
		buf_len = SDFAT_I(inode)->sd_info_i.end - SDFAT_I(inode)->sd_info_i.start + 1;

		if(((pos >= buf_pos) && (pos < buf_pos + buf_len)) //始点がメモリ内
			|| ((pos + count - 1 >= buf_pos)
				&& (pos + count - 1 < buf_pos + buf_len)) //終点がメモリ内
			|| ((pos < buf_pos) && (pos + count - 1 >= buf_pos + buf_len))){ //メモリを包合

			loff_t cpy_to_pos = (pos > buf_pos ? 0 : buf_pos - pos);
			loff_t cpy_to_end_pos = (pos + count > buf_pos + buf_len
							? buf_pos + buf_len - pos
							: count);
			loff_t cpy_to_len = cpy_to_end_pos - cpy_to_pos;

			loff_t cpy_from_pos = (pos > buf_pos
				? (pos - buf_pos) + SDFAT_I(inode)->sd_info_i.start
				: SDFAT_I(inode)->sd_info_i.start);
			loff_t cpy_from_end_pos = (pos + count > buf_pos + buf_len
							? SDFAT_I(inode)->sd_info_i.end + 1
							: (SDFAT_I(inode)->sd_info_i.end + 1
							    - ((buf_pos + buf_len) - (pos + count))));
			loff_t cpy_from_len = cpy_from_end_pos - cpy_from_pos;

			if(cpy_to_len != cpy_from_len){
				printk("[%s-%d] Different Size : %llu != %llu\n",
					__FUNCTION__, __LINE__,
					cpy_to_len, cpy_from_len);
				up(&SDFAT_I(inode)->sd_info_i.buf_lock);
				return -EINVAL;
			}

			if(copy_to_user((void *)(iov->iov_base + cpy_to_pos),
					SDFAT_I(inode)->sd_info_i.buf + cpy_from_pos, cpy_to_len)){
				printk("[%s-%d] Copy error.\n", __FUNCTION__, __LINE__);
				up(&SDFAT_I(inode)->sd_info_i.buf_lock);
				return -EINVAL;
			}

			//指定サイズ読み込めなかった場合
			if(retval != count){
				//読めなかった領域はメモリコピーした場合
				if(pos + retval < buf_pos + buf_len){
					//読もうとした終端がメモリ上のデータの範囲外の場合
					if(pos + count > buf_pos + buf_len){
						retval = buf_pos + buf_len - pos;
						iocb->ki_pos = pos + retval;
					}
					//読もうとした終端がメモリ上のデータの範囲内の場合
					else{
						retval = count;
						iocb->ki_pos = pos + retval;
					}
				}
			}
		}
	}
	up(&SDFAT_I(inode)->sd_info_i.buf_lock);

	return retval;
}
/*-----------------------------------------*/

/* Modified by Panasonic (SAV), 2009-oct-5 */
ssize_t fat_sd_file_write(struct kiocb *iocb, const struct iovec *iov,
		unsigned long nr_segs, loff_t pos)
{
	struct inode *inode = iocb->ki_filp->f_dentry->d_inode;
	size_t count = iocb->ki_left;
	struct kvec kiov;
	int retval;
	int mem_retval = 0;
	loff_t start_offset;
	loff_t start;
	loff_t end_offset;
	loff_t end;
	loff_t buf_offset;

	if(SDFAT_SB(inode->i_sb)->sd_info_sb.param.au_size == 0){
		printk("[%s-%d] AU size is 0.\n", __FUNCTION__, __LINE__);
		return -EINVAL;
	}

	down(&SDFAT_I(inode)->sd_info_i.buf_lock);

	start_offset = pos / FAT_SD_RU_SIZE;
	start = pos % FAT_SD_RU_SIZE;

	end_offset = (pos + count - 1) / FAT_SD_RU_SIZE;
	end = (pos + count - 1) % FAT_SD_RU_SIZE;

	buf_offset = 0;

	if(SDFAT_I(inode)->sd_info_i.dirty){
		//前回と違うRUへの書き込み：前回分を書き出し
		if(SDFAT_I(inode)->sd_info_i.offset != start_offset){
			loff_t pre_pos = iocb->ki_pos; //位置は変更しないので覚えておく

			iocb->ki_pos = SDFAT_I(inode)->sd_info_i.offset * FAT_SD_RU_SIZE
				+ SDFAT_I(inode)->sd_info_i.start;

			kiov.iov_base = SDFAT_I(inode)->sd_info_i.buf + SDFAT_I(inode)->sd_info_i.start;
			kiov.iov_len = SDFAT_I(inode)->sd_info_i.end - SDFAT_I(inode)->sd_info_i.start + 1;

			mem_retval = kernel_file_aio_write(iocb, &kiov,	nr_segs, iocb->ki_pos);

			if(mem_retval > 0){
				inode->i_mtime = inode->i_ctime = CURRENT_TIME;
				SDFAT_I(inode)->i_attrs |= ATTR_ARCH;
				mark_inode_dirty(inode);
			}
			else{
				SDFAT_I(inode)->sd_info_i.dirty = 0;
				up(&SDFAT_I(inode)->sd_info_i.buf_lock);
				return mem_retval;
			}

			iocb->ki_pos = pre_pos;
		}
		//前回分の続きではないところへの書き込み：前回分を書き出し
		else if(SDFAT_I(inode)->sd_info_i.end + 1 != start){
			loff_t pre_pos = iocb->ki_pos; //位置は変更しないので覚えておく

			iocb->ki_pos = SDFAT_I(inode)->sd_info_i.offset * FAT_SD_RU_SIZE
				+ SDFAT_I(inode)->sd_info_i.start;

			kiov.iov_base = SDFAT_I(inode)->sd_info_i.buf + SDFAT_I(inode)->sd_info_i.start;
			kiov.iov_len = SDFAT_I(inode)->sd_info_i.end - SDFAT_I(inode)->sd_info_i.start + 1;
			mem_retval = kernel_file_aio_write(iocb, &kiov, nr_segs, iocb->ki_pos);

			if(mem_retval > 0){
				inode->i_mtime = inode->i_ctime = CURRENT_TIME;
				SDFAT_I(inode)->i_attrs |= ATTR_ARCH;
				mark_inode_dirty(inode);
			}
			else{
				SDFAT_I(inode)->sd_info_i.dirty = 0;
				up(&SDFAT_I(inode)->sd_info_i.buf_lock);
				return mem_retval;
			}

			iocb->ki_pos = pre_pos;
		}
		//前回のRUを超える書き込み：前回分+今回分(RUで収まる部分)を書き出し
		else if(SDFAT_I(inode)->sd_info_i.offset != end_offset){
			int pre_size = SDFAT_I(inode)->sd_info_i.end - SDFAT_I(inode)->sd_info_i.start + 1;

			if(copy_from_user(SDFAT_I(inode)->sd_info_i.buf + start, (void *)iov->iov_base, FAT_SD_RU_SIZE - start)){
				printk("[%s-%d] Copy error.\n", __FUNCTION__, __LINE__);
				SDFAT_I(inode)->sd_info_i.dirty = 0;
				up(&SDFAT_I(inode)->sd_info_i.buf_lock);
				return -EINVAL;
			}

			SDFAT_I(inode)->sd_info_i.end = FAT_SD_RU_SIZE - 1;

			iocb->ki_pos = SDFAT_I(inode)->sd_info_i.offset * FAT_SD_RU_SIZE
				+ SDFAT_I(inode)->sd_info_i.start;

			kiov.iov_base = SDFAT_I(inode)->sd_info_i.buf + SDFAT_I(inode)->sd_info_i.start;
			kiov.iov_len = SDFAT_I(inode)->sd_info_i.end - SDFAT_I(inode)->sd_info_i.start + 1;
			mem_retval = kernel_file_aio_write(iocb, &kiov, nr_segs, iocb->ki_pos);

			if(mem_retval > 0){
				inode->i_mtime = inode->i_ctime = CURRENT_TIME;
				SDFAT_I(inode)->i_attrs |= ATTR_ARCH;
				mark_inode_dirty(inode);

				if(mem_retval - pre_size != FAT_SD_RU_SIZE - start){
					SDFAT_I(inode)->sd_info_i.dirty = 0;
					up(&SDFAT_I(inode)->sd_info_i.buf_lock);
					return mem_retval - pre_size;
				}
			}
			else{
				SDFAT_I(inode)->sd_info_i.dirty = 0;
				up(&SDFAT_I(inode)->sd_info_i.buf_lock);
				return mem_retval;
			}

			buf_offset = FAT_SD_RU_SIZE - start;
			start_offset++;
			start = 0;
		}
		//前回分と今回分がRUピッタリの書き込み：書き出して終了
		else if(end == FAT_SD_RU_SIZE - 1){
			int pre_size = SDFAT_I(inode)->sd_info_i.end - SDFAT_I(inode)->sd_info_i.start + 1;

			SDFAT_I(inode)->sd_info_i.dirty = 0;

			if(copy_from_user(SDFAT_I(inode)->sd_info_i.buf + start, (void *)iov->iov_base, end - start + 1)){
				printk("[%s-%d] Copy error.\n", __FUNCTION__, __LINE__);
				up(&SDFAT_I(inode)->sd_info_i.buf_lock);
				return -EINVAL;
			}

			SDFAT_I(inode)->sd_info_i.end = end;
			iocb->ki_pos = SDFAT_I(inode)->sd_info_i.offset * FAT_SD_RU_SIZE
				+ SDFAT_I(inode)->sd_info_i.start;

			kiov.iov_base = SDFAT_I(inode)->sd_info_i.buf + SDFAT_I(inode)->sd_info_i.start;
			kiov.iov_len = SDFAT_I(inode)->sd_info_i.end - SDFAT_I(inode)->sd_info_i.start + 1;
			mem_retval = kernel_file_aio_write(iocb, &kiov, nr_segs, iocb->ki_pos);

			if(mem_retval > 0){
				inode->i_mtime = inode->i_ctime = CURRENT_TIME;
				SDFAT_I(inode)->i_attrs |= ATTR_ARCH;
				mark_inode_dirty(inode);

				if(mem_retval - pre_size != FAT_SD_RU_SIZE - start){
					up(&SDFAT_I(inode)->sd_info_i.buf_lock);
					return mem_retval - pre_size;
				}
			}
			else{
				up(&SDFAT_I(inode)->sd_info_i.buf_lock);
				return mem_retval;
			}

			up(&SDFAT_I(inode)->sd_info_i.buf_lock);
			return end - start + 1;
		}
		//RU未満の書き込み：メモリに書き込み終了
		else{
			if(copy_from_user(SDFAT_I(inode)->sd_info_i.buf + start, (void *)iov->iov_base, end - start + 1)){
				printk("[%s-%d] Copy error.\n", __FUNCTION__, __LINE__);
				up(&SDFAT_I(inode)->sd_info_i.buf_lock);
				return -EINVAL;
			}

			SDFAT_I(inode)->sd_info_i.end = end;
			iocb->ki_pos += end - start + 1;

			up(&SDFAT_I(inode)->sd_info_i.buf_lock);
			return end - start + 1;
		}
	}

	//RUピッタリの書き込み：書き出し
	if(end == FAT_SD_RU_SIZE - 1){
		struct iovec tmp_iov;

		SDFAT_I(inode)->sd_info_i.dirty = 0;

		tmp_iov.iov_base = iov->iov_base + buf_offset;
		tmp_iov.iov_len = count - buf_offset;

		retval = generic_file_aio_write(iocb, &tmp_iov, nr_segs, iocb->ki_pos);

		up(&SDFAT_I(inode)->sd_info_i.buf_lock);

		return retval + buf_offset;
	}
	//RU未満の書き込み：メモリに書き込み
	else if(start_offset == end_offset){
		SDFAT_I(inode)->sd_info_i.start = start;
		SDFAT_I(inode)->sd_info_i.end = end;
		SDFAT_I(inode)->sd_info_i.offset = start_offset;
		SDFAT_I(inode)->sd_info_i.dirty = 1;

		if(copy_from_user(SDFAT_I(inode)->sd_info_i.buf + start, (void *)(iov->iov_base + buf_offset), end - start + 1)){
			printk("[%s-%d] Copy error.\n", __FUNCTION__, __LINE__);
			up(&SDFAT_I(inode)->sd_info_i.buf_lock);
		       return -EINVAL;
		}
		iocb->ki_pos += end - start + 1;

		up(&SDFAT_I(inode)->sd_info_i.buf_lock);
		return buf_offset + end - start + 1;
	}
	//RU以上の書き込み：RU分書き込み、のこりはメモリに書き込み
	else{
		struct iovec tmp_iov;

		tmp_iov.iov_base = iov->iov_base + buf_offset;
		tmp_iov.iov_len = count - buf_offset - end - 1;

		retval = generic_file_aio_write(iocb, &tmp_iov, nr_segs, iocb->ki_pos);

		if(retval <= 0){
			up(&SDFAT_I(inode)->sd_info_i.buf_lock);
			return retval;
		}
		else if(retval != count - buf_offset - end - 1){

			up(&SDFAT_I(inode)->sd_info_i.buf_lock);
			return retval + buf_offset;
		}

		SDFAT_I(inode)->sd_info_i.start = 0;
		SDFAT_I(inode)->sd_info_i.end = end;
		SDFAT_I(inode)->sd_info_i.offset = end_offset;
		SDFAT_I(inode)->sd_info_i.dirty = 1;

		if(copy_from_user(SDFAT_I(inode)->sd_info_i.buf, (void *)(iov->iov_base + count - end - 1), end + 1)){
			up(&SDFAT_I(inode)->sd_info_i.buf_lock);
			return -EINVAL;
		}
		iocb->ki_pos += end + 1;

		up(&SDFAT_I(inode)->sd_info_i.buf_lock);
		return retval + buf_offset + end + 1;
	}
}
/*-----------------------------------------*/

/* Modified by Panasonic (SAV), 2009-oct-5 */
ssize_t fat_file_read(struct kiocb *iocb, const struct iovec *iov,
		unsigned long nr_segs, loff_t pos)
{
	struct inode *inode = iocb->ki_filp->f_dentry->d_inode;

	if(SDFAT_I(inode)->sd_info_i.sd_open){
		return fat_sd_file_read(iocb, iov, nr_segs, pos);
	}

	return generic_file_aio_read(iocb, iov, nr_segs, pos);
}
/*-----------------------------------------*/

/* Modified by Panasonic (SAV), 2009-oct-5 */
ssize_t fat_file_write(struct kiocb *iocb, const struct iovec *iov,
		unsigned long nr_segs, loff_t pos)
{
	ssize_t ret;
	struct inode *inode = iocb->ki_filp->f_dentry->d_inode;

	if(SDFAT_I(inode)->sd_info_i.sd_open){
		ret = fat_sd_file_write(iocb, iov, nr_segs, pos);
	}
	else{
		ret = generic_file_aio_write(iocb, iov, nr_segs, pos);
	}

#if defined(CONFIG_SDFAT_USE_SM331)
	if(iocb->ki_pos > SDFAT_I(inode)->i_dummy_f_pos)
		SDFAT_I(inode)->i_dummy_f_pos = iocb->ki_pos;
#endif //CONFIG_SDFAT_USE_SM331

	return ret;
}
/*-----------------------------------------*/

/* Modified by Panasonic (SAV), 2009-oct-5 */
static int sdfat_sync_buffer(struct file *filp, int lock)
{
	int retval = 0;
	struct inode *inode = filp->f_dentry->d_inode;
	struct kiocb iocb;
	struct kvec kiov;

	//kiocbの初期化 (include/linux/aio.h)
	init_sync_kiocb(&iocb, filp);

	down(&SDFAT_I(inode)->sd_info_i.buf_lock);
	if(SDFAT_I(inode)->sd_info_i.sd_open){
		if(SDFAT_I(inode)->sd_info_i.buf != NULL){
			if(SDFAT_I(inode)->sd_info_i.dirty){
				loff_t space_size = 0;
				SDFAT_I(inode)->sd_info_i.dirty = 0;

				iocb.ki_pos = SDFAT_I(inode)->sd_info_i.offset * FAT_SD_RU_SIZE
					+ SDFAT_I(inode)->sd_info_i.start;
				kiov.iov_base = SDFAT_I(inode)->sd_info_i.buf
						+ SDFAT_I(inode)->sd_info_i.start;
				kiov.iov_len = SDFAT_I(inode)->sd_info_i.end
						- SDFAT_I(inode)->sd_info_i.start + 1;

				//ファイル終端もRU書き込み
				if(SDFAT_I(inode)->sd_info_i.start == 0 //書き込み位置がRUの先頭
					&& (iocb.ki_pos + SDFAT_I(inode)->sd_info_i.end + 1)
						== inode->i_size + kiov.iov_len){  //ファイルの終端

					space_size = FAT_SD_RU_SIZE - SDFAT_I(inode)->sd_info_i.end - 1;

					//余分な部分は０埋め
					memset(SDFAT_I(inode)->sd_info_i.buf
						+ SDFAT_I(inode)->sd_info_i.end + 1, 0, space_size);
					kiov.iov_len = FAT_SD_RU_SIZE;
				}

				iocb.ki_left = kiov.iov_len;

				if(lock)
					retval = kernel_file_aio_write(&iocb, &kiov, 1, iocb.ki_pos);
				else
					retval = kernel_file_aio_write_nolock(&iocb, &kiov, 1, iocb.ki_pos);

				if(retval < 0){
					printk("[%s-%d] write error %08X.\n",__FUNCTION__,__LINE__,retval);
				}

				if(retval > 0){
					if(retval == kiov.iov_len){
						inode->i_size -= space_size;
					}
					else{
						if(kiov.iov_len - retval < space_size){
							inode->i_size -= (space_size - kiov.iov_len + retval);
						}
					}
					retval = 0;
					inode->i_mtime = inode->i_ctime = CURRENT_TIME;
					SDFAT_I(inode)->i_attrs |= ATTR_ARCH;
					mark_inode_dirty(inode);
					sdfat_sync_inode(inode);
				}
			}
		}
	}
	up(&SDFAT_I(inode)->sd_info_i.buf_lock);

	return retval;
}
/*-----------------------------------------*/

/* Modified by Panasonic (SAV), 2009-oct-5 */
int fat_file_flush(struct file *filp, fl_owner_t id)
{
#if defined(CONFIG_SDFAT_USE_SM331)
	int ret;
	struct inode *inode = filp->f_dentry->d_inode;

	SDFAT_I(inode)->i_flush_context = 1;
	ret = sdfat_sync_buffer(filp, 1);
	SDFAT_I(inode)->i_flush_context = 0;

	return ret;
#else
	return sdfat_sync_buffer(filp, 1);
#endif //CONFIG_SDFAT_USE_SM331
}
/*-----------------------------------------*/

/* Modified by Panasonic (SAV), 2009-oct-5 */
static int fat_file_fsync(struct file *filp, struct dentry *dentry, int datasync)
{
	int ret, retval;

	retval = sdfat_sync_buffer(filp, 0);

	ret = file_fsync(filp, dentry, datasync);

	return (ret == 0 ? retval : ret);
}
/*-----------------------------------------*/

/* Modified by Panasonic (SAV), 2009-oct-5 */
loff_t fat_file_llseek(struct file *file, loff_t offset, int origin)
{
	long long retval;
	struct inode *inode = file->f_mapping->host;

	mutex_lock(&inode->i_mutex);
	switch (origin) {
		case SEEK_END:
#if defined(CONFIG_SDFAT_USE_SM331)
			if(SDFAT_I(inode)->i_dummy_f_pos > inode->i_size)
				offset += SDFAT_I(inode)->i_dummy_f_pos;
			else
#endif //CONFIG_SDFAT_USE_SM331
				offset += inode->i_size;
			break;
		case SEEK_CUR:
			offset += file->f_pos;
	}
	retval = -EINVAL;
	if (offset>=0 && offset<=inode->i_sb->s_maxbytes) {
		if (offset != file->f_pos) {
			file->f_pos = offset;
			file->f_version = 0;
		}
		retval = offset;
	}
	mutex_unlock(&inode->i_mutex);
	return retval;
}
/*-----------------------------------------*/

const struct file_operations sdfat_file_operations = {
/* Modified by Panasonic (SAV), 2009-oct-5 */
	.open		= fat_file_open,
	.flush		= fat_file_flush,
	.llseek		= fat_file_llseek,
/*-----------------------------------------*/
	.read		= do_sync_read,
	.write		= do_sync_write,
/* Modified by Panasonic (SAV), 2009-oct-5 */
	.aio_read	= fat_file_read,//generic_file_aio_read,
	.aio_write	= fat_file_write,// generic_file_aio_write,
/*-----------------------------------------*/
	.mmap		= generic_file_mmap,
	.release	= fat_file_release,
	.ioctl		= sdfat_generic_ioctl,
/* Modified by Panasonic (SAV), 2009-oct-5 */
	.fsync		= fat_file_fsync,// file_fsync,
/*-----------------------------------------*/
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

static int check_mode(const struct sdfat_sb_info *sbi, mode_t mode)
{
	mode_t req = mode & ~S_IFMT;

	/*
	 * Of the r and x bits, all (subject to umask) must be present. Of the
	 * w bits, either all (subject to umask) or none must be present.
	 */

	if (S_ISREG(mode)) {
		req &= ~sbi->options.fs_fmask;

		if ((req & (S_IRUGO | S_IXUGO)) !=
		    ((S_IRUGO | S_IXUGO) & ~sbi->options.fs_fmask))
			return -EPERM;

		if ((req & S_IWUGO) != 0 &&
		    (req & S_IWUGO) != (S_IWUGO & ~sbi->options.fs_fmask))
			return -EPERM;
	} else if (S_ISDIR(mode)) {
		req &= ~sbi->options.fs_dmask;

		if ((req & (S_IRUGO | S_IXUGO)) !=
		    ((S_IRUGO | S_IXUGO) & ~sbi->options.fs_dmask))
			return -EPERM;

		if ((req & S_IWUGO) != 0 &&
		    (req & S_IWUGO) != (S_IWUGO & ~sbi->options.fs_dmask))
			return -EPERM;
	} else {
		return -EPERM;
	}

	return 0;
}

int sdfat_notify_change(struct dentry *dentry, struct iattr *attr)
{
	struct sdfat_sb_info *sbi = SDFAT_SB(dentry->d_sb);
	struct inode *inode = dentry->d_inode;
	int mask, error = 0;

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
	     (attr->ia_gid != sbi->options.fs_gid)))
		error = -EPERM;

	if (error) {
		if (sbi->options.quiet)
			error = 0;
		goto out;
	}

	if (attr->ia_valid & ATTR_MODE) {
		error = check_mode(sbi, attr->ia_mode);
		if (error != 0 && !sbi->options.quiet)
			goto out;
	}

	error = inode_setattr(inode, attr);
	if (error)
		goto out;

	if (S_ISDIR(inode->i_mode))
		mask = sbi->options.fs_dmask;
	else
		mask = sbi->options.fs_fmask;
	inode->i_mode &= S_IFMT | (S_IRWXUGO & ~mask);
out:
	unlock_kernel();
	return error;
}

EXPORT_SYMBOL_GPL(sdfat_notify_change);

/* Free all clusters after the skip'th cluster. */
static int fat_free(struct inode *inode, int skip)
{
	struct super_block *sb = inode->i_sb;
	int err, wait, free_start, i_start, i_logstart;

	if (SDFAT_I(inode)->i_start == 0)
		return 0;

	sdfat_cache_inval_inode(inode);

	wait = IS_DIRSYNC(inode);
	i_start = free_start = SDFAT_I(inode)->i_start;
	i_logstart = SDFAT_I(inode)->i_logstart;

	/* First, we write the new file size. */
	if (!skip) {
		SDFAT_I(inode)->i_start = 0;
		SDFAT_I(inode)->i_logstart = 0;
	}
	SDFAT_I(inode)->i_attrs |= ATTR_ARCH;
	inode->i_ctime = inode->i_mtime = CURRENT_TIME_SEC;
	if (wait) {
		err = sdfat_sync_inode(inode);
		if (err) {
			SDFAT_I(inode)->i_start = i_start;
			SDFAT_I(inode)->i_logstart = i_logstart;
			return err;
		}
	} else
		mark_inode_dirty(inode);

	/* Write a new EOF, and get the remaining cluster chain for freeing. */
	if (skip) {
		struct fat_entry fatent;
		int ret, fclus, dclus;

		ret = sdfat_get_cluster(inode, skip - 1, &fclus, &dclus);
		if (ret < 0)
			return ret;
		else if (ret == FAT_ENT_EOF)
			return 0;

		fatent_init(&fatent);
		ret = sdfat_ent_read(inode, &fatent, dclus);
		if (ret == FAT_ENT_EOF) {
			fatent_brelse(&fatent);
			return 0;
		} else if (ret == FAT_ENT_FREE) {
			sdfat_fs_panic(sb,
				     "%s: invalid cluster chain (i_pos %lld)",
				     __FUNCTION__, SDFAT_I(inode)->i_pos);
			ret = -EIO;
		} else if (ret > 0) {
			err = sdfat_ent_write(inode, &fatent, FAT_ENT_EOF, wait);
			if (err)
				ret = err;
		}
		fatent_brelse(&fatent);
		if (ret < 0)
			return ret;

		free_start = ret;
	}
	inode->i_blocks = skip << (SDFAT_SB(sb)->cluster_bits - 9);

	/* Freeing the remained cluster chain */
	return sdfat_free_clusters(inode, free_start);
}

void sdfat_truncate(struct inode *inode)
{
	struct sdfat_sb_info *sbi = SDFAT_SB(inode->i_sb);
	const unsigned int cluster_size = sbi->cluster_size;
	int nr_clusters;

	/*
	 * This protects against truncating a file bigger than it was then
	 * trying to write into the hole.
	 */
	if (SDFAT_I(inode)->mmu_private > inode->i_size)
		SDFAT_I(inode)->mmu_private = inode->i_size;

	nr_clusters = (inode->i_size + (cluster_size - 1)) >> sbi->cluster_bits;

	/* Modified by Panasonic (SAV), 2009-oct-5 */
#if defined(CONFIG_SDFAT_USE_SM331)
	SDFAT_I(inode)->i_dummy_f_pos = inode->i_size;
#endif //CONFIG_SDFAT_USE_SM331
	/*-----------------------------------------*/

	lock_kernel();
	fat_free(inode, nr_clusters);
	unlock_kernel();
	sdfat_flush_inodes(inode->i_sb, inode, NULL);
}

int sdfat_getattr(struct vfsmount *mnt, struct dentry *dentry, struct kstat *stat)
{
	struct inode *inode = dentry->d_inode;
	generic_fillattr(inode, stat);
	stat->blksize = SDFAT_SB(inode->i_sb)->cluster_size;
	return 0;
}
EXPORT_SYMBOL_GPL(sdfat_getattr);

const struct inode_operations sdfat_file_inode_operations = {
	.truncate	= sdfat_truncate,
	.setattr	= sdfat_notify_change,
	.getattr	= sdfat_getattr,
};
