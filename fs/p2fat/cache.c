/*
 *  linux/fs/p2fat/cache.c
 *
 *  Written 1992,1993 by Werner Almesberger
 *
 *  Mar 1999. AV. Changed cache, so that it uses the starting cluster instead
 *	of inode number.
 *  May 1999. AV. Fixed the bogosity with FAT32 (read "FAT28"). Fscking lusers.
 */

#include <linux/fs.h>
#include <linux/p2fat_fs.h>
#include <linux/buffer_head.h>

/* this must be > 0. */
#define FAT_MAX_CACHE	8

struct fat_cache {
	struct list_head cache_list;
	int nr_contig;	/* number of contiguous clusters */
	int fcluster;	/* cluster number in the file. */
	int dcluster;	/* cluster number on disk. */
};

struct fat_cache_id {
	unsigned int id;
	int nr_contig;
	int fcluster;
	int dcluster;
};

static inline int fat_max_cache(struct inode *inode)
{
	return FAT_MAX_CACHE;
}

static struct kmem_cache *fat_cache_cachep;

static void init_once(void *foo)
{
	struct fat_cache *cache = (struct fat_cache *)foo;

	INIT_LIST_HEAD(&cache->cache_list);
}

int __init p2fat_cache_init(void)
{
	fat_cache_cachep = kmem_cache_create("p2fat_cache",
				sizeof(struct fat_cache),
				0, SLAB_RECLAIM_ACCOUNT|SLAB_MEM_SPREAD,
				init_once);
	if (fat_cache_cachep == NULL)
		return -ENOMEM;
	return 0;
}

void p2fat_cache_destroy(void)
{
	kmem_cache_destroy(fat_cache_cachep);
}

static inline struct fat_cache *fat_cache_alloc(struct inode *inode)
{
	return kmem_cache_alloc(fat_cache_cachep, GFP_KERNEL);
}

static inline void fat_cache_free(struct fat_cache *cache)
{
	BUG_ON(!list_empty(&cache->cache_list));
	kmem_cache_free(fat_cache_cachep, cache);
}

static inline void fat_cache_update_lru(struct inode *inode,
					struct fat_cache *cache)
{
	if (P2FAT_I(inode)->cache_lru.next != &cache->cache_list)
		list_move(&cache->cache_list, &P2FAT_I(inode)->cache_lru);
}

static int fat_cache_lookup(struct inode *inode, int fclus,
			    struct fat_cache_id *cid,
			    int *cached_fclus, int *cached_dclus)
{
	static struct fat_cache nohit = { .fcluster = 0, };

	struct fat_cache *hit = &nohit, *p;
	int offset = -1;

	spin_lock(&P2FAT_I(inode)->cache_lru_lock);
	list_for_each_entry(p, &P2FAT_I(inode)->cache_lru, cache_list) {
		/* Find the cache of "fclus" or nearest cache. */
		if (p->fcluster <= fclus && hit->fcluster < p->fcluster) {
			hit = p;
			if ((hit->fcluster + hit->nr_contig) < fclus) {
				offset = hit->nr_contig;
			} else {
				offset = fclus - hit->fcluster;
				break;
			}
		}
	}
	if (hit != &nohit) {
		fat_cache_update_lru(inode, hit);

		cid->id = P2FAT_I(inode)->cache_valid_id;
		cid->nr_contig = hit->nr_contig;
		cid->fcluster = hit->fcluster;
		cid->dcluster = hit->dcluster;
		*cached_fclus = cid->fcluster + offset;
		*cached_dclus = cid->dcluster + offset;
	}
	spin_unlock(&P2FAT_I(inode)->cache_lru_lock);

	return offset;
}

static struct fat_cache *fat_cache_merge(struct inode *inode,
					 struct fat_cache_id *new)
{
	struct fat_cache *p;

	list_for_each_entry(p, &P2FAT_I(inode)->cache_lru, cache_list) {
		/* Find the same part as "new" in cluster-chain. */
		if (p->fcluster == new->fcluster) {
			BUG_ON(p->dcluster != new->dcluster);
			if (new->nr_contig > p->nr_contig)
				p->nr_contig = new->nr_contig;
			return p;
		}
	}
	return NULL;
}

