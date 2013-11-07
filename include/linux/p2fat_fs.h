#ifndef _LINUX_P2FAT_FS_H
#define _LINUX_P2FAT_FS_H

/*
 * The P2FAT filesystem constants/structures
 */
#include <asm/byteorder.h>

#define SECTOR_SIZE	512		/* sector size (bytes) */
#define SECTOR_BITS	9		/* log2(SECTOR_SIZE) */
#define MSDOS_DPB	(MSDOS_DPS)	/* dir entries per block */
#define MSDOS_DPB_BITS	4		/* log2(MSDOS_DPB) */
#define MSDOS_DPS	(SECTOR_SIZE / sizeof(struct msdos_dir_entry))
#define MSDOS_DPS_BITS	4		/* log2(MSDOS_DPS) */
#define CF_LE_W(v)	le16_to_cpu(v)
#define CF_LE_L(v)	le32_to_cpu(v)
#define CT_LE_W(v)	cpu_to_le16(v)
#define CT_LE_L(v)	cpu_to_le32(v)

/* Panasonic Original */
#define P2FAT_FAT_INO	1	/* == FAT table INO */
/*--------------------*/
/* Panasonic Changed */
#define P2FAT_ROOT_INO	2	/* == MINIX_ROOT_INO */
/*--------------------*/
#define MSDOS_DIR_BITS	5	/* log2(sizeof(struct msdos_dir_entry)) */

/* directory limit */
#define FAT_MAX_DIR_ENTRIES	(65536)
#define FAT_MAX_DIR_SIZE	(FAT_MAX_DIR_ENTRIES << MSDOS_DIR_BITS)

#define ATTR_NONE	0	/* no attribute bits */
#define ATTR_RO		1	/* read-only */
#define ATTR_HIDDEN	2	/* hidden */
#define ATTR_SYS	4	/* system */
#define ATTR_VOLUME	8	/* volume label */
#define ATTR_DIR	16	/* directory */
#define ATTR_ARCH	32	/* archived */

/* attribute bits that are copied "as is" */
#define ATTR_UNUSED	(ATTR_VOLUME | ATTR_ARCH | ATTR_SYS | ATTR_HIDDEN)
/* bits that are used by the Windows 95/Windows NT extended FAT */
#define ATTR_EXT	(ATTR_RO | ATTR_HIDDEN | ATTR_SYS | ATTR_VOLUME)

#define CASE_LOWER_BASE	8	/* base is lower case */
#define CASE_LOWER_EXT	16	/* extension is lower case */

#define DELETED_FLAG	0xe5	/* marks file as deleted when in name[0] */
#define IS_FREE(n)	(!*(n) || *(n) == DELETED_FLAG)

/* valid file mode bits */
#define MSDOS_VALID_MODE (S_IFREG | S_IFDIR | S_IRWXU | S_IRWXG | S_IRWXO)
/* Convert attribute bits and a mask to the UNIX mode. */
#define MSDOS_MKMODE(a, m) (m & (a & ATTR_RO ? S_IRUGO|S_IXUGO : S_IRWXUGO))

#define MSDOS_NAME	11	/* maximum name length */
#define MSDOS_LONGNAME	256	/* maximum name length */
#define MSDOS_SLOTS	21	/* max # of slots for short and long names */
#define MSDOS_DOT	".          "	/* ".", padded to MSDOS_NAME chars */
#define MSDOS_DOTDOT	"..         "	/* "..", padded to MSDOS_NAME chars */

/* media of boot sector */
#define FAT_VALID_MEDIA(x)	((0xF8 <= (x) && (x) <= 0xFF) || (x) == 0xF0)
#define FAT_FIRST_ENT(s, x)	((P2FAT_SB(s)->fat_bits == 32 ? 0x0FFFFF00 : \
	P2FAT_SB(s)->fat_bits == 16 ? 0xFF00 : 0xF00) | (x))

/* start of data cluster's entry (number of reserved clusters) */
#define FAT_START_ENT	2

