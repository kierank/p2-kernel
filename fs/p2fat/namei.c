/*
 *  linux/fs/p2fat/namei.c
 *
 *  Written 1992,1993 by Werner Almesberger
 *  Hidden files 1995 by Albert Cahalan <albert@ccs.neu.edu> <adc@coe.neu.edu>
 *  Rewritten for constant inumbers 1999 by Al Viro
 *  Modification for P2 Camcoder System is written by S.Horita
 */

#include <linux/module.h>
#include <linux/time.h>
#include <linux/buffer_head.h>
#include <linux/p2fat_fs.h>
#include <linux/smp_lock.h>

/* Characters that are undesirable in an MS-DOS file name */
static unsigned char bad_chars[] = "*?<>|\"";
static unsigned char bad_if_strict_pc[] = "+=,; ";
/* GEMDOS is less restrictive */
static unsigned char bad_if_strict_atari[] = " ";

#define bad_if_strict(opts) \
	((opts)->atari ? bad_if_strict_atari : bad_if_strict_pc)

/***** Formats an MS-DOS file name. Rejects invalid names. */
static int msdos_format_name(const unsigned char *name, int len,
			     unsigned char *res, struct p2fat_mount_options *opts)
	/*
	 * name is the proposed name, len is its length, res is
	 * the resulting name, opts->name_check is either (r)elaxed,
	 * (n)ormal or (s)trict, opts->dotsOK allows dots at the
	 * beginning of name (for hidden files)
	 */
{
	unsigned char *walk;
	unsigned char c;
	int space;

	if (name[0] == '.') {	/* dotfile because . and .. already done */
		if (opts->dotsOK) {
			/* Get rid of dot - test for it elsewhere */
			name++;
			len--;
		} else if (!opts->atari)
			return -EINVAL;
	}
	/*
	 * disallow names that _really_ start with a dot for MS-DOS,
	 * GEMDOS does not care
	 */
	space = !opts->atari;
	c = 0;
	for (walk = res; len && walk - res < 8; walk++) {
		c = *name++;
		len--;
		if (opts->name_check != 'r' && strchr(bad_chars, c))
			return -EINVAL;
		if (opts->name_check == 's' && strchr(bad_if_strict(opts), c))
			return -EINVAL;
		if (c >= 'A' && c <= 'Z' && opts->name_check == 's')
			return -EINVAL;
		if (c < ' ' || c == ':' || c == '\\')
			return -EINVAL;
	/*
	 * 0xE5 is legal as a first character, but we must substitute
	 * 0x05 because 0xE5 marks deleted files.  Yes, DOS really
	 * does this.
	 * It seems that Microsoft hacked DOS to support non-US
	 * characters after the 0xE5 character was already in use to
	 * mark deleted files.
	 */
		if ((res == walk) && (c == 0xE5))
			c = 0x05;
		if (c == '.')
			break;
		space = (c == ' ');
		*walk = (!opts->nocase && c >= 'a' && c <= 'z') ? c - 32 : c;
	}
	if (space)
		return -EINVAL;
	if (opts->name_check == 's' && len && c != '.') {
		c = *name++;
		len--;
		if (c != '.')
			return -EINVAL;
	}
	while (c != '.' && len--)
		c = *name++;
	if (c == '.') {
		while (walk - res < 8)
			*walk++ = ' ';
		while (len > 0 && walk - res < MSDOS_NAME) {
			c = *name++;
			len--;
			if (opts->name_check != 'r' && strchr(bad_chars, c))
				return -EINVAL;
			if (opts->name_check == 's' &&
			    strchr(bad_if_strict(opts), c))
				return -EINVAL;
			if (c < ' ' || c == ':' || c == '\\')
				return -EINVAL;
			if (c == '.') {
				if (opts->name_check == 's')
					return -EINVAL;
				break;
			}
			if (c >= 'A' && c <= 'Z' && opts->name_check == 's')
				return -EINVAL;
			space = c == ' ';
			if (!opts->nocase && c >= 'a' && c <= 'z')
				*walk++ = c - 32;
			else
				*walk++ = c;
		}
		if (space)
			return -EINVAL;
		if (opts->name_check == 's' && len)
			return -EINVAL;
	}
	while (walk - res < MSDOS_NAME)
		*walk++ = ' ';

	return 0;
}