static void fat_cache_add(struct inode *inode, struct fat_cache_id *new)
{
	struct fat_cache *cache, *tmp;

	if (new->fcluster == -1) /* dummy cache */
		return;

	spin_lock(&P2FAT_I(inode)->cache_lru_lock);
	if (new->id != FAT_CACHE_VALID &&
	    new->id != P2FAT_I(inode)->cache_valid_id)
		goto out;	/* this cache was invalidated */

	cache = fat_cache_merge(inode, new);
	if (cache == NULL) {
		if (P2FAT_I(inode)->nr_caches < fat_max_cache(inode)) {
			P2FAT_I(inode)->nr_caches++;
			spin_unlock(&P2FAT_I(inode)->cache_lru_lock);

			tmp = fat_cache_alloc(inode);
			spin_lock(&P2FAT_I(inode)->cache_lru_lock);
			cache = fat_cache_merge(inode, new);
			if (cache != NULL) {
				P2FAT_I(inode)->nr_caches--;
				fat_cache_free(tmp);
				goto out_update_lru;
			}
			cache = tmp;
		} else {
			struct list_head *p = P2FAT_I(inode)->cache_lru.prev;
			cache = list_entry(p, struct fat_cache, cache_list);
		}
		cache->fcluster = new->fcluster;
		cache->dcluster = new->dcluster;
		cache->nr_contig = new->nr_contig;
	}
out_update_lru:
	fat_cache_update_lru(inode, cache);
out:
	spin_unlock(&P2FAT_I(inode)->cache_lru_lock);
}

/*
 * Cache invalidation occurs rarely, thus the LRU chain is not updated. It
 * fixes itself after a while.
 */
static void __fat_cache_inval_inode(struct inode *inode)
{
	struct p2fat_inode_info *i = P2FAT_I(inode);
	struct fat_cache *cache;

	while (!list_empty(&i->cache_lru)) {
		cache = list_entry(i->cache_lru.next, struct fat_cache, cache_list);
		list_del_init(&cache->cache_list);
		i->nr_caches--;
		fat_cache_free(cache);
	}
	/* Update. The copy of caches before this id is discarded. */
	i->cache_valid_id++;
	if (i->cache_valid_id == FAT_CACHE_VALID)
		i->cache_valid_id++;
}

void p2fat_cache_inval_inode(struct inode *inode)
{
	spin_lock(&P2FAT_I(inode)->cache_lru_lock);
	__fat_cache_inval_inode(inode);
	spin_unlock(&P2FAT_I(inode)->cache_lru_lock);
}

static inline int cache_contiguous(struct fat_cache_id *cid, int dclus)
{
	cid->nr_contig++;
	return ((cid->dcluster + cid->nr_contig) == dclus);
}

static inline void cache_init(struct fat_cache_id *cid, int fclus, int dclus)
{
	cid->id = FAT_CACHE_VALID;
	cid->fcluster = fclus;
	cid->dcluster = dclus;
	cid->nr_contig = 0;
}

/* Panasonic Original */
inline static void fat_read_lookup(struct inode *inode, int cluster,int *count,int *nr)
{
	int mile;

	/* Lookup milestones */
	if(!P2FAT_I(inode)->i_cluster_milestones[0]){
		return;  /* no milestones */
	}
	else{  /* we have valid milestones */
		mile = (cluster >> FAT_MILE_BITS);

		if(mile > FAT_MILESTONES)
			return;

		/* find nearest milestone */
		while(!P2FAT_I(inode)->i_cluster_milestones[mile]){
			mile--;
		}
	}

	if(*count < (mile << FAT_MILE_BITS)){
		*count = (mile << FAT_MILE_BITS);
		*nr = P2FAT_I(inode)->i_cluster_milestones[mile];
	}

	return;
}
/*--------------------*/

/* Panasonic Original */
inline static void fat_establish_milestone(struct inode *inode, int cluster, int nr)
{
	int entry = ( cluster >> FAT_MILE_BITS );

	if(entry >= FAT_MILESTONES)
		return;

	/* file_cluster = cluster */
	P2FAT_I(inode)->i_cluster_milestones[entry] = nr;

	return;
}
/*--------------------*/

