/*
 * Copyright (C) 2004, OGAWA Hirofumi
 * Released under GPL v2.
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/p2fat_fs.h>

/* Panasonic Original */
#include <linux/swap.h>		//for mark_page_accessed()
#include <linux/writeback.h>	//for struct writeback_control
/*--------------------*/

/* Panasonic Original */
typedef int (fat_get_block_t)(struct inode *, sector_t, struct buffer_head *, int, int);
/*--------------------*/

/* Panasonic Original */
struct fatent_page{	//FAT�ơ��֥�Υ��饤���Ⱦ���
	unsigned int align_index;	//���饤�����ֹ�
	unsigned long io_pages;
	int *indexes;			//������Ƥ�줿FAT_list�Υ���ǥå���
};
/*--------------------*/

/* Panasonic Original */
#define FAT_STATUS_USED			1  //������ƺѤ�
#define FAT_STATUS_ROOT			2  //�롼�ȥ��饹��(�ɤ��Ф���ʤ�)
#define FAT_STATUS_DIRTY		3  //�񤭹��ޤ줿

struct fatent_page_status{	//FAT_list�ξ���
	unsigned long status;	//����
	unsigned int index;	//�б�����FAT_pages����Ƭ�Υ���ǥå���

	int assign_index;
	struct fatent_page *fat_page; 

	struct list_head lru;

	struct mutex lock;

	struct super_block *sb;	//������Ƥ�줿�֥�å�
};
/*--------------------*/

/* Panasonic Original */
struct fatent_list{	//I/O�ѥꥹ��
	struct fatent_page_status *page;
	int page_index;
	struct list_head lru;
};
/*--------------------*/

/* Panasonic Original */
struct fatent_reserve_fat{
	int nr_cluster;
	struct list_head lru;
};
/*--------------------*/

/* Panasonic Original */
static struct page *FAT_pages[FAT_TOTAL_PAGES];	//FAT�ơ��֥��֤���
/*--------------------*/

/* Panasonic Original */
static struct fatent_page_status FAT_list[FAT_LIST_NUM];	//FAT�ơ��֥��֤������
static struct list_head list_clean_fat;	//���꡼����֤���Υꥹ��
static struct list_head list_dirty_fat;	//����Ƥ����֤���Υꥹ��
static spinlock_t clean_fat_lock;
static spinlock_t dirty_fat_lock;
static struct mutex get_page_lock;
/*--------------------*/

/* Panasonic Original */
#define GET_LIST_COUNT(count, walk, list) { \
	count = 0; list_for_each(walk, &list){ count++; } \
}
/*--------------------*/

/* Panasonic Original */
static int fatent_check_exist(struct fatent_page *fat_page, int list_index, int io_index)
{
	//�����ˤ��뤫�����å�
	if(list_index >= 0 && list_index < FAT_LIST_NUM){  //�ڡ����ֹ椬�����ξ��
		if(test_bit(FAT_STATUS_USED, &FAT_list[list_index].status) //������ƺѤ�
			 && (FAT_list[list_index].fat_page == fat_page)
			 && (FAT_list[list_index].assign_index == io_index)){
			return 1;
		}
	}
	return 0;
}
/*--------------------*/

/* Panasonic Original */
static void p2fat_sync_dirtiest_sb(void)
{
	int dirty_count = 0;
	struct super_block *sb = NULL;
	struct list_head *walk;
	struct fatent_page_status *stat;

	spin_lock(&dirty_fat_lock);
	list_for_each(walk, &list_dirty_fat){
		stat = list_entry(walk, struct fatent_page_status, lru);
		if(dirty_count < P2FAT_SB(stat->sb)->dirty_count){
			sb = stat->sb;
			dirty_count = P2FAT_SB(stat->sb)->dirty_count;
		}
	}
	spin_unlock(&dirty_fat_lock);

	//�����ƥ��ʥ�ǥ������ʤ����
	if(!sb) return;

	p2fat_sync_nolock(sb);
}
/*--------------------*/

/* Panasonic Original */
static int fatent_set_dirty(int nr)
{
	if(nr >= FAT_LIST_NUM){
		printk("LIST NUM is too big. %d (MAX %lu)\n", nr, FAT_LIST_NUM);
		return -1;
	}

	if(!test_bit(FAT_STATUS_USED, &FAT_list[nr].status)){
		printk("This page is not used\n");
		return -1;
	}

	if(test_and_set_bit(FAT_STATUS_DIRTY, &FAT_list[nr].status)){
		printk("PAGE has been already dirty.\n");
		return 0;
	}

	P2FAT_SB(FAT_list[nr].sb)->dirty_count++;

	spin_lock(&dirty_fat_lock);
	list_del(&FAT_list[nr].lru);
	list_add_tail(&FAT_list[nr].lru, &list_dirty_fat);
	spin_unlock(&dirty_fat_lock);

	return 0;
}
/*--------------------*/

/* Panasonic Original */
static int fatent_set_clean(int nr)
{
	if(nr >= FAT_LIST_NUM){
		printk("LIST NUM is too big. %d (MAX %lu)\n", nr, FAT_LIST_NUM);
		return -1;
	}

	if(!test_bit(FAT_STATUS_USED, &FAT_list[nr].status)){
		printk("This page is not used\n");
		return -1;
	}

	if(!test_and_clear_bit(FAT_STATUS_DIRTY, &FAT_list[nr].status)){
		printk("PAGE has not dirty!\n");
		return -1;
	}

	if(P2FAT_SB(FAT_list[nr].sb)->dirty_count == 0){
		printk("dirty count has already been 0.\n");
	}
	else
		P2FAT_SB(FAT_list[nr].sb)->dirty_count--;

	spin_lock(&clean_fat_lock);
	list_del(&FAT_list[nr].lru);
	list_add_tail(&FAT_list[nr].lru, &list_clean_fat);
	spin_unlock(&clean_fat_lock);

	return 0;
}
/*--------------------*/

/* Panasonic Original */
static int fatent_bmap(struct inode *inode, sector_t sector, sector_t *phys,
	     unsigned long *mapped_blocks)
{
	struct super_block *sb = inode->i_sb;
	struct p2fat_sb_info *sbi = P2FAT_SB(sb);
	sector_t last_block;

	*phys = 0;
	*mapped_blocks = 0;

	//�ե�����κǽ�������
	last_block = (P2FAT_I(inode)->mmu_private + (sb->s_blocksize - 1))
		>> sb->s_blocksize_bits;

	//���ꥯ�饹�����ǽ����饹�����Ǥ������Ͻ�λ
	if (sector >= last_block){
		return 0;
	}

	if(sbi->fat_bits == 32){
		*phys = sbi->write_fat_num * sbi->fat_length + sbi->fat_start + sector;
	}
	else{
		*phys = sbi->write_fat_num * 2 * sbi->fat_length + sbi->fat_start + sector;
	}

	*mapped_blocks = last_block - sector;

	return 0;
}
/*--------------------*/

/* Panasonic Original */
static int fatent_get_block(struct inode *inode, sector_t iblock,
			 struct buffer_head *bh_result, int create)
{
	int err;
	sector_t phys;

	struct super_block *sb = inode->i_sb;
	//struct p2fat_sb_info *sbi = P2FAT_SB(sb);

	//�ɤ߹��⤦�Ȥ��륯�饹����
	unsigned long max_blocks = bh_result->b_size >> inode->i_blkbits;
	unsigned long mapped_blocks;

	err = fatent_bmap(inode, iblock, &phys, &mapped_blocks);
	if (err)
		return err;
	if (phys) {
		map_bh(bh_result, sb, phys);
		max_blocks = min(mapped_blocks, max_blocks);
		return 0;
	}

	bh_result->b_size = max_blocks << sb->s_blocksize_bits;

	return 0;
}
/*--------------------*/

/* Panasonic Experiment */
static int fatent_readpage(struct file *file, struct page *page)
{
	if(current_is_pdflush()){
		return 0;
	}
	return p2fat_mpage_readpage(page, fatent_get_block);
}
/*----------------------*/

/* Panasonic Experiment */
static int fatent_readpages(struct file *file, struct address_space *mapping,
			 struct list_head *pages, unsigned nr_pages)
{
	if(current_is_pdflush()){
		return 0;
	}
	return p2fat_mpage_readpages(mapping, pages, nr_pages, fatent_get_block);
}
/*----------------------*/

/* Panasonic Experiment */
static int fatent_write_begin(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned flags,
			struct page **pagep, void **fsdata)
{
	*pagep = NULL;
	if(current_is_pdflush()){
		return 0;
	}
	return cont_write_begin(file, mapping, pos, len, flags, pagep, fsdata,
				fatent_get_block,
				&P2FAT_I(mapping->host)->mmu_private);
}
/*----------------------*/

/* Panasonic Experiment */
static int fatent_write_end(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct page *pagep, void *fsdata)
{
	if(current_is_pdflush()){
		return 0;
	}
	return generic_write_end(file, mapping, pos, len, copied, pagep, fsdata);
}
/*----------------------*/

/* Panasonic Experiment */
static int fatent_writepage(struct page *page, struct writeback_control *wbc)
{
	if(current_is_pdflush()){
		return 0;
	}
	return block_write_full_page(page, fatent_get_block, wbc);
}
/*----------------------*/

/* Panasonic Experiment */
static int fatent_writepages(struct address_space *mapping,
			  struct writeback_control *wbc)
{
	if(current_is_pdflush()){
		return 0;
	}
	return p2fat_mpage_writepages(mapping, wbc, fatent_get_block);
}
/*----------------------*/

/* Panasonic Experiment */
static sector_t fatent_fat_bmap(struct address_space *mapping, sector_t block)
{
	if(current_is_pdflush()){
		return 0;
	}
	return generic_block_bmap(mapping, block, fatent_get_block);
}
/*----------------------*/

/* Panasonic Experiment */
static void fatent_sync_page(struct page *page)
{
	if(current_is_pdflush()){
		return;
	}
	block_sync_page(page);
}
/*----------------------*/

