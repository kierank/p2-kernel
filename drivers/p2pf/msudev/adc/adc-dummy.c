/*
 *  adc_dummy.c  --- dummy lower driver for AD/C
 */
/* $Id: adc-dummy.c 5088 2010-02-09 02:49:16Z Sawada Koji $ */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/moduleparam.h>  /* module_param */

#include <linux/p2msudev_user.h>
#include "adc.h"

#define MODULE_NAME "msudev-adc-dummy"
#include "../debug.h"


#define NR_CHANNEL CONFIG_MSUDEV_ADC_DEV_DUMMY_CAHN_NUMBER

static struct adc_param {

    unsigned long dummy_chan[NR_CHANNEL];
    unsigned int nr_chan;

    int flag_reset;

} adc_param;

module_param_array_named(adc,adc_param.dummy_chan,ulong,&adc_param.nr_chan,0644);


/* initilazie */
static int init_adc(struct msudev_adc_ops *ops)
{
    int retval = 0;
    struct adc_param *ap = (struct adc_param *)ops->data;

    memset(ap,0,sizeof(struct adc_param));
    ap->nr_chan = NR_CHANNEL;

    /* complete */
    return retval;
}

/* cleanup */
static void cleanup_adc(struct msudev_adc_ops *ops)
{
/*     struct key_param *ap = (struct adc_param *)ops->data; */

}

/* get value */
static int get_value(struct msudev_adc_ops *ops, int chan, unsigned long *p_value)
{
    struct adc_param *ap = (struct adc_param *)ops->data;
    if(chan<0 || chan>=ap->nr_chan)
        return -EINVAL;
    *p_value = ap->flag_reset?0:ap->dummy_chan[chan];
    return 0;
}

/* reset cntrol */
static int do_reset(struct msudev_adc_ops *ops, int reset_on)
{
    struct adc_param *ap = (struct adc_param *)ops->data;
    int i;
    for(i=0;i<ap->nr_chan;i++)
        ap->dummy_chan[i] = 0;
    ap->flag_reset = reset_on?1:0;
    return 0;
}

/* check reset status */
static int check_reset(struct msudev_adc_ops *ops, int *check)
{
    struct adc_param *ap = (struct adc_param *)ops->data;
    if(!check)
        return -EINVAL;
    *check = ap->flag_reset;
    return 0;
}

/* chanel number */
static unsigned int nr_chan(struct msudev_adc_ops *ops)
{
    struct adc_param *ap = (struct adc_param *)ops->data;
    return ap->nr_chan;
}


/*
 *  structure of operational functions
 */
static struct msudev_adc_ops adc_ops = {
    .init_adc    = init_adc,
    .cleanup_adc = cleanup_adc,
    .get_value  = get_value,
    .do_reset   = do_reset,
    .check_reset = check_reset,
    .nr_chan = nr_chan,
    .data           = (void*)&adc_param,
};

/*
 *  get operational functions
 */
struct msudev_adc_ops *  __adc_get_ops(void)
{
    return &adc_ops;
}

