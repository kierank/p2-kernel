/*
 * Copyright (C) 2004, OGAWA Hirofumi
 * Released under GPL v2.
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/sdfat_fs.h>

/* 128kb is the whole sectors for FAT12 and FAT16 */
#define FAT_READA_SIZE		(128 * 1024)

static int fat_cont_search(struct inode *, struct file *, struct fat_ioctl_space *);
static int fat_alloc_cluster(struct inode *, int *, struct buffer_head **, int *);

#if defined(CONFIG_SDFAT_USE_RICOH_R5C822)
static int fat_block_cont_search(struct inode *, struct file *, struct fat_ioctl_space *);
static int fat_alloc_block_cluster(struct inode *, int *, struct buffer_head **, int *);
#elif defined(CONFIG_SDFAT_USE_SM331)
static int __sdfat_free_clusters(struct inode *inode, int cluster, int lock);
#endif
static void fat_ent_reada(struct super_block *sb, struct fat_entry *fatent,
			  unsigned long reada_blocks);

struct fatent_operations {
	void (*ent_blocknr)(struct super_block *, int, int *, sector_t *);
	void (*ent_set_ptr)(struct fat_entry *, int);
	int (*ent_bread)(struct super_block *, struct fat_entry *,
			 int, sector_t);
	int (*ent_get)(struct fat_entry *);
	void (*ent_put)(struct fat_entry *, int);
	int (*ent_next)(struct fat_entry *);
	/* Modified by Panasonic (SAV), 2009-oct-5 */
	int (*cont_search)(struct inode *, struct file *, struct fat_ioctl_space *);
	int (*alloc_cluster)(struct inode *, int *, struct buffer_head **, int *);
	/*-----------------------------------------*/
};

static DEFINE_SPINLOCK(fat12_entry_lock);

static void fat12_ent_blocknr(struct super_block *sb, int entry,
			      int *offset, sector_t *blocknr)
{
	struct sdfat_sb_info *sbi = SDFAT_SB(sb);
	int bytes = entry + (entry >> 1);
	WARN_ON(entry < FAT_START_ENT || sbi->max_cluster <= entry);
	*offset = bytes & (sb->s_blocksize - 1);
	*blocknr = sbi->fat_start + (bytes >> sb->s_blocksize_bits);
}

static void fat_ent_blocknr(struct super_block *sb, int entry,
			    int *offset, sector_t *blocknr)
{
	struct sdfat_sb_info *sbi = SDFAT_SB(sb);
	int bytes = (entry << sbi->fatent_shift);
	WARN_ON(entry < FAT_START_ENT || sbi->max_cluster <= entry);
	*offset = bytes & (sb->s_blocksize - 1);
	*blocknr = sbi->fat_start + (bytes >> sb->s_blocksize_bits);
}

static void fat12_ent_set_ptr(struct fat_entry *fatent, int offset)
{
	struct buffer_head **bhs = fatent->bhs;
	if (fatent->nr_bhs == 1) {
		WARN_ON(offset >= (bhs[0]->b_size - 1));
		fatent->u.ent12_p[0] = bhs[0]->b_data + offset;
		fatent->u.ent12_p[1] = bhs[0]->b_data + (offset + 1);
	} else {
		WARN_ON(offset != (bhs[0]->b_size - 1));
		fatent->u.ent12_p[0] = bhs[0]->b_data + offset;
		fatent->u.ent12_p[1] = bhs[1]->b_data;
	}
}

static void fat16_ent_set_ptr(struct fat_entry *fatent, int offset)
{
	WARN_ON(offset & (2 - 1));
	fatent->u.ent16_p = (__le16 *)(fatent->bhs[0]->b_data + offset);
}

static void fat32_ent_set_ptr(struct fat_entry *fatent, int offset)
{
	WARN_ON(offset & (4 - 1));
	fatent->u.ent32_p = (__le32 *)(fatent->bhs[0]->b_data + offset);
}

static int fat12_ent_bread(struct super_block *sb, struct fat_entry *fatent,
			   int offset, sector_t blocknr)
{
	struct buffer_head **bhs = fatent->bhs;

	WARN_ON(blocknr < SDFAT_SB(sb)->fat_start);
	bhs[0] = meta_bread(sb, blocknr, BH_Fat);
	if (!bhs[0])
		goto err;

	if ((offset + 1) < sb->s_blocksize)
		fatent->nr_bhs = 1;
	else {
		/* This entry is block boundary, it needs the next block */
		blocknr++;
		bhs[1] = meta_bread(sb, blocknr, BH_Fat);
		if (!bhs[1])
			goto err_brelse;
		fatent->nr_bhs = 2;
	}
	fat12_ent_set_ptr(fatent, offset);
	return 0;

err_brelse:
	brelse(bhs[0]);
err:
	printk(KERN_ERR "FAT: FAT read failed (blocknr %llu)\n",
	       (unsigned long long)blocknr);
	return -EIO;
}

static int fat_ent_bread(struct super_block *sb, struct fat_entry *fatent,
			 int offset, sector_t blocknr)
{
	struct fatent_operations *ops = SDFAT_SB(sb)->fatent_ops;

	WARN_ON(blocknr < SDFAT_SB(sb)->fat_start);
	fatent->bhs[0] = meta_bread(sb, blocknr, BH_Fat);
	if (!fatent->bhs[0]) {
		printk(KERN_ERR "FAT: FAT read failed (blocknr %llu)\n",
		       (unsigned long long)blocknr);
		return -EIO;
	}
	fatent->nr_bhs = 1;
	ops->ent_set_ptr(fatent, offset);
	return 0;
}

static int fat12_ent_get(struct fat_entry *fatent)
{
	u8 **ent12_p = fatent->u.ent12_p;
	int next;

	spin_lock(&fat12_entry_lock);
	if (fatent->entry & 1)
		next = (*ent12_p[0] >> 4) | (*ent12_p[1] << 4);
	else
		next = (*ent12_p[1] << 8) | *ent12_p[0];
	spin_unlock(&fat12_entry_lock);

	next &= 0x0fff;
	if (next >= BAD_FAT12)
		next = FAT_ENT_EOF;
	return next;
}

static int fat16_ent_get(struct fat_entry *fatent)
{
	int next = le16_to_cpu(*fatent->u.ent16_p);
	WARN_ON((unsigned long)fatent->u.ent16_p & (2 - 1));
	if (next >= BAD_FAT16)
		next = FAT_ENT_EOF;
	return next;
}

static int fat32_ent_get(struct fat_entry *fatent)
{
	int next = le32_to_cpu(*fatent->u.ent32_p) & 0x0fffffff;
	WARN_ON((unsigned long)fatent->u.ent32_p & (4 - 1));
	if (next >= BAD_FAT32)
		next = FAT_ENT_EOF;
	return next;
}

static void fat12_ent_put(struct fat_entry *fatent, int new)
{
	u8 **ent12_p = fatent->u.ent12_p;

	if (new == FAT_ENT_EOF)
		new = EOF_FAT12;

	spin_lock(&fat12_entry_lock);
	if (fatent->entry & 1) {
		*ent12_p[0] = (new << 4) | (*ent12_p[0] & 0x0f);
		*ent12_p[1] = new >> 4;
	} else {
		*ent12_p[0] = new & 0xff;
		*ent12_p[1] = (*ent12_p[1] & 0xf0) | (new >> 8);
	}
	spin_unlock(&fat12_entry_lock);

	mark_buffer_dirty(fatent->bhs[0]);
	if (fatent->nr_bhs == 2)
		mark_buffer_dirty(fatent->bhs[1]);
}

static void fat16_ent_put(struct fat_entry *fatent, int new)
{
	if (new == FAT_ENT_EOF)
		new = EOF_FAT16;

	*fatent->u.ent16_p = cpu_to_le16(new);
	mark_buffer_dirty(fatent->bhs[0]);
}

static void fat32_ent_put(struct fat_entry *fatent, int new)
{
	if (new == FAT_ENT_EOF)
		new = EOF_FAT32;

	WARN_ON(new & 0xf0000000);
	new |= le32_to_cpu(*fatent->u.ent32_p) & ~0x0fffffff;
	*fatent->u.ent32_p = cpu_to_le32(new);
	mark_buffer_dirty(fatent->bhs[0]);
}

static int fat12_ent_next(struct fat_entry *fatent)
{
	u8 **ent12_p = fatent->u.ent12_p;
	struct buffer_head **bhs = fatent->bhs;
	u8 *nextp = ent12_p[1] + 1 + (fatent->entry & 1);

	fatent->entry++;
	if (fatent->nr_bhs == 1) {
		WARN_ON(ent12_p[0] > (u8 *)(bhs[0]->b_data + (bhs[0]->b_size - 2)));
		WARN_ON(ent12_p[1] > (u8 *)(bhs[0]->b_data + (bhs[0]->b_size - 1)));
		if (nextp < (u8 *)(bhs[0]->b_data + (bhs[0]->b_size - 1))) {
			ent12_p[0] = nextp - 1;
			ent12_p[1] = nextp;
			return 1;
		}
	} else {
		WARN_ON(ent12_p[0] != (u8 *)(bhs[0]->b_data + (bhs[0]->b_size - 1)));
		WARN_ON(ent12_p[1] != (u8 *)bhs[1]->b_data);
		ent12_p[0] = nextp - 1;
		ent12_p[1] = nextp;
		brelse(bhs[0]);
		bhs[0] = bhs[1];
		fatent->nr_bhs = 1;
		return 1;
	}
	ent12_p[0] = NULL;
	ent12_p[1] = NULL;
	return 0;
}

