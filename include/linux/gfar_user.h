/*
 *  include/linux/gfar_user.h
 */
/* $Id: gfar_user.h 5237 2010-02-18 00:25:41Z Noguchi Isao $ */
 
#ifndef __LINUX_GFAR_USER_H__
#define __LINUX_GFAR_USER_H__
 
 
#include <linux/sockios.h>
#ifdef __KERNEL__
#include <linux/sockios_user.h>
#else  /* ! __KERNEL__ */
#include <linux-include/linux/sockios_user.h>
#endif  /* __KERNEL__ */

/* IOC_CMD */
#define GFAR_IOC_DSAS  (SIOCDEVPRIVATE)

/**
 *  structure for ioctl(GFAR_IOC_DSAS_INFO)
 */
struct gfar_dsas_info {

    /* if not-zero then DSAS is running now */
    int is_running;

    /* state for DSAS */
    enum gfar_dsas_state {
        ST_DSAS_NORMAL = 0,
        ST_DSAS_BLOCK,
        ST_DSAS_HWBLOCK,
        ST_DSAS_BLOCK_TO_NORMAL,
    } state;

    /* rx-packet number got in estimate period */
    unsigned long rx_pktnum;
};



struct gfar_dsas_param {

    /* estimate period to got rx-packet number for threshold [msec]*/
    unsigned int estimate_period;

    /* threshold of move to blocking state  */
    unsigned long th_pktnum_block;

    /* threshold of move to H/W blocking state  */
    unsigned long th_pktnum_hwblock;

    /* threshold of return to normal state  */
    unsigned long th_pktnum_normal;

    /* moratorium period to move to normal state from block state */
    unsigned int moratorium_period;

    /* period to stay in  H/W blocking state */
    unsigned int hwblock_period;
};


/**
 *  structure for ioctl(GFAR_IOC_DSAS)
 */
struct gfar_ioc_dsas {

    /* command select */
    enum gfar_dsas_cmd {
        CMD_DSAS_STOP = 0,
        CMD_DSAS_START,
        CMD_DSAS_SET_PARAM,
        CMD_DSAS_GET_PARAM,
        CMD_DSAS_GET_INFO
    } cmd;

    /* union for option data */
    union {
        /* parameters */
        struct gfar_dsas_param param;
        /* info */
        struct gfar_dsas_info info; 
    } opt;
};





#endif  /* __LINUX_GFAR_USER_H__ */
