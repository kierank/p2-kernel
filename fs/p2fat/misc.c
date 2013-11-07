/*
 *  linux/fs/p2fat/misc.c
 *
 *  Written 1992,1993 by Werner Almesberger
 *  22/11/2000 - Fixed fat_date_unix2dos for dates earlier than 01/01/1980
 *		 and date_dos2unix for date==0 by Igor Zhbanov(bsg@uniyar.ac.ru)
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/p2fat_fs.h>
#include <linux/buffer_head.h>

/*
 * p2fat_fs_panic reports a severe file system problem and sets the file system
 * read-only. The file system can be made writable again by remounting it.
 */
void p2fat_fs_panic(struct super_block *s, const char *fmt, ...)
{
	va_list args;

	printk(KERN_ERR "P2FAT: Filesystem panic (dev %s)\n", s->s_id);

	printk(KERN_ERR "    ");
	va_start(args, fmt);
	vprintk(fmt, args);
	va_end(args);
	printk("\n");

	if (!(s->s_flags & MS_RDONLY)) {
		s->s_flags |= MS_RDONLY;
		printk(KERN_ERR "    File system has been set read-only\n");
	}
}

EXPORT_SYMBOL_GPL(p2fat_fs_panic);

/* Flushes the number of free clusters on FAT32 */
/* XXX: Need to write one per FSINFO block.  Currently only writes 1 */
void p2fat_clusters_flush(struct super_block *sb, int sync)
{
	struct p2fat_sb_info *sbi = P2FAT_SB(sb);
	struct buffer_head *bh;
	struct p2fat_boot_fsinfo *fsinfo;

	if (sbi->fat_bits != 32)
		return;

	bh = sb_bread(sb, sbi->fsinfo_sector);
	if (bh == NULL) {
		printk(KERN_ERR "FAT: bread failed in p2fat_clusters_flush\n");
		return;
	}

	fsinfo = (struct p2fat_boot_fsinfo *)bh->b_data;
	/* Sanity check */
	if (!IS_FSINFO(fsinfo)) {
		printk(KERN_ERR "FAT: Did not find valid FSINFO signature.\n"
		       "     Found signature1 0x%08x signature2 0x%08x"
		       " (sector = %lu)\n",
		       le32_to_cpu(fsinfo->signature1),
		       le32_to_cpu(fsinfo->signature2),
		       sbi->fsinfo_sector);
	} else {
		if (sbi->free_clusters != -1) {
			/* Panasonic Original (checking updated) */
			if (fsinfo->free_clusters != cpu_to_le32(sbi->free_clusters)) {
				fsinfo->free_clusters = cpu_to_le32(sbi->free_clusters);
				mark_buffer_dirty(bh);
			}
		}
		if (sbi->prev_free != -1) {
			/* Panasonic Original (checking updated) */
			if (fsinfo->next_cluster != cpu_to_le32(sbi->prev_free)) {
				fsinfo->next_cluster = cpu_to_le32(sbi->prev_free);
				mark_buffer_dirty(bh);
			}
		}

		/* Panasonic Original */
		set_bit(BH_Dirent, &bh->b_state); //FS Info bh.
		/*--------------------*/

		if(sync && buffer_dirty(bh))
		{
			lock_rton(MAJOR(sb->s_dev));

			if(check_rt_status(sb))
			{
				struct request_queue *q = bdev_get_queue(sb->s_bdev);

				q->elevator->ops->elevator_force_dispatch_fn(q, 0);
				lock_buffer(bh);

				if(test_clear_buffer_dirty(bh))
				{
					if(q->elevator->ops->elevator_force_dispatch_fn!=NULL)
					  {
					    get_bh(bh);
					    bh->b_end_io = end_buffer_write_sync;
					    submit_bh(WRITE, bh);
					    q->elevator->ops->elevator_force_dispatch_fn(q, 0);
					  }
					else
					  {
					    sync_dirty_buffer(bh);
					  }
				}
				else
				{
					unlock_buffer(bh);
				}
			}

			unlock_rton(MAJOR(sb->s_dev));
		}

	}

	brelse(bh);
}

/*
 * fat_chain_add() adds a new cluster to the chain of clusters represented
 * by inode.
 */