static int fat16_ent_next(struct fat_entry *fatent)
{
	const struct buffer_head *bh = fatent->bhs[0];
	fatent->entry++;
	if (fatent->u.ent16_p < (__le16 *)(bh->b_data + (bh->b_size - 2))) {
		fatent->u.ent16_p++;
		return 1;
	}
	fatent->u.ent16_p = NULL;
	return 0;
}

static int fat32_ent_next(struct fat_entry *fatent)
{
	const struct buffer_head *bh = fatent->bhs[0];
	fatent->entry++;
	if (fatent->u.ent32_p < (__le32 *)(bh->b_data + (bh->b_size - 4))) {
		fatent->u.ent32_p++;
		return 1;
	}
	fatent->u.ent32_p = NULL;
	return 0;
}

static struct fatent_operations fat12_ops = {
	.ent_blocknr	= fat12_ent_blocknr,
	.ent_set_ptr	= fat12_ent_set_ptr,
	.ent_bread	= fat12_ent_bread,
	.ent_get	= fat12_ent_get,
	.ent_put	= fat12_ent_put,
	.ent_next	= fat12_ent_next,
	/* Modified by Panasonic (SAV), 2009-oct-5 */
	.cont_search	= fat_cont_search,
	.alloc_cluster	= fat_alloc_cluster,
	/*-----------------------------------------*/
};

static struct fatent_operations fat16_ops = {
	.ent_blocknr	= fat_ent_blocknr,
	.ent_set_ptr	= fat16_ent_set_ptr,
	.ent_bread	= fat_ent_bread,
	.ent_get	= fat16_ent_get,
	.ent_put	= fat16_ent_put,
	.ent_next	= fat16_ent_next,
	/* Modified by Panasonic (SAV), 2009-oct-5 */
	.cont_search	= fat_cont_search,
	.alloc_cluster	= fat_alloc_cluster,
	/*-----------------------------------------*/
};

static struct fatent_operations fat32_ops = {
	.ent_blocknr	= fat_ent_blocknr,
	.ent_set_ptr	= fat32_ent_set_ptr,
	.ent_bread	= fat_ent_bread,
	.ent_get	= fat32_ent_get,
	.ent_put	= fat32_ent_put,
	.ent_next	= fat32_ent_next,
	/* Modified by Panasonic (SAV), 2009-oct-5 */
#if defined(CONFIG_SDFAT_USE_RICOH_R5C822)
	.cont_search	= fat_block_cont_search,
	.alloc_cluster	= fat_alloc_block_cluster,
#elif defined(CONFIG_SDFAT_USE_SM331)
	.cont_search	= fat_cont_search,
	.alloc_cluster	= fat_alloc_cluster,
#else
	.cont_search	= fat_cont_search,
	.alloc_cluster	= NULL,
#endif
	/*-----------------------------------------*/
};

void sdfat_ent_access_init(struct super_block *sb)
{
	struct sdfat_sb_info *sbi = SDFAT_SB(sb);

	mutex_init(&sbi->fat_lock);

	/* Modified by Panasonic (SAV), 2009-oct-5 */
	sbi->cont_space.n = 0;
	sbi->cont_space.prev_free = 0;
	sbi->cont_space.cont = 0;
	sbi->cont_space.pos = 0;

#if defined(CONFIG_SDFAT_USE_SM331)
	sbi->use_continuously = 0;
	INIT_LIST_HEAD(&sbi->open_list);
	mutex_init(&sbi->open_lru_lock);
#endif //CONFIG_SDFAT_USE_SM331
	/*-----------------------------------------*/

	switch (sbi->fat_bits) {
	case 32:
		sbi->fatent_shift = 2;
		sbi->fatent_ops = &fat32_ops;
		break;
	case 16:
		sbi->fatent_shift = 1;
		sbi->fatent_ops = &fat16_ops;
		break;
	case 12:
		sbi->fatent_shift = -1;
		sbi->fatent_ops = &fat12_ops;
		break;
	}
}

static inline int fat_ent_update_ptr(struct super_block *sb,
				     struct fat_entry *fatent,
				     int offset, sector_t blocknr)
{
	struct sdfat_sb_info *sbi = SDFAT_SB(sb);
	struct fatent_operations *ops = sbi->fatent_ops;
	struct buffer_head **bhs = fatent->bhs;

	/* Is this fatent's blocks including this entry? */
	if (!fatent->nr_bhs || bhs[0]->b_blocknr != blocknr)
		return 0;
	/* Does this entry need the next block? */
	if (sbi->fat_bits == 12 && (offset + 1) >= sb->s_blocksize) {
		if (fatent->nr_bhs != 2 || bhs[1]->b_blocknr != (blocknr + 1))
			return 0;
	}
	ops->ent_set_ptr(fatent, offset);
	return 1;
}

int sdfat_ent_read(struct inode *inode, struct fat_entry *fatent, int entry)
{
	struct super_block *sb = inode->i_sb;
	struct sdfat_sb_info *sbi = SDFAT_SB(inode->i_sb);
	struct fatent_operations *ops = sbi->fatent_ops;
	int err, offset;
	sector_t blocknr;

	if (entry < FAT_START_ENT || sbi->max_cluster <= entry) {
		fatent_brelse(fatent);
		sdfat_fs_panic(sb, "invalid access to FAT (entry 0x%08x)", entry);
		return -EIO;
	}

	fatent_set_entry(fatent, entry);
	ops->ent_blocknr(sb, entry, &offset, &blocknr);

	if (!fat_ent_update_ptr(sb, fatent, offset, blocknr)) {
		fatent_brelse(fatent);
		err = ops->ent_bread(sb, fatent, offset, blocknr);
		if (err)
			return err;
	}
	return ops->ent_get(fatent);
}

/* FIXME: We can write the blocks as more big chunk. */
static int fat_mirror_bhs(struct super_block *sb, struct buffer_head **bhs,
			  int nr_bhs)
{
	struct sdfat_sb_info *sbi = SDFAT_SB(sb);
	struct buffer_head *c_bh;
	int err, n, copy;

	err = 0;
	for (copy = 1; copy < sbi->fats; copy++) {
		sector_t backup_fat = sbi->fat_length * copy;

		for (n = 0; n < nr_bhs; n++) {
			c_bh = sb_getblk(sb, backup_fat + bhs[n]->b_blocknr);
			if (!c_bh) {
				err = -ENOMEM;
				goto error;
			}
			set_bit(BH_Fat, &c_bh->b_state);

			memcpy(c_bh->b_data, bhs[n]->b_data, sb->s_blocksize);
			set_buffer_uptodate(c_bh);
			mark_buffer_dirty(c_bh);
			if (sb->s_flags & MS_SYNCHRONOUS)
				err = sync_dirty_buffer(c_bh);
			brelse(c_bh);
			if (err)
				goto error;
		}
	}
error:
	return err;
}

int sdfat_ent_write(struct inode *inode, struct fat_entry *fatent,
		  int new, int wait)
{
	struct super_block *sb = inode->i_sb;
	struct fatent_operations *ops = SDFAT_SB(sb)->fatent_ops;
	int err;

	ops->ent_put(fatent, new);
	if (wait) {
		err = sdfat_sync_bhs(fatent->bhs, fatent->nr_bhs);
		if (err)
			return err;
	}
	return fat_mirror_bhs(sb, fatent->bhs, fatent->nr_bhs);
}

static inline int fat_ent_next(struct sdfat_sb_info *sbi,
			       struct fat_entry *fatent)
{
	if (sbi->fatent_ops->ent_next(fatent)) {
		if (fatent->entry < sbi->max_cluster)
			return 1;
	}
	return 0;
}

static inline int fat_ent_read_block(struct super_block *sb,
				     struct fat_entry *fatent)
{
	struct fatent_operations *ops = SDFAT_SB(sb)->fatent_ops;
	sector_t blocknr;
	int offset;

	if(fatent->entry < FAT_START_ENT || SDFAT_SB(sb)->max_cluster <= fatent->entry)
		return -EINVAL;

	fatent_brelse(fatent);
	ops->ent_blocknr(sb, fatent->entry, &offset, &blocknr);
	return ops->ent_bread(sb, fatent, offset, blocknr);
}

static void fat_collect_bhs(struct buffer_head **bhs, int *nr_bhs,
			    struct fat_entry *fatent)
{
	int n, i;

	for (n = 0; n < fatent->nr_bhs; n++) {
		for (i = 0; i < *nr_bhs; i++) {
			if (fatent->bhs[n] == bhs[i])
				break;
		}
		if (i == *nr_bhs) {
			get_bh(fatent->bhs[n]);
			bhs[i] = fatent->bhs[n];
			(*nr_bhs)++;
		}
	}
}