/* maximum number of clusters */
#define MAX_FAT12	0xFF4
#define MAX_FAT16	0xFFF4
#define MAX_FAT32	0x0FFFFFF6
#define MAX_FAT(s)	(P2FAT_SB(s)->fat_bits == 32 ? MAX_FAT32 : \
	P2FAT_SB(s)->fat_bits == 16 ? MAX_FAT16 : MAX_FAT12)

/* bad cluster mark */
#define BAD_FAT12	0xFF7
#define BAD_FAT16	0xFFF7
#define BAD_FAT32	0x0FFFFFF7

/* standard EOF */
#define EOF_FAT12	0xFFF
#define EOF_FAT16	0xFFFF
#define EOF_FAT32	0x0FFFFFFF

#define FAT_ENT_FREE	(0)
#define FAT_ENT_BAD	(BAD_FAT32)
#define FAT_ENT_EOF	(EOF_FAT32)

#define FAT_FSINFO_SIG1	0x41615252
#define FAT_FSINFO_SIG2	0x61417272
#define IS_FSINFO(x)	(le32_to_cpu((x)->signature1) == FAT_FSINFO_SIG1 \
			 && le32_to_cpu((x)->signature2) == FAT_FSINFO_SIG2)

struct __p2fat_dirent {
	long d_ino;
	__kernel_off_t d_off;
	unsigned short d_reclen;
	char d_name[256];
};

/*
 * ioctl commands
 */
#define VFAT_IOCTL_READDIR_BOTH		_IOR('r', 1, struct __p2fat_dirent [2])
#define VFAT_IOCTL_READDIR_SHORT	_IOR('r', 2, struct __p2fat_dirent [2])
/* <linux/videotext.h> has used 0x72 ('r') in collision, so skip a few */
#define FAT_IOCTL_GET_ATTRIBUTES	_IOR('r', 0x10, __u32)
#define FAT_IOCTL_SET_ATTRIBUTES	_IOW('r', 0x11, __u32)


/* Panasonic Original */
#define FAT_IOCTL_CHECK_REGION          _IOWR('r', 0x24, struct fat_ioctl_chk_region)
#define FAT_IOCTL_CONT_SEARCH		_IOWR('r', 0x25, struct fat_ioctl_space)
#define FAT_IOCTL_FAT_SYNC		_IO('r',   0x26)
//#define FAT_IOCTL_QUEUE_INIT		_IO('r',   0x27)
//#define FAT_IOCTL_WRITE_EXEC		_IOWR('r', 0x28, struct fat_ioctl_write_exec)
//#define FAT_IOCTL_QUEUE_DEPTH		_IOW('r',  0x29, int)
#define FAT_IOCTL_OPEN_INFO		_IOWR('r', 0x30, struct fat_ioctl_open_info)
#define FAT_IOCTL_FILE_REPAIR		_IO('r',   0x31)
//#define FAT_IOCTL_EBIO_ON		_IO('r',   0x32)
//#define FAT_IOCTL_EBIO_OFF		_IO('r',   0x33)
//#define FAT_IOCTL_EBIO_SET_WAITTIME	_IOW('r',  0x34, int)
#define FAT_IOCTL_GET_DEVNO		_IOR('r',  0x35, unsigned short)
#define FAT_IOCTL_SKIP_FATSYNC		_IO('r',   0x36)
#define FAT_IOCTL_FORCED_FAT_SYNC	_IO('r',   0x37)
#define FAT_IOCTL_FAT_SYNC2		FAT_IOCTL_FORCED_FAT_SYNC //__deprecated
#define FAT_IOCTL_IS_FAT_DIRTY		_IOR('r',  0x38, unsigned char)

#define FAT_IOCTL_INIT_DELAYPROC		_IOW('r', 0x40, struct delayproc_param_s)
#define FAT_IOCTL_CHECK_DELAYPROC_DIRTY	_IOR('r', 0x41, unsigned char)
#define FAT_IOCTL_CLEAR_DELAYPROC	_IO('r',  0x42)
#define FAT_IOCTL_GET_CARD_PARAMS		_IOR('r', 0x43, struct p2_params)
//#define FAT_IOCTL_IS_INODE_DIRTY		_IOR('r', 0x44, unsigned char)
#define FAT_IOCTL_EXEC_DELAYPROC		_IO('r',  0x45)
#define FAT_IOCTL_GET_NOTIFY_END                _IOWR('r', 0x46, struct fat_end_notify)
#define FAT_IOCTL_KICK_NOTIFY_UP                _IO('r', 0x47)
#define FAT_IOCTL_SET_DEVICE_INFO               _IOW('r', 0x48, struct p2fat_device_info)
/*--------------------*/
#define FAT_IOCTL_FILE_PRERELEASE               _IO('r', 0x49)
#define FAT_IOCTL_GET_FS_TYPE			_IOR('r', 0x50, enum fat_fs_type)

