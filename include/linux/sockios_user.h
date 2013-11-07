/*
 *  include/linux/socketios_user.h
 */
/* $Id: sockios_user.h 21639 2013-04-09 00:15:24Z Yasuyuki Matsumoto $ */
 
#ifndef __LINUX_SOCKETIOS_USER_H__
#define __LINUX_SOCKETIOS_USER_H__
 
 
#include <linux/sockios.h>
#include <linux/mii.h>
#ifdef __KERNEL__
 #include <linux/if.h>
#else  /* ! __KERNEL__ */
 /* Not Kernel header but devkit header is used at App. compile. */
 /* So use the one below because linux/if.h in devkit does not work properly. */
 #include <net/if.h>
#endif  /* __KERNEL__ */
 
struct ifreq_mii {
    union 
    {
        char ifrn_name[IFNAMSIZ];
    } ifr_ifrn;
    struct mii_ioctl_data mii_data;
};

#endif  /* __LINUX_SOCKETIOS_USER_H__ */