/* Modified by Panasonic (SAV), 2009-oct-5 */
static int sdfat_check_cont_space(struct super_block *sb, int cluster)
{
	struct sdfat_sb_info *sbi = SDFAT_SB(sb);
	struct fatent_operations *ops = sbi->fatent_ops;
	struct fat_entry fatent;
	int i, pos, ret = 0;

	//サーチ開始ずみかチェック
	if(!sbi->cont_space.n){
		return 0;
	}

	pos = cluster - FAT_START_ENT;
	if(pos < 0){
		return 0;
	}

	pos = pos / sbi->cont_space.n;
	if(pos >= sbi->cont_space.pos){
		return 0;
	}

	fatent_init(&fatent);
	fatent_set_entry(&fatent, pos * sbi->cont_space.n + FAT_START_ENT);

	//読み込む
	if(fat_ent_read_block(sb, &fatent)){
		printk("fat_ent_read_block error\n");
		goto out;
	}

	for(i = 0; i < sbi->cont_space.n; i++){
		if (ops->ent_get(&fatent) != FAT_ENT_FREE) { //空いていない場合
			goto out;
		}

		if(fatent.entry >= sbi->max_cluster - 1)
			break;

		if(!fat_ent_next(sbi, &fatent)){
			if(fat_ent_read_block(sb, &fatent)){
				printk("fat_ent_read_block error\n");
				goto out;
			}
		}
	}
	ret = 1;

out:
	fatent_brelse(&fatent);

	return ret;
}
/*-----------------------------------------*/

/* Panasonic Original */
static int sdfat_fast_check_cont_space(struct super_block *sb, int cluster, int pre_entry, int *offset, int *au_num, int eof)
{
	struct sdfat_sb_info *sbi = SDFAT_SB(sb);
	int pos, ret = 0;
	int pre_off, cur_off;
	int pre_au, cur_au;

	//サーチ開始ずみかチェック
	if(!sbi->cont_space.n){
		return 0;
	}

	pos = cluster - FAT_START_ENT;
	if(pos < 0){
		return 0;
	}

	pre_off = *offset; //前回のAU内のクラスタ番号
	pre_au = *au_num;  //前回のAU番号

	cur_off = pos % sbi->cont_space.n; //今回のAU内のクラスタ番号
	cur_au = pos / sbi->cont_space.n;  //今回のAU番号

	//今回の分を覚えておく
	*offset = cur_off;
	*au_num = cur_au;

	//前回と同じAU
	if(cur_au == pre_au){
		//前回の次のクラスタ
		if((pre_off >= 0) && (pre_off + 1 == cur_off)){
			//AU内の最終クラスタ
			if(cur_off + 1 == sbi->cont_space.n){
				*offset = -1;
				*au_num = -1;
				if(cur_au < sbi->cont_space.pos){ //サーチ済み
					//カウントアップ
					ret++;
				}
			}
			//AU内の終端でないクラスタ
			else{
				if(eof){ //ファイル終端の場合はチェック
					if(cur_au < sbi->cont_space.pos){ //サーチ済み
						ret += sdfat_check_cont_space(sb, cluster);
					}
				}
			}
		}
		//前回とは不連続のクラスタ
		else{
			*offset = -1;
			if(eof){ //ファイル終端の場合はチェック
				if(cur_au < sbi->cont_space.pos){ //サーチ済み
					ret += sdfat_check_cont_space(sb, cluster);
				}
			}
		}
	}
	//前回と違うAU
	else{
		if(cur_off != 0){
			*offset = -1;
		}

		//前回分のAUがあるときはその領域をサーチ
		if(pre_au != -1){
			if(pre_au >= sbi->cont_space.pos){ //未サーチ
				if(eof){ //ファイル終端の場合はチェック
					if(cur_au < sbi->cont_space.pos){ //サーチ済み
						ret += sdfat_check_cont_space(sb, cluster);
					}
				}
			}
			else{
				if(sdfat_check_cont_space(sb, pre_entry))
					ret++;
				if(eof){ //ファイル終端の場合はチェック
					if(cur_au < sbi->cont_space.pos){ //サーチ済み
						if(sdfat_check_cont_space(sb, cluster))
							ret++;
					}
				}
			}
		}
		else{
			if(eof){ //ファイル終端の場合はチェック
				if(cur_au < sbi->cont_space.pos){ //サーチ済み
					if(sdfat_check_cont_space(sb, cluster))
						ret++;
				}
			}
		}
	}

	return ret;
}
/*----------------------*/

/* Modified by Panasonic (SAV), 2009-oct-5 */
#if defined(CONFIG_SDFAT_USE_SM331)
int sdfat_replace_chain(struct inode *inode, struct inode *next_inode, int exist)
{
	int err = 0;
	struct super_block *sb = inode->i_sb;
	struct sdfat_sb_info *sbi = SDFAT_SB(sb);
	struct fat_entry fatent, prev_ent;
	int last_cluster, next_last_cluster, next_cluster;
	struct fatent_operations *ops = sbi->fatent_ops;

	//念のためもう一回チェック
	if(sbi->sd_info_sb.write_pos.inode != inode){
		return 0;
	}

	last_cluster = SDFAT_I(inode)->sd_info_i.last_cluster;

	fatent_init(&fatent);
	fatent_init(&prev_ent);

	fatent_set_entry(&fatent, last_cluster);
	err = fat_ent_read_block(sb, &fatent);
	if (err)
		goto out;

	next_cluster = ops->ent_get(&fatent);
	if(next_cluster == FAT_ENT_FREE){
		sdfat_fs_panic(sb, "%s:%d: invalid cluster chain"
			     " (cluster %lld)", __FUNCTION__, __LINE__,
			     last_cluster);
		err = -EIO;
		goto out;
	}

	//まだ開いているファイルがある場合
	if(next_inode){
		sbi->sd_info_sb.write_pos.inode = next_inode;

		if(next_cluster == FAT_ENT_EOF)
			goto out;

		next_last_cluster = SDFAT_I(next_inode)->sd_info_i.last_cluster;

		err = sdfat_ent_write(inode, &fatent, FAT_ENT_EOF, 0);
		if (err)
			goto out;

		fatent_set_entry(&prev_ent, next_last_cluster);
		err = fat_ent_read_block(sb, &prev_ent);
		if (err)
			goto out;

		if(ops->ent_get(&prev_ent) != FAT_ENT_EOF){
			sdfat_fs_panic(sb, "%s:%d: invalid cluster chain"
				     " (cluster %d : %08x)", __FUNCTION__, __LINE__,
				     next_last_cluster, ops->ent_get(&prev_ent));
			err = -EIO;
			goto out;
		}

		err = sdfat_ent_write(next_inode, &prev_ent, next_cluster, 0);
		if (err)
			goto out;
	}
	//まだデータがかきこまれていないファイルがある場合
	else if(exist){
		sbi->sd_info_sb.write_pos.inode = NULL;

		if(next_cluster == FAT_ENT_EOF)
			goto out;

		err = sdfat_ent_write(inode, &fatent, FAT_ENT_EOF, 0);
		if (err)
			goto out;
	}
	//これが最後のファイルの場合
	else{
		sbi->sd_info_sb.write_pos.cluster = 0;
		sbi->sd_info_sb.write_pos.offset = 0;
		sbi->sd_info_sb.write_pos.inode = NULL;

		if(next_cluster == FAT_ENT_EOF)
			goto out;

		err = sdfat_ent_write(inode, &fatent, FAT_ENT_EOF, 0);
		if (err)
			goto out;

		err = __sdfat_free_clusters(inode, next_cluster, 0);
		if (err)
			goto out;
	}

out:
	fatent_brelse(&fatent);
	fatent_brelse(&prev_ent);

	return err;
}
#endif //CONFIG_SDFAT_USE_SM331
/*-----------------------------------------*/

/* Modified by Panasonic (SAV), 2009-oct-5 */
static int sdfat_alloc_cluster_from_au_block(struct inode *inode, int *cluster, int *chain)
{
	struct super_block *sb = inode->i_sb;
	struct sdfat_sb_info *sbi = SDFAT_SB(sb);
	struct fatent_operations *ops = sbi->fatent_ops;
	struct fat_entry fatent;
	int err = 0;
	long offset = sbi->sd_info_sb.write_pos.offset;
	unsigned long clust = sbi->sd_info_sb.write_pos.cluster;
	int au_cluster = sbi->sd_info_sb.param.au_size >> (SDFAT_SB(sb)->cluster_bits - 10);

#if defined(CONFIG_SDFAT_USE_SM331)
	*chain = 1; //あとでFATチェーンをつなぐ必要あり

	if(offset == au_cluster){
		sbi->sd_info_sb.write_pos.offset = 0;
		return 0;
	}
#endif //CONFIG_SDFAT_USE_SM331

	/* when free AU block is found. */
	if(offset != 0){
		fatent_init(&fatent);
#if defined(CONFIG_SDFAT_USE_RICOH_R5C822)
		fatent_set_entry(&fatent, clust + offset + FAT_START_ENT);

		sbi->sd_info_sb.write_pos.offset = 0;

		while (offset < au_cluster) {
			if (fatent.entry >= sbi->max_cluster)
				goto out;

			fatent_set_entry(&fatent, fatent.entry);

			err = fat_ent_read_block(sb, &fatent);
			if (err)
				goto out;

			do {
				if (ops->ent_get(&fatent) == FAT_ENT_FREE) {
					sbi->sd_info_sb.write_pos.offset = offset;

					/* make the cluster chain */
					ops->ent_put(&fatent, FAT_ENT_EOF);

					sbi->prev_free = clust + offset + FAT_START_ENT;
					if (sbi->free_clusters != -1)
						sbi->free_clusters--;
					sb->s_dirt = 1;

					*cluster = sbi->prev_free;
					goto out;
				}
				offset++;
				if(offset == au_cluster)
					break;
			} while (fat_ent_next(sbi, &fatent));
		}

#elif defined(CONFIG_SDFAT_USE_SM331)
		sbi->sd_info_sb.write_pos.offset++;
		if(sbi->sd_info_sb.write_pos.inode == inode){
			*chain = 0; //すでにFATチェーンはつながっているはず
		}
		else{
			struct inode *pre_inode = sbi->sd_info_sb.write_pos.inode;

			if(pre_inode){
				//AUチェーンにつながっていたファイルの終端をEOFにする
				fatent_set_entry(&fatent, SDFAT_I(pre_inode)->sd_info_i.last_cluster);
				err = fat_ent_read_block(sb, &fatent);
				if (err)
					goto out;

				if(ops->ent_get(&fatent) != clust + offset + FAT_START_ENT){
					sdfat_fs_panic(sb, "%s:%d: invalid cluster chain"
					     " ( %d != %d)", __FUNCTION__, __LINE__,
					     ops->ent_get(&fatent), clust + offset + FAT_START_ENT);
					err = -EIO;
					goto out;
				}

				err = sdfat_ent_write(pre_inode, &fatent, FAT_ENT_EOF, 1);
				if (err)
					goto out;
			}

			sbi->sd_info_sb.write_pos.inode = inode;
		}
		sbi->prev_free = clust + offset + FAT_START_ENT;
		*cluster = sbi->prev_free;
		SDFAT_I(inode)->sd_info_i.last_cluster = *cluster;
		goto out;
#endif
	}
	else{
		return 0;
	}

out:
	fatent_brelse(&fatent);
	return err;
}
/*-----------------------------------------*/