/* Panasonic Original */
static const struct address_space_operations fat_fatent_aops = {
	.readpage	= fatent_readpage,
	.readpages	= fatent_readpages,
	.writepage	= fatent_writepage,
	.writepages	= fatent_writepages,
	.sync_page	= fatent_sync_page,
	.write_begin	= fatent_write_begin,
	.write_end	= fatent_write_end,
	.bmap		= fatent_fat_bmap
};
/*--------------------*/

/* Panasonic Original */
static void fat_list_lock(int list_index)
{
	if(unlikely(list_index >= 0 && list_index < FAT_LIST_NUM)){  //�ڡ����ֹ椬�����ξ��
		return;
	}
	mutex_lock(&FAT_list[list_index].lock);
}
/*--------------------*/

/* Panasonic Original */
static void fat_list_unlock(int list_index)
{
	if(unlikely(list_index >= 0 && list_index < FAT_LIST_NUM)){  //�ڡ����ֹ椬�����ξ��
		return;
	}
	mutex_unlock(&FAT_list[list_index].lock);
}
/*--------------------*/

/* Panasonic Original */
static int fat_ent_pagenr(struct super_block *sb, struct p2fat_entry *fatent,
			int align_offset, sector_t align_index, int *page_offset, int *list_index)
{
	struct p2fat_sb_info *sbi = P2FAT_SB(sb);
	unsigned long io_size;
	int io_index, io_offset;
	int page_index;

	if(!fatent->pages){
		printk("fatent->pages is NULL\n");
		return -1;
	}

	fatent->page_index = -1;

	/* Is this alignment's page including this entry? */ 
	io_size   = 1L << (FAT_IO_PAGES_BITS + PAGE_SHIFT);

	io_offset = align_offset & (io_size - 1);
	io_index  = align_offset >> (FAT_IO_PAGES_BITS + PAGE_SHIFT);

	if(io_index > ((1 << (sbi->fatent_align_bits - PAGE_SHIFT)) - 1)){
		return -1;
	}
	*list_index = fatent->pages->indexes[io_index];
	if(*list_index < 0 || *list_index > FAT_LIST_NUM - 1){
		return -1;
	}

	*page_offset = io_offset & (PAGE_CACHE_SIZE - 1);
	page_index  = FAT_list[*list_index].index + (io_offset >> PAGE_SHIFT);

	if(page_index < 0 || page_index >= FAT_TOTAL_PAGES){
		printk("page index is bad : %d\n", page_index);
		return -1;
	}

	fatent->page_index = page_index;

	return 0;
} 
/*--------------------*/

/* Panasonic Original */
static void fatent_remove_from_page_cache(struct page *page)
{
	struct address_space *mapping = page->mapping;

	struct zone *zone = NULL;
	unsigned long flags = 0;

	if(atomic_read(&page->_count) < 2){
		return;
	}

	if(!trylock_page(page)){
		printk("Locked!\n");
	}

	if(PageWriteback(page)){
		unlock_page(page);
		printk("PageWriteback! : %08lX  INDEX %lu\n", (unsigned long)page, page->index);
	}

	cancel_dirty_page(page, PAGE_CACHE_SIZE);

	ClearPageUptodate(page);
	ClearPageMappedToDisk(page);

	ClearPagePrivate(page);
	ClearPageReferenced(page);

	page_cache_release(page);

	BUG_ON(!PageLocked(page));

	//ǰ�Τ���
	if(unlikely(PageLRU(page))){
		zone = page_zone(page);

		spin_lock_irqsave(&zone->lru_lock, flags);
		VM_BUG_ON(!PageLRU(page));
		__ClearPageLRU(page);

		list_del(&page->lru);
		if(PageActive(page)){
			__ClearPageActive(page);
			__dec_zone_page_state(page, NR_ACTIVE);
		}
		else{
			__dec_zone_page_state(page, NR_INACTIVE);
		}
		spin_unlock_irqrestore(&zone->lru_lock, flags);
	}

	if(mapping){
		write_lock_irq(&mapping->tree_lock);
		radix_tree_delete(&mapping->page_tree, page->index);
		page->mapping = NULL;
		mapping->nrpages--;
		__dec_zone_page_state(page, NR_FILE_PAGES);
		write_unlock_irq(&mapping->tree_lock);
	}

	unlock_page(page);
}
/*--------------------*/

/* Panasonic Original */
static int fat_ent_readpages(struct super_block *sb, struct list_head *list)
{
	int i, count;
	struct address_space *mapping;
	struct list_head *walk;
	struct fatent_list *fat_list = NULL;
	struct inode *inode = P2FAT_SB(sb)->fat_inode;
	loff_t i_size = i_size_read(inode);
	pgoff_t end_index = i_size >> PAGE_CACHE_SHIFT;
	unsigned offset = i_size & (PAGE_CACHE_SIZE-1);

	LIST_HEAD(page_pool);
	mapping = P2FAT_SB(sb)->fat_inode->i_mapping;

	count = 0;
	list_for_each(walk, list){
		fat_list = list_entry(walk, struct fatent_list, lru);
		for(i = 0; i < FAT_IO_PAGES; i++){

			if (fat_list->page_index + i >= end_index){
				if (fat_list->page_index + i >= end_index + 1 || !offset) {
					break;
				}
			}

			lock_page(FAT_pages[fat_list->page->index + i]);
			FAT_pages[fat_list->page->index + i]->index = fat_list->page_index + i;
/*
			if(PageUptodate(FAT_pages[fat_list->page->index + i]))
			{
				ClearPageUptodate(FAT_pages[fat_list->page->index + i]);
			}
*/
			list_add(&FAT_pages[fat_list->page->index + i]->lru, &page_pool);
			count++;
		}
	}

	p2fat_mpage_readpages(mapping, &page_pool, count, fatent_get_block);

	list_for_each(walk, list){
		fat_list = list_entry(walk, struct fatent_list, lru);
		for(i = 0; i < FAT_IO_PAGES; i++){

			if (fat_list->page_index + i >= end_index){
				if (fat_list->page_index + i >= end_index + 1 || !offset) {
					break;
				}
			}

			if(!PageUptodate(FAT_pages[fat_list->page->index + i])) {
				lock_page(FAT_pages[fat_list->page->index + i]);
				if(!PageUptodate(FAT_pages[fat_list->page->index + i])){
					printk("fat_mpage_readpages failed\n");
					unlock_page(FAT_pages[fat_list->page->index + i]);
					return -EIO;
				}
				unlock_page(FAT_pages[fat_list->page->index + i]);
			}
		}
	}
	return 0;
}
/*--------------------*/

/* Panasonic Original */
static struct fatent_page *__fatent_get_page(struct super_block *sb, int io_index, sector_t align_index)
{
	int i, j, index, tmp;
	int start_index, end_index;
	int list_index;
	struct p2fat_sb_info *sbi = P2FAT_SB(sb);
	struct list_head *walk;
	struct fatent_page_status *fat_list;
	struct list_head list;
	struct fatent_list *lists;

	//writeñ��
	unsigned long io_pages = sbi->fat_pages[align_index].io_pages;

	if(io_index < 0 || io_index > io_pages - 1){
		printk("io_index %d is bad \n", io_index);
		return NULL;
	}

	list_index = sbi->fat_pages[align_index].indexes[io_index];

	//�����ˤ��뤫�����å�
	if(fatent_check_exist(&sbi->fat_pages[align_index], list_index, io_index)){
		//�Ǹ����ʺǿ��ˤ˰�ư
		if(!test_bit(FAT_STATUS_DIRTY, &FAT_list[list_index].status)){
			spin_lock(&clean_fat_lock);
			list_del(&FAT_list[list_index].lru);
			list_add_tail(&FAT_list[list_index].lru, &list_clean_fat);
			spin_unlock(&clean_fat_lock);
		}

		return &sbi->fat_pages[align_index];
	}

	start_index = 0;
	end_index = io_pages;

	//���饤���Ȥ�¾�Υ֥�å��������ˤ��뤫�����å�
	//(ɬ�פʥ֥�å������ܤ����֥�å��������ˤʤ����ϰ����ɤ߹��ि��)
	for(i = 0; i < io_pages; i++){
		tmp = sbi->fat_pages[align_index].indexes[i];

		if(fatent_check_exist(&sbi->fat_pages[align_index], tmp, i)){ //���ä����
			if(i < io_index)
				start_index = i + 1;
			else{
				end_index = i;
				break;
			}
		}
	}

	//�ꥹ�Ȥ����ǿ������
RETRY:
	spin_lock(&clean_fat_lock);
	GET_LIST_COUNT(i, walk, list_clean_fat);

	if(end_index - start_index > i){
	spin_unlock(&clean_fat_lock);
		p2fat_sync_dirtiest_sb(); //���ֱ���Ƥ����ǥ�����p2fat_sync().
		goto RETRY;
	}
	spin_unlock(&clean_fat_lock);

	for(i = start_index; i < end_index; i++){
		spin_lock(&clean_fat_lock);
		list_for_each(walk, &list_clean_fat){
			fat_list = list_entry(walk, struct fatent_page_status, lru);

			mutex_lock(&fat_list->lock);

			if(test_and_set_bit(FAT_STATUS_USED, &fat_list->status)){

				fat_list->fat_page->indexes[fat_list->assign_index] = -1;

				//����ʬ�Υ�꡼������
				for(j = 0; j < FAT_IO_PAGES; j++){
					fatent_remove_from_page_cache(FAT_pages[fat_list->index + j]);
				}
			}
			fat_list->assign_index = i;
			fat_list->fat_page = &sbi->fat_pages[align_index];
			sbi->fat_pages[align_index].indexes[i] = fat_list->index >> FAT_IO_PAGES_BITS;

			fat_list->sb = sb;

			mutex_unlock(&fat_list->lock);

			//�Ǹ����ʺǿ��ˤ˰�ư
			list_del(&fat_list->lru);
			list_add_tail(&fat_list->lru, &list_clean_fat);

			break;
		}
		spin_unlock(&clean_fat_lock);
	}

	lists = kmalloc(io_pages * sizeof(struct fatent_list), GFP_KERNEL);
	if(!lists){
		printk("kmalloc failed!\n");
		return NULL;
	}

