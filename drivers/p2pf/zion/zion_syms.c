#include <linux/autoconf.h>

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/fcntl.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/uaccess.h>

#include <linux/pci.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/stddef.h>
#include <linux/version.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/proc_fs.h>

#include <linux/zion.h>

#ifndef MODULE

EXPORT_SYMBOL(find_zion);
EXPORT_SYMBOL(zion_enable_mbus_interrupt);
EXPORT_SYMBOL(zion_disable_mbus_interrupt);
EXPORT_SYMBOL(zion_mbus_int_clear);
EXPORT_SYMBOL(zion_pci_dma_int_clear);
EXPORT_SYMBOL(zion_backend_pci_int_clear);

#ifdef CONFIG_ZION_PCI
EXPORT_SYMBOL(ZION_pci_cache_clear);
EXPORT_SYMBOL(ZION_check_addr_and_pci_cache_clear);
#endif /* CONFIG_ZION_PCI */

#endif /* MODULE */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Panasonic");