/* Panasonic TEST */
#define FAT_IOCTL_READ_FAT		_IOR('r', 0xF0, struct fat_table)
#define FAT_IOCTL_WRITE_FAT		_IOW('r', 0xF1, struct fat_table)
#define FAT_IOCTL_ALLOC_FAT		_IOR('r', 0xF2, int)
#define FAT_IOCTL_CONT_ALLOC_FAT	_IOR('r', 0xF3, int)
#define FAT_IOCTL_FREE_FAT		_IOR('r', 0xF4, int)
#define FAT_IOCTL_RESERVE_FAT_FREE	_IOR('r', 0xF5, int)
#define FAT_IOCTL_APPLY_RESERVE_FAT	_IO('r', 0xF6)

enum fat_fs_type {KRNL_P2FAT, FUSE_P2FAT};

struct fat_table{
        int nr;
        int next;
};
/*--------------------*/

/* Panasonic Original */

/* CONT SEARCH TYPE */
#define FAT_TYPE_CONT_SEARCH   0	/* search from top to bottom. */
#define FAT_TYPE_AREA_SEARCH   1	/* search specified size area */
#define FAT_TYPE_CAPA_SEARCH   2	/* search until specified capacity is found */

struct fat_ioctl_space{
	int n;
	int strict;
	int type;		/* CONT SEARCH TYPE */
	unsigned int unit;
	unsigned long ret;
};

struct fat_ioctl_chk_region{
  unsigned char version; /* [out] Card Version */
  int continuance;       /* [out] Number of Clusters */
  int result;            /* [out] Result 0:OK -1:NG */
  unsigned long first;
  unsigned long last;
};

struct fat_ioctl_open_info{
  int nonRTwrite;
  int nonRTread;
  int RTwrite;
  int RTread;
};

struct p2fat_device_info{
  unsigned long device_addr;
  unsigned long trash_can_offset;
};
/*--------------------*/

/*
 * vfat shortname flags
 */
#define VFAT_SFN_DISPLAY_LOWER	0x0001 /* convert to lowercase for display */
#define VFAT_SFN_DISPLAY_WIN95	0x0002 /* emulate win95 rule for display */
#define VFAT_SFN_DISPLAY_WINNT	0x0004 /* emulate winnt rule for display */
#define VFAT_SFN_CREATE_WIN95	0x0100 /* emulate win95 rule for create */
#define VFAT_SFN_CREATE_WINNT	0x0200 /* emulate winnt rule for create */

struct p2fat_boot_sector {
	__u8	ignored[3];	/* Boot strap short or near jump */
	__u8	system_id[8];	/* Name - can be used to special case
				   partition manager volumes */
	__u8	sector_size[2];	/* bytes per logical sector */
	__u8	sec_per_clus;	/* sectors/cluster */
	__le16	reserved;	/* reserved sectors */
	__u8	fats;		/* number of FATs */
	__u8	dir_entries[2];	/* root directory entries */
	__u8	sectors[2];	/* number of sectors */
	__u8	media;		/* media code */
	__le16	fat_length;	/* sectors/FAT */
	__le16	secs_track;	/* sectors per track */
	__le16	heads;		/* number of heads */
	__le32	hidden;		/* hidden sectors (unused) */
	__le32	total_sect;	/* number of sectors (if sectors == 0) */

	/* The following fields are only used by FAT32 */
	__le32	fat32_length;	/* sectors/FAT */
	__le16	flags;		/* bit 8: fat mirroring, low 4: active fat */
	__u8	version[2];	/* major, minor filesystem version */
	__le32	root_cluster;	/* first cluster in root directory */
	__le16	info_sector;	/* filesystem info sector */
	__le16	backup_boot;	/* backup boot sector */
	__le16	reserved2[6];	/* Unused */
};