	INIT_LIST_HEAD(&list);

	for(i = start_index; i < end_index; i++){
		index = sbi->fat_pages[align_index].indexes[i];

		lists[i].page_index = (align_index << (sbi->fatent_align_bits - PAGE_SHIFT)) + (i << FAT_IO_PAGES_BITS);
		lists[i].page = &FAT_list[index];

		list_add_tail(&lists[i].lru, &list);
	}
	
	fat_ent_readpages(sb, &list);

	kfree(lists);

	return &sbi->fat_pages[align_index];
}
/*--------------------*/

/* Panasonic Original */
static struct fatent_page *fatent_get_page(struct super_block *sb, int align_offset, sector_t align_index)
{
	int io_index = align_offset >> (FAT_IO_PAGES_BITS + PAGE_SHIFT);
	return __fatent_get_page(sb, io_index, align_index);
}
/*--------------------*/

/* Panasonic Original */
static void fatent_remove_from_list(struct super_block *sb)
{
	int i;
	struct list_head *walk, *tmp;
	struct fatent_page_status *stat;

	//���ݺѤߥ��꡼��ꥹ�Ȳ���

	spin_lock(&clean_fat_lock);
	list_for_each_safe(walk, tmp, &list_clean_fat){
		stat = list_entry(walk, struct fatent_page_status, lru);
		if(stat->sb == sb){
			clear_bit(FAT_STATUS_USED, &stat->status);

			for(i = 0; i < FAT_IO_PAGES; i++){
				fatent_remove_from_page_cache(FAT_pages[stat->index + i]);
			}

			stat->sb = NULL;
			list_del(&stat->lru);
			list_add(&stat->lru, &list_clean_fat);
		}
	}
	spin_unlock(&clean_fat_lock);
	spin_lock(&dirty_fat_lock);
	list_for_each_safe(walk, tmp, &list_dirty_fat){
		stat = list_entry(walk, struct fatent_page_status, lru);
		if(stat->sb == sb){
			printk("dirty fat has not been written yet\n");
			clear_bit(FAT_STATUS_USED, &stat->status);
			clear_bit(FAT_STATUS_DIRTY, &stat->status);
			if(P2FAT_SB(sb)->dirty_count == 0)
				printk("dirty count has already been 0.\n");
			else
				P2FAT_SB(sb)->dirty_count--;
			stat->sb = NULL;
			spin_lock(&clean_fat_lock);
			list_del(&stat->lru);
			list_add(&stat->lru, &list_clean_fat);

			for(i = 0; i < FAT_IO_PAGES; i++){
				fatent_remove_from_page_cache(FAT_pages[stat->index + i]);
			}

			spin_unlock(&clean_fat_lock);
		}
	}
	spin_unlock(&dirty_fat_lock);
}
/*--------------------*/

/* Panasonic Original */
static void fatent_mark_page_dirty(struct super_block *sb, struct p2fat_entry *fatent)
{
	int i, index, need_read = 0;
	unsigned long io_pages = fatent->pages->io_pages;

	mutex_lock(&get_page_lock);

	//�����ʬ����˥����ƥ������ꤷ�Ƥ���(�ʤ�ʬ���ɤ߹���Ȥ����ɤ��Ф���ʤ��褦�ˤ��뤿��)
	for(i = 0; i < io_pages; i++){
		index = fatent->pages->indexes[i];
		if(index < 0 || index > FAT_LIST_NUM - 1){
			//�ɤ߹��߽����ɲ�
			need_read = 1;
		}
		else{
			if(test_bit(FAT_STATUS_DIRTY, &FAT_list[index].status)){
				break; //��ĤǤ�����ƥ��ʾ��Ϥ��٤ƥ����ƥ��ΤϤ�
			}
			else{
				if(fatent_set_dirty(index) < 0){
					printk("SET DIRTY(%d) FAILED\n", index);
				}
			}
		}
	}

	if(!need_read){
		mutex_unlock(&get_page_lock);
		return;
	}

	//�ʤ�ʬ���ɤ߹���
	for(i = 0; i < io_pages; i++){
		index = fatent->pages->indexes[i];
		if(index < 0 || index > FAT_LIST_NUM - 1){
			//�ɤ߹���
			if(!__fatent_get_page(sb, i, fatent->pages->align_index)){
				printk("GET PAGE FAILED\n");
			}

			index = fatent->pages->indexes[i];
		}

		if(!test_bit(FAT_STATUS_DIRTY, &FAT_list[index].status)){
			if(fatent_set_dirty(index) < 0){
				printk("SET DIRTY(%d) FAILED\n", index);
			}
		}
	}
	mutex_unlock(&get_page_lock);
}
/*--------------------*/

//FAT��������δؿ�
struct fatent_operations {
	void (*ent_set_ptr)(struct p2fat_entry *, int);
	int (*ent_get)(struct p2fat_entry *);
	void (*ent_put)(struct p2fat_entry *, int);
	int (*ent_next)(struct p2fat_entry *);
};

/* Panasonic Original */
static void fat_ent_blocknr(struct super_block *sb, int entry,
			    int *offset, sector_t *blocknr)
{
	struct p2fat_sb_info *sbi = P2FAT_SB(sb);
	int bytes = (entry << sbi->fatent_shift); //sbi->fatent_shift�� 2(16bit) or 4(32bit)
	unsigned long fatent_align = (1L << sbi->fatent_align_bits);
	WARN_ON(entry < FAT_START_ENT || sbi->max_cluster <= entry);
	*offset = bytes & (fatent_align - 1);
	*blocknr = bytes >> sbi->fatent_align_bits;
}
/*--------------------*/

/* Panasonic Original */
static void fat16_ent_set_ptr(struct p2fat_entry *fatent, int offset)
{
	WARN_ON(offset & (2 - 1));
	fatent->u.ent16_p = (__le16 *)(page_address(FAT_pages[fatent->page_index]) + offset);
}
/*--------------------*/

/* Panasonic Original */
static void fat32_ent_set_ptr(struct p2fat_entry *fatent, int offset)
{
	WARN_ON(offset & (4 - 1));
	fatent->u.ent32_p = (__le32 *)(page_address(FAT_pages[fatent->page_index]) + offset);
}
/*--------------------*/

/* Panasonic Original */
static int fat_ent_update_ptr(struct super_block *sb,
				     struct p2fat_entry *fatent,
				     int align_offset, sector_t align_index)
{
	struct p2fat_sb_info *sbi = P2FAT_SB(sb);
	struct fatent_operations *ops = sbi->fatent_ops;
	int page_offset;
	int list_index;

	/* Is this fatent's aligns including this entry? */
	if (!fatent->nr_bhs || !fatent->pages){
		return 0;
	}

	if(fatent->pages->align_index != (unsigned int)align_index){
		return 0;
	}

	mutex_lock(&get_page_lock);

	//���饤�����ֹ�ȥ��ե��åȤ���ڡ����ֹ�ȥ��ե��åȤ����
	if(fat_ent_pagenr(sb, fatent, align_offset, align_index, &page_offset, &list_index)){
		mutex_unlock(&get_page_lock);
		return 0;	
	}

	fatent->list_index = list_index;

	fat_list_lock(list_index);

	mutex_unlock(&get_page_lock);

	ops->ent_set_ptr(fatent, page_offset); //�ݥ�������

	return 1;
}
/*--------------------*/

/* Panasonic Original */
static int fat_ent_bread(struct super_block *sb, struct p2fat_entry *fatent,
			 int align_offset, sector_t align_index)
{
	struct p2fat_sb_info *sbi = P2FAT_SB(sb);
	struct fatent_operations *ops = sbi->fatent_ops;
	int page_offset;
	int list_index;

	mutex_lock(&get_page_lock);

	fatent->pages = fatent_get_page(sb, align_offset, align_index); //���ꥻ�����ΥХåե��إåɤ򥲥å�
	if (!fatent->pages) {
		printk(KERN_ERR "FAT: FAT read failed (blocknr %llu)\n",
		       (unsigned long long)align_index);
		mutex_unlock(&get_page_lock);
		return -EIO;
	}

	fatent->nr_bhs = 1; //FAT16, FAT32�Ǥϰ㤦���饤���Ȥ����֤���뤳�ȤϤʤ���

	if(fat_ent_pagenr(sb, fatent, align_offset, align_index, &page_offset, &list_index)){
		printk("##### fat_ent_pagenr ERROR\n");
		mutex_unlock(&get_page_lock);
		return -EIO;
	}

	fatent->list_index = list_index;

	fat_list_lock(list_index);

	mutex_unlock(&get_page_lock);

	ops->ent_set_ptr(fatent, page_offset); //���ꥪ�ե��åȤإݥ��󥿤򥻥å�

	return 0;
}
/*--------------------*/

//FAT�ơ��֥����(���Υ��饹���ֹ�)���������(FAT16��)
// fatent  : FAT����
//
// �����  : ���Υ��饹���ֹ� or EOF(��ü)
//
// ���ȸ�  : p2fat_ent_read(), p2fat_alloc_clusters(), p2fat_count_free_clusters() //���٤�fatent.c��
static int fat16_ent_get(struct p2fat_entry *fatent)
{
	int next = le16_to_cpu(*fatent->u.ent16_p);
	WARN_ON((unsigned long)fatent->u.ent16_p & (2 - 1));
	if (next >= BAD_FAT16)
		next = FAT_ENT_EOF;
	return next;
}

//FAT�ơ��֥����(���Υ��饹���ֹ�)���������(FAT32��)
// fatent  : FAT����
//
// �����  : ���Υ��饹���ֹ� or EOF(��ü)
//
// ���ȸ�  : p2fat_ent_read(), p2fat_alloc_clusters(), p2fat_count_free_clusters() //���٤�fatent.c��
static int fat32_ent_get(struct p2fat_entry *fatent)
{
	int next = le32_to_cpu(*fatent->u.ent32_p) & 0x0fffffff;
	WARN_ON((unsigned long)fatent->u.ent32_p & (4 - 1));
	if (next >= BAD_FAT32)
		next = FAT_ENT_EOF;
	return next;
}