int p2fat_chain_add(struct inode *inode, int new_dclus, int nr_cluster)
{
	struct super_block *sb = inode->i_sb;
	struct p2fat_sb_info *sbi = P2FAT_SB(sb);
	int ret, new_fclus, last;

	/*
	 * We must locate the last cluster of the file to add this new
	 * one (new_dclus) to the end of the link list (the FAT).
	 */
	last = new_fclus = 0;
	if (P2FAT_I(inode)->i_start) {
		int fclus, dclus;

		ret = p2fat_get_cluster(inode, FAT_ENT_EOF, &fclus, &dclus, 0);
		if (ret < 0)
			return ret;
		new_fclus = fclus + 1;
		last = dclus;
	}

	/* add new one to the last of the cluster chain */
	if (last) {
		struct p2fat_entry fatent;

		p2fatent_init(&fatent);

		ret = p2fat_ent_read(inode, &fatent, last);
		if (ret >= 0) {
			int wait = inode_needs_sync(inode);
			ret = p2fat_ent_write(inode, &fatent, new_dclus, wait);
			p2fatent_brelse(&fatent);
		}

		if (ret < 0)
			return ret;
//		fat_cache_add(inode, new_fclus, new_dclus);
	} else {
		P2FAT_I(inode)->i_start = new_dclus;
		P2FAT_I(inode)->i_logstart = new_dclus;
		/*
		 * Since generic_osync_inode() synchronize later if
		 * this is not directory, we don't here.
		 */
		if (S_ISDIR(inode->i_mode) && IS_DIRSYNC(inode)) {
			ret = p2fat_sync_inode(inode);
			if (ret)
				return ret;
		} else {
			lock_rton(MAJOR(sb->s_dev));
			if (check_delay_status(sb)){
				if(!test_bit(RS_RT, &inode->i_rsrvr_flags)
				   && (atomic_read(&(RS_SB(sb)->rt_total_files)))==0
				   && check_rt_status(sb))
				{
					p2fat_sync(sb);
				}
				mark_inode_dirty(inode);
			} else {
				mutex_lock(&P2FAT_SB(inode->i_sb)->rt_inode_dirty_lock);
				set_bit(FAT_NEWDIR_INODE, &P2FAT_I(inode)->i_flags);
				if(list_empty(&P2FAT_I(inode)->i_rt_dirty)){
					__iget(inode);
					list_add_tail(&P2FAT_I(inode)->i_rt_dirty, &P2FAT_SB(inode->i_sb)->rt_inode_dirty_list);
				}
				mutex_unlock(&P2FAT_SB(inode->i_sb)->rt_inode_dirty_lock);
			}
			unlock_rton(MAJOR(sb->s_dev));
		}
	}
	if (new_fclus != (inode->i_blocks >> (sbi->cluster_bits - 9))) {
		p2fat_fs_panic(sb, "clusters badly computed (%d != %lu)",
			new_fclus, inode->i_blocks >> (sbi->cluster_bits - 9));
		p2fat_cache_inval_inode(inode);
	}
	inode->i_blocks += nr_cluster << (sbi->cluster_bits - 9);

	return 0;
}

extern struct timezone sys_tz;

/* Linear day numbers of the respective 1sts in non-leap years. */
static int day_n[] = {
   /* Jan  Feb  Mar  Apr   May  Jun  Jul  Aug  Sep  Oct  Nov  Dec */
	0,  31,  59,  90,  120, 151, 181, 212, 243, 273, 304, 334, 0, 0, 0, 0
};

/* Convert a MS-DOS time/date pair to a UNIX date (seconds since 1 1 70). */
int p2fat_date_dos2unix(unsigned short time, unsigned short date)
{
	int month, year, secs;

	/*
	 * first subtract and mask after that... Otherwise, if
	 * date == 0, bad things happen
	 */
	month = ((date >> 5) - 1) & 15;
	year = date >> 9;
	secs = (time & 31)*2+60*((time >> 5) & 63)+(time >> 11)*3600+86400*
	    ((date & 31)-1+day_n[month]+(year/4)+year*365-((year & 3) == 0 &&
	    month < 2 ? 1 : 0)+3653);
			/* days since 1.1.70 plus 80's leap day */
	secs += sys_tz.tz_minuteswest*60;
	return secs;
}

/* Convert linear UNIX date to a MS-DOS time/date pair. */
void p2fat_date_unix2dos(int unix_date, __le16 *time, __le16 *date)
{
	int day, year, nl_day, month;

	unix_date -= sys_tz.tz_minuteswest*60;

	/* Jan 1 GMT 00:00:00 1980. But what about another time zone? */
	if (unix_date < 315532800)
		unix_date = 315532800;

	*time = cpu_to_le16((unix_date % 60)/2+(((unix_date/60) % 60) << 5)+
	    (((unix_date/3600) % 24) << 11));
	day = unix_date/86400-3652;
	year = day/365;
	if ((year+3)/4+365*year > day)
		year--;
	day -= (year+3)/4+365*year;
	if (day == 59 && !(year & 3)) {
		nl_day = day;
		month = 2;
	} else {
		nl_day = (year & 3) || day <= 59 ? day : day-1;
		for (month = 0; month < 12; month++) {
			if (day_n[month] > nl_day)
				break;
		}
	}
	*date = cpu_to_le16(nl_day-day_n[month-1]+1+(month << 5)+(year << 9));
}

EXPORT_SYMBOL_GPL(p2fat_date_unix2dos);

int p2fat_sync_bhs(struct buffer_head **bhs, int nr_bhs)
{
	int i, err = 0;

	ll_rw_block(SWRITE, nr_bhs, bhs);
	for (i = 0; i < nr_bhs; i++) {
		wait_on_buffer(bhs[i]);
		if (buffer_eopnotsupp(bhs[i])) {
			clear_buffer_eopnotsupp(bhs[i]);
			err = -EOPNOTSUPP;
		} else if (!err && !buffer_uptodate(bhs[i]))
			err = -EIO;
	}
	return err;
}