struct p2fat_boot_fsinfo {
	__le32   signature1;	/* 0x41615252L */
	__le32   reserved1[120];	/* Nothing as far as I can tell */
	__le32   signature2;	/* 0x61417272L */
	__le32   free_clusters;	/* Free cluster count.  -1 if unknown */
	__le32   next_cluster;	/* Most recently allocated cluster */
	__le32   reserved2[4];
};

struct msdos_dir_entry {
	__u8	name[8],ext[3];	/* name and extension */
	__u8	attr;		/* attribute bits */
	__u8    lcase;		/* Case for base and extension */
	__u8	ctime_cs;	/* Creation time, centiseconds (0-199) */
	__le16	ctime;		/* Creation time */
	__le16	cdate;		/* Creation date */
	__le16	adate;		/* Last access date */
	__le16	starthi;	/* High 16 bits of cluster in FAT32 */
	__le16	time,date,start;/* time, date and first cluster */
	__le32	size;		/* file size (in bytes) */
};

/* Up to 13 characters of the name */
struct msdos_dir_slot {
	__u8    id;		/* sequence number for slot */
	__u8    name0_4[10];	/* first 5 characters in name */
	__u8    attr;		/* attribute byte */
	__u8    reserved;	/* always 0 */
	__u8    alias_checksum;	/* checksum for 8.3 alias */
	__u8    name5_10[12];	/* 6 more characters in name */
	__le16   start;		/* starting cluster number, 0 in long slots */
	__u8    name11_12[4];	/* last 2 characters in name */
};

struct fat_slot_info {
	loff_t i_pos;		/* on-disk position of directory entry */
	loff_t slot_off;	/* offset for slot or de start */
	int nr_slots;		/* number of slots + 1(de) in filename */
	struct msdos_dir_entry *de;
	struct buffer_head *bh;
};

#define FAT_MAX_NOTIFY_END (12)
enum {P2FAT_FORCE_WAKEUP};

struct fat_end_notify{
  unsigned long flags;
  int wait;
  int entries;
  int ret;
  unsigned long file_ids[FAT_MAX_NOTIFY_END];
  unsigned long notify_ids[FAT_MAX_NOTIFY_END];
  unsigned long sizes[FAT_MAX_NOTIFY_END];
};

#ifdef __KERNEL__

#include <linux/buffer_head.h>
#include <linux/string.h>
#include <linux/nls.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/magic.h>

#include <linux/reservoir_fs.h>
#include <p2/spd_ioctl.h>
#include <linux/rtctrl.h>

/* コールバック用のCharデバイス */
#define P2FAT_CHARDEV_MAJOR (60)
#define P2FAT_CHARDEV_MINOR (0)
#define P2FAT_CHARDEV_NAME  "P2FAT"

#define CONFIG_MK_RDONLY_FILE

struct p2fat_mount_options {
	uid_t fs_uid;
	gid_t fs_gid;
	unsigned short fs_fmask;
	unsigned short fs_dmask;
	unsigned short codepage;  /* Codepage for shortname conversions */
	char *iocharset;          /* Charset used for filename input/display */
	unsigned short shortname; /* flags for shortname display/create rule */
	unsigned char name_check; /* r = relaxed, n = normal, s = strict */
	unsigned quiet:1,         /* set = fake successful chmods and chowns */
		 showexec:1,      /* set = only set x bit for com/exe/bat */
		 sys_immutable:1, /* set = system files are immutable */
		 dotsOK:1,        /* set = hidden and system files are named '.filename' */
		 isvfat:1,        /* 0=no vfat long filename support, 1=vfat support */
		 utf8:1,	  /* Use of UTF-8 character set (Default) */
		 unicode_xlate:1, /* create escape sequences for unhandled Unicode */
		 numtail:1,       /* Does first alias have a numeric '~1' type tail? */
		 atari:1,         /* Use Atari GEMDOS variation of MS-DOS fs */
		 flush:1,	  /* write things quickly */
		 nocase:1;	  /* Does this need case conversion? 0=need case conversion*/