/* Panasonic Original */
static void fat16_ent_put(struct p2fat_entry *fatent, int new)
{
	if (new == FAT_ENT_EOF)
		new = EOF_FAT16;

	*fatent->u.ent16_p = cpu_to_le16(new);
}
/*--------------------*/

/* Panasonic Original */
static void fat32_ent_put(struct p2fat_entry *fatent, int new)
{
	if (new == FAT_ENT_EOF)
		new = EOF_FAT32;

	WARN_ON(new & 0xf0000000);
	new |= le32_to_cpu(*fatent->u.ent32_p) & ~0x0fffffff;
	*fatent->u.ent32_p = cpu_to_le32(new);
}
/*--------------------*/

/* Panasonic Original */
static int fat16_ent_next(struct p2fat_entry *fatent)
{
	fatent->entry++;
	if(fatent->u.ent16_p < (__le16 *)(page_address(FAT_pages[fatent->page_index]) + (PAGE_CACHE_SIZE - 2))){
		fatent->u.ent16_p++;
		return 1;
	}
	fatent->u.ent16_p = NULL;
	return 0;
}
/*--------------------*/

/* Panasonic Original */
static int fat32_ent_next(struct p2fat_entry *fatent)
{
	fatent->entry++;
	if(fatent->u.ent32_p < (__le32 *)(page_address(FAT_pages[fatent->page_index]) + (PAGE_CACHE_SIZE - 4))){
		fatent->u.ent32_p++;
		return 1;
	}
	fatent->u.ent32_p = NULL; //���Υǡ������Хåե��إåɳ��ΤȤ���NULL�ˤ���
	return 0;
}
/*--------------------*/

/* Panasonic Original */
static struct fatent_operations fat16_ops = {
	.ent_set_ptr		= fat16_ent_set_ptr,
	.ent_get		= fat16_ent_get,
	.ent_put		= fat16_ent_put,
	.ent_next		= fat16_ent_next,
};
/*--------------------*/

/* Panasonic Original */
static struct fatent_operations fat32_ops = {
	.ent_set_ptr		= fat32_ent_set_ptr,
	.ent_get		= fat32_ent_get,
	.ent_put		= fat32_ent_put,
	.ent_next		= fat32_ent_next,
};
/*--------------------*/

/* Panasonic Original */
static void fat_ent_put(struct super_block *sb, struct p2fat_entry *fatent, int new)
{
	struct p2fat_sb_info *sbi = P2FAT_SB(sb);
	struct fatent_operations *ops = sbi->fatent_ops;

	ops->ent_put(fatent, new);
	fatent_mark_page_dirty(sb, fatent);
}
/*--------------------*/

/* Panasonic Original */
static int fatent_build_fat_inode(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	struct p2fat_sb_info *sbi = P2FAT_SB(sb);

	P2FAT_I(inode)->i_pos = 0;
	inode->i_uid = sbi->options.fs_uid;
	inode->i_gid = sbi->options.fs_gid;
	inode->i_version++;
	inode->i_generation = 0;
	inode->i_mode = (S_IRWXUGO & ~sbi->options.fs_dmask) | S_IFREG;
	inode->i_op = NULL;
	inode->i_fop = NULL;
	inode->i_mapping->a_ops = &fat_fatent_aops;

	P2FAT_I(inode)->i_start = 0;
	inode->i_size = sbi->fat_length << sb->s_blocksize_bits;
	if(sbi->fat_bits == 16){
		inode->i_size *= 2;
	}

	inode->i_blocks = ((inode->i_size + (sb->s_blocksize - 1))
			   & ~((loff_t)sb->s_blocksize - 1)) >> sb->s_blocksize_bits;
	P2FAT_I(inode)->i_logstart = 0;
	P2FAT_I(inode)->mmu_private = inode->i_size;

	P2FAT_I(inode)->i_attrs = ATTR_NONE;
	inode->i_mtime.tv_sec = inode->i_atime.tv_sec = inode->i_ctime.tv_sec = 0;
	inode->i_mtime.tv_nsec = inode->i_atime.tv_nsec = inode->i_ctime.tv_nsec = 0;
	inode->i_nlink = 0;

	return 0;
}
/*--------------------*/

/* Panasonic Original */
static int fatent_fat_page_init(struct super_block *sb)
{
	int i;
	int error = -ENOMEM;
	struct p2fat_sb_info *sbi = P2FAT_SB(sb);
 
	unsigned long fatent_align = 1L << sbi->fatent_align_bits;
	unsigned long io_pages = 1L << (sbi->fatent_align_bits - PAGE_SHIFT - FAT_IO_PAGES_BITS);

	sbi->dirty_count = 0;

	if(sbi->fat_bits == 32){
		sbi->fat_pages_num = ((sbi->fat_length << sb->s_blocksize_bits)
			+ fatent_align - 1) >> sbi->fatent_align_bits;
	}
	else{
		sbi->fat_pages_num = (((2*sbi->fat_length) << sb->s_blocksize_bits)
			+ fatent_align - 1) >> sbi->fatent_align_bits;
	}

	sbi->fat_pages = kmalloc(sbi->fat_pages_num * sizeof(struct fatent_page), GFP_KERNEL);
	if(!sbi->fat_pages){
		printk("kmalloc failed!\n");
		goto fat_failed;
	}

	memset(sbi->fat_pages, 0, sbi->fat_pages_num * sizeof(struct fatent_page));

	for(i = 0; i < sbi->fat_pages_num; i++){
		sbi->fat_pages[i].align_index = i;
		sbi->fat_pages[i].io_pages = io_pages;
		sbi->fat_pages[i].indexes = kmalloc(io_pages * sizeof(int), GFP_KERNEL);
		if(!sbi->fat_pages[i].indexes){
			printk("kmalloc failed!\n");
			goto fat_failed;
		}

		//���٤�-1������
		memset(sbi->fat_pages[i].indexes, ~0, io_pages * sizeof(int));
	}

	//FAT��inode����
	sbi->fat_inode = new_inode(sb);
	if (!sbi->fat_inode)
		goto inode_failed;

	sbi->fat_inode->i_ino = P2FAT_FAT_INO;
	sbi->fat_inode->i_version = 1;
	error = fatent_build_fat_inode(sbi->fat_inode);
	if (error < 0)
		goto inode_failed;

	//FAT��inode��Ͽ
	insert_inode_hash(sbi->fat_inode);

	return 0;

inode_failed:
	if(sbi->fat_inode){
		iput(sbi->fat_inode);
		sbi->fat_inode = NULL;
	}

fat_failed:
	if(sbi->fat_pages){
		for(i = 0; i < sbi->fat_pages_num; i++){
			if(sbi->fat_pages[i].indexes){
				kfree(sbi->fat_pages[i].indexes);
				sbi->fat_pages[i].indexes = NULL;
			}
		}
		kfree(sbi->fat_pages);
		sbi->fat_pages = NULL;
	}

	return error;
}

// fatent.c����ѿ����ν����
// sb      : �����ѡ��֥�å�
//
// ���ȸ�  : fat_fill_super() / inode.c
/* Panasonic Change */
int/*void*/ p2fat_ent_access_init(struct super_block *sb)
/*------------------*/
{
	struct p2fat_sb_info *sbi = P2FAT_SB(sb);

	mutex_init(&sbi->fat_lock);

	/* Panasonic Original */
	INIT_LIST_HEAD(&sbi->reserved_list); //���ͽ�󤵤줿���饹���ֹ�Υꥹ�Ƚ����

	mutex_init(&sbi->rt_inode_dirty_lock);
	INIT_LIST_HEAD(&sbi->rt_inode_dirty_list);

	sbi->fat_pages = NULL;
	sbi->fat_inode = NULL;

	sbi->cont_space.n = 0;
	sbi->cont_space.prev_free = 0;
	sbi->cont_space.cont = 0;
	sbi->cont_space.pos = 0;
	sbi->sync_flag = 0;
	/*--------------------*/

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
		/* Panasonic Change */
		printk("P2 FileSystem cannot be FAT12.\n");
		return -EINVAL;
		/*------------------*/
	}

	/* Panasonic Original */
	return fatent_fat_page_init(sb);
	/*--------------------*/
}

//���ꤷ�����饹����FAT�ơ��֥����(���Υ��饹���ֹ�)���������
// inode   : �����Ρ���
// fatent  : FAT����
// entry   : ���饹���ֹ�
//
// �����  : ���� ���Υ��饹���ֹ� or EOF(��ü)   ���� -EIO
//
// ���ȸ�  : p2fat_free_clusters() //fatent.c
//           fat_get_cluster()   //cache.c
//           fat_free()          //file.c
//           fat_chain_add()     //misc.c
int __p2fat_ent_read(struct super_block *sb, struct p2fat_entry *fatent, int entry)
{
	struct p2fat_sb_info *sbi = P2FAT_SB(sb);
	struct fatent_operations *ops = sbi->fatent_ops;
	int err, offset;
	sector_t blocknr;

	if (entry < FAT_START_ENT || sbi->max_cluster <= entry) { //���饹���ֹ�����
		p2fatent_brelse(fatent);
		p2fat_fs_panic(sb, "invalid access to FAT (entry 0x%08x)", entry);
		return -EIO;
	}

	p2fatent_set_entry(fatent, entry); //fatent->entry = entry; fatent->u.ent32_p = NULL;��2�����Ԥ�
	fat_ent_blocknr(sb, entry, &offset, &blocknr); //�������ֹ�ȥ��ե��åȤ����(���饤�����ֹ�ȥ��ե��åȤ����)

	//�Хåե��إå��Υݥ��󥿤򥻥å�
	if (!fat_ent_update_ptr(sb, fatent, offset, blocknr)) { //����(�Хåե��إåɤ����ꤵ��Ƥ��ʤ�)
		p2fatent_brelse(fatent); //�Хåե��إåɤβ���
		err = fat_ent_bread(sb, fatent, offset, blocknr); //�ɤ߹���
		if (err)
			return err;
	}

	fat_list_unlock(fatent->list_index);

	return ops->ent_get(fatent); //FAT�ơ��֥���ͤ����
}