/* Modified by Panasonic (SAV), 2009-oct-5 */
#if defined(CONFIG_SDFAT_USE_SM331)
int sdfat_sync_rde(struct inode *inode)
{
	int ret = 0;
	struct super_block *sb = inode->i_sb;
	struct sdfat_sb_info *sbi = SDFAT_SB(sb);
	struct list_head *walk;
	struct sdfat_open_list_head *open_list;
	int i, count = 0, num = 0;
	struct buffer_head *bhs[10];

	mutex_lock(&sbi->open_lru_lock);
	list_for_each(walk, &SDFAT_SB(sb)->open_list){
		open_list = list_entry(walk, struct sdfat_open_list_head, lru);

		if(!test_bit(FAT_SUSPENDED_INODE, &SDFAT_I(open_list->inode)->i_flags)){

			if(SDFAT_I(open_list->inode)->i_need_sync){
				SDFAT_I(open_list->inode)->i_need_sync = 0;
				ret = sdfat_write_inode(open_list->inode, 0, &bhs[count]);

				mark_buffer_dirty(bhs[count]);

				count++;
				if((ret != 0) || (count >= 10))
					break;
			}
		}

		num++;
		if(num > 200){
			printk("SYNC RDE  OVER 200: %p\n", open_list->inode);
			break;
		}
	}

	if(count > 0)
		sdfat_sync_bhs(bhs, count);

	for(i = 0; i < count; i++){
		brelse(bhs[i]);
	}

//	fsync_super(sb);

	mutex_unlock(&sbi->open_lru_lock);

	return ret;
}
#endif //CONFIG_SDFAT_USE_SM331
/*-----------------------------------------*/

/* Modified by Panasonic (SAV), 2009-oct-5 */
#if defined(CONFIG_SDFAT_USE_SM331)
void sdfat_update_rde_size(struct inode *inode, int *sync)
{
	struct super_block *sb = inode->i_sb;
	struct sdfat_sb_info *sbi = SDFAT_SB(sb);
	struct list_head *walk;
	struct sdfat_open_list_head *open_list;

	mutex_lock(&sbi->open_lru_lock);
	list_for_each(walk, &SDFAT_SB(inode->i_sb)->open_list){
		open_list = list_entry(walk, struct sdfat_open_list_head, lru);

		if((open_list->inode->i_size == 0)
				&& open_list->inode != inode){
			continue;
		}
		if(open_list->inode->i_size == 0)
			SDFAT_I(inode)->i_need_sync = 1;
//			*sync = 1; //First Sync
		SDFAT_I(open_list->inode)->sd_info_i.current_au = sbi->sd_info_sb.write_pos.cluster;
		SDFAT_I(open_list->inode)->i_dummy_size =
			open_list->inode->i_size;// + (sbi->sd_info_sb.param.au_size << 10);
	}
	mutex_unlock(&sbi->open_lru_lock);
}
#endif //CONFIG_SDFAT_USE_SM331
/*-----------------------------------------*/

/* Modified by Panasonic (SAV), 2009-oct-5 */
#if defined(CONFIG_SDFAT_USE_SM331)
static int __sdfat_alloc_clusters(struct inode *inode, int start_cluster, int nr_cluster, int *sync)
{
	struct super_block *sb = inode->i_sb;
	struct sdfat_sb_info *sbi = SDFAT_SB(sb);
	struct fatent_operations *ops = sbi->fatent_ops;
	struct fat_entry fatent, prev_ent;
	struct buffer_head **bhs;
	int i, count, err, nr_bhs, idx_clus;

	*sync = 0;

	if(nr_cluster < 0){
		return -EINVAL;
	}

	if (sbi->free_clusters != -1 && sbi->free_clusters < nr_cluster) {
		return -ENOSPC;
	}

	bhs = kmalloc(nr_cluster * sizeof(struct buffer_head *), GFP_KERNEL);
	if(!bhs){
		printk("kmalloc failed!\n");
		return -ENOMEM;
	}

	err = nr_bhs = idx_clus = 0;
	count = FAT_START_ENT;
	fatent_init(&prev_ent);
	fatent_init(&fatent);
	fatent_set_entry(&fatent, start_cluster);
	while (count < nr_cluster + FAT_START_ENT) {
		if (fatent.entry >= sbi->max_cluster){
			err = -ENOSPC;
			goto out;
		}
		fatent_set_entry(&fatent, fatent.entry);
		err = fat_ent_read_block(sb, &fatent);
		if (err){
			goto out;
		}

		/* Find the free entries in a block */
		do {
			if (ops->ent_get(&fatent) == FAT_ENT_FREE) {
				int entry = fatent.entry;

				/* make the cluster chain */
				ops->ent_put(&fatent, FAT_ENT_EOF);
				if (prev_ent.nr_bhs){
					ops->ent_put(&prev_ent, entry);
				}

				fat_collect_bhs(bhs, &nr_bhs, &fatent);

				sbi->prev_free = entry;
				if (sbi->free_clusters != -1)
					sbi->free_clusters--;
				sb->s_dirt = 1;

				idx_clus++;
				if (idx_clus == nr_cluster){
					goto out;
				}

				/*
				 * fat_collect_bhs() gets ref-count of bhs,
				 * so we can still use the prev_ent.
				 */
				prev_ent = fatent;
			}
			else{
				err = -EIO;
				goto out;
			}
			count++;
			if (count == sbi->max_cluster)
				break;
		} while (fat_ent_next(sbi, &fatent));
	}

	err = -ENOSPC;

out:
	fatent_brelse(&fatent);
	if (!err) {
		err = sdfat_sync_bhs(bhs, nr_bhs);
		if (!err)
			err = fat_mirror_bhs(sb, bhs, nr_bhs);
		*sync = 1; //Sync dirent in every AU block writing.
	}
	for (i = 0; i < nr_bhs; i++)
		brelse(bhs[i]);

	if (err && idx_clus){
		__sdfat_free_clusters(inode, start_cluster, 0);
	}

	kfree(bhs);

	return err;
}
#endif //CONFIG_SDFAT_USE_SM331
/*-----------------------------------------*/

