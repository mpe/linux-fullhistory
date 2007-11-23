/*
 *	linux/kernel/ioport.c
 *
 * This contains the io-permission bitmap code - written by obz, with changes
 * by Linus.
 */

#include <linux/sched.h>
#include <linux/kernel.h>

#include <sys/types.h>
#include <errno.h>

#define _IODEBUG

#ifdef IODEBUG
static char * ios(unsigned long l)
{
	static char str[33] = { '\0' };
	int i;
	unsigned long mask;

	for (i = 0, mask = 0x80000000; i < 32; ++i, mask >>= 1)
		str[i] = (l & mask) ? '1' : '0';
	return str;
}

static void dump_io_bitmap(void)
{
	int i, j;
	int numl = sizeof(current->tss.io_bitmap) >> 2;

	for (i = j = 0; j < numl; ++i)
	{
		printk("%4d [%3x]: ", 64*i, 64*i);
		printk("%s ", ios(current->tss.io_bitmap[j++]));
		if (j < numl)
			printk("%s", ios(current->tss.io_bitmap[j++]));
		printk("\n");
	}
}
#endif

/*
 * this changes the io permissions bitmap in the current task.
 */
int sys_ioperm(unsigned long from, unsigned long num, int turn_on)
{
	unsigned long froml, lindex, tnum, numl, rindex, mask;
	unsigned long *iop;

	froml = from >> 5;
	lindex = from & 0x1f;
	tnum = lindex + num;
	numl = (tnum + 0x1f) >> 5;
	rindex = tnum & 0x1f;

	if (!suser())
		return -EPERM;
	if (froml * 32 + tnum > sizeof(current->tss.io_bitmap) * 8 - 8)
		return -EINVAL;

#ifdef IODEBUG
	printk("io: from=%d num=%d %s\n", from, num, (turn_on ? "on" : "off"));
#endif

	if (numl) {
		iop = (unsigned long *)current->tss.io_bitmap + froml;
		if (lindex != 0) {
			mask = (~0 << lindex);
			if (--numl == 0 && rindex)
				mask &= ~(~0 << rindex);
			if (turn_on)
				*iop++ &= ~mask;
			else
				*iop++ |= mask;
		}
		if (numl) {
			if (rindex)
				--numl;
			mask = (turn_on ? 0 : ~0);
			while (numl--)
				*iop++ = mask;
			if (numl && rindex) {
				mask = ~(~0 << rindex);
				if (turn_on)
					*iop++ &= ~mask;
				else
					*iop++ |= mask;
			}
		}
	}
	return 0;
}
