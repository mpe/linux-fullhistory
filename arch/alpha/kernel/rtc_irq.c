/* RTC irq callbacks, 1999 Andrea Arcangeli <andrea@suse.de> */

#include <linux/init.h>
#include <linux/sched.h>
#include <linux/irq.h>

static void enable_rtc(unsigned int irq) { }
static unsigned int startup_rtc(unsigned int irq) { return 0; }
#define shutdown_rtc	enable_rtc
#define end_rtc		enable_rtc
#define ack_rtc		enable_rtc
#define disable_rtc	enable_rtc

void __init
init_RTC_irq(void)
{
	static struct hw_interrupt_type rtc_irq_type = { "RTC",
							 startup_rtc,
							 shutdown_rtc,
							 enable_rtc,
							 disable_rtc,
							 ack_rtc,
							 end_rtc };
	irq_desc[RTC_IRQ].status = IRQ_DISABLED;
	irq_desc[RTC_IRQ].handler = &rtc_irq_type;
}