/* Modified by Panasonic (SAV), 2009-oct-5 */
int fat_alloc_cluster(struct inode *inode, int *cluster, struct buffer_head **bh, int *sync)
{
	int i, j, err = 0, nr_bhs = 0;
	int au_cluster;
	char used_flag;
	char loop_flag = 0;
	unsigned long start;
	unsigned long search_pos;
	struct super_block *sb = inode->i_sb;
	struct sdfat_sb_info *sbi = SDFAT_SB(sb);
	struct fatent_operations *ops = sbi->fatent_ops;
	struct fat_entry fatent, prev_ent;
	unsigned long reada_blocks, reada_clust, cur_block;

	au_cluster = sbi->sd_info_sb.param.au_size >> (SDFAT_SB(sb)->cluster_bits - 10);
	start = sbi->sd_info_sb.write_pos.cluster;

	fatent_init(&fatent);

	reada_blocks = FAT_READA_SIZE >> sb->s_blocksize_bits;
	if( sbi->fatent_shift > 0 ){
		reada_clust = FAT_READA_SIZE >> sbi->fatent_shift;
	}
	else{ //FAT12
		reada_clust = FAT_READA_SIZE >> 1;
	}
	cur_block = sbi->max_cluster + 1; //最初はreadaするように範囲外を設定

	for(i = start; i + FAT_START_ENT < sbi->max_cluster; i += au_cluster){

		//一周して開始クラスタに戻ったら終了
		if(loop_flag && i >= start){
			break;
		}
		//終端のAUを超えたら先頭に戻る
		else if(i + FAT_START_ENT + au_cluster > sbi->max_cluster){
			loop_flag = 1;
			i = 0;
		}

		used_flag = 0;

		search_pos = i + FAT_START_ENT;

		fatent_set_entry(&fatent, search_pos); //FATテーブルの現在位置にセット

		/* readahead of fat blocks */
		if ( (cur_block > fatent.entry) || (fatent.entry >= cur_block + reada_clust) ){
			unsigned long rest;
			sector_t blocknr;
			int tmp;

			ops->ent_blocknr(inode->i_sb, fatent.entry, &tmp, &blocknr);
			rest = sbi->fat_length - (blocknr - sbi->fat_start);
			fat_ent_reada(sb, &fatent, min(reada_blocks, rest));
			cur_block = fatent.entry;
		}

		err = fat_ent_read_block(inode->i_sb, &fatent); //FAT情報を読み込む
		if (err){
			printk("fat_ent_read_block(%d) error %08X\n", __LINE__, err);
			goto out;
		}

		//ブロックの先頭のバッファヘッドを保持（後で書き込むため）
		fatent_init(&prev_ent);

		for (j = 0; j < nr_bhs; j++){
			brelse(bh[j]);
			bh[j] = NULL;
		}
		nr_bhs = 0;

		fat_collect_bhs(bh, &nr_bhs, &fatent);

		prev_ent = fatent;

		for(j = 0; j < au_cluster; j++){
			if(ops->ent_get(&fatent) != FAT_ENT_FREE){ //値をゲットし、空きかどうかチェック
				used_flag = 1;
				break;
			}

			if(!fat_ent_next(sbi, &fatent)){
				if(search_pos + j + 1 >= sbi->max_cluster)
					break;

				fatent_set_entry(&fatent, search_pos + j + 1); //FATテーブルの現在位置にセット

				/* readahead of fat blocks */
				if ( (cur_block > fatent.entry) || (fatent.entry >= cur_block + reada_clust) ){
					unsigned long rest;
					sector_t blocknr;
					int tmp;

					ops->ent_blocknr(inode->i_sb, fatent.entry, &tmp, &blocknr);
					rest = sbi->fat_length - (blocknr - sbi->fat_start);
					fat_ent_reada(sb, &fatent, min(reada_blocks, rest));
					cur_block = fatent.entry;
				}

				err = fat_ent_read_block(inode->i_sb, &fatent); //FAT情報を読み込む
				if (err){
					printk("fat_ent_read_block(%d) error %08X\n", __LINE__, err);
					goto out;
				}
			}
		}

		if(used_flag==0){
			sbi->sd_info_sb.write_pos.cluster = i;
			sbi->sd_info_sb.write_pos.offset = 1;

			//サーチ開始ずみかチェック
			if(sbi->cont_space.n){
				int pos = sbi->sd_info_sb.write_pos.cluster / sbi->cont_space.n;
				if(pos < sbi->cont_space.pos){
					sbi->cont_space.cont--;
				}
			}

#if defined(CONFIG_SDFAT_USE_RICOH_R5C822)
			ops->ent_put(&prev_ent, FAT_ENT_EOF);
			sbi->prev_free = search_pos;
			*cluster = search_pos;
			if (sbi->free_clusters != -1)
				sbi->free_clusters--;
#elif defined(CONFIG_SDFAT_USE_SM331)
			sbi->sd_info_sb.write_pos.inode = inode;
			err = __sdfat_alloc_clusters(inode, search_pos, au_cluster, sync);
			if(err){
				if(err != -ENOSPC){
					printk("__sdfat_alloc_clusters(%d) error %08X\n",
						__LINE__, err);
				}
				goto out;
			}
			sbi->prev_free = search_pos + au_cluster;
			*cluster = search_pos;
			SDFAT_I(inode)->sd_info_i.last_cluster = *cluster;
#endif
			sb->s_dirt = 1;
			goto out;
		}
	}

	err = -ENOSPC;
out:
	fatent_brelse(&fatent);
	if(err && *bh){
		brelse(*bh);
		*bh = NULL;
	}
	return err;
}
/*-----------------------------------------*/

/* Modified by Panasonic (SAV), 2009-oct-5 */
#if defined(CONFIG_SDFAT_USE_RICOH_R5C822)
int fat_alloc_block_cluster(struct inode *inode, int *cluster, struct buffer_head **bh, int *sync)
{
	struct super_block *sb = inode->i_sb;
	struct sdfat_sb_info *sbi = SDFAT_SB(sb);
	struct fatent_operations *ops = sbi->fatent_ops;
	struct fat_entry fatent, prev_ent;
	int i, j, k, err = 0, nr_bhs = 0;
	int au_cluster = sbi->sd_info_sb.param.au_size >> (SDFAT_SB(sb)->cluster_bits - 10);
	unsigned long blk_size;
	unsigned long au_num;
	unsigned long start;
	unsigned long search_pos;
	unsigned long reada_blocks, reada_clust, cur_block;

	if(sbi->fat_length <= 1024)  //under 4GB
		blk_size = FAT_SD_SEARCH_BLK_SIZE;
	else if(sbi->fat_length <= 2048) //under 8GB
		blk_size = FAT_SD_SEARCH_BLK_SIZE << 1;
	else if(sbi->fat_length <= 4096) //under 16GB
		blk_size = FAT_SD_SEARCH_BLK_SIZE << 2;
	else  //etc
		blk_size = FAT_SD_SEARCH_BLK_SIZE << 3;

	reada_blocks = FAT_READA_SIZE >> sb->s_blocksize_bits;
	if( sbi->fatent_shift > 0 ){
		reada_clust = FAT_READA_SIZE >> sbi->fatent_shift;
	}
	else{ //FAT12
		reada_clust = FAT_READA_SIZE >> 1;
	}
	cur_block = sbi->max_cluster + 1; //最初はreadaするように範囲外を設定

RETRY:
	au_num = blk_size / au_cluster;
	start = sbi->sd_info_sb.write_pos.cluster / blk_size;

	fatent_init(&fatent);

	for(i = start; i * blk_size + FAT_START_ENT < sbi->max_cluster; i++){
		char ischeck = 1;

		//if last block size is under AU size, front block is searched.
		if((i + 1) * blk_size + au_cluster + FAT_START_ENT > sbi->max_cluster)
			;
		else{ /* check some clusters of each block. */
			/* if some clusters are free, the block is searched. */
			search_pos = (i + 1) * blk_size + FAT_START_ENT;

			fatent_set_entry(&fatent, search_pos); //FATテーブルの現在位置にセット

			/* readahead of fat blocks */
			if ( (cur_block > fatent.entry) || (fatent.entry >= cur_block + reada_clust) ){
				unsigned long rest;
				sector_t blocknr;
				int tmp;

				ops->ent_blocknr(inode->i_sb, fatent.entry, &tmp, &blocknr);
				rest = sbi->fat_length - (blocknr - sbi->fat_start);
				fat_ent_reada(sb, &fatent, min(reada_blocks, rest));
				cur_block = fatent.entry;
			}

			err = fat_ent_read_block(inode->i_sb, &fatent); //FAT情報を読み込む
			if (err){
				printk("fat_ent_read_block(%d) error %08X\n", __LINE__, err);
				goto out;
			}

			for(j = 0; j < FAT_SD_CHECK_CLUST_NUM; j++){
				if(ops->ent_get(&fatent) != FAT_ENT_FREE){ //値をゲットし、空きかどうかチェック
					ischeck = 0;
					break;
				}

				if(!fat_ent_next(sbi, &fatent)){
					if(search_pos + j + 1 >= sbi->max_cluster)
						break;

					fatent_set_entry(&fatent, search_pos + j + 1); //FATテーブルの現在位置にセット
					err = fat_ent_read_block(inode->i_sb, &fatent); //FAT情報を読み込む
					if (err){
						printk("fat_ent_read_block(%d) error %08X\n", __LINE__, err);
						goto out;
					}
				}
			}
		}
		if(ischeck){ /* search cont_space in block. */
			for(j = 0; j < au_num; j++){
				char used_flag;
				search_pos = i * blk_size + j * au_cluster + FAT_START_ENT;

				fatent_set_entry(&fatent, search_pos); //FATテーブルの現在位置にセット

				/* readahead of fat blocks */
				if ( (cur_block > fatent.entry) || (fatent.entry >= cur_block + reada_clust) ){
					unsigned long rest;
					sector_t blocknr;
					int tmp;

					ops->ent_blocknr(inode->i_sb, fatent.entry, &tmp, &blocknr);
					rest = sbi->fat_length - (blocknr - sbi->fat_start);
					fat_ent_reada(sb, &fatent, min(reada_blocks, rest));
					cur_block = fatent.entry;
				}

				err = fat_ent_read_block(inode->i_sb, &fatent); //FAT情報を読み込む
				if (err){
					printk("fat_ent_read_block(%d) error %08X\n", __LINE__, err);
					goto out;
				}
				//前回分のバッファヘッドを解放
				if(*bh){
					brelse(*bh);
					*bh = NULL;
				}

				//ブロックの先頭のバッファヘッドを保持（後で書き込むため）
				nr_bhs = 0;
				fatent_init(&prev_ent);
				fat_collect_bhs(bh, &nr_bhs, &fatent);

				prev_ent = fatent;

				used_flag = 0;
				for(k = 0; k < au_cluster; k++){
					if(ops->ent_get(&fatent) != FAT_ENT_FREE){ //値をゲットし、空きかどうかチェック
						used_flag = 1;
						break;
					}

					if(!fat_ent_next(sbi, &fatent)){
						if(search_pos + k + 1 >= sbi->max_cluster){
							printk("%s(%d) : max cluster\n",__FUNCTION__,__LINE__); 
							break;
						}

						fatent_set_entry(&fatent, search_pos + k + 1); //FATテーブルの現在位置にセット

						/* readahead of fat blocks */
						if ( (cur_block > fatent.entry) || (fatent.entry >= cur_block + reada_clust) ){
							unsigned long rest;
							sector_t blocknr;
							int tmp;

							ops->ent_blocknr(inode->i_sb, fatent.entry, &tmp, &blocknr);
							rest = sbi->fat_length - (blocknr - sbi->fat_start);
							fat_ent_reada(sb, &fatent, min(reada_blocks, rest));
							cur_block = fatent.entry;
						}

						err = fat_ent_read_block(inode->i_sb, &fatent); //FAT情報を読み込む
						if (err){
							printk("fat_ent_read_block(%d) error %08X\n", __LINE__, err);
							goto out;
						}
					}

					if(!used_flag){
						sbi->sd_info_sb.write_pos.cluster = i * blk_size + j * au_cluster;
						sbi->sd_info_sb.write_pos.offset = 1;
						ops->ent_put(&prev_ent, FAT_ENT_EOF);
						sbi->prev_free = sbi->sd_info_sb.write_pos.cluster + FAT_START_ENT;
						*cluster = sbi->prev_free;

						//サーチ開始ずみかチェック
						if(sbi->cont_space.n){
							int pos = sbi->sd_info_sb.write_pos.cluster / sbi->cont_space.n;
							if(pos < sbi->cont_space.pos){
								sbi->cont_space.cont--;
							}
						}

						if (sbi->free_clusters != -1)
							sbi->free_clusters--;
						sb->s_dirt = 1;
						goto out;
					}
				}
			}
		}
	}

	//空きがあるかチェック、あれば再サーチ
	if(sbi->cont_space.cont && sbi->cont_space.n){
		sbi->sd_info_sb.write_pos.cluster = 0;
		sbi->sd_info_sb.write_pos.offset = 0;
		fatent_brelse(&fatent);
		if(*bh){
			brelse(*bh);
			*bh = NULL;
		}
		goto RETRY;
	}

	err = -ENOSPC;
out:
	fatent_brelse(&fatent);
	if(err && *bh){
		brelse(*bh);
		*bh = NULL;
	}
	return err;
}
#endif //CONFIG_SDFAT_USE_RICOH_R5C822
/*-----------------------------------------*/

