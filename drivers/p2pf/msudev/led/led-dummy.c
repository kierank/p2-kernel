/*
 *  led_dummy.c  --- dummy lower driver for LED control
 */
/* $Id: led-dummy.c 5088 2010-02-09 02:49:16Z Sawada Koji $ */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/moduleparam.h>  /* module_param */

#include <linux/p2msudev_user.h>
#include "led.h"

#define MODULE_NAME "msudev-led-dummy"
#include "../debug.h"


#define NR_LED CONFIG_MSUDEV_LED_DEV_DUMMY_NUMBER

static struct led_param {

    struct p2msudev_ioc_led_ctrl ctrl[NR_LED];
    unsigned int nr_led;

} led_param;


/* initilazie */
static int init_led(struct msudev_led_ops *ops)
{
    int retval = 0;
    struct led_param *lp = (struct led_param *)ops->data;

    memset(lp,0,sizeof(struct led_param));
    lp->nr_led = NR_LED;

    /* complete */
    return retval;
}

/* cleanup */
static void cleanup_led(struct msudev_led_ops *ops)
{
/*     struct key_param *lp = (struct led_param *)ops->data; */

}

/* setting */
static int set_led(struct msudev_led_ops *ops, struct p2msudev_ioc_led_ctrl *param)
{
    struct led_param *lp = (struct led_param *)ops->data;
    pr_info("**** set LED[%d] : bright=%d, timing=%d\n",
            param->no, param->bright, param->timing);
    memcpy(&lp->ctrl[param->no],param,sizeof(struct p2msudev_ioc_led_ctrl));
    return 0;
}

    /* getting */
static int get_led(struct msudev_led_ops *ops, struct p2msudev_ioc_led_ctrl *param)
{
    struct led_param *lp = (struct led_param *)ops->data;
    memcpy(param,&lp->ctrl[param->no],sizeof(struct p2msudev_ioc_led_ctrl));
    pr_info("**** get LED[%d] : bright=%d, timing=%d\n",
            param->no, param->bright, param->timing);
    return 0;
}


/* LED number */
static unsigned int nr_led(struct msudev_led_ops *ops)
{
    struct led_param *lp = (struct led_param *)ops->data;
    return lp->nr_led;
}

/*
 *  structure of operational functions
 */
static struct msudev_led_ops msudev_led_ops = {
    .init_led    = init_led,
    .cleanup_led = cleanup_led,
    .set_led = set_led,
    .get_led = get_led,
    .nr_led = nr_led,
    .data           = (void*)&led_param,
};

/*
 *  get operational functions
 */
struct msudev_led_ops *  __led_get_ops(void)
{
    return &msudev_led_ops;
}