	/* Panasonic Original */
	unsigned long  fat_align; /* Alignment */
	unsigned long  AU_size;	  /* AU_size */
	/*--------------------*/
};

#define FAT_HASH_BITS	8
#define FAT_HASH_SIZE	(1UL << FAT_HASH_BITS)
#define FAT_HASH_MASK	(FAT_HASH_SIZE-1)

/* Panasonic Original */
struct fat_cont_space{
	int n;
	unsigned int prev_free;		//連続空き領域の先頭クラスタ番号
	unsigned long cont;
	unsigned long pos;
};
/*--------------------*/

/* Panasonic Original */
//msdos_sb_info - sync_flag用
#define ON_FAT_SYNC	0	//FAT SYNC(fatent.c : fatent_sync())が実行中
#define SKIP_FAT_SYNC	1	//FAT SYNCをスキップする
/*--------------------*/

/* Panasonic Original */
#define FAT_IO_PAGES_BITS	4				//16ページ(64KB)単位で管理
#define FAT_IO_PAGES		(1 << FAT_IO_PAGES_BITS)
#define FAT_SPACE_SIZE		(6L << 20)			//FAT置き場(6MB)
#define FAT_TOTAL_PAGES		(FAT_SPACE_SIZE >> PAGE_SHIFT)	//FAT置き場のページ数
#define FAT_LIST_NUM		(FAT_TOTAL_PAGES >> FAT_IO_PAGES_BITS) //FATリストの要素数

struct p2fat_cluster_t{
  unsigned long file_cluster;
  unsigned long disk_cluster;
  struct list_head clusters_list;
};
/*--------------------*/

/*
 * MS-DOS file system in-core superblock data
 */
struct p2fat_sb_info {
	unsigned short sec_per_clus; /* sectors/cluster */
	unsigned short cluster_bits; /* log2(cluster_size) */
	unsigned int cluster_size;   /* cluster size */
	unsigned char fats,fat_bits; /* number of FATs, FAT bits (12 or 16) */
	unsigned short fat_start;
	unsigned long fat_length;    /* FAT start & length (sec.) */
	unsigned long dir_start;
	unsigned short dir_entries;  /* root dir start & entries */
	unsigned long data_start;    /* first data sector */
	unsigned long max_cluster;   /* maximum cluster number */
	unsigned long root_cluster;  /* first cluster of the root directory */
	unsigned long fsinfo_sector; /* sector number of FAT32 fsinfo */
	struct mutex fat_lock;
	unsigned int prev_free;      /* previously allocated cluster number */
	unsigned int free_clusters;  /* -1 if undefined */
	unsigned int show_inval_log;  /* 1 if show log in case of detecting invalid free_cluster */
	struct p2fat_mount_options options;
	struct nls_table *nls_disk;  /* Codepage used on disk */
	struct nls_table *nls_io;    /* Charset used for input and display */
	const void *dir_ops;		     /* Opaque; default directory operations */
	int dir_per_block;	     /* dir entries per block */
	int dir_per_block_bits;	     /* log2(dir_per_block) */

/* Panasonic Original */
	struct inode *fat_inode;	//FAT用inode
	struct p2_params  p2_params;	//P2カード情報

	unsigned char is_fat_collapsed;		//FATが壊れている場合は空き容量は０にする
	unsigned char fatent_align_bits;	//FATのアライメント(書き込み単位(ビット))
	unsigned char write_fat_num;		//書き出すFAT番号
	unsigned long sync_flag;		//FAT SYNC用フラグ
	unsigned long data_cluster_offset;	//データ領域のオフセット
	int fat_pages_num;               	//FATのアライメント数
	struct fatent_page *fat_pages;
	struct fat_cont_space cont_space;	//FATの連続空きスペース情報
	unsigned int dirty_count;		//汚れているFAT用ページリストの数
	struct list_head reserved_list;		//削除予約されたクラスタ番号のリスト
	struct mutex rt_inode_dirty_lock;
	struct list_head rt_inode_dirty_list;

  unsigned long reserved_cluster_head;  //確保した連続領域の先頭
  unsigned long reserved_cluster_count; //確保した連続領域の残りクラスタ数
  struct mutex reserved_cluster_lock;  //上記2変数に関するロック