/* Modified by Panasonic (SAV), 2009-oct-5 */
int sdfat_alloc_cont_clusters(
	struct inode *inode, int *cluster, int *dummy_cluster, int *chain, int *sync)
{
	struct super_block *sb = inode->i_sb;
	struct sdfat_sb_info *sbi = SDFAT_SB(sb);
	struct fatent_operations *ops = sbi->fatent_ops;
	struct buffer_head *bh = NULL;
	int err = 0, nr_bhs = 0;

	lock_fat(sbi);
	if (sbi->free_clusters != -1 && sbi->free_clusters < 1) {
		unlock_fat(sbi);
		return -ENOSPC;
	}
	if((err = sdfat_alloc_cluster_from_au_block(inode, cluster, chain)) != 0)
		goto out;

	/* when free AU block is not found. */
	if(sbi->sd_info_sb.write_pos.offset == 0){
		if((err = ops->alloc_cluster(inode, cluster, &bh, sync)) != 0)
			goto out;
	}
out:
#if defined(CONFIG_SDFAT_USE_SM331)
	if(SDFAT_I(inode)->sd_info_i.current_au != sbi->sd_info_sb.write_pos.cluster){
		*dummy_cluster = sbi->sd_info_sb.param.au_size >> (SDFAT_SB(sb)->cluster_bits - 10);
		sdfat_update_rde_size(inode, sync);
	}
	else{
		*dummy_cluster = 0;
	}
#endif //CONFIG_SDFAT_USE_SM331

	unlock_fat(sbi);

	if(bh){
		nr_bhs = 1;

		if (!err) {
			if (inode_needs_sync(inode))
				err = sdfat_sync_bhs(&bh, nr_bhs);
			if (!err)
				err = fat_mirror_bhs(sb, &bh, nr_bhs);
		}
		brelse(bh);
	}

	return err;
}
/*-----------------------------------------*/

int sdfat_alloc_clusters(struct inode *inode, int *cluster, int nr_cluster)
{
	struct super_block *sb = inode->i_sb;
	struct sdfat_sb_info *sbi = SDFAT_SB(sb);
	struct fatent_operations *ops = sbi->fatent_ops;
	struct fat_entry fatent, prev_ent;
	struct buffer_head *bhs[MAX_BUF_PER_PAGE];
	int i, count, err, nr_bhs, idx_clus;
	unsigned long reada_blocks, reada_clust, cur_block;

	BUG_ON(nr_cluster > (MAX_BUF_PER_PAGE / 2));	/* fixed limit */

	lock_fat(sbi);
	if (sbi->free_clusters != -1 && sbi->free_clusters < nr_cluster) {
		unlock_fat(sbi);
		return -ENOSPC;
	}

	err = nr_bhs = idx_clus = 0;
	count = FAT_START_ENT;
	fatent_init(&prev_ent);
	fatent_init(&fatent);
	fatent_set_entry(&fatent, sbi->prev_free);

	reada_blocks = FAT_READA_SIZE >> sb->s_blocksize_bits;
	if( sbi->fatent_shift > 0 ){
		reada_clust = FAT_READA_SIZE >> sbi->fatent_shift;
	}
	else{ //FAT12
		reada_clust = FAT_READA_SIZE >> 1;
	}
	cur_block = sbi->max_cluster + 1; //最初はreadaするように範囲外を設定

	while (count < sbi->max_cluster) {
		if (fatent.entry >= sbi->max_cluster)
			fatent.entry = FAT_START_ENT;
		fatent_set_entry(&fatent, fatent.entry);

		/* readahead of fat blocks */
		if ( (cur_block > fatent.entry) || (fatent.entry >= cur_block + reada_clust) ){
			unsigned long rest;
			sector_t blocknr;
			int tmp;

			ops->ent_blocknr(inode->i_sb, fatent.entry, &tmp, &blocknr);
			rest = sbi->fat_length - (blocknr - sbi->fat_start);
			fat_ent_reada(sb, &fatent, min(reada_blocks, rest));
			cur_block = fatent.entry;
		}

		err = fat_ent_read_block(sb, &fatent);
		if (err)
			goto out;

		/* Find the free entries in a block */
		do {
			if (ops->ent_get(&fatent) == FAT_ENT_FREE) {
				int entry = fatent.entry;

				/* Modified by Panasonic (SAV), 2009-oct-5 */
				if(sdfat_check_cont_space(sb, entry))
					sbi->cont_space.cont--;
				/*-----------------------------------------*/

				/* make the cluster chain */
				ops->ent_put(&fatent, FAT_ENT_EOF);
				if (prev_ent.nr_bhs)
					ops->ent_put(&prev_ent, entry);

				fat_collect_bhs(bhs, &nr_bhs, &fatent);

				sbi->prev_free = entry;
				if (sbi->free_clusters != -1)
					sbi->free_clusters--;
				sb->s_dirt = 1;

				cluster[idx_clus] = entry;
				idx_clus++;
				if (idx_clus == nr_cluster)
					goto out;

				/*
				 * fat_collect_bhs() gets ref-count of bhs,
				 * so we can still use the prev_ent.
				 */
				prev_ent = fatent;
			}
			count++;
			if (count == sbi->max_cluster)
				break;
		} while (fat_ent_next(sbi, &fatent));
	}

	/* Couldn't allocate the free entries */
	sbi->free_clusters = 0;
	sb->s_dirt = 1;
	err = -ENOSPC;

out:
	unlock_fat(sbi);
	fatent_brelse(&fatent);
	if (!err) {
		if (inode_needs_sync(inode))
			err = sdfat_sync_bhs(bhs, nr_bhs);
		if (!err)
			err = fat_mirror_bhs(sb, bhs, nr_bhs);
	}
	for (i = 0; i < nr_bhs; i++)
		brelse(bhs[i]);

	if (err && idx_clus)
		sdfat_free_clusters(inode, cluster[0]);

	return err;
}