/***** Locates a directory entry.  Uses unformatted name. */
static int msdos_find(struct inode *dir, const unsigned char *name, int len,
		      struct fat_slot_info *sinfo)
{
	struct p2fat_sb_info *sbi = P2FAT_SB(dir->i_sb);
	unsigned char msdos_name[MSDOS_NAME];
	int err;

	err = msdos_format_name(name, len, msdos_name, &sbi->options);
	if (err)
		return -ENOENT;

	err = p2fat_scan(dir, msdos_name, sinfo);
	if (!err && sbi->options.dotsOK) {
		if (name[0] == '.') {
			if (!(sinfo->de->attr & ATTR_HIDDEN))
				err = -ENOENT;
		} else {
			if (sinfo->de->attr & ATTR_HIDDEN)
				err = -ENOENT;
		}
		if (err)
			brelse(sinfo->bh);
	}
	return err;
}

/*
 * Compute the hash for the msdos name corresponding to the dentry.
 * Note: if the name is invalid, we leave the hash code unchanged so
 * that the existing dentry can be used. The msdos fs routines will
 * return ENOENT or EINVAL as appropriate.
 */
static int msdos_hash(struct dentry *dentry, struct qstr *qstr)
{
	struct p2fat_mount_options *options = &P2FAT_SB(dentry->d_sb)->options;
	unsigned char msdos_name[MSDOS_NAME];
	int error;

	error = msdos_format_name(qstr->name, qstr->len, msdos_name, options);
	if (!error)
		qstr->hash = full_name_hash(msdos_name, MSDOS_NAME);
	return 0;
}

/*
 * Compare two msdos names. If either of the names are invalid,
 * we fall back to doing the standard name comparison.
 */
static int msdos_cmp(struct dentry *dentry, struct qstr *a, struct qstr *b)
{
	struct p2fat_mount_options *options = &P2FAT_SB(dentry->d_sb)->options;
	unsigned char a_msdos_name[MSDOS_NAME], b_msdos_name[MSDOS_NAME];
	int error;

	error = msdos_format_name(a->name, a->len, a_msdos_name, options);
	if (error)
		goto old_compare;
	error = msdos_format_name(b->name, b->len, b_msdos_name, options);
	if (error)
		goto old_compare;
	error = memcmp(a_msdos_name, b_msdos_name, MSDOS_NAME);
out:
	return error;

old_compare:
	error = 1;
	if (a->len == b->len)
		error = memcmp(a->name, b->name, a->len);
	goto out;
}

static struct dentry_operations p2fat_dentry_operations = {
	.d_hash		= msdos_hash,
	.d_compare	= msdos_cmp,
};

/*
 * AV. Wrappers for FAT sb operations. Is it wise?
 */

/***** Get inode using directory and name */
static struct dentry *msdos_lookup(struct inode *dir, struct dentry *dentry,
				   struct nameidata *nd)
{
	struct super_block *sb = dir->i_sb;
	struct fat_slot_info sinfo;
	struct inode *inode = NULL;
	int res;

	dentry->d_op = &p2fat_dentry_operations;

	lock_kernel();
	res = msdos_find(dir, dentry->d_name.name, dentry->d_name.len, &sinfo);
	if (res == -ENOENT)
		goto add;
	if (res < 0)
		goto out;
	inode = p2fat_build_inode(sb, sinfo.de, sinfo.i_pos);
	brelse(sinfo.bh);
	if (IS_ERR(inode)) {
		res = PTR_ERR(inode);
		goto out;
	}
add:
	res = 0;
	dentry = d_splice_alias(inode, dentry);
	if (dentry)
		dentry->d_op = &p2fat_dentry_operations;
out:
	unlock_kernel();
	if (!res)
		return dentry;
	return ERR_PTR(res);
}

