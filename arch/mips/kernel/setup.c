/* $Id: setup.c,v 1.16 1999/06/17 13:25:47 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995  Linus Torvalds
 * Copyright (C) 1995, 1996, 1997, 1998  Ralf Baechle
 * Copyright (C) 1996  Stoned Elipot
 */
#include <linux/config.h>
#include <linux/errno.h>
#include <linux/hdreg.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/user.h>
#include <linux/utsname.h>
#include <linux/a.out.h>
#include <linux/tty.h>
#ifdef CONFIG_BLK_DEV_RAM
#include <linux/blk.h>
#endif
#include <linux/ide.h>
#ifdef CONFIG_RTC
#include <linux/ioport.h>
#include <linux/timex.h>
#endif

#include <asm/asm.h>
#include <asm/bootinfo.h>
#include <asm/cachectl.h>
#include <asm/io.h>
#include <asm/stackframe.h>
#include <asm/system.h>
#ifdef CONFIG_SGI
#include <asm/sgialib.h>
#endif

struct mips_cpuinfo boot_cpu_data = { NULL, NULL, 0 };

/*
 * Not all of the MIPS CPUs have the "wait" instruction available.  This
 * is set to true if it is available.  The wait instruction stops the
 * pipeline and reduces the power consumption of the CPU very much.
 */
char wait_available;

/*
 * Do we have a cyclecounter available?
 */
char cyclecounter_available;

/*
 * There are several bus types available for MIPS machines.  "RISC PC"
 * type machines have ISA, EISA, VLB or PCI available, DECstations
 * have Turbochannel or Q-Bus, SGI has GIO, there are lots of VME
 * boxes ...
 * This flag is set if a EISA slots are available.
 */
int EISA_bus = 0;

/*
 * Milo passes some information to the kernel that looks like as if it
 * had been returned by a Intel PC BIOS.  Milo doesn't fill the passed
 * drive_info and Linux can find out about this anyway, so I'm going to
 * remove this sometime.  screen_info contains information about the 
 * resolution of the text screen.  For VGA graphics based machine this
 * information is being use to continue the screen output just below
 * the BIOS printed text and with the same text resolution.
 */
struct screen_info screen_info = DEFAULT_SCREEN_INFO;

#ifdef CONFIG_BLK_DEV_FD
extern struct fd_ops no_fd_ops;
struct fd_ops *fd_ops;
#endif

#ifdef CONFIG_BLK_DEV_IDE
extern struct ide_ops no_ide_ops;
struct ide_ops *ide_ops;
#endif

extern struct rtc_ops no_rtc_ops;
struct rtc_ops *rtc_ops;

extern struct kbd_ops no_kbd_ops;
struct kbd_ops *kbd_ops;

/*
 * Setup information
 *
 * These are initialized so they are in the .data section
 */
unsigned long mips_memory_upper = KSEG0; /* this is set by kernel_entry() */
unsigned long mips_cputype = CPU_UNKNOWN;
unsigned long mips_machtype = MACH_UNKNOWN;
unsigned long mips_machgroup = MACH_GROUP_UNKNOWN;

unsigned char aux_device_present;
extern int _end;

extern char empty_zero_page[PAGE_SIZE];

/*
 * This is set up by the setup-routine at boot-time
 */
#define PARAM	empty_zero_page

static char command_line[CL_SIZE] = { 0, };
       char saved_command_line[CL_SIZE];
extern char arcs_cmdline[CL_SIZE];

/*
 * The board specific setup routine sets irq_setup to point to a board
 * specific setup routine.
 */
void (*irq_setup)(void);

/*
 * mips_io_port_base is the begin of the address space to which x86 style
 * I/O ports are mapped.
 */
unsigned long mips_io_port_base;

/*
 * isa_slot_offset is the address where E(ISA) busaddress 0 is is mapped
 * for the processor.
 */
unsigned long isa_slot_offset;

__initfunc(static void default_irq_setup(void))
{
	panic("Unknown machtype in init_IRQ");
}

__initfunc(void setup_arch(char **cmdline_p,
           unsigned long * memory_start_p, unsigned long * memory_end_p))
{
	unsigned long memory_end;
#ifdef CONFIG_BLK_DEV_INITRD
	unsigned long tmp;
	unsigned long *initrd_header;
#endif
	void baget_setup(void);
	void cobalt_setup(void);
	void decstation_setup(void);
	void deskstation_setup(void);
	void jazz_setup(void);
	void sni_rm200_pci_setup(void);
	void sgi_setup(void);

	/* Save defaults for configuration-dependent routines.  */
	irq_setup = default_irq_setup;

#ifdef CONFIG_BLK_DEV_FD
	fd_ops = &no_fd_ops;
#endif

#ifdef CONFIG_BLK_DEV_IDE
	ide_ops = &no_ide_ops;
#endif

	rtc_ops = &no_rtc_ops;
	kbd_ops = &no_kbd_ops;

	switch(mips_machgroup)
	{
#ifdef CONFIG_BAGET_MIPS
	case MACH_GROUP_UNKNOWN: 
		baget_setup();
		break;
#endif
#ifdef CONFIG_COBALT_MICRO_SERVER
	case MACH_GROUP_COBALT:
		cobalt_setup();
		break;
#endif
#ifdef CONFIG_DECSTATION
	case MACH_GROUP_DEC:
		decstation_setup();
		break;
#endif
#ifdef CONFIG_MIPS_JAZZ
	case MACH_GROUP_JAZZ:
		jazz_setup();
		break;
#endif
#ifdef CONFIG_SGI
	case MACH_GROUP_SGI:
		sgi_setup();
		break;
#endif
#ifdef CONFIG_SNI_RM200_PCI
	case MACH_GROUP_SNI_RM:
		sni_rm200_pci_setup();
		break;
#endif
	default:
		panic("Unsupported architecture");
	}

	memory_end = mips_memory_upper;
	/*
	 * Due to prefetching and similar mechanism the CPU sometimes
	 * generates addresses beyond the end of memory.  We leave the size
	 * of one cache line at the end of memory unused to make shure we
	 * don't catch this type of bus errors.
	 */
	memory_end -= 128;
	memory_end &= PAGE_MASK;

        strncpy (command_line, arcs_cmdline, CL_SIZE);
	memcpy(saved_command_line, command_line, CL_SIZE);
	saved_command_line[CL_SIZE-1] = '\0';

	*cmdline_p = command_line;
	*memory_start_p = (unsigned long) &_end;
	*memory_end_p = memory_end;

#ifdef CONFIG_BLK_DEV_INITRD
	tmp = (((unsigned long)&_end + PAGE_SIZE-1) & PAGE_MASK) - 8;
	if (tmp < (unsigned long)&_end)
		tmp += PAGE_SIZE;
	initrd_header = (unsigned long *)tmp;
	if (initrd_header[0] == 0x494E5244) {
		initrd_start = (unsigned long)&initrd_header[2];
		initrd_end = initrd_start + initrd_header[1];
		initrd_below_start_ok = 1;
		if (initrd_end > memory_end) {
			printk("initrd extends beyond end of memory "
			       "(0x%08lx > 0x%08lx)\ndisabling initrd\n",
			       initrd_end,memory_end);
			initrd_start = 0;
		} else
			*memory_start_p = initrd_end;
	}
#endif
}