int __sdfat_free_clusters(struct inode *inode, int cluster, int lock)
{
	struct super_block *sb = inode->i_sb;
	struct sdfat_sb_info *sbi = SDFAT_SB(sb);
	struct fatent_operations *ops = sbi->fatent_ops;
	struct fat_entry fatent;
	struct buffer_head *bhs[MAX_BUF_PER_PAGE];
	int i, err, nr_bhs;

	int pre_entry = -1;
	int offset = -1, au_num = -1;

#if defined(CONFIG_SDFAT_USE_SM331)
	//Clear milestone cache
	memset(SDFAT_I(inode)->i_cluster_milestones, 0, sizeof(int)*(FAT_MILESTONES + 1));
#endif //defined(CONFIG_SDFAT_USE_SM331)

	nr_bhs = 0;
	fatent_init(&fatent);
	if(lock)
		lock_fat(sbi);
	do {
		cluster = sdfat_ent_read(inode, &fatent, cluster);
		if (cluster < 0) {
			err = cluster;
			goto error;
		} else if (cluster == FAT_ENT_FREE) {
			sdfat_fs_panic(sb, "%s: deleting FAT entry beyond EOF",
				     __FUNCTION__);
			err = -EIO;
			goto error;
		}

		ops->ent_put(&fatent, FAT_ENT_FREE);
		if (sbi->free_clusters != -1) {
			sbi->free_clusters++;
			sb->s_dirt = 1;
		}

		/* Modified by Panasonic (SAV), 2009-oct-5 */
		sbi->cont_space.cont += sdfat_fast_check_cont_space(
			sb, fatent.entry, pre_entry, &offset, &au_num, cluster == FAT_ENT_EOF);

		pre_entry = fatent.entry;
		/*-----------------------------------------*/

		if (nr_bhs + fatent.nr_bhs > MAX_BUF_PER_PAGE) {
			if (sb->s_flags & MS_SYNCHRONOUS) {
				err = sdfat_sync_bhs(bhs, nr_bhs);
				if (err)
					goto error;
			}
			err = fat_mirror_bhs(sb, bhs, nr_bhs);
			if (err)
				goto error;
			for (i = 0; i < nr_bhs; i++)
				brelse(bhs[i]);
			nr_bhs = 0;
		}
		fat_collect_bhs(bhs, &nr_bhs, &fatent);
	} while (cluster != FAT_ENT_EOF);

	if (sb->s_flags & MS_SYNCHRONOUS) {
		err = sdfat_sync_bhs(bhs, nr_bhs);
		if (err)
			goto error;
	}
	err = fat_mirror_bhs(sb, bhs, nr_bhs);
error:
	fatent_brelse(&fatent);
	for (i = 0; i < nr_bhs; i++)
		brelse(bhs[i]);
	if(lock)
		unlock_fat(sbi);

	sdfat_clusters_flush(sb);

	return err;
}

int sdfat_free_clusters(struct inode *inode, int cluster)
{
	return __sdfat_free_clusters(inode, cluster, 1);
}
EXPORT_SYMBOL_GPL(sdfat_free_clusters);

static void fat_ent_reada(struct super_block *sb, struct fat_entry *fatent,
			  unsigned long reada_blocks)
{
	struct fatent_operations *ops = SDFAT_SB(sb)->fatent_ops;
	sector_t blocknr;
	int i, offset;

	ops->ent_blocknr(sb, fatent->entry, &offset, &blocknr);

	for (i = 0; i < reada_blocks; i++)
		meta_breadahead(sb, blocknr + i, BH_Fat);
}

int sdfat_count_free_clusters(struct super_block *sb)
{
	struct sdfat_sb_info *sbi = SDFAT_SB(sb);
	struct fatent_operations *ops = sbi->fatent_ops;
	struct fat_entry fatent;
	unsigned long reada_blocks, reada_mask, cur_block;
	int err = 0, free;

	lock_fat(sbi);
	if (sbi->free_clusters != -1)
		goto out;

	reada_blocks = FAT_READA_SIZE >> sb->s_blocksize_bits;
	reada_mask = reada_blocks - 1;
	cur_block = 0;

	free = 0;
	fatent_init(&fatent);
	fatent_set_entry(&fatent, FAT_START_ENT);
	while (fatent.entry < sbi->max_cluster) {
		/* readahead of fat blocks */
		if ((cur_block & reada_mask) == 0) {
			unsigned long rest = sbi->fat_length - cur_block;
			fat_ent_reada(sb, &fatent, min(reada_blocks, rest));
		}
		cur_block++;

		err = fat_ent_read_block(sb, &fatent);
		if (err){
			fatent_brelse(&fatent);
			goto out;
		}

		do {
			if (ops->ent_get(&fatent) == FAT_ENT_FREE)
				free++;
		} while (fat_ent_next(sbi, &fatent));
	}
	sbi->free_clusters = free;
	sb->s_dirt = 1;
	fatent_brelse(&fatent);
out:
	unlock_fat(sbi);
	return err;
}

/* Modified by Panasonic (SAV), 2009-oct-5 */
int fat_cont_search(struct inode *inode, struct file *filp, struct fat_ioctl_space *arg)
{
	struct sdfat_sb_info *sbi = SDFAT_SB(inode->i_sb);
	struct fatent_operations *ops = sbi->fatent_ops;
	struct fat_entry fatent;
	int err = 0;
	unsigned long search_pos;
	unsigned long i, j;
	int used_flag;
	unsigned long null_count;
	unsigned long reada_blocks, reada_clust, cur_block;

	if(arg->n <= 0){
		arg->ret = 0;
		printk("[%s-%d] arg->n must be larger than zero.\n",__FUNCTION__,__LINE__);
		return -EINVAL;
	}

	if(arg->type == FAT_TYPE_CONT_SEARCH && arg->strict == 0){ //空きサーチの途中経過を取得する場合
		if(SDFAT_SB(inode->i_sb)->cont_space.n != arg->n){ //
			SDFAT_SB(inode->i_sb)->cont_space.cont = 0;
		}
		arg->ret = SDFAT_SB(inode->i_sb)->cont_space.cont;
		return 0;
	}

	lock_fat(sbi);

	if(arg->strict == 1 || sbi->cont_space.n != arg->n){ //先頭からサーチ、簡易サーチでも初回は先頭から
		sbi->cont_space.n = arg->n;
		sbi->cont_space.cont = 0;
		sbi->cont_space.pos = 0;
	}

	//終端AUはエラーを返して終了
	if((sbi->cont_space.pos + 1) * (arg->n) + FAT_START_ENT >= sbi->max_cluster){
		arg->ret = sbi->cont_space.cont;
		unlock_fat(sbi);
		return -EINVAL;
	}

	reada_blocks = FAT_READA_SIZE >> inode->i_sb->s_blocksize_bits;
	if( sbi->fatent_shift > 0 ){
		reada_clust = FAT_READA_SIZE >> sbi->fatent_shift;
	}
	else{ //FAT12
		reada_clust = FAT_READA_SIZE >> 1;
	}
	cur_block = sbi->max_cluster + 1; //最初はreadaするように範囲外を設定

	null_count = 0;

	fatent_init(&fatent); //FAT情報の初期化

	for(i = sbi->cont_space.pos; (i + 1) * (arg->n) - 1 + FAT_START_ENT < sbi->max_cluster; i++){

		used_flag = 0;
		search_pos = i * (arg->n) + FAT_START_ENT;

		fatent_set_entry(&fatent, search_pos); //FATテーブルの現在位置にセット

		/* readahead of fat blocks */
		if ( (cur_block > fatent.entry) || (fatent.entry >= cur_block + reada_clust) ){
			unsigned long rest;
			sector_t blocknr;
			int tmp;

			ops->ent_blocknr(inode->i_sb, fatent.entry, &tmp, &blocknr);
			rest = sbi->fat_length - (blocknr - sbi->fat_start);
			fat_ent_reada(inode->i_sb, &fatent, min(reada_blocks, rest));
			cur_block = fatent.entry;
		}

		err = fat_ent_read_block(inode->i_sb, &fatent); //FAT情報を読み込む
		if (err){
			printk("fat_ent_read_block(%d) error %08X\n", __LINE__, err);
			goto out;
		}

		for(j = 0; j < arg->n; j++){
			if (ops->ent_get(&fatent) != FAT_ENT_FREE){ //値をゲットし、空きかどうかチェック
				used_flag = 1;
				break;
			}
			if(!fat_ent_next(sbi, &fatent)){
				if(search_pos + j + 1 >= sbi->max_cluster)
					break;

				fatent_set_entry(&fatent, search_pos + j + 1); //FATテーブルの現在位置にセット

				/* readahead of fat blocks */
				if ( (cur_block > fatent.entry) || (fatent.entry >= cur_block + reada_clust) ){
					unsigned long rest;
					sector_t blocknr;
					int tmp;

					ops->ent_blocknr(inode->i_sb, fatent.entry, &tmp, &blocknr);
					rest = sbi->fat_length - (blocknr - sbi->fat_start);
					fat_ent_reada(inode->i_sb, &fatent, min(reada_blocks, rest));
					cur_block = fatent.entry;
				}
				err = fat_ent_read_block(inode->i_sb, &fatent); //FAT情報を読み込む
				if (err){
					printk("fat_ent_read_block(%d) error %08X\n", __LINE__, err);
					goto out;
				}
			}
		}
		if(used_flag){
			used_flag = 0;
		}
		else{
			null_count++;
		}

		if(arg->type == FAT_TYPE_AREA_SEARCH){
			if(i + 1 >= sbi->cont_space.pos + arg->unit){
				i++;
				break;
			}
		}
		else if(arg->type == FAT_TYPE_CAPA_SEARCH){
			if(null_count >= arg->unit){
				i++;
				break;
			}
		}
		cond_resched();
	}

	fatent_brelse(&fatent);
out:
	sbi->cont_space.pos = i;
	sbi->cont_space.cont += null_count;
	arg->ret = sbi->cont_space.cont;

	fatent_brelse(&fatent);

	unlock_fat(sbi);
	return err;
}
/*-----------------------------------------*/

