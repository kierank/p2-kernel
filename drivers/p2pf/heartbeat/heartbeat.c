#include <linux/io.h>
#include <linux/sched.h>
#include <asm/time.h>

/* Used for doing event-scans */
DEFINE_PER_CPU(struct timer_list, heartbeat_timer);
static unsigned long event_scan_interval;

#include <linux/p2ioport.h>
static struct p2ioport_operations p2io_ops;

void heartbeat_scan(unsigned long unused)
{
  static unsigned int cnt = 0, period = 0;

  cnt += 1;
  if (cnt < period) {
    goto modtimer;
  }

  cnt = 0;
  period = ( 110 - ( (300<<FSHIFT) / ((avenrun[0]/5) + (3<<FSHIFT)) ) ) * 4;

  if (p2io_ops.heartbeat_led)
    p2io_ops.heartbeat_led(0);

 modtimer:
  mod_timer(&__get_cpu_var(heartbeat_timer), jiffies + event_scan_interval);
}


void __init setup_heartbeat(unsigned long interval, unsigned long offset, int ncpus)
{
  int cpu;
  struct timer_list *timer;

  printk(KERN_INFO "Heartbeat driver\n");

  event_scan_interval = interval;

  memset( &p2io_ops, 0, sizeof(p2io_ops));
  p2ioport_get_operations( &p2io_ops );

  if (p2io_ops.heartbeat_led) {
    for (cpu = 0; cpu < ncpus; ++cpu) {
      timer = &per_cpu(heartbeat_timer, cpu);
      setup_timer(timer, heartbeat_scan, 0);
      timer->expires = jiffies + offset;
      add_timer_on(timer, cpu);
      offset += interval;
    }
  } else {
    printk(KERN_INFO " Disable Heartbeat LED\n");
  }
}
