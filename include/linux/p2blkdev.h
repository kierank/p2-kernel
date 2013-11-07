/*****************************************************************************
 * linux/include/linux/p2blkdev.h
 * 
 *   Header file for using delayproc
 *
 *     Copyright (C) 2010 Panasonic corp.
 *     All Rights Reserved.
 *
 *****************************************************************************/

#ifndef _P2BLKDEV_H_
#define _P2BLKDEV_H_

/*** header include ***/

/*** I/F structure definitions ***/
struct blkdev_list_arg
{
  unsigned long start; /* [in] start LBA */
  unsigned long size;  /* [in] size[byte] */
};


/*** macro ***/
 /* for ioctl */
#define BLK_P2PF_IOC_MAGIC (0xBC)

#define BLK_P2PF_ADD_DIRENT_LIST _IOW(BLK_P2PF_IOC_MAGIC,  1, struct blkdev_list_arg)
#define BLK_P2PF_DEL_DIRENT_LIST _IOW(BLK_P2PF_IOC_MAGIC,  2, struct blkdev_list_arg)
#define BLK_P2PF_CLR_DIRENT_LIST _IO (BLK_P2PF_IOC_MAGIC,  3)

#define BLKMNTFS  _IO (BLK_P2PF_IOC_MAGIC,  10)
#define BLKUMNTFS _IO (BLK_P2PF_IOC_MAGIC,  11)


#ifdef __KERNEL__  /* for kernel ---------------> */


/*** header include ***/
#include <linux/list.h>    /* for list_head operations */
#include <linux/vmalloc.h> /* for vmalloc/vfree */

/*** struct ***/
struct blkdev_dirent_list
{
  unsigned long start; /* [in] start LBA */
  unsigned long size;  /* [in] size[Byte] */
  struct list_head dlist;
};

/*** function ***/
struct blkdev_dirent_list *__p2pf_is_dirent(struct list_head *head, unsigned long lba)
{
  struct blkdev_dirent_list *entry = NULL;

  if (list_empty(head)) return (NULL);

/*   printk("lba: 0x%08lX\n", lba); */
  
  list_for_each_entry(entry, head, dlist) {
/*     printk("[lba:0x%08lX size:%ld]\n", entry->start, entry->size); */
    if (entry->start == lba) {
      printk("Found [lba:0x%08lX]\n", entry->start);
      return (entry);
    }
  }
  return (NULL);
}


int blkdev_p2pf_is_dirent(struct block_device *bdev, unsigned long lba)
{
  struct blkdev_dirent_list *entry = NULL;
  struct list_head *head = &(bdev->bd_dlist);

  if (list_empty(head)) return (0);

/*   printk("lba: 0x%08lX\n", lba); */
  
  list_for_each_entry(entry, head, dlist) {
/*     printk("[lba:0x%08lX size:%ld]\n", entry->start, entry->size); */

    if (entry->start <= lba && lba <= entry->start + (entry->size>>9)) {
      printk("Found [lba:0x%08lX]\n", lba);
      return (1);
    }
  }
  return (0);
}


void blkdev_p2pf_add_dirent_list(struct block_device *bdev, struct blkdev_list_arg *arg)
{
  struct blkdev_dirent_list *entry = NULL;
  struct list_head *head = NULL;
  unsigned long lba = 0;

  /* Check arguments. */
  if (NULL == bdev || NULL == arg) {
    printk("Invalid arguments at %s!\n", __FUNCTION__);
    return;
  }
  head = &(bdev->bd_dlist);
  lba = arg->start;

  /* Check that arg is already added or not. */
  entry = __p2pf_is_dirent(head, lba);
  if (entry) {
    printk("Already added(0x%08lX)!\n", lba);
    return;
  }

  /* Malloc dirent list. */
  entry = (struct blkdev_dirent_list *)vmalloc(sizeof(struct blkdev_dirent_list));
  if (NULL == entry) {
    printk("vmalloc failed at %s!\n", __FUNCTION__);
    return;
  }

  /* Init list head. */
  INIT_LIST_HEAD(&(entry->dlist));
  entry->start = lba;
  entry->size = arg->size;

  /* Add dirent list. */
  list_add(&(entry->dlist), head);
}


void blkdev_p2pf_del_dirent_list(struct block_device *bdev, struct blkdev_list_arg *arg)
{
  struct blkdev_dirent_list *entry = NULL;
  struct list_head *head = NULL;
  unsigned long lba = 0;

  /* Check arguments. */
  if (NULL == bdev || NULL == arg) {
    printk("Invalid arguments at %s!\n", __FUNCTION__);
    return;
  }
  head = &(bdev->bd_dlist);
  lba = arg->start;

  /* Find the entry. */
  entry = __p2pf_is_dirent(head, lba);
  if (NULL == entry) {
    printk("Not found(0x%08lX)!\n", lba);
    return;
  }

  /* Delete dirent list. */
  list_del(&(entry->dlist));

  /* Free the entry. */
  vfree(entry);
  entry = NULL;
}


void blkdev_p2pf_clr_dirent_list(struct block_device *bdev)
{
  struct list_head *walk = NULL;
  struct list_head *tmp = NULL;
  struct list_head *head = NULL;

  /* Check arguments. */
  if (NULL == bdev) {
    printk("Invalid arguments at %s!\n", __FUNCTION__);
    return;
  }
  head = &(bdev->bd_dlist);

  list_for_each_safe(walk, tmp, &(bdev->bd_dlist)) {
    struct blkdev_dirent_list *entry = list_entry(walk, struct blkdev_dirent_list, dlist);
    printk("Delete [lba:0x%08lX size:%ld]\n", entry->start, entry->size);
    list_del_init(&(entry->dlist));
    vfree(entry);
    entry = NULL;
  }

  INIT_LIST_HEAD(&(bdev->bd_dlist));
}

#endif /* __KERNEL__ */  /* <--------------- for kernel */


#endif /* _P2BLKDEV_H_ */

/******************** the end of the file "p2blkdev.h" ********************/