  struct workqueue_struct *rt_chain_updater_wq; //下記更新ワーク用workqueue
  struct work_struct rt_chain_updater;    //クラスタチェーンの更新用ワーク
  struct list_head rt_updated_clusters;   //FAT更新の必要のあるクラスタ
  spinlock_t rt_updated_clusters_lock;    //上記変数に関するロック
  unsigned long rt_private_count[MAX_RESERVOIRS];   //i/o schedulerに渡ったbioの数

  struct super_block *sb;                 //親となるsuper_block

/*--------------------*/

	int fatent_shift;
	struct fatent_operations *fatent_ops;

	spinlock_t inode_hash_lock;
	struct hlist_head inode_hashtable[FAT_HASH_SIZE];
};

#define FAT_CACHE_VALID	0	/* special case for valid cache */

/* Panasonic Original */
/** milestones per 8192 **/
#define FAT_MILE_BITS		8
#define FAT_MILE		256
/*** 131072 clusters = 4GB ***/
#define FAT_MILESTONES		( 131072 >> FAT_MILE_BITS )

#define FAT_SUSPENDED_INODE	1	/* サイズゼロのエントリを書き出さない */
#define FAT_RM_RESERVED		2	/* 上書きリネームを安全におこなう */
#define FAT_NEWDIR_INODE	3	/* 遅延処理中に新規作成されたエントリを書き出さない */ 
/*--------------------*/

/*
 * MS-DOS file system inode data in memory
 */
struct p2fat_inode_info {
	spinlock_t cache_lru_lock;
	struct list_head cache_lru;
	int nr_caches;
	/* for avoiding the race between fat_free() and fat_get_cluster() */
	unsigned int cache_valid_id;

	loff_t mmu_private;
	int i_start;		/* first cluster or 0 */
	int i_logstart;		/* logical first cluster */
	int i_attrs;		/* unused attribute bits */
	loff_t i_pos;		/* on-disk position of directory entry or 0 */
	struct hlist_node i_fat_hash;	/* hash by i_location */
	struct inode vfs_inode;

	/* Panasonic Original */
	unsigned long i_flags;
 	int i_cluster_milestones[FAT_MILESTONES + 1];   /* milestones of disk_cluster per FAT_MILE */
	struct list_head i_rt_dirty;    /* hash by i_location */
	struct buffer_head *suspended_bh;   /* bh ponter for reflection delay */
 	/*--------------------*/
};

static inline struct p2fat_sb_info *P2FAT_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}

static inline struct p2fat_inode_info *P2FAT_I(struct inode *inode)
{
	return container_of(inode, struct p2fat_inode_info, vfs_inode);
}

/* Return the FAT attribute byte for this inode */
static inline u8 fat_attr(struct inode *inode)
{
	return ((inode->i_mode & S_IWUGO) ? ATTR_NONE : ATTR_RO) |
		(S_ISDIR(inode->i_mode) ? ATTR_DIR : ATTR_NONE) |
		P2FAT_I(inode)->i_attrs;
}

static inline unsigned char fat_checksum(const __u8 *name)
{
	unsigned char s = name[0];
	s = (s<<7) + (s>>1) + name[1];	s = (s<<7) + (s>>1) + name[2];
	s = (s<<7) + (s>>1) + name[3];	s = (s<<7) + (s>>1) + name[4];
	s = (s<<7) + (s>>1) + name[5];	s = (s<<7) + (s>>1) + name[6];
	s = (s<<7) + (s>>1) + name[7];	s = (s<<7) + (s>>1) + name[8];
	s = (s<<7) + (s>>1) + name[9];	s = (s<<7) + (s>>1) + name[10];
	return s;
}

static inline sector_t fat_clus_to_blknr(struct p2fat_sb_info *sbi, int clus)
{
	return ((sector_t)clus - FAT_START_ENT) * sbi->sec_per_clus
		+ sbi->data_start;
}

static inline void fat16_towchar(wchar_t *dst, const __u8 *src, size_t len)
{
#ifdef __BIG_ENDIAN
	while (len--) {
		*dst++ = src[0] | (src[1] << 8);
		src += 2;
	}
#else
	memcpy(dst, src, len * 2);
#endif
}

