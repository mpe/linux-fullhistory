/* Support for the latches on the old Archimedes which control the floppy,
 * hard disc and printer
 *
 * (c) David Alan Gilbert 1995/1996
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/hardware.h>

static unsigned char latch_a_copy;
static unsigned char latch_b_copy;

/* newval=(oldval & ~mask)|newdata */
void oldlatch_aupdate(unsigned char mask,unsigned char newdata)
{
	if (machine_is_arc()) {
		latch_a_copy = (latch_a_copy & ~mask) | newdata;

		printk("Latch: A = 0x%02x\n", latch_a_copy);

		outb(latch_a_copy, LATCHAADDR);
	} else
		BUG();
}


/* newval=(oldval & ~mask)|newdata */
void oldlatch_bupdate(unsigned char mask,unsigned char newdata)
{
	if (machine_is_arc()) {
		latch_b_copy = (latch_b_copy & ~mask) | newdata;

		printk("Latch: B = 0x%02x\n", latch_b_copy);

		outb(latch_b_copy, LATCHBADDR);
	} else
		BUG();
}

static void __init oldlatch_init(void)
{
	if (machine_is_arc()) {
		oldlatch_aupdate(0xff, 0xff);
		/* Thats no FDC reset...*/
		oldlatch_bupdate(0xff, LATCHB_FDCRESET);
	}
}

initcall(oldlatch_init);

EXPORT_SYMBOL(oldlatch_aupdate);
EXPORT_SYMBOL(oldlatch_bupdate);