int p2fat_ent_read(struct inode *inode, struct p2fat_entry *fatent, int entry)
{
	struct super_block *sb = inode->i_sb;
	return __p2fat_ent_read(sb, fatent, entry);
}

//FAT�ơ��֥���ͤ��ѹ�����
// inode   : �����Ρ���
// fatent  : FAT����
// new     : ��������
// wait    : �����˹������뤫�ݤ�  0 �������ʤ�  0�ʳ� ��������
// 
// �����  : ���� 0    ���� -EIO -ENOMEM -EOPNOTSUPP
//
// ���ȸ�  : fat_free()       // file.c
//           fat_chain_add()  //misc.c
int __p2fat_ent_write(struct super_block *sb, struct p2fat_entry *fatent,
		      int new, int wait)
{
	int err = 0;

	fat_ent_put(sb, fatent, new); //FAT�ơ��֥���ͤ��ѹ�����(�Хåե��إåɤ����Ƥ��ѹ�)
	if(wait) { //�����˹���������(P2FAT�Ǥ�̤�б�)
//		printk("NEED SYNC\n");
//		err = fat_sync_bhs(fatent->bhs, fatent->nr_bhs); //����(misc.c)
//		if (err)
//			return err;
	}
	return err;
}

int p2fat_ent_write(struct inode *inode, struct p2fat_entry *fatent,
		  int new, int wait)
{
	struct super_block *sb = inode->i_sb;
	return __p2fat_ent_write(sb, fatent, new, wait);
}

//�Хåե��إåɤؤΥݥ��󥿤򼡤˿ʤ��(fat��_ent_next�Υ�åѡ�)
// sbi     : �����ѡ��֥�å�����
// fatent  : FAT����
// 
// �����  : ���� 1   �ʤ��ä���� 0
//
// ���ȸ�  : p2fat_alloc_clusters(), p2fat_count_free_clusters() //���٤�fatent.c��
static inline int fat_ent_next(struct p2fat_sb_info *sbi,
			       struct p2fat_entry *fatent)
{
	if (sbi->fatent_ops->ent_next(fatent)) {
		if (fatent->entry <= sbi->max_cluster)
			return 1;
	}
	return 0;
}

static inline int fat_ent_read_block(struct super_block *sb,
				     struct p2fat_entry *fatent)
{
	sector_t blocknr;
	int offset;
	int ret;

	p2fatent_brelse(fatent);
	fat_ent_blocknr(sb, fatent->entry, &offset, &blocknr);
	ret = fat_ent_bread(sb, fatent, offset, blocknr);

	fat_list_unlock(fatent->list_index);

	return ret;
}

/* Panasonic Experiment */
int p2fat_check_cont_space(struct super_block *sb, int cluster)
{
	struct p2fat_sb_info *sbi = P2FAT_SB(sb);
	struct fatent_operations *ops = sbi->fatent_ops;
	struct p2fat_entry fatent;
	int i, pos, ret = 0;

	//���������Ϥ��ߤ������å�
	if(!sbi->cont_space.n){
		return 0;
	}

	pos = cluster - FAT_START_ENT - sbi->data_cluster_offset;
	if(pos < 0){
		return 0;
	}

	pos = pos / sbi->cont_space.n;
	if(pos >= sbi->cont_space.pos){
		return 0;
	}

	p2fatent_init(&fatent);
	p2fatent_set_entry(&fatent, pos * sbi->cont_space.n + FAT_START_ENT + sbi->data_cluster_offset);

	//�ɤ߹���
	p2fatent_set_entry(&fatent, fatent.entry);
	if(fat_ent_read_block(sb, &fatent)){
		printk("fat_ent_read_block error\n");
		goto out;
	}

	for(i = 0; i < sbi->cont_space.n; i++){
		if (ops->ent_get(&fatent) != FAT_ENT_FREE) { //�����Ƥ��ʤ����
			goto out;
		}

		if(!fat_ent_next(sbi, &fatent)){
			if(fatent.entry == sbi->max_cluster)
				break;

			if(fatent.entry > sbi->max_cluster)
				goto out;

			if(fat_ent_read_block(sb, &fatent)){
				printk("fat_ent_read_block error\n");
				goto out;
			}
		}
	}
	ret = 1;

out:
	p2fatent_brelse(&fatent);

	return ret;
}
/*----------------------*/

/* Panasonic Original */
static int p2fat_fast_check_cont_space(struct super_block *sb, int cluster, int pre_entry, int *offset, int *au_num, int eof)
{
	struct p2fat_sb_info *sbi = P2FAT_SB(sb);
	int pos, ret = 0;
	int pre_off, cur_off;
	int pre_au, cur_au;

	//���������Ϥ��ߤ������å�
	if(!sbi->cont_space.n){
		return 0;
	}

	pos = cluster - FAT_START_ENT - sbi->data_cluster_offset;
	if(pos < 0){
		return 0;
	}

	pre_off = *offset; //�����AU��Υ��饹���ֹ�
	pre_au = *au_num;  //�����AU�ֹ�

	cur_off = pos % sbi->cont_space.n; //�����AU��Υ��饹���ֹ�
	cur_au = pos / sbi->cont_space.n;  //�����AU�ֹ�

	//�����ʬ��Ф��Ƥ���
	*offset = cur_off;
	*au_num = cur_au;

	//�����Ʊ��AU
	if(cur_au == pre_au){
		//����μ��Υ��饹��
		if((pre_off >= 0) && (pre_off + 1 == cur_off)){
			//AU��κǽ����饹��
			if(cur_off + 1 == sbi->cont_space.n){
				*offset = -1;
				*au_num = -1;
				if(cur_au < sbi->cont_space.pos){ //�������Ѥ�
					//������ȥ��å�
					ret++;
				}
			}
			//AU��ν�ü�Ǥʤ����饹��
			else{
				if(eof){ //�ե����뽪ü�ξ��ϥ����å�
					if(cur_au < sbi->cont_space.pos){ //�������Ѥ�
						ret += p2fat_check_cont_space(sb, cluster);
					}
				}
			}
		}
		//����Ȥ���Ϣ³�Υ��饹��
		else{
			*offset = -1;
			if(eof){ //�ե����뽪ü�ξ��ϥ����å�
				if(cur_au < sbi->cont_space.pos){ //�������Ѥ�
					ret += p2fat_check_cont_space(sb, cluster);
				}
			}
		}
	}
	//����Ȱ㤦AU
	else{
		if(cur_off != 0){
			*offset = -1;
		}

		//����ʬ��AU������Ȥ��Ϥ����ΰ�򥵡���
		if(pre_au != -1){
			if(pre_au >= sbi->cont_space.pos){ //̤������
				if(eof){ //�ե����뽪ü�ξ��ϥ����å�
					if(cur_au < sbi->cont_space.pos){ //�������Ѥ�
						ret += p2fat_check_cont_space(sb, cluster);
					}
				}
			}
			else{
				if(p2fat_check_cont_space(sb, pre_entry))
					ret++;
				if(eof){ //�ե����뽪ü�ξ��ϥ����å�
					if(cur_au < sbi->cont_space.pos){ //�������Ѥ�
						if(p2fat_check_cont_space(sb, cluster))
							ret++;
					}
				}
			}
		}
		else{
			if(eof){ //�ե����뽪ü�ξ��ϥ����å�
				if(cur_au < sbi->cont_space.pos){ //�������Ѥ�
					if(p2fat_check_cont_space(sb, cluster))
						ret++;
				}
			}
		}
	}

	return ret;
}
/*----------------------*/

/* Panasonic Original */
int p2fat_alloc_cont_clusters(struct super_block *sb, int nr_cluster)
{
	struct p2fat_sb_info *sbi = P2FAT_SB(sb);
	struct fatent_operations *ops = sbi->fatent_ops;
	struct p2fat_entry fatent;
	unsigned long limit;
	int i, count, ret, idx_clus, header;

	lock_fat(sbi);

	//�������饹�������å�
/* //FS Info��Free Cluster���󤬴ְ�äƤ����礬���뤿������å����ʤ��褦���ѹ� (2010.09.09)
	if (sbi->free_clusters != -1 && sbi->free_clusters < nr_cluster) {
		unlock_fat(sbi);
		return -ENOSPC;
	}
*/

	//�ݤ�
	sbi->cont_space.prev_free = (sbi->cont_space.prev_free / nr_cluster) * nr_cluster;

	ret = idx_clus = header = 0;
	count = FAT_START_ENT;
	limit = sbi->max_cluster - sbi->data_cluster_offset;

	p2fatent_init(&fatent);
	p2fatent_set_entry(&fatent, sbi->cont_space.prev_free + FAT_START_ENT + sbi->data_cluster_offset);

	while (count < limit) {
		//����ζ����μ��Υ��饹���������饹�����ʾ�ξ�����Ƭ�����
		if (fatent.entry >= sbi->max_cluster)
			fatent.entry = FAT_START_ENT + sbi->data_cluster_offset;

		//�ɤ߹���
		p2fatent_set_entry(&fatent, fatent.entry);
		ret = fat_ent_read_block(sb, &fatent);
		if (ret){
			printk("fat_ent_read_block(%d) error %08X\n", __LINE__, ret);
			goto END;
		}

		/* Find the free entries in a block */
		while(1) {
			int entry = fatent.entry;
			if (ops->ent_get(&fatent) == FAT_ENT_FREE) { //�����Ƥ�����
				if(idx_clus == 0)
					header = entry; //��Ƭ���饹���ֹ��Ф��Ƥ���
				idx_clus++;
				if (idx_clus == nr_cluster)
					goto END;

				count++;
				if (count == limit)
					break;

				if(!fat_ent_next(sbi, &fatent))
					break;
			}
			else{ //�����Ƥ��ʤ��ä����
				count += (nr_cluster - idx_clus);
				entry += (nr_cluster - idx_clus);
				idx_clus = 0;
				p2fatent_set_entry(&fatent, entry);
				break;
			}
		}
	}

	/* Couldn't allocate the free entries */
	ret = -ENOSPC;

END:
	if (!ret) {
		sbi->cont_space.prev_free = header + nr_cluster - FAT_START_ENT - sbi->data_cluster_offset;
		p2fatent_set_entry(&fatent, header);
		ret = fat_ent_read_block(sb, &fatent);
		if (ret){
			printk("fat_ent_read_block(%d) error %08X\n", __LINE__, ret);
			goto OUT;
		}

		//�������ڡ����򸺤餹
		if(sbi->cont_space.cont != 0){
			if(sbi->cont_space.n != nr_cluster){
				printk("WARN : Search : %d != Get : %d clusters\n", sbi->cont_space.n, nr_cluster);
				sbi->cont_space.pos = 0;
				sbi->cont_space.cont = 0;
			}
			if(sbi->cont_space.pos * sbi->cont_space.n >= sbi->cont_space.prev_free)
				sbi->cont_space.cont--;
		}

		for(i = 0; i < nr_cluster; i++){
			fat_ent_put(sb, &fatent, FAT_ENT_EOF);  //���߰��֤�FAT��EOF�ͤ�����

			if (sbi->free_clusters != -1){
				sbi->free_clusters--;  //�������Ĥؤ餹

				if(sbi->show_inval_log){
					if(sbi->cont_space.cont * sbi->cont_space.n > sbi->free_clusters){
						printk("[%s:%d] Cont space(%lu) is bigger than fs info(%u)\n", 
							__FILE__, __LINE__, sbi->cont_space.cont * sbi->cont_space.n, sbi->free_clusters);
						sbi->show_inval_log = 0;
					}
				}
			}
			sb->s_dirt = 1;

			if(i < nr_cluster - 1){
				if(!fat_ent_next(sbi, &fatent)){
					if(fatent.entry >= sbi->max_cluster){
						printk("invalid access to FAT(%d) (entry 0x%08x)", __LINE__, fatent.entry);
						ret = -EINVAL;
						goto OUT;
					}
					ret = fat_ent_read_block(sb, &fatent); //FAT������ɤ߹���
					if (ret){
						printk("fat_ent_read_block(%d) error %08X\n", __LINE__, ret);
						goto OUT;
					}
				}
			}
		}
		ret = header;
	}

OUT:
	unlock_fat(sbi);
	p2fatent_brelse(&fatent);

	return ret;
}
/*--------------------*/

