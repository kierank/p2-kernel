/*
 *  drivers/p2pf/p2pfinfo/p2pfinfo.c
 *  $Id: p2pfinfo.c 3998 2009-09-17 02:49:00Z Noguchi Isao $
 */

#include <linux/module.h>		/* for module */
#include <linux/kernel.h>		/* for kernel module */
#include <linux/init.h>			/* for init */
#include <linux/proc_fs.h>		/* for proc filesystem */


static struct proc_dir_entry *pentry = NULL;


#if defined(CONFIG_PROC_P2PF_BOOTMODE)
/*
 * read_proc for /proc/p2pf/bootmode
 */
static int procfunc_p2pf_bootmode(char *buff, char **start, off_t offset, int count, int *eof, void *data)
{
    int len=0;
#if defined(CONFIG_PROC_P2PF_BOOTMODE_ROM)
    len += sprintf(buff+len, "ROM\n");
#elif defined(CONFIG_PROC_P2PF_BOOTMODE_NFS)
    len += sprintf(buff+len, "NFS\n");
#elif defined(CONFIG_PROC_P2PF_BOOTMODE_VUP)
    len += sprintf(buff+len, "VUP\n");
#else
    len += sprintf(buff+len, "UNKNOWN\n");
#endif
    /* finished */
    *eof=1;
    return len;
}
#endif  /* CONFIG_PROC_P2PF_BOOTMODE */


#if defined(CONFIG_PROC_P2PF_TARGET)
/*
 * read_proc for /proc/p2pf/target
 */
static int procfunc_p2pf_target(char *buff, char **start, off_t offset, int count, int *eof, void *data)
{
    int len=0;

#if defined(CONFIG_P2PF_TARGET)
	len += sprintf(buff+len, "%s\n", CONFIG_P2PF_TARGET);
#else /* ! CONFIG_P2PF_TARGET */
    len += sprintf(buff+len, "Kxxx\n");
#endif /* CONFIG_P2PF_TARGET */
	
    /* finished */
    *eof=1;
    return len;
}


/*
 * read_proc for /proc/p2pf/hw_version
 */
static int procfunc_p2pf_hw_version(char *buff, char **start, off_t offset, int count, int *eof, void *data)
{
    int len=0;
#if defined(CONFIG_P2PF_HW_VERSION)
    len += sprintf(buff+len, "%d\n", CONFIG_P2PF_HW_VERSION);
#endif  /* CONFIG_P2PF_HW_VERSION */
    *eof=1;
    return len;
}
#endif /* CONFIG_PROC_P2PF_TARGET */


#if defined(CONFIG_P2PF_VERINFO)
/*
 * read_proc for /proc/p2pf/verinfo
 */
static int procfunc_p2pf_verinfo(char *buff, char **start, off_t offset, int count, int *eof, void *data)
{
    int len=0;
#if defined(VERINFO)
    len += sprintf(buff+len, "%s\n", VERINFO);
#endif  /* VERINFO */
    *eof=1;
    return len;
}
#endif  /* CONFIG_P2PF_VERINFO */

/*
 *  initialize /proc/p2pf/xxxxxx
 */
static int __init init_proc_p2pf(void)
{
    printk(KERN_INFO "INFO: initialize /proc/p2pf entries.\n");

	pentry = create_proc_entry("p2pf", S_IFDIR, 0);
	if (!pentry) {
		printk(KERN_WARNING "WARNING: Can't create /proc/p2pf\n");
		return 0;
	}
	
#if defined(CONFIG_PROC_P2PF_BOOTMODE)
    /* create /proc/p2pf/bootmode */
    if(!create_proc_read_entry("bootmode",0,pentry,procfunc_p2pf_bootmode, NULL))
        printk(KERN_WARNING "WARNING: Can't create /proc/p2pf/bootmode\n");
#endif  /* CONFIG_PROC_P2PF_BOOTMODE */
	
#if defined(CONFIG_PROC_P2PF_TARGET)
    /* create /proc/p2pf/target */
    if(!create_proc_read_entry("target",0,pentry,procfunc_p2pf_target, NULL))
        printk(KERN_WARNING "WARNING: Can't create /proc/p2pf/target\n");

    /* create /proc/p2pf/hw_version */
    if(!create_proc_read_entry("hw_version",0,pentry,procfunc_p2pf_hw_version, NULL))
        printk(KERN_WARNING "WARNING: Can't create /proc/p2pf/hw_version\n");
#endif  /* CONFIG_PROC_P2PF_TARGET */

#if defined(CONFIG_P2PF_VERINFO)
    /* create /proc/p2pf/verinfo */
    if(!create_proc_read_entry("verinfo",0,pentry,procfunc_p2pf_verinfo, NULL))
        printk(KERN_WARNING "WARNING: Can't create /proc/p2pf/verinfo\n");
#endif  /* CONFIG_P2PF_VERINFO */

	return 0;
}


/*
 *  clean up /proc/p2pf/xxxxxx
 */
static void __exit exit_proc_p2pf(void)
{
	printk(KERN_INFO "INFO: clean up /proc/p2pf entries.\n");

    if(NULL==pentry)
        return;
	
#if defined(CONFIG_PROC_P2PF_BOOTMODE)
	remove_proc_entry("bootmode", pentry);
#endif  /* CONFIG_PROC_P2PF_BOOTMODE */

#if defined(CONFIG_PROC_P2PF_TARGET)
	remove_proc_entry("target", 0);
	remove_proc_entry("hw_version", pentry);
#endif  /* CONFIG_PROC_P2PF_TARGET */

#if defined(CONFIG_P2PF_VERINFO)
	remove_proc_entry("verinfo", pentry);
#endif  /* CONFIG_P2PF_VERINFO */

	remove_proc_entry("p2pf", 0);
}


module_init(init_proc_p2pf);
module_exit(exit_proc_p2pf);
