/**
 *  drivers/net/gianfar_dsas.h
 */
/* $Id: gianfar_dsas.h 5237 2010-02-18 00:25:41Z Noguchi Isao $ */

#ifndef __GIANFAR_DSAS_H
#define __GIANFAR_DSAS_H

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/unistd.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/semaphore.h>
#include <linux/gfar_user.h>

/**
 *  default value of config parameters
 */

/* A interval period to measure the number of the reception packets
   [msec] */
#define DEFAULT_DSAS_IVAL   CONFIG_GIANFAR_DSAS_IVAL
#if DEFAULT_DSAS_IVAL <= 0
#error *** "DEFAULT_DSAS_IVAL" must not be zero
#endif  /* DEFAULT_DSAS_IVAL <= 0 */

/* A maximam period to measure the number of the rx-packets [msec] */
#define DEFAULT_DSAS_MAX_MEASURE_PERIOD CONFIG_GIANFAR_DSAS_MAX_MEASURE_PERIOD
#if DEFAULT_DSAS_MAX_MEASURE_PERIOD < DEFAULT_DSAS_IVAL
#error *** "DEFAULT_DSAS_MAX_MEASURE_PERIOD" must be larger than or equal to "DEFAULT_DSAS_IVAL"
#endif  /* DEFAULT_DSAS_MAX_MEASURE_PERIOD < DEFAULT_DSAS_IVAL  */

/* estimate period to got rx-packet number for threshold [msec] */
#define DEFAULT_DSAS_ESTIMATE_PERIOD CONFIG_GIANFAR_DSAS_ESTIMATE_PERIOD
#if DEFAULT_DSAS_ESTIMATE_PERIOD < DEFAULT_DSAS_IVAL
#error *** "DEFAULT_DSAS_ESTIMATE_PERIOD" must be larger than or equal to "DEFAULT_DSAS_IVAL"
#endif  /* DEFAULT_DSAS_ESTIMATE_PERIOD < DEFAULT_DSAS_IVAL  */

/* period to stay in H/W blocking state */
#define DEFAULT_DSAS_HWBLOCK_PERIOD CONFIG_GIANFAR_DSAS_HWBLOCK_PERIOD
#if DEFAULT_DSAS_HWBLOCK_PERIOD < DEFAULT_DSAS_IVAL
#error *** "DEFAULT_DSAS_HWBLOCK_PERIOD" must be larger than or equal to "DEFAULT_DSAS_IVAL"
#endif  /* DEFAULT_DSAS_HWBLOCK_PERIOD < DEFAULT_DSAS_IVAL  */


struct gfar_dsas {

#define GFAR_DSAS_SZ_NAME   16
    /* name */
    char name[GFAR_DSAS_SZ_NAME];

    /* spinlock */
	spinlock_t lock;

    /* semaphore */
    struct semaphore sema;

    /* timer_list */
    struct timer_list timer;

    /* tasklet */
    struct tasklet_struct   tasklet;

    /* A interval period to measure the number of the rx-packets [msec] */
    unsigned int interval;
    unsigned int nr_interval;

    /* A maximam period  to measure the number of the rx-packets [msec]*/
    unsigned int max_period;
    unsigned int nr_max_period;

    /* state for DSAS */
    enum gfar_dsas_state state;

    /* flag whether is running */
    int is_running;

    /* estimate period to got rx-packet number for threshold [msec]*/
    unsigned int estimate_period;
    unsigned int nr_estimate_period;

    /* threshold of move to blocking state  */
    unsigned long th_pktnum_block;

    /* threshold of move to H/W blocking state  */
    unsigned long th_pktnum_hwblock;

    /* threshold of return to normal state  */
    unsigned long th_pktnum_normal;

    /* moratorium period to move to normal state from block state */
    unsigned int moratorium_period;
    unsigned int nr_moratorium_period;
    unsigned int moratorium_count;

    /* period to stay in  H/W blocking state */
    unsigned int hwblock_period;
    unsigned int nr_hwblock_period;
    unsigned int hwblock_count;

    /* rx-packet number got in the estimate period */
    unsigned long rx_pktnum;

    /* rx-packet number got in the interval period */
    unsigned long rx_pktnum_interval;

    /* ring buffer of rx-packet number got in the interval period */
    unsigned long *ring_rx_pktnum_interval;

    /* current index of ring_rx_pktnum_interval[] */
    unsigned int cur_ptr;

    /* function  */
    void (*hwblock)(int enable, unsigned long data);

    /* private data */
    unsigned long data;
};

#define gfar_dsas_name(p)   ((p)->name)
#define gfar_dsas_rxpkts(p) ((p)->rx_pktnum)
#define gfar_dsas_running(p) ((p)->is_running)
#define gfar_dsas_state(p) ((p)->state)
#define gfar_dsas_hwblock(p)    ((p)->hwblock)
#define gfar_dsas_set_hwblock(p,f)    ((p)->hwblock=(f))

void gfar_dsas_countup(struct gfar_dsas *const dsas);
int gfar_dsas_info(struct gfar_dsas *const dsas, struct gfar_dsas_info *const info);
/* 2009/9/16,  added by panasonic >>>> */
#ifdef CONFIG_GIANFAR_EXTENTION_PROC_FS
int gfar_dsas_procfunc(char *buff, char **start, off_t offset, int count, int *eof, void *data);
#endif  /* CONFIG_GIANFAR_EXTENTION_PROC_FS */
/* <<<< 2009/9/16,  added by panasonic */
int gfar_dsas_set_param(struct gfar_dsas *const dsas, const struct gfar_dsas_param * const param);
int gfar_dsas_get_param(struct gfar_dsas *const dsas, struct gfar_dsas_param * const param);
int gfar_dsas_start(struct gfar_dsas *const dsas);
int gfar_dsas_stop(struct gfar_dsas *const dsas);
int gfar_dsas_init(struct gfar_dsas *const dsas, const char * const name, unsigned long data);
int gfar_dsas_exit(struct gfar_dsas *const dsas);


#endif  /* __GIANFAR_DSAS_H */

