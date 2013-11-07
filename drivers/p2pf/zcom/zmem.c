#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/proc_fs.h>

#include "zcom.h"

static int zmem_major = ZMEM_MAJOR;
static zmem_dev_t zmem_dev;

static int zmem_open(struct inode *inode, struct file *file)
{
	PRINT_FUNC;

	return 0;
}

static int zmem_release(struct inode *inode, struct file *file)
{
	PRINT_FUNC;

	return 0;
}

static int zmem_mmap(struct file *file, struct vm_area_struct *vma)
{
	int ret;
	u32 base;
	u32 size;

	PRINT_FUNC;

	base = __pa(zmem_dev.zion->zmem_base);
	size = vma->vm_end - vma->vm_start;
	vma->vm_flags |= VM_IO;
	vma->vm_flags |= VM_RESERVED;

	ret = remap_pfn_range(vma, vma->vm_start, base >> PAGE_SHIFT, size, vma->vm_page_prot);
	if(ret < 0){
		PERROR("remap_pfn_range() failed(%d)", ret);
		return -EAGAIN;
	}

	return 0;
}

static struct file_operations fops = {
	.open 		= zmem_open,
	.release	= zmem_release,
	.mmap		= zmem_mmap,
};

static int read_procfs_dump(char *buf, char **start, off_t offset, int count, int *eof, void *data)
{
	u8 *ptr;
	int size;

	PRINT_FUNC;

	size = zmem_dev.zion->zmem_size;

	if(offset >= size){
		*eof = 1;
		return 0;
	}
	size -= offset;
	size = ((size < count) ? size : count);

	ptr = (u8 *)zmem_dev.zion->zmem_base;
	ptr +=offset;

	*start = buf;
	memcpy(buf, ptr, size);

	return size;
}

static struct proc_dir_entry *dir_entry;

static int zmem_procfs_init(void)
{
	PRINT_FUNC;

	dir_entry = proc_mkdir("driver/zmem", NULL);
	if(!dir_entry){
		PERROR("proc_mkdir() failed");
		return -EFAULT;
	}
	create_proc_read_entry("dump", 0, dir_entry, read_procfs_dump, NULL);

	return 0;
}

static int zmem_procfs_exit(void)
{
	PRINT_FUNC;

	if(dir_entry != NULL){
		remove_proc_entry("dump", dir_entry);
		remove_proc_entry("driver/zmem", NULL);
	}

	return 0;
}

int zmem_init(zion_dev_t *zion)
{
	int ret;
	zmem_dev_t *dev;

	PRINT_FUNC;

	dev = &zmem_dev;
	dev->zion = zion;

	ret = register_chrdev(zmem_major, ZMEM_DEV_NAME, &fops);
	if(ret < 0){
		PERROR("register_chrdev() failed(%d)", ret);
		return ret;
	}

	if(zmem_major == 0){
		zmem_major = ret;
	}

	zmem_procfs_init();

	return 0;
}

void zmem_exit(void)
{
	PRINT_FUNC;

	unregister_chrdev(zmem_major, ZMEM_DEV_NAME);

	zmem_procfs_exit();
}