/* Modified by Panasonic (SAV), 2009-oct-5 */
#if defined(CONFIG_SDFAT_USE_RICOH_R5C822)
int fat_block_cont_search(struct inode *inode, struct file *filp, struct fat_ioctl_space *arg)
{
	struct sdfat_sb_info *sbi = SDFAT_SB(inode->i_sb);
	struct fatent_operations *ops = sbi->fatent_ops;
	struct fat_entry fatent;
	int err = 0;
	unsigned long search_pos;
	unsigned long i, j = 0, k;
	unsigned long au_num;
	unsigned long null_count, total_null_count;
	unsigned long blk_size;
	unsigned long start_block;
	unsigned long start_au;
	unsigned long start_pos;
	unsigned long reada_blocks, reada_clust, cur_block;

	if(arg->n <= 0){
		arg->ret = 0;
		printk("[%s-%d] arg->n must be larger than zero.\n",__FUNCTION__,__LINE__);
		return -EINVAL;
	}

	if(arg->type == FAT_TYPE_CONT_SEARCH && arg->strict == 0){ //空きサーチの途中経過を取得する場合
		if(SDFAT_SB(inode->i_sb)->cont_space.n != arg->n){ //
			SDFAT_SB(inode->i_sb)->cont_space.cont = 0;
		}
		arg->ret = SDFAT_SB(inode->i_sb)->cont_space.cont;
		return 0;
	}

	lock_fat(sbi);

	if(arg->strict == 1 || sbi->cont_space.n != arg->n){ //先頭からサーチ、簡易サーチでも初回は先頭から
		sbi->cont_space.n = arg->n;
		sbi->cont_space.cont = 0;
		sbi->cont_space.pos = 0;
	}

	//終端AUはエラーを返して終了
	if((sbi->cont_space.pos + 1) * (arg->n) + FAT_START_ENT >= sbi->max_cluster){
		arg->ret = sbi->cont_space.cont;
		unlock_fat(sbi);
		return -EINVAL;
	}

	if(sbi->fat_length <= 1024)  //under 4GB
		 blk_size = FAT_SD_SEARCH_BLK_SIZE;
	else if(sbi->fat_length <= 2048) //under 8GB
		blk_size = FAT_SD_SEARCH_BLK_SIZE << 1;
	else if(sbi->fat_length <= 4096) //under 16GB
		blk_size = FAT_SD_SEARCH_BLK_SIZE << 2;
	else  //etc
		blk_size = FAT_SD_SEARCH_BLK_SIZE << 3;

	au_num = blk_size / (arg->n);

	unlock_fat(sbi);

	start_block = sbi->cont_space.pos / au_num;
	start_au    = sbi->cont_space.pos % au_num;
	start_pos   = sbi->cont_space.pos;

	fatent_init(&fatent); //FAT情報の初期化

	reada_blocks = FAT_READA_SIZE >> inode->i_sb->s_blocksize_bits;
	if( sbi->fatent_shift > 0 ){
		reada_clust = FAT_READA_SIZE >> sbi->fatent_shift;
	}
	else{ //FAT12
		reada_clust = FAT_READA_SIZE >> 1;	//厳密には1.5(12bit)で割るべきだが
											//容量的に1回のreadaで済むはずなので
											//大雑把にしておく
	}
	cur_block = sbi->max_cluster + 1; //最初はreadaするように範囲外を設定

	null_count = 0;
	total_null_count = 0;

	for(i = start_block; i * blk_size + FAT_START_ENT < sbi->max_cluster; i++){
		char ischeck = 0;

		lock_fat(sbi);
		if(start_au == 0){
			ischeck = 1;
			//if last block size is under AU size, front block is searched.
			if((i + 1) * blk_size + arg->n + FAT_START_ENT > sbi->max_cluster)
				;
			else{
				search_pos = (i + 1) * blk_size + FAT_START_ENT;

				fatent_set_entry(&fatent, search_pos); //FATテーブルの現在位置にセット

				/* readahead of fat blocks */
				if ( (cur_block > fatent.entry) || (fatent.entry >= cur_block + reada_clust) ){
					unsigned long rest;
					sector_t blocknr;
					int tmp;

					ops->ent_blocknr(inode->i_sb, fatent.entry, &tmp, &blocknr);
					rest = sbi->fat_length - (blocknr - sbi->fat_start);
					fat_ent_reada(inode->i_sb, &fatent, min(reada_blocks, rest));
					cur_block = fatent.entry;
				}

				err = fat_ent_read_block(inode->i_sb, &fatent); //FAT情報を読み込む
				if (err){
					printk("fat_ent_read_block(%d) error %08X\n", __LINE__, err);
					goto out;
				}

				for(j = 0; j < FAT_SD_CHECK_CLUST_NUM; j++){
					if(ops->ent_get(&fatent) != FAT_ENT_FREE){ //値をゲットし、空きかどうかチェック
						ischeck = 0;
						break;
					}

					if(!fat_ent_next(sbi, &fatent)){
						if(search_pos + j + 1 >= sbi->max_cluster)
							break;

						fatent_set_entry(&fatent, search_pos + j + 1); //FATテーブルの現在位置にセット

						/* readahead of fat blocks */
						if ( (cur_block > fatent.entry) || (fatent.entry >= cur_block + reada_clust) ){
							unsigned long rest;
							sector_t blocknr;
							int tmp;

							ops->ent_blocknr(inode->i_sb, fatent.entry, &tmp, &blocknr);
							rest = sbi->fat_length - (blocknr - sbi->fat_start);
							fat_ent_reada(inode->i_sb, &fatent, min(reada_blocks, rest));
							cur_block = fatent.entry;
						}

						err = fat_ent_read_block(inode->i_sb, &fatent); //FAT情報を読み込む
						if (err){
							printk("fat_ent_read_block(%d) error %08X\n", __LINE__, err);
							goto out;
						}
					}
				}
			}
		}

		if(ischeck || start_au != 0){
			for(j = start_au; j < au_num; j++){
				char used_flag;
				search_pos = i * blk_size + j * arg->n + FAT_START_ENT;

				if(search_pos >= sbi->max_cluster)
					goto out;

				fatent_set_entry(&fatent, search_pos); //FATテーブルの現在位置にセット

				/* readahead of fat blocks */
				if ( (cur_block > fatent.entry) || (fatent.entry >= cur_block + reada_clust) ){
					unsigned long rest;
					sector_t blocknr;
					int tmp;

					ops->ent_blocknr(inode->i_sb, fatent.entry, &tmp, &blocknr);
					rest = sbi->fat_length - (blocknr - sbi->fat_start);
					fat_ent_reada(inode->i_sb, &fatent, min(reada_blocks, rest));
					cur_block = fatent.entry;
				}

				err = fat_ent_read_block(inode->i_sb, &fatent); //FAT情報を読み込む
				if (err){
					printk("fat_ent_read_block(%d) error %08X\n", __LINE__, err);
					goto out;
				}

				used_flag = 0;
				for(k = 0; k < (arg->n); k++){
					if(ops->ent_get(&fatent) != FAT_ENT_FREE){ //値をゲットし、空きかどうかチェック
						used_flag = 1;
						break;
					}

					if(!fat_ent_next(sbi, &fatent)){
						if(search_pos + k + 1>= sbi->max_cluster)
							goto out;

						fatent_set_entry(&fatent, search_pos + k + 1); //FATテーブルの現在位置にセット

						/* readahead of fat blocks */
						if ( (cur_block > fatent.entry) || (fatent.entry >= cur_block + reada_clust) ){
							unsigned long rest;
							sector_t blocknr;
							int tmp;

							ops->ent_blocknr(inode->i_sb, fatent.entry, &tmp, &blocknr);
							rest = sbi->fat_length - (blocknr - sbi->fat_start);
							fat_ent_reada(inode->i_sb, &fatent, min(reada_blocks, rest));
							cur_block = fatent.entry;
						}

						err = fat_ent_read_block(inode->i_sb, &fatent); //FAT情報を読み込む
						if (err){
							printk("fat_ent_read_block(%d) error %08X\n", __LINE__, err);
							goto out;
						}
					}
				}

				if(!used_flag){
					null_count++;
					total_null_count++;
				}
				else
					used_flag = 0;

				if(arg->type == FAT_TYPE_AREA_SEARCH){
					if(i * au_num + j + 1 >= start_pos + arg->unit){
						j++;
						goto out;
					}
				}
				else if(arg->type == FAT_TYPE_CAPA_SEARCH){
					if(total_null_count >= arg->unit){
						j++;
						goto out;
					}
				}
				cond_resched();
			}
			start_au = 0;
		}
		sbi->cont_space.pos = (i + 1) * au_num;

		sbi->cont_space.cont += null_count;
		arg->ret = sbi->cont_space.cont;
		null_count = 0;

		unlock_fat(sbi);
		cond_resched();
	}
	return 0;

out:
	sbi->cont_space.pos = i * au_num + j;
	sbi->cont_space.cont += null_count;
	arg->ret = sbi->cont_space.cont;

	fatent_brelse(&fatent);

	unlock_fat(sbi);

	return err;
}
#endif //CONFIG_SDFAT_USE_RICOH_R5C822
/*-----------------------------------------*/

/* Modified by Panasonic (SAV), 2009-oct-5 */
int sdfat_cont_search(struct inode *inode, struct file *filp, struct fat_ioctl_space *arg)
{
	struct fatent_operations *ops = SDFAT_SB(inode->i_sb)->fatent_ops;
	return ops->cont_search(inode, filp, arg);
}
/*-----------------------------------------*/