/***** Creates a directory entry (name is already formatted). */
static int msdos_add_entry(struct inode *dir, const unsigned char *name,
			   int is_dir, int is_hid, int cluster,
			   struct timespec *ts, struct fat_slot_info *sinfo,
			   int suspend/* Panasonic Add */)
{
	struct msdos_dir_entry de;
	__le16 time, date;
	int err;

	memcpy(de.name, name, MSDOS_NAME);
	de.attr = is_dir ? ATTR_DIR : ATTR_ARCH;
	if (is_hid)
		de.attr |= ATTR_HIDDEN;
	de.lcase = 0;
	p2fat_date_unix2dos(ts->tv_sec, &time, &date);
	de.cdate = de.adate = 0;
	de.ctime = 0;
	de.ctime_cs = 0;
	de.time = time;
	de.date = date;
	de.start = cpu_to_le16(cluster);
	de.starthi = cpu_to_le16(cluster >> 16);
	de.size = 0;

	err = p2fat_add_entries(dir, &de, 1, sinfo, suspend);
	if (err)
		return err;

	dir->i_ctime = dir->i_mtime = *ts;
	if (IS_DIRSYNC(dir))
		(void)p2fat_sync_inode(dir);
	else
		mark_inode_dirty(dir);

	return 0;
}

/***** Create a file */
static int msdos_create(struct inode *dir, struct dentry *dentry, int mode,
			struct nameidata *nd)
{
	struct super_block *sb = dir->i_sb;
	struct inode *inode = NULL;
	struct fat_slot_info sinfo;
	struct timespec ts;
	unsigned char msdos_name[MSDOS_NAME];
	int err, is_hid;

	lock_kernel();

	err = msdos_format_name(dentry->d_name.name, dentry->d_name.len,
				msdos_name, &P2FAT_SB(sb)->options);
	if (err)
		goto out;
	is_hid = (dentry->d_name.name[0] == '.') && (msdos_name[0] != '.');
	/* Have to do it due to foo vs. .foo conflicts */
	if (!p2fat_scan(dir, msdos_name, &sinfo)) {
		brelse(sinfo.bh);
		err = -EINVAL;
		goto out;
	}

	ts = CURRENT_TIME_SEC;
	/* Panasonic Change */
	err = msdos_add_entry(dir, msdos_name, 0, is_hid, 0, &ts, &sinfo, 1);
	/*------------------*/

	if (err)
		goto out;
	inode = p2fat_build_inode(sb, sinfo.de, sinfo.i_pos);
	brelse(sinfo.bh);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		/* Panasonic Original */
		mark_buffer_dirty(sinfo.bh);
		put_bh(sinfo.bh);
		/*--------------------*/
		goto out;
	}
	inode->i_mtime = inode->i_atime = inode->i_ctime = ts;
	/* timestamp is already written, so mark_inode_dirty() is unneeded. */

	/* Panasonic Original */
	set_bit(FAT_SUSPENDED_INODE, &P2FAT_I(inode)->i_flags);
	P2FAT_I(inode)->suspended_bh = sinfo.bh;
	/*--------------------*/

	d_instantiate(dentry, inode);
out:
	unlock_kernel();
	if (!err)
		err = p2fat_flush_inodes(sb, dir, inode);
	return err;
}

/***** Remove a directory */
static int msdos_rmdir(struct inode *dir, struct dentry *dentry)
{
	/* Panasonic Original */
	struct super_block *sb = dir->i_sb;
	/*--------------------*/
	struct inode *inode = dentry->d_inode;
	struct fat_slot_info sinfo;
	int err;

	lock_kernel();
	/*
	 * Check whether the directory is not in use, then check
	 * whether it is empty.
	 */
	err = p2fat_dir_empty(inode);
	if (err)
		goto out;
	err = msdos_find(dir, dentry->d_name.name, dentry->d_name.len, &sinfo);
	if (err)
		goto out;

	/* Panasonic Original */
	mutex_lock(&P2FAT_SB(sb)->rt_inode_dirty_lock);

	if(!list_empty(&P2FAT_I(inode)->i_rt_dirty))
	{
		if(test_bit(FAT_SUSPENDED_INODE, &P2FAT_I(inode)->i_flags)){
			if(P2FAT_I(inode)->suspended_bh){
				put_bh(P2FAT_I(inode)->suspended_bh);
			}
			else{
				printk("[%s-%d] LOST Suspended BH !\n", __PRETTY_FUNCTION__, __LINE__);
			}

			clear_bit(FAT_SUSPENDED_INODE, &P2FAT_I(inode)->i_flags);
		}

		clear_bit(FAT_NEWDIR_INODE, &P2FAT_I(inode)->i_flags);

		list_del_init(&P2FAT_I(inode)->i_rt_dirty);
		iput(inode);
	}
	mutex_unlock(&P2FAT_SB(sb)->rt_inode_dirty_lock);

	//mutex_lock(&RS_SB(sb)->rt_files_lock);
	//if(!list_empty(&inode->i_reservoir_list))
	//{
	//	list_del_init(&inode->i_reservoir_list);
	//	atomic_dec(&inode->i_count);
	//	clear_bit(RS_SUSPENDED, &inode->i_rsrvr_flags);
	//}
	//mutex_unlock(&RS_SB(sb)->rt_files_lock);

	/*--------------------*/

	err = p2fat_remove_entries(dir, &sinfo);	/* and releases bh */
	if (err)
		goto out;
	drop_nlink(dir);

	clear_nlink(inode);
	inode->i_ctime = CURRENT_TIME_SEC;
	p2fat_detach(inode);
out:
	unlock_kernel();
	if (!err)
		err = p2fat_flush_inodes(inode->i_sb, dir, inode);

	/* Panasonic Original */
	lock_rton(MAJOR(sb->s_dev));
	if(check_rt_status(sb)){
		p2fat_sync(sb);
		write_inode_now(inode, 1);
	}
	unlock_rton(MAJOR(sb->s_dev));
	/*--------------------*/

	return err;
}