//�������饹�������ĳ��ݤ���
// inode      : �����Ρ���
// clusters   : ���ݤ������饹���ֹ���Ǽ��������
// nr_cluster : ���ݤ��륯�饹����
//
// �����  : ���� 0  ���� -ENOSPC -EIO -ENOMEM -EOPNOTSUPP
//
// ���ȸ�  : fat_alloc_new_dir   //dir.c
//           fat_add_new_entries //dir.c
//           fat_add_cluster     //inode.c
int p2fat_alloc_clusters(struct inode *inode, int *cluster, int nr_cluster)
{
	struct super_block *sb = inode->i_sb;
	struct p2fat_sb_info *sbi = P2FAT_SB(sb);
	struct fatent_operations *ops = sbi->fatent_ops;
	struct p2fat_entry fatent, prev_ent;
	int count, err, nr_bhs, idx_clus;

	lock_fat(sbi);

	//�������饹�������å�
/* //FS Info��Free Cluster���󤬴ְ�äƤ����礬���뤿������å����ʤ��褦���ѹ� (2010.09.09)
	if (sbi->free_clusters != -1 && sbi->free_clusters < nr_cluster) {
		unlock_fat(sbi);
		return -ENOSPC;
	}
*/

	err = nr_bhs = idx_clus = 0;
	count = FAT_START_ENT;
	p2fatent_init(&prev_ent);
	p2fatent_init(&fatent);
	p2fatent_set_entry(&fatent, sbi->prev_free);
	while (count < sbi->max_cluster) {
		//����ζ����μ��Υ��饹�������饹�����ʾ�ξ�����Ƭ�����
		if (fatent.entry >= sbi->max_cluster)
			fatent.entry = FAT_START_ENT;

		//�ɤ߹���
		p2fatent_set_entry(&fatent, fatent.entry);
		err = fat_ent_read_block(sb, &fatent);
		if (err){
			printk("fat_ent_read_block(%d) error %08X\n", __LINE__, err);
			goto out;
		}

		/* Find the free entries in a block */
		do {
			if (fatent.entry >= sbi->max_cluster){
				break;
			}

			if (ops->ent_get(&fatent) == FAT_ENT_FREE) { //�����Ƥ�����
				int entry = fatent.entry;

				if(p2fat_check_cont_space(sb, entry))
					sbi->cont_space.cont--;

				/* make the cluster chain */
				fat_ent_put(sb, &fatent, FAT_ENT_EOF);  //���߰��֤�FAT��EOF�ͤ�����
				if (prev_ent.nr_bhs)
					fat_ent_put(sb, &prev_ent, entry);//���Υ��饹���˸��߰��֤Υ��饹���ֹ������

				sbi->prev_free = entry;
				if (sbi->free_clusters != -1){
					sbi->free_clusters--;  //�������Ĥؤ餹

					if(sbi->show_inval_log){
						if(sbi->cont_space.cont * sbi->cont_space.n > sbi->free_clusters){
							printk("[%s:%d] Cont space(%lu) is bigger than fs info(%u)\n", 
								__FILE__, __LINE__, sbi->cont_space.cont * sbi->cont_space.n, sbi->free_clusters);
						}
						sbi->show_inval_log = 0;
					}
				}
				sb->s_dirt = 1;

				cluster[idx_clus] = entry; //���ݤ������饹���ֹ��Ф��Ƥ���
				idx_clus++;
				if (idx_clus == nr_cluster)
					goto out;

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
	p2fatent_brelse(&fatent);

	if (!err) {
		if (inode_needs_sync(inode)){
//			err = fat_sync_bhs(bhs, nr_bhs);
		}
	}

	if (err && idx_clus) //���饹�����ĤǤ���ݤ��ƥ��顼�ξ��ϲ�������
		p2fat_free_clusters(inode, cluster[0]);
	else
		p2fat_clusters_flush(sb, 0);
	
	return err;
}

//���ꥯ�饹�������FAT��������򤹤٤ƶ����ˤ���
// inode   : �����Ρ���
// cluster : ���饹���ֹ�
//
// �����  : ���� 0  ���� -EIO -ENOMEM -EOPNOTSUPP
int p2fat_free_clusters(struct inode *inode, int cluster)
{
	struct super_block *sb = inode->i_sb;
	struct p2fat_sb_info *sbi = P2FAT_SB(sb);
	struct p2fat_entry fatent;
	int err = 0;
	int old_cluster;

	int pre_entry = -1;
	int offset = -1, au_num = -1;

	p2fatent_init(&fatent); //FAT���������ʪ�ν����
	lock_fat(sbi);
	do {
		//FAT�ơ��֥��ͤ��ɤ߹���
		old_cluster = cluster;
		cluster = p2fat_ent_read(inode, &fatent, cluster);
		if (cluster < 0) { //�ɤ߹��߼���
			err = cluster;
			printk(" p2fat_ent_read failed : clust %d err %08X\n",
				old_cluster, err);
			goto error;
		} else if (cluster == FAT_ENT_FREE) { //���Ǥ˶����Ƥ������
			p2fat_fs_panic(sb, "%s: deleting FAT entry beyond EOF",
				     __FUNCTION__);
			err = -EIO;
			goto error;
		}

		if(test_bit(FAT_RM_RESERVED, &P2FAT_I(inode)->i_flags)){
			if(p2fat_reserve_fat_free(inode, old_cluster) < 0){
				printk("p2fat_reserve_fat_free(%d) error\n", __LINE__);
			}
		}
		else{
			//����������
			fat_ent_put(sb, &fatent, FAT_ENT_FREE);

			if (sbi->free_clusters != -1) {
				sbi->free_clusters++; //�����������䤹
				sb->s_dirt = 1;
			}

			sbi->cont_space.cont += p2fat_fast_check_cont_space(
				sb, fatent.entry, pre_entry, &offset, &au_num, cluster == FAT_ENT_EOF);

			pre_entry = fatent.entry;
		}

	} while (cluster != FAT_ENT_EOF); //�ե����뽪ü�ޤǷ����֤�

	if (sb->s_flags & MS_SYNCHRONOUS) { //SYNC�ե饰��������
//		err = fat_sync_bhs(bhs, nr_bhs); //sync����
//		if (err)
//			goto error;
	}
error:
	p2fatent_brelse(&fatent);
	unlock_fat(sbi);

	//FS INFO�򹹿������FAT32�ʳ��ϲ��⤷�ʤ���
	p2fat_clusters_flush(sb, 1);

	return err;
}

EXPORT_SYMBOL_GPL(p2fat_free_clusters);

/* Panasonic Experiment */
int p2fat_reserve_fat_free(struct inode *inode, int nr_cluster)
{
	int ret;
	struct super_block *sb = inode->i_sb;
	struct p2fat_sb_info *sbi = P2FAT_SB(sb);
	struct fatent_reserve_fat *fat_reserved;
	struct p2fat_entry fatent;

	p2fatent_init(&fatent); //FAT���������ʪ�ν����
	//FAT�ơ��֥��ͤ��ɤ߹���
	ret = p2fat_ent_read(inode, &fatent, nr_cluster);
	p2fatent_brelse(&fatent);

	fat_reserved = (struct fatent_reserve_fat *)kmalloc(sizeof(struct fatent_reserve_fat), GFP_KERNEL);
	if(!fat_reserved){
		printk("[%s-%d] kmalloc failed!\n",__FUNCTION__,__LINE__);
		return -ENOMEM;
	}

	fat_reserved->nr_cluster = nr_cluster;

	INIT_LIST_HEAD(&fat_reserved->lru);
	list_add_tail(&fat_reserved->lru, &sbi->reserved_list);

	return ret;
}
/*----------------------*/

/* Panasonic Original */
static int __p2fat_apply_reserved_fat(struct super_block *sb, int lock)
{
	struct p2fat_sb_info *sbi = P2FAT_SB(sb);
	struct list_head *walk, *tmp;
	struct fatent_reserve_fat *fat_reserved = NULL;
	struct fatent_operations *ops = sbi->fatent_ops;
	struct p2fat_entry fatent;
	int err = 0;
	int dirty = 0;

	p2fatent_init(&fatent); //FAT����ν����

	if(lock)
		lock_fat(sbi);

	list_for_each_safe(walk, tmp, &sbi->reserved_list){

		fat_reserved = list_entry(walk, struct fatent_reserve_fat, lru);

		//FAT�ơ��֥��ͤ��ɤ߹���
		p2fatent_set_entry(&fatent, fat_reserved->nr_cluster);
		err = fat_ent_read_block(sb, &fatent); //FAT������ɤ߹���
		if (err){
			printk("fat_ent_read_block(%d) error %08X\n", __LINE__, err);
			goto error;
		}

		if (ops->ent_get(&fatent) == FAT_ENT_FREE) { //���Ǥ˶����Ƥ������
			p2fat_fs_panic(sb, "%s: deleting FAT entry beyond EOF",
				     __FUNCTION__);
			err = -EIO;
			goto error;
		}

		//����������
		fat_ent_put(sb, &fatent, FAT_ENT_FREE);
		if (sbi->free_clusters != -1) {
			sbi->free_clusters++; //�����������䤹
			sb->s_dirt = 1;
			dirty = 1;
		}

		if(p2fat_check_cont_space(sb, fatent.entry))
			sbi->cont_space.cont++;

		list_del(walk);
		kfree(fat_reserved);
	}

error:
	if(dirty)
		p2fat_clusters_flush(sb, 1);

	p2fatent_brelse(&fatent);

	if(lock)
		unlock_fat(sbi);

	return err;
}
/*--------------------*/

/* Panasonic Original */
int p2fat_apply_reserved_fat(struct super_block *sb)
{
	return __p2fat_apply_reserved_fat(sb, 1);
}
/*--------------------*/

/* Panasonic Original */
int p2fat_apply_reserved_fat_nolock(struct super_block *sb)
{
	return __p2fat_apply_reserved_fat(sb, 0);
}
/*--------------------*/

//�������饹�����򥫥���Ȥ���
// sb      : �����ѡ��֥�å�
// 
// �����  : ���� 0   ���� -EIO
//
// ���ȸ�  : fat_statfs()  / inode.c
int p2fat_count_free_clusters(struct super_block *sb)
{
	struct p2fat_sb_info *sbi = P2FAT_SB(sb);
	struct fatent_operations *ops = sbi->fatent_ops;
	struct p2fat_entry fatent;
	int err = 0, free;

	lock_fat(sbi);
	if (sbi->free_clusters != -1) //���Ǥ�ʬ���äƤ���Ȥ��ϲ��⤷�ʤ�
		goto out;

	free = 0;
	p2fatent_init(&fatent); //FAT����ν����
	p2fatent_set_entry(&fatent, FAT_START_ENT); //FAT�ơ��֥����Ƭ�˥��å�
	while (fatent.entry < sbi->max_cluster) {
		err = fat_ent_read_block(sb, &fatent); //FAT������ɤ߹���
		if (err){
			printk("fat_ent_read_block(%d) error %08X\n", __LINE__, err);
			goto out;
		}

		do {
			if (ops->ent_get(&fatent) == FAT_ENT_FREE) //�ͤ򥲥åȤ����������ɤ��������å�
				free++;
		} while (fat_ent_next(sbi, &fatent)); //�Ǹ�ޤ�³����
	}
	sbi->free_clusters = free; //�ե꡼���饹������
	sb->s_dirt = 1; //���������Τǥ����ƥ��ˤ���
	p2fatent_brelse(&fatent);
out:
	unlock_fat(sbi);
	return err;
}

/* Panasonic Experiment */
static void fatent_mark_page_accessed(struct page *page)
{
	if (!PageActive(page) && PageReferenced(page)) {
		activate_page(page);
		ClearPageReferenced(page);
	} else if (!PageReferenced(page)) {
		SetPageReferenced(page);
	}
}
/*----------------------*/

/* Panasonic Experiment */
static void p2fat_free_buffer_head(struct page *page)
{
	struct buffer_head *bh, *head;

	lock_page(page);

	head = page_buffers(page);
	bh = head;

	do{
		struct buffer_head *next = bh->b_this_page;
		free_buffer_head(bh);
		bh = next;
	}while(bh != head);

	unlock_page(page);
}
/*----------------------*/

/* Panasonic Experiment */
static int fat_ent_writepages(struct inode *inode, struct list_head *list)
{
	int ret = 0;

	int i, fat_num = 0;
	int count = 0;
	unsigned int start, end;
	struct address_space *mapping = inode->i_mapping;
	struct list_head *walk;
	struct fatent_list *fat_list = NULL;
	struct page *page;
	struct writeback_control wbc = {
		.sync_mode = WB_SYNC_ALL,
		.nr_to_write = mapping->nrpages * 2,
	};
	loff_t i_size = i_size_read(inode);
	pgoff_t end_index = i_size >> PAGE_CACHE_SHIFT;
	unsigned offset = i_size & (PAGE_CACHE_SIZE-1);

	if(P2FAT_SB(inode->i_sb)->fat_bits == 16){
		int page_count;
		int *page_indexes;

		page_indexes = kmalloc(end_index * sizeof(int), GFP_KERNEL);
		if(!page_indexes){
			printk("[%s:%d] kmalloc failed!\n", __FILE__, __LINE__);
			return -ENOMEM;
		}

		page_count = 0;
		list_for_each(walk, list){
			fat_list = list_entry(walk, struct fatent_list, lru);
			for(i = 0; i < FAT_IO_PAGES; i++){
				page_indexes[page_count] = fat_list->page->index + i;
				page_count++;
				if(page_count >= end_index)
					break;
			}
			if(page_count >= end_index)
				break;
		}

		for(i = 0; i < end_index / 2; i++){
			int src_index = page_indexes[i];
			int dest_index = page_indexes[i + end_index / 2];

			if(src_index >= FAT_TOTAL_PAGES || dest_index >= FAT_TOTAL_PAGES){
				printk("[%s:%d] index is out of ranges : src %d dest %d\n",
					__FILE__, __LINE__, src_index, dest_index);
				break;
			}

			memcpy(page_address(FAT_pages[dest_index]),
				page_address(FAT_pages[src_index]), PAGE_SIZE);
		}

		kfree(page_indexes);
		page_indexes = NULL;
	}

RETRY:
	wbc.nr_to_write = mapping->nrpages * 2;
	P2FAT_SB(inode->i_sb)->write_fat_num = fat_num;

	start = ~0;
	end = 0;

	list_for_each(walk, list){
		fat_list = list_entry(walk, struct fatent_list, lru);
		for(i = 0; i < FAT_IO_PAGES; i++){
			count++;
			page = FAT_pages[fat_list->page->index + i];

			if (fat_list->page_index + i >= end_index){
				if (fat_list->page_index + i >= end_index + 1 || !offset) {
					break;
				}
			}

			if(page->index < start)
				start = page->index;
			if(page->index > end)
				end = page->index;

			if(!PageLocked(page)){
				lock_page(page);
			}

			ret = block_prepare_write(page, 0, PAGE_CACHE_SIZE, fatent_get_block);
			if(ret){
				printk("block_prepare_write err : %d\n", ret);
			}

			flush_dcache_page(page);

			ret = block_commit_write(page, 0, PAGE_CACHE_SIZE);
			if (!ret) {
				mark_inode_dirty(inode);
			}

			//�ڡ����������ƥ��������å����� (include/linux/backing-dev.h)
			if (!mapping_cap_writeback_dirty(mapping)) //�񤭽Ф���ɬ�פΤȤ��ϲ��⤷�ʤ�
				printk("NOT DIRTY\n");

			if(PageLocked(page)){
				unlock_page(page);
			}
			fatent_mark_page_accessed(page);
			balance_dirty_pages_ratelimited(mapping);
			page_cache_release(page);
		}
	}

	wbc.range_start = start << PAGE_SHIFT;
	wbc.range_end = (end+1) << PAGE_SHIFT;

	inode->i_state &= ~I_DIRTY;

	//�ڡ����ν񤭽Ф�(�ǽ�Ū�ˤ�fs/mpage.c��__mpage_writepage���ƤФ��)
	wbc.for_writepages = 1;
	mapping->a_ops->writepages(mapping, &wbc); //FAT�Ǥ�fat_writepages() (fs/fat/inode.c)
	wbc.for_writepages = 0;

	list_for_each(walk, list){
		fat_list = list_entry(walk, struct fatent_list, lru);
		for(i = 0; i < FAT_IO_PAGES; i++){
			count++;

			if (fat_list->page_index + i >= end_index){
				if (fat_list->page_index + i >= end_index + 1 || !offset) {
					break;
				}
			}

			page = FAT_pages[fat_list->page->index + i];
			
			wait_on_page_writeback(page);

			//try_to_free_buffers()��mapping�������Τ�����
			p2fat_free_buffer_head(page);

			ClearPagePrivate(page);
			ClearPageReferenced(page);
		}
	}

	//This inode is clean, inuse
	list_move(&inode->i_list, &inode_in_use);

	if(P2FAT_SB(inode->i_sb)->fat_bits != 16){
		fat_num++;
		if(fat_num < P2FAT_SB(inode->i_sb)->fats)
			goto RETRY;
	}

	return ret;
}
/*----------------------*/

/* Panasonic Original */
void p2fat_ent_access_exit(struct super_block *sb)
{
	int i;
	struct p2fat_sb_info *sbi = P2FAT_SB(sb);

	struct list_head *walk, *tmp;
	struct fatent_reserve_fat *fat_reserved = NULL;

	if(p2fat_apply_reserved_fat(sb) < 0){
		printk("p2fat_apply_reserved_fat error\n");
	}

	if(p2fat_sync(sb) < 0){
		printk("p2fat_sync error\n");
	}

	fatent_remove_from_list(sb);

	if(sbi->fat_pages){
		for(i = 0; i < sbi->fat_pages_num; i++){
			if(sbi->fat_pages[i].indexes){
				kfree(sbi->fat_pages[i].indexes);
				sbi->fat_pages[i].indexes = NULL;
			}
		}
		kfree(sbi->fat_pages);
	}

	if(sbi->fat_inode){
		iput(sbi->fat_inode);
		sbi->fat_inode = NULL;
	}

	//ǰ�Τ��ᡢ���ͽ��Υꥹ�Ȥβ���(�ꥹ�Ȥ϶��ΤϤ�����)
	list_for_each_safe(walk, tmp, &sbi->reserved_list){
		fat_reserved = list_entry(walk, struct fatent_reserve_fat, lru);
		kfree(fat_reserved);
	}
	INIT_LIST_HEAD(&sbi->reserved_list);
}

/*--------------------*/

/* Panasonic Original */
int p2fat_cont_search(struct inode *inode, struct file *filp, struct fat_ioctl_space *arg)
{
	struct p2fat_sb_info *sbi = P2FAT_SB(inode->i_sb);
	struct fatent_operations *ops = sbi->fatent_ops;
	struct p2fat_entry fatent;
	int err = 0;
	unsigned long search_pos, start_cluster, each_cluster;
	int used_flag;
	unsigned long null_count;

	if(arg->n <= 0){
		arg->ret = 0;
		printk("[%s-%d] arg->n must be larger than zero.\n",__FUNCTION__,__LINE__);
		return -EINVAL;
	}

	// �����̼���
	if(arg->strict == 2){
		arg->ret = (sbi->max_cluster - FAT_START_ENT - sbi->data_cluster_offset) / arg->n;
		if(sbi->data_cluster_offset == 0){ //under 8G card
			arg->ret -= 2; //for Root Dir, P2-Contents Dir
		}
		return 0;
	}

	if(arg->type == FAT_TYPE_CONT_SEARCH && arg->strict == 0){ //����������������в�����������
		if(P2FAT_SB(inode->i_sb)->cont_space.n != arg->n){ //
			P2FAT_SB(inode->i_sb)->cont_space.cont = 0;
		}
		arg->ret = P2FAT_SB(inode->i_sb)->cont_space.cont;
		return 0;
	}

	lock_fat(sbi);

	if(arg->strict == 1 || sbi->cont_space.n != arg->n){ //��Ƭ���饵�������ʰץ������Ǥ������Ƭ����
		sbi->cont_space.n = arg->n;
		sbi->cont_space.cont = 0;
		sbi->cont_space.pos = 0;
	}

	//��üAU�ϥ��顼���֤��ƽ�λ
	if((sbi->cont_space.pos + 1) * arg->n + FAT_START_ENT + sbi->data_cluster_offset > sbi->max_cluster){
		arg->ret = sbi->cont_space.cont;
		sbi->show_inval_log = 1;
		if(sbi->free_clusters != -1){
			if(sbi->cont_space.cont * sbi->cont_space.n > sbi->free_clusters){
				printk("[%s:%d] Cont space(%lu) is bigger than fs info(%u) in first time\n", 
					__FILE__, __LINE__, sbi->cont_space.cont * sbi->cont_space.n, sbi->free_clusters);
			}
		}
		unlock_fat(sbi);
		return -EINVAL;
	}

	null_count = 0;

	p2fatent_init(&fatent); //FAT����ν����

	for(start_cluster = sbi->cont_space.pos; 
			(start_cluster+1)*arg->n + FAT_START_ENT + sbi->data_cluster_offset <= sbi->max_cluster;
			start_cluster++){
		used_flag = 0;
		search_pos = start_cluster*( arg->n ) + sbi->data_cluster_offset + FAT_START_ENT;

		p2fatent_set_entry(&fatent, search_pos); //FAT�ơ��֥�θ��߰��֤˥��å�
		err = fat_ent_read_block(inode->i_sb, &fatent); //FAT������ɤ߹���
		if (err){
			printk("fat_ent_read_block(%d) error %08X\n", __LINE__, err);
			goto out;
		}

		for(each_cluster = 0; each_cluster< (arg->n); each_cluster++){
			if (ops->ent_get(&fatent) != FAT_ENT_FREE){ //�ͤ򥲥åȤ����������ɤ��������å�
				used_flag = 1;
				break;
			}
			if(!fat_ent_next(sbi, &fatent)){
				if(search_pos + each_cluster + 1 >= sbi->max_cluster)
					break;

				p2fatent_set_entry(&fatent, search_pos + each_cluster + 1); //FAT�ơ��֥�θ��߰��֤˥��å�
				err = fat_ent_read_block(inode->i_sb, &fatent); //FAT������ɤ߹���
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
			if(start_cluster + 1 >= sbi->cont_space.pos + arg->unit){
				start_cluster++;
				break;
			}
		}
		else if(arg->type == FAT_TYPE_CAPA_SEARCH){
			if(null_count >= arg->unit){
				start_cluster++;
				break;
			}
		}
		cond_resched();
	}

	p2fatent_brelse(&fatent);
out:
	sbi->cont_space.pos = start_cluster;
	sbi->cont_space.cont += null_count;
	arg->ret = sbi->cont_space.cont;

	unlock_fat(sbi);
	return err;
}
/*--------------------*/

/* Panasonic Original */
static int __p2fat_sync(struct super_block *sb, int lock)
{
	int i, index;
	int ret = 0;
	struct list_head *walk;
	struct fatent_page_status *stat;
	struct list_head list;
	struct fatent_list *lists;
	struct p2fat_sb_info *sbi = P2FAT_SB(sb);

	//writeñ��
	unsigned long io_pages = 1L << (sbi->fatent_align_bits - PAGE_SHIFT - FAT_IO_PAGES_BITS);

	//do nothing in case of pdflush context.
	if(current_is_pdflush()){
		return 0;
	}

	if(test_and_clear_bit(SKIP_FAT_SYNC, &sbi->sync_flag)){
		return 0;
	}

	if(test_and_set_bit(ON_FAT_SYNC, &sbi->sync_flag)){
		//printk("%s duplicated.\n", __FUNCTION__);
		return 0;
	}

	if(P2FAT_SB(sb)->dirty_count == 0){
		clear_bit(ON_FAT_SYNC, &sbi->sync_flag);
		return 0;
	}

	lists = kmalloc(io_pages * sizeof(struct fatent_list), GFP_KERNEL);
	if(!lists){
		printk("kmalloc failed!\n");
		clear_bit(ON_FAT_SYNC, &sbi->sync_flag);
		return -ENOMEM;
	}

	if(lock){
		lock_fat(sbi);
		mutex_lock(&get_page_lock);
	}

	spin_lock(&dirty_fat_lock);
RETRY:
	list_for_each(walk, &list_dirty_fat){
		stat = list_entry(walk, struct fatent_page_status, lru);

		if(stat->sb != sb)
			continue;

		INIT_LIST_HEAD(&list);

		for(i = 0; i < io_pages; i++){
			index = stat->fat_page->indexes[i];

			//DIRTY�ӥåȤ򲼤�DIRTY�ꥹ�Ȥ���CLEAN�ꥹ�Ȥ˰�ư
			if(fatent_set_clean(index) < 0){
				ret = -EIO;
				goto END;
			}

			lists[i].page_index = (stat->fat_page->align_index
				<< (sbi->fatent_align_bits - PAGE_SHIFT)) + (i << FAT_IO_PAGES_BITS);
			lists[i].page = &FAT_list[index];

			//�񤭹����ѥꥹ��
			list_add_tail(&lists[i].lru, &list);
		}

		fat_ent_writepages(sbi->fat_inode, &list);

		goto RETRY;
	}

END:
	spin_unlock(&dirty_fat_lock);

	kfree(lists);
	
	if(lock){
		mutex_unlock(&get_page_lock);
	}

	if(!ret)
		ret = p2fat_apply_reserved_fat_nolock(sb);

	write_rt_dirty_inodes(sb);
	lock_rton(MAJOR(sb->s_dev));
	if(check_rt_status(sb)){
		if(sb->s_bdev){
			filemap_flush(sb->s_bdev->bd_inode->i_mapping);
		}
	}
	unlock_rton(MAJOR(sb->s_dev));
	
	if(lock){
		unlock_fat(sbi);
	}

	clear_bit(ON_FAT_SYNC, &sbi->sync_flag);
	return ret;
}
/*--------------------*/

/* Panasonic Original */
int p2fat_sync(struct super_block *sb)
{
	return __p2fat_sync(sb, 1);
}
/*--------------------*/

/* Panasonic Original */
int p2fat_sync_nolock(struct super_block *sb)
{
	return __p2fat_sync(sb, 0);
}
/*--------------------*/

/* Panasonic Original */
int p2fat_mem_init(void)
{
	int i;

	INIT_LIST_HEAD(&list_clean_fat);
	INIT_LIST_HEAD(&list_dirty_fat);

	spin_lock_init(&clean_fat_lock);
	spin_lock_init(&dirty_fat_lock);
	mutex_init(&get_page_lock);

	for(i = 0; i < FAT_TOTAL_PAGES; i++){
		FAT_pages[i] = alloc_page(GFP_KERNEL | __GFP_ZERO);

		if(!FAT_pages[i]){
			printk("oh my gosh! FAT Memory Allocation Failed.\n");
			return -ENOMEM;
		}
	}

	for(i = 0; i < FAT_LIST_NUM; i++){
		FAT_list[i].status = 0;
		FAT_list[i].assign_index = -1;
		FAT_list[i].index = i * FAT_IO_PAGES;
		FAT_list[i].sb = NULL;

		mutex_init(&FAT_list[i].lock);

		list_add_tail(&FAT_list[i].lru, &list_clean_fat);
	}
	return 0;
}
/*--------------------*/

/* Panasonic Original */
void p2fat_free_mem(void)
{
	int i;

	for(i = 0; i < FAT_TOTAL_PAGES; i++){
		fatent_remove_from_page_cache(FAT_pages[i]);
		__free_page(FAT_pages[i]);
	}
}
/*--------------------*/
