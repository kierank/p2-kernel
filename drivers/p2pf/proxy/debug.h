#ifndef __DEBUG_H__
#define __DEBUG_H__

#define DL_PANIC     1
#define DL_ALERT     2
#define DL_CRITICAL  3
#define DL_ERROR     4
#define DL_WARNING   5
#define DL_NOTICE    6
#define DL_INFO      7
#define DL_DEBUG     8

#ifndef DBG_LEVEL
	#define DBG_LEVEL DL_INFO
#endif

#define PPANIC(fmt, args...)	printk(KERN_EMERG "1:%s:%d:" fmt "\n", __FILE__, __LINE__, ## args);
#define PALERT(fmt, args...)	printk(KERN_ALERT "2:%s:%d:" fmt "\n", __FILE__, __LINE__, ## args);
#define PCRITICAL(fmt, args...)	printk(KERN_CRIT "3:%s:%d:" fmt "\n", __FILE__, __LINE__, ## args);
#define PERROR(fmt, args...)	printk(KERN_ERR "4:%s:%d:" fmt "\n", __FILE__, __LINE__, ## args);
#define PWARNING(fmt, args...)	printk(KERN_WARNING "5:%s:%d:" fmt "\n", __FILE__, __LINE__, ## args);
#define PNOTICE(fmt, args...)	printk(KERN_NOTICE "6:%s:%d:" fmt "\n", __FILE__, __LINE__, ## args);
#define PINFO(fmt, args...)	printk(KERN_INFO "7:%s:%d:" fmt "\n", __FILE__, __LINE__, ## args);
#define PDEBUG(fmt, args...)	printk(KERN_DEBUG "8:%s:%d:" fmt "\n", __FILE__, __LINE__, ## args);

#if DBG_LEVEL < DL_PANIC
	#undef PPANIC
	#define PPANIC(fmt, args...)
#endif

#if DBG_LEVEL < DL_ALERT
	#undef PALERT
	#define PALERT(fmt, args...)
#endif

#if DBG_LEVEL < DL_CRITICAL
	#undef PCRITICAL
	#define PCRITICAL(fmt, args...)
#endif

#if DBG_LEVEL < DL_ERROR
	#undef PERROR
	#define PERROR(fmt, args...)
#endif

#if DBG_LEVEL < DL_WARNING
	#undef PWARNING
	#define PWARNING(fmt, args...)
#endif

#if DBG_LEVEL < DL_NOTICE
	#undef PNOTICE
	#define PNOTICE(fmt, args...)
#endif

#if DBG_LEVEL < DL_INFO
	#undef PINFO
	#define PINFO(fmt, args...)
#endif

#if DBG_LEVEL < DL_DEBUG
	#undef PDEBUG
	#define PDEBUG(fmt, args...)
#endif

#if defined(DBG_PFUNC)
	#define PRINT_FUNC		printk("[%s]\n", __FUNCTION__);
#else
	#define PRINT_FUNC
#endif

#if defined(DBG_TRACE)
#define PTRACE(fmt, args...)  printk(KERN_INFO ">>>>%s" fmt "\n",\
           __FUNCTION__, ## args);
#else
#define PTRACE(fmt, args...)
#endif

#endif // __DEBUG_H__