/***** Make a directory */
static int msdos_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	struct super_block *sb = dir->i_sb;
	struct fat_slot_info sinfo;
	struct inode *inode;
	unsigned char msdos_name[MSDOS_NAME];
	struct timespec ts;
	int err, is_hid, cluster;

	lock_kernel();

	err = msdos_format_name(dentry->d_name.name, dentry->d_name.len,
				msdos_name, &P2FAT_SB(sb)->options);
	if (err)
		goto out;
	is_hid = (dentry->d_name.name[0] == '.') && (msdos_name[0] != '.');
	/* foo vs .foo situation */
	if (!p2fat_scan(dir, msdos_name, &sinfo)) {
		brelse(sinfo.bh);
		err = -EINVAL;
		goto out;
	}

	ts = CURRENT_TIME_SEC;
	cluster = p2fat_alloc_new_dir(dir, &ts);
	if (cluster < 0) {
		err = cluster;
		goto out;
	}
	/* Panasonic Change */
	err = msdos_add_entry(dir, msdos_name, 1, is_hid, cluster, &ts, &sinfo, 0);
	/*------------------*/
	if (err)
		goto out_free;
	inc_nlink(dir);

	inode = p2fat_build_inode(sb, sinfo.de, sinfo.i_pos);
	brelse(sinfo.bh);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		/* the directory was completed, just return a error */
		goto out;
	}
	inode->i_nlink = 2;
	inode->i_mtime = inode->i_atime = inode->i_ctime = ts;
	/* timestamp is already written, so mark_inode_dirty() is unneeded. */

	d_instantiate(dentry, inode);

	unlock_kernel();
	p2fat_flush_inodes(sb, dir, inode);

	/* Panasonic Original */
	lock_rton(MAJOR(sb->s_dev));
	if(check_rt_status(sb)){
		p2fat_sync(sb);
		write_inode_now(inode, 1);
	}
	unlock_rton(MAJOR(sb->s_dev));
	/*--------------------*/

	return 0;

out_free:
	p2fat_free_clusters(dir, cluster);
out:
	unlock_kernel();
	return err;
}

