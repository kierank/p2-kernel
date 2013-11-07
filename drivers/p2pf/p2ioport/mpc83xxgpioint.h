#include <linux/autoconf.h>

/*
 *  Driver information:
 *
 *    Major number: 246
 *    Driver name : mpc83xxgpio
 */
#define MPC83XXGPIO_MAJOR	300
#define	MPC83XXGPIO_NAME	"mpc83xxgpio"
#define MPC83XX_IRQ_GPIO        74
#define	MPC83XXGPIO_VERSION	"0.1"


/*
 *  Debug out
 */
#define DEBUG_MODE

#ifdef DEBUG_MODE
#define DbgPrint( _x_ ) printk _x_ 
#else
#define DbgPrint( _x_ )
#endif


/*
 * MPC83XX GPIO Unit Registers
 */
#define MPC83XX_GPIOUNIT_START   0x0C00
#define MPC83XX_GPIOUNIT_SIZE    0x0020

#define MPC83XX_GPIO_DIRECTION   0x0000
#define MPC83XX_GPIO_OPENDRAIN   0x0004
#define MPC83XX_GPIO_DATA        0x0008
#define MPC83XX_GPIO_INTEVENT    0x000C
#define MPC83XX_GPIO_INTMASK     0x0010
#define MPC83XX_GPIO_EXTINTCTRL  0x0014
