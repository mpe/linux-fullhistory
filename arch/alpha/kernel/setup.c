/*
 *  linux/arch/alpha/kernel/setup.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 */

/*
 * bootup setup stuff..
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/ldt.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/tty.h>

#include <asm/segment.h>
#include <asm/system.h>
#include <asm/hwrpb.h>
#include <asm/io.h>

unsigned char aux_device_present;

/*
 * The format of "screen_info" is strange, and due to early
 * i386-setup code. This is just enough to make the console
 * code think we're on a EGA+ colour display.
 */
struct screen_info screen_info = {
	0, 0,			/* orig-x, orig-y */
	0, 0,			/* unused */
	0,			/* orig-video-page */
	0,			/* orig-video-mode */
	80,			/* orig-video-cols */
	0,0,0,			/* ega_ax, ega_bx, ega_cx */
	25			/* orig-video-lines */
};

unsigned long bios32_init(unsigned long memory_start, unsigned long memory_end)
{
	return memory_start;
}

static unsigned long find_end_memory(void)
{
	int i;
	unsigned long high = 0;
	struct memclust_struct * cluster;
	struct memdesc_struct * memdesc;

	memdesc = (struct memdesc_struct *) (INIT_HWRPB->mddt_offset + (unsigned long) INIT_HWRPB);
	cluster = memdesc->cluster;
	for (i = memdesc->numclusters ; i > 0; i--, cluster++) {
		unsigned long tmp;
		tmp = (cluster->start_pfn + cluster->numpages) << PAGE_SHIFT;
		if (tmp > high)
			high = tmp;
	}
	/* round it up to an even number of pages.. */
	high = (high + PAGE_SIZE) & (PAGE_MASK*2);
	return PAGE_OFFSET + high;
}

void setup_arch(char **cmdline_p,
	unsigned long * memory_start_p, unsigned long * memory_end_p)
{
	static char cmdline[] = "";
	extern int _end;

	ROOT_DEV = 0x0200;	/* fd0 */
	aux_device_present = 0xaa;
	*cmdline_p = cmdline;
	*memory_start_p = (unsigned long) &_end;
	*memory_end_p = find_end_memory();
}

asmlinkage int sys_ioperm(unsigned long from, unsigned long num, int on)
{
	return -EIO;
}