/***** Unlink a file */
static int msdos_unlink(struct inode *dir, struct dentry *dentry)
{
	struct super_block *sb = dir->i_sb;
	struct inode *inode = dentry->d_inode;
	struct fat_slot_info sinfo;
	int err;

	lock_kernel();
	err = msdos_find(dir, dentry->d_name.name, dentry->d_name.len, &sinfo);
	if (err)
		goto out;

	/* Panasonic Original */
	mutex_lock(&P2FAT_SB(sb)->rt_inode_dirty_lock);
	if(!list_empty(&P2FAT_I(inode)->i_rt_dirty))
	{
		if(test_bit(FAT_SUSPENDED_INODE, &P2FAT_I(inode)->i_flags)){
			if(P2FAT_I(inode)->suspended_bh){
				put_bh(P2FAT_I(inode)->suspended_bh);
			}
			else{
				printk("[%s-%d] LOST Suspended BH !\n", __PRETTY_FUNCTION__, __LINE__);
			}
			
			clear_bit(FAT_SUSPENDED_INODE, &P2FAT_I(inode)->i_flags);
		}

		clear_bit(FAT_NEWDIR_INODE, &P2FAT_I(inode)->i_flags);

		list_del_init(&P2FAT_I(inode)->i_rt_dirty);
		iput(inode);      
	}
	mutex_unlock(&P2FAT_SB(sb)->rt_inode_dirty_lock);

	//mutex_lock(&RS_SB(sb)->rt_files_lock);
	//if(!list_empty(&inode->i_reservoir_list))
	//{
	//	list_del_init(&inode->i_reservoir_list);
	//	atomic_dec(&inode->i_count);
	//	clear_bit(RS_SUSPENDED, &inode->i_rsrvr_flags);
	//}
	//mutex_unlock(&RS_SB(sb)->rt_files_lock);

	/*--------------------*/

	err = p2fat_remove_entries(dir, &sinfo);	/* and releases bh */
	if (err)
		goto out;
	clear_nlink(inode);
	inode->i_ctime = CURRENT_TIME_SEC;
	p2fat_detach(inode);
out:
	unlock_kernel();
	if (!err)
		err = p2fat_flush_inodes(inode->i_sb, dir, inode);

	/* Panasonic Original */
	lock_rton(MAJOR(sb->s_dev));
	if(check_rt_status(sb)){
		p2fat_sync(sb);
		inode->i_sb->s_op->write_inode(inode, 1);
	}
	unlock_rton(MAJOR(sb->s_dev));
	/*--------------------*/

	return err;
}

static int do_msdos_rename(struct inode *old_dir, unsigned char *old_name,
			   struct dentry *old_dentry,
			   struct inode *new_dir, unsigned char *new_name,
			   struct dentry *new_dentry, int is_hid)
{
	struct super_block *sb = old_dir->i_sb;
	struct buffer_head *dotdot_bh;
	struct msdos_dir_entry *dotdot_de;
	struct inode *old_inode, *new_inode;
	struct fat_slot_info old_sinfo, sinfo;
	struct timespec ts;
	loff_t dotdot_i_pos, new_i_pos;
	int err, old_attrs, is_dir, update_dotdot, corrupt = 0;

	old_sinfo.bh = sinfo.bh = dotdot_bh = NULL;
	old_inode = old_dentry->d_inode;
	new_inode = new_dentry->d_inode;

	err = p2fat_scan(old_dir, old_name, &old_sinfo);
	if (err) {
		err = -EIO;
		goto out;
	}

	is_dir = S_ISDIR(old_inode->i_mode);
	update_dotdot = (is_dir && old_dir != new_dir);
	if (update_dotdot) {
		if (p2fat_get_dotdot_entry(old_inode, &dotdot_bh, &dotdot_de,
					 &dotdot_i_pos) < 0) {
			err = -EIO;
			goto out;
		}
	}

	old_attrs = P2FAT_I(old_inode)->i_attrs;
	err = p2fat_scan(new_dir, new_name, &sinfo);
	if (!err) {
		if (!new_inode) {
			/* "foo" -> ".foo" case. just change the ATTR_HIDDEN */
			if (sinfo.de != old_sinfo.de) {
				err = -EINVAL;
				goto out;
			}
			if (is_hid)
				P2FAT_I(old_inode)->i_attrs |= ATTR_HIDDEN;
			else
				P2FAT_I(old_inode)->i_attrs &= ~ATTR_HIDDEN;
			if (IS_DIRSYNC(old_dir)) {
				err = p2fat_sync_inode(old_inode);
				if (err) {
					P2FAT_I(old_inode)->i_attrs = old_attrs;
					goto out;
				}
			} else
				mark_inode_dirty(old_inode);

			old_dir->i_version++;
			old_dir->i_ctime = old_dir->i_mtime = CURRENT_TIME_SEC;
			if (IS_DIRSYNC(old_dir))
				(void)p2fat_sync_inode(old_dir);
			else
				mark_inode_dirty(old_dir);
			goto out;
		}
	}