int p2fat_get_cluster(struct inode *inode, int cluster, int *fclus, int *dclus, int RT)
{
	struct super_block *sb = inode->i_sb;
	const int limit = sb->s_maxbytes >> P2FAT_SB(sb)->cluster_bits;
	struct p2fat_entry fatent;
	struct fat_cache_id cid;
	int nr;

	BUG_ON(P2FAT_I(inode)->i_start == 0);

	*fclus = 0;
	*dclus = P2FAT_I(inode)->i_start;
	if (cluster == 0)
		return 0;

	if (fat_cache_lookup(inode, cluster, &cid, fclus, dclus) < 0) {
		/*
		 * dummy, always not contiguous
		 * This is reinitialized by cache_init(), later.
		 */
		cache_init(&cid, -1, -1);
	}

/* Panasonic Original */
	if(RT)	fat_read_lookup(inode, cluster, fclus, dclus);
/*--------------------*/

	p2fatent_init(&fatent);
	while (*fclus < cluster) {

/* Panasonic Original */
		if(RT && !(*fclus & (FAT_MILE-1)))
			fat_establish_milestone(inode, *fclus, *dclus);
/*--------------------*/

		/* prevent the infinite loop of cluster chain */
		if (*fclus > limit) {
			p2fat_fs_panic(sb, "%s: detected the cluster chain loop"
				     " (i_pos %lld)", __FUNCTION__,
				     P2FAT_I(inode)->i_pos);
			nr = -EIO;
			goto out;
		}

		nr = p2fat_ent_read(inode, &fatent, *dclus);
		if (nr < 0)
			goto out;
		else if (nr == FAT_ENT_FREE) {
			p2fat_fs_panic(sb, "%s: invalid cluster chain"
				     " (i_pos %lld)", __FUNCTION__,
				     P2FAT_I(inode)->i_pos);
			nr = -EIO;
			goto out;
		} else if (nr == FAT_ENT_EOF) {
			fat_cache_add(inode, &cid);
			goto out;
		}
		(*fclus)++;
		*dclus = nr;
		if (!cache_contiguous(&cid, *dclus))
			cache_init(&cid, *fclus, *dclus);
	}
	nr = 0;
	fat_cache_add(inode, &cid);
out:
	p2fatent_brelse(&fatent);
	return nr;
}

static int fat_bmap_cluster(struct inode *inode, int cluster, int RT)
{
	struct super_block *sb = inode->i_sb;
	int ret, fclus, dclus;

	if (P2FAT_I(inode)->i_start == 0)
		return 0;

	ret = p2fat_get_cluster(inode, cluster, &fclus, &dclus, RT);
	if (ret < 0)
		return ret;
	else if (ret == FAT_ENT_EOF) {
		p2fat_fs_panic(sb, "%s: request beyond EOF (i_pos %lld)",
			     __FUNCTION__, P2FAT_I(inode)->i_pos);
		return -EIO;
	}
	return dclus;
}

int p2fat_bmap(struct inode *inode, sector_t sector, sector_t *phys,
	     unsigned long *mapped_blocks, int RT)
{
	struct super_block *sb = inode->i_sb;
	struct p2fat_sb_info *sbi = P2FAT_SB(sb);
	sector_t last_block;
	int cluster, offset;

	*phys = 0;
	*mapped_blocks = 0;
	if ((sbi->fat_bits != 32) && (inode->i_ino == P2FAT_ROOT_INO)) {
		if (sector < (sbi->dir_entries >> sbi->dir_per_block_bits)) {
			*phys = sector + sbi->dir_start;
			*mapped_blocks = 1;
		}
		return 0;
	}
	//ファイルの最終セクタ
	last_block = (P2FAT_I(inode)->mmu_private + (sb->s_blocksize - 1))
		>> sb->s_blocksize_bits;

	//指定クラスタが最終クラスタよりでかい場合は終了
	if (sector >= last_block)
		return 0;

	//指定セクタのクラスタ番号
	cluster = sector >> (sbi->cluster_bits - sb->s_blocksize_bits);

	//指定セクタのクラスタ内でのオフセット
	offset  = sector & (sbi->sec_per_clus - 1);

	cluster = fat_bmap_cluster(inode, cluster, RT);
	if (cluster < 0)
		return cluster;
	else if (cluster) {
		//クラスタ番号とオフセットから物理セクタを取得
		*phys = fat_clus_to_blknr(sbi, cluster) + offset;

		//マップしたセクタ数
		*mapped_blocks = sbi->sec_per_clus - offset;
		if (*mapped_blocks > last_block - sector)
			*mapped_blocks = last_block - sector;
	}
	return 0;
}
