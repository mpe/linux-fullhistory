/* auto_irq.c: Auto-configure IRQ lines for linux. */
/*
    Written 1994 by Donald Becker.

    The author may be reached as becker@CESDIS.gsfc.nasa.gov, or C/O
    Center of Excellence in Space Data and Information Sciences
      Code 930.5, Goddard Space Flight Center, Greenbelt MD 20771

    This code is a general-purpose IRQ line detector for devices with
    jumpered IRQ lines.  If you can make the device raise an IRQ (and
    that IRQ line isn't already being used), these routines will tell
    you what IRQ line it's using -- perfect for those oh-so-cool boot-time
    device probes!

    To use this, first call autoirq_setup(timeout). TIMEOUT is how many
    'jiffies' (1/100 sec.) to detect other devices that have active IRQ lines,
    and can usually be zero at boot.  'autoirq_setup()' returns the bit
    vector of nominally-available IRQ lines (lines may be physically in-use,
    but not yet registered to a device).
    Next, set up your device to trigger an interrupt.
    Finally call autoirq_report(TIMEOUT) to find out which IRQ line was
    most recently active.  The TIMEOUT should usually be zero, but may
    be set to the number of jiffies to wait for a slow device to raise an IRQ.

    The idea of using the setup timeout to filter out bogus IRQs came from
    the serial driver.
*/


#ifdef version
static char *version=
"auto_irq.c:v1.11 Donald Becker (becker@cesdis.gsfc.nasa.gov)";
#endif

#include <linux/sched.h>
#include <linux/delay.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <linux/netdevice.h>

struct device *irq2dev_map[16] = {0, 0, /* ... zeroed */};

int irqs_busy = 0x2147;		/* The set of fixed IRQs (keyboard, timer, etc) */
int irqs_used = 0x0001;		/* The set of fixed IRQs sometimes enabled. */
int irqs_reserved = 0x0000;	/* An advisory "reserved" table. */
int irqs_shared = 0x0000;	/* IRQ lines "shared" among conforming cards.*/

static volatile int irq_number;	/* The latest irq number we actually found. */
static volatile int irq_bitmap; /* The irqs we actually found. */
static int irq_handled;		/* The irq lines we have a handler on. */

static void autoirq_probe(int irq, struct pt_regs * regs)
{
	irq_number = irq;
	set_bit(irq, (void *)&irq_bitmap);	/* irq_bitmap |= 1 << irq; */
	disable_irq(irq);
	return;
}

int autoirq_setup(int waittime)
{
	int i, mask;
	int timeout = jiffies + waittime;
	int boguscount = (waittime*loops_per_sec) / 100;

	irq_handled = 0;
	for (i = 0; i < 16; i++) {
		if (test_bit(i, &irqs_busy) == 0
			&& request_irq(i, autoirq_probe, SA_INTERRUPT, "irq probe") == 0)
			set_bit(i, (void *)&irq_handled);	/* irq_handled |= 1 << i;*/
	}
	/* Update our USED lists. */
	irqs_used |= ~irq_handled;
	irq_number = 0;
	irq_bitmap = 0;

	/* Hang out at least <waittime> jiffies waiting for bogus IRQ hits. */
	while (timeout > jiffies  &&  --boguscount > 0)
		;

	for (i = 0, mask = 0x01; i < 16; i++, mask <<= 1) {
		if (irq_bitmap & irq_handled & mask) {
			irq_handled &= ~mask;
#ifdef notdef
			printk(" Spurious interrupt on IRQ %d\n", i);
#endif
			free_irq(i);
		}
	}
	return irq_handled;
}

int autoirq_report(int waittime)
{
	int i;
	int timeout = jiffies+waittime;
	int boguscount = (waittime*loops_per_sec) / 100;

	/* Hang out at least <waittime> jiffies waiting for the IRQ. */

	while (timeout > jiffies  &&  --boguscount > 0)
		if (irq_number)
			break;

	/* Retract the irq handlers that we installed. */
	for (i = 0; i < 16; i++) {
		if (test_bit(i, (void *)&irq_handled))
			free_irq(i);
	}
	return irq_number;
}

/*
 * Local variables:
 *  compile-command: "gcc -DKERNEL -Wall -O6 -fomit-frame-pointer -I/usr/src/linux/net/tcp -c auto_irq.c"
 *  version-control: t
 *  kept-new-versions: 5
 *  c-indent-level: 4
 *  tab-width: 4
 * End:
 */