	ts = CURRENT_TIME_SEC;
	if (new_inode) {

		/* Panasonic Original */
		mutex_lock(&P2FAT_SB(sb)->rt_inode_dirty_lock);

		if(!list_empty(&P2FAT_I(new_inode)->i_rt_dirty))
		{
			if(test_bit(FAT_SUSPENDED_INODE, &P2FAT_I(new_inode)->i_flags)){
				if(P2FAT_I(new_inode)->suspended_bh){
					put_bh(P2FAT_I(new_inode)->suspended_bh);
				}
				else{
					printk("[%s-%d] LOST Suspended BH !\n", __PRETTY_FUNCTION__, __LINE__);
				}

				clear_bit(FAT_SUSPENDED_INODE, &P2FAT_I(new_inode)->i_flags);
			}

			clear_bit(FAT_NEWDIR_INODE, &P2FAT_I(new_inode)->i_flags);

			list_del_init(&P2FAT_I(new_inode)->i_rt_dirty);
			iput(new_inode);

		}
		mutex_unlock(&P2FAT_SB(sb)->rt_inode_dirty_lock);

		mutex_lock(&RS_SB(sb)->rt_files_lock);
		if(!list_empty(&new_inode->i_reservoir_list))
		{
			list_del_init(&new_inode->i_reservoir_list);
			atomic_dec(&new_inode->i_count);
			clear_bit(RS_SUSPENDED, &new_inode->i_rsrvr_flags);
		}
		mutex_unlock(&RS_SB(sb)->rt_files_lock);

		set_bit(FAT_RM_RESERVED, &P2FAT_I(new_inode)->i_flags);

		/*--------------------*/

		if (err)
			goto out;
		if (is_dir) {
			err = p2fat_dir_empty(new_inode);
			if (err)
				goto out;
		}
		new_i_pos = P2FAT_I(new_inode)->i_pos;
		p2fat_detach(new_inode);
	} else {
		/* Panasonic Change */
		err = msdos_add_entry(new_dir, new_name, is_dir, is_hid, 0,
				      &ts, &sinfo, 0);
		/*------------------*/
		if (err)
			goto out;
		new_i_pos = sinfo.i_pos;
	}
	new_dir->i_version++;

	p2fat_detach(old_inode);
	p2fat_attach(old_inode, new_i_pos);
	if (is_hid)
		P2FAT_I(old_inode)->i_attrs |= ATTR_HIDDEN;
	else
		P2FAT_I(old_inode)->i_attrs &= ~ATTR_HIDDEN;
	if (IS_DIRSYNC(new_dir)) {
		err = p2fat_sync_inode(old_inode);
		if (err)
			goto error_inode;
	} else
		mark_inode_dirty(old_inode);

	if (update_dotdot) {
		int start = P2FAT_I(new_dir)->i_logstart;
		dotdot_de->start = cpu_to_le16(start);
		dotdot_de->starthi = cpu_to_le16(start >> 16);
		mark_buffer_dirty(dotdot_bh);
		if (IS_DIRSYNC(new_dir)) {
			err = sync_dirty_buffer(dotdot_bh);
			if (err)
				goto error_dotdot;
		}
		drop_nlink(old_dir);
		if (!new_inode)
			inc_nlink(new_dir);
	}

	err = p2fat_remove_entries(old_dir, &old_sinfo);	/* and releases bh */
	old_sinfo.bh = NULL;
	if (err)
		goto error_dotdot;
	old_dir->i_version++;
	old_dir->i_ctime = old_dir->i_mtime = ts;
	if (IS_DIRSYNC(old_dir))
		(void)p2fat_sync_inode(old_dir);
	else
		mark_inode_dirty(old_dir);

	if (new_inode) {
		drop_nlink(new_inode);
		if (is_dir)
			drop_nlink(new_inode);
		new_inode->i_ctime = ts;
	}
out:
	brelse(sinfo.bh);
	brelse(dotdot_bh);
	brelse(old_sinfo.bh);

	/* Panasonic Original */
	if(new_inode && old_inode){
		lock_rton(MAJOR(sb->s_dev));
		if(check_rt_status(sb)){
			old_inode->i_sb->s_op->write_inode(old_inode, 1);
			//new_inode->i_sb->s_op->write_inode(new_inode, 1);
			p2fat_sync(sb);
		}
		unlock_rton(MAJOR(sb->s_dev));
	}
	/*--------------------*/
	return err;

error_dotdot:
	/* data cluster is shared, serious corruption */
	corrupt = 1;