static inline void fatwchar_to16(__u8 *dst, const wchar_t *src, size_t len)
{
#ifdef __BIG_ENDIAN
	while (len--) {
		dst[0] = *src & 0x00FF;
		dst[1] = (*src & 0xFF00) >> 8;
		dst += 2;
		src++;
	}
#else
	memcpy(dst, src, len * 2);
#endif
}

/****************** For P2FAT fs *******************/

/* p2fat/cache.c */
extern void p2fat_cache_inval_inode(struct inode *inode);
extern int p2fat_get_cluster(struct inode *inode, int cluster,
			   int *fclus, int *dclus, /*Pana Add*/int RT/**/);
extern int p2fat_bmap(struct inode *inode, sector_t sector, sector_t *phys,
		    unsigned long *mapped_blocks, /*Pana Add*/int RT/**/);

/* p2fat/dir.c */
extern const struct file_operations p2fat_dir_operations;
extern int p2fat_search_long(struct inode *inode, const unsigned char *name,
			   int name_len, struct fat_slot_info *sinfo);
extern int p2fat_dir_empty(struct inode *dir);
extern int p2fat_subdirs(struct inode *dir);
extern int p2fat_scan(struct inode *dir, const unsigned char *name,
		    struct fat_slot_info *sinfo);
extern int p2fat_get_dotdot_entry(struct inode *dir, struct buffer_head **bh,
				struct msdos_dir_entry **de, loff_t *i_pos);
extern int p2fat_alloc_new_dir(struct inode *dir, struct timespec *ts);
extern int p2fat_add_entries(struct inode *dir, void *slots, int nr_slots,
			   struct fat_slot_info *sinfo, int suspend);
extern int p2fat_remove_entries(struct inode *dir, struct fat_slot_info *sinfo);

/* p2fat/fatent.c */
struct p2fat_entry {
	int entry;
	union {
//		u8 *ent12_p[2];
		__le16 *ent16_p;
		__le32 *ent32_p;
	} u;
	int nr_bhs;
//	struct buffer_head *bhs[2];
	/* Panasonic Original */
	int page_index; //データを格納するFAT_pagesのインデックス
	struct fatent_page *pages;
	int list_index;
	/*--------------------*/
};

static inline void p2fatent_init(struct p2fat_entry *fatent)
{
	fatent->nr_bhs = 0;
	fatent->entry = 0;
	fatent->u.ent32_p = NULL;
//	fatent->bhs[0] = fatent->bhs[1] = NULL;
	/* Panasonic Original */
	fatent->page_index = 0;
	fatent->pages = NULL;
	fatent->list_index = 0;
	/*--------------------*/
}

static inline void p2fatent_set_entry(struct p2fat_entry *fatent, int entry)
{
	fatent->entry = entry;
	fatent->u.ent32_p = NULL;
}

static inline void p2fatent_brelse(struct p2fat_entry *fatent)
{
//	int i;
	fatent->u.ent32_p = NULL;
//	for (i = 0; i < fatent->nr_bhs; i++)
//		brelse(fatent->bhs[i]);
	fatent->nr_bhs = 0;
//	fatent->bhs[0] = fatent->bhs[1] = NULL;
	/* Panasonic Original */
	fatent->page_index = 0;
	fatent->list_index = 0;
	fatent->pages = NULL;
	/*--------------------*/
}

/* Panasonic Move */
//FATテーブルの排他制御(ロック)
// 参照元  : p2fat_alloc_clusters(), p2fat_free_clusters(), p2fat_count_free_clusters() //すべてfatent.c内
static inline void lock_fat(struct p2fat_sb_info *sbi)
{
	mutex_lock(&sbi->fat_lock);
}

//FATテーブルの排他制御(アンロック)
// 参照元  : p2fat_alloc_clusters(), p2fat_free_clusters(), p2fat_count_free_clusters() //すべてfatent.c内
static inline void unlock_fat(struct p2fat_sb_info *sbi)
{
	mutex_unlock(&sbi->fat_lock);
}
/*----------------*/