	if (update_dotdot) {
		int start = P2FAT_I(old_dir)->i_logstart;
		dotdot_de->start = cpu_to_le16(start);
		dotdot_de->starthi = cpu_to_le16(start >> 16);
		mark_buffer_dirty(dotdot_bh);
		corrupt |= sync_dirty_buffer(dotdot_bh);
	}
error_inode:
	p2fat_detach(old_inode);
	p2fat_attach(old_inode, old_sinfo.i_pos);
	P2FAT_I(old_inode)->i_attrs = old_attrs;
	if (new_inode) {
		p2fat_attach(new_inode, new_i_pos);
		if (corrupt)
			corrupt |= p2fat_sync_inode(new_inode);
	} else {
		/*
		 * If new entry was not sharing the data cluster, it
		 * shouldn't be serious corruption.
		 */
		int err2 = p2fat_remove_entries(new_dir, &sinfo);
		if (corrupt)
			corrupt |= err2;
		sinfo.bh = NULL;
	}
	if (corrupt < 0) {
		p2fat_fs_panic(new_dir->i_sb,
			     "%s: Filesystem corrupted (i_pos %lld)",
			     __FUNCTION__, sinfo.i_pos);
	}
	goto out;
}

/***** Rename, a wrapper for rename_same_dir & rename_diff_dir */
static int msdos_rename(struct inode *old_dir, struct dentry *old_dentry,
			struct inode *new_dir, struct dentry *new_dentry)
{
	unsigned char old_msdos_name[MSDOS_NAME], new_msdos_name[MSDOS_NAME];
	int err, is_hid;

	lock_kernel();

	err = msdos_format_name(old_dentry->d_name.name,
				old_dentry->d_name.len, old_msdos_name,
				&P2FAT_SB(old_dir->i_sb)->options);
	if (err)
		goto out;
	err = msdos_format_name(new_dentry->d_name.name,
				new_dentry->d_name.len, new_msdos_name,
				&P2FAT_SB(new_dir->i_sb)->options);
	if (err)
		goto out;

	is_hid =
	     (new_dentry->d_name.name[0] == '.') && (new_msdos_name[0] != '.');

	err = do_msdos_rename(old_dir, old_msdos_name, old_dentry,
			      new_dir, new_msdos_name, new_dentry, is_hid);
out:
	unlock_kernel();
	if (!err)
		err = p2fat_flush_inodes(old_dir->i_sb, old_dir, new_dir);
	return err;
}

static const struct inode_operations p2fat_dir_inode_operations = {
	.create		= msdos_create,
	.lookup		= msdos_lookup,
	.unlink		= msdos_unlink,
	.mkdir		= msdos_mkdir,
	.rmdir		= msdos_rmdir,
	.rename		= msdos_rename,
	.setattr	= p2fat_notify_change,
	.getattr	= p2fat_getattr,
};

static int p2fat_fill_super(struct super_block *sb, void *data, int silent)
{
	int res;

	res = __p2fat_fill_super(sb, data, silent, &p2fat_dir_inode_operations, 0);
	if (res)
		return res;

	sb->s_flags |= MS_NOATIME;
	sb->s_root->d_op = &p2fat_dentry_operations;
	return 0;
}

static int p2fat_get_sb(struct file_system_type *fs_type,
			int flags, const char *dev_name,
			void *data, struct vfsmount *mnt)
{
	return get_sb_bdev(fs_type, flags, dev_name, data, p2fat_fill_super,
			   mnt);
}

static struct file_system_type p2fat_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "p2fat",
	.get_sb		= p2fat_get_sb,
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
};

static int __init init_p2fat_fs_register(void)
{
	return register_filesystem(&p2fat_fs_type);
}

static void __exit exit_p2fat_fs_unregister(void)
{
	unregister_filesystem(&p2fat_fs_type);
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Werner Almesberger / Seiji Horita");
MODULE_DESCRIPTION("P2FAT filesystem support");

module_init(init_p2fat_fs_register)
module_exit(exit_p2fat_fs_unregister)