/* Panasonic Change */
extern int/*void*/ p2fat_ent_access_init(struct super_block *sb);
/*------------------*/
extern int __p2fat_ent_read(struct super_block *sb, struct p2fat_entry *fatent,
			    int entry);
extern int __p2fat_ent_write(struct super_block *sb, struct p2fat_entry *fatent,
			     int new, int wait);
extern int p2fat_ent_read(struct inode *inode, struct p2fat_entry *fatent,
			int entry);
extern int p2fat_ent_write(struct inode *inode, struct p2fat_entry *fatent,
			 int new, int wait);
extern int p2fat_alloc_clusters(struct inode *inode, int *cluster,
			      int nr_cluster);
extern int p2fat_free_clusters(struct inode *inode, int cluster);
extern int p2fat_count_free_clusters(struct super_block *sb);

/* Panasonic Original */
extern void p2fat_ent_access_exit(struct super_block *sb);
extern int p2fat_reserve_fat_free(struct inode *, int);
extern int p2fat_apply_reserved_fat(struct super_block *);
extern int p2fat_sync(struct super_block *);
extern int p2fat_sync_nolock(struct super_block *);
extern int p2fat_cont_search(struct inode *, struct file *, struct fat_ioctl_space *);
extern int p2fat_alloc_cont_clusters(struct super_block *, int);
extern int p2fat_check_cont_space(struct super_block *, int);
extern int p2fat_mem_init(void);
extern void p2fat_free_mem(void);
/*--------------------*/

/* Panasonic Original */
/* p2fat/mpage.c */
extern int p2fat_mpage_readpage(struct page *, get_block_t);
extern int p2fat_mpage_readpages(struct address_space *, struct list_head *, unsigned, get_block_t);
extern int p2fat_mpage_writepages(struct address_space *, struct writeback_control *, get_block_t);
/*--------------------*/

/* p2fat/file.c */
extern int p2fat_generic_ioctl(struct inode *inode, struct file *filp,
			     unsigned int cmd, unsigned long arg);
extern const struct file_operations p2fat_file_operations;
extern const struct inode_operations p2fat_file_inode_operations;
extern int p2fat_notify_change(struct dentry * dentry, struct iattr * attr);
extern void p2fat_truncate(struct inode *inode);
extern int p2fat_getattr(struct vfsmount *mnt, struct dentry *dentry,
		       struct kstat *stat);

/* p2fat/inode.c */
extern void p2fat_attach(struct inode *inode, loff_t i_pos);
extern void p2fat_detach(struct inode *inode);
extern struct inode *p2fat_iget(struct super_block *sb, loff_t i_pos);
extern struct inode *p2fat_build_inode(struct super_block *sb,
			struct msdos_dir_entry *de, loff_t i_pos);
extern int p2fat_sync_inode(struct inode *inode);
extern int __p2fat_fill_super(struct super_block *sb, void *data, int silent,
			const struct inode_operations *fs_dir_inode_ops, int isvfat);
extern int p2fat_flush_inodes(struct super_block *sb, struct inode *i1,
		            struct inode *i2);
extern int p2fat_get_block(struct inode *inode, sector_t iblock,
			   struct buffer_head *bh_result, int create);
/* Panasonic Original */
extern void write_rt_dirty_inodes(struct super_block *sb);
/*--------------------*/

/* p2fat/misc.c */
extern void p2fat_fs_panic(struct super_block *s, const char *fmt, ...);
extern void p2fat_clusters_flush(struct super_block *sb, int sync);
extern int p2fat_chain_add(struct inode *inode, int new_dclus, int nr_cluster);
extern int p2fat_date_dos2unix(unsigned short time, unsigned short date);
extern void p2fat_date_unix2dos(int unix_date, __le16 *time, __le16 *date);
extern int p2fat_sync_bhs(struct buffer_head **bhs, int nr_bhs);

int p2fat_cache_init(void);
void p2fat_cache_destroy(void);

/* p2fat/reservoir.c */
extern struct reservoir_operations p2fat_rs_ops;
int init_p2fat_callback_module(void);
void exit_p2fat_callback_module(void);
int reservoir_rt_fsync(struct inode *inode);
void p2fat_update_cluster_chain(struct work_struct *work);

#endif /* __KERNEL__ */

#endif

