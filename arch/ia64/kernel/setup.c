/*
 * Architecture-specific setup.
 *
 * Copyright (C) 1998-2000 Hewlett-Packard Co
 * Copyright (C) 1998-2000 David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 1998, 1999 Stephane Eranian <eranian@hpl.hp.com>
 * Copyright (C) 2000, Rohit Seth <rohit.seth@intel.com>
 * Copyright (C) 1999 VA Linux Systems
 * Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 *
 * 04/04/00 D.Mosberger renamed cpu_initialized to cpu_online_map
 * 03/31/00 R.Seth	cpu_initialized and current->processor fixes
 * 02/04/00 D.Mosberger	some more get_cpuinfo fixes...
 * 02/01/00 R.Seth	fixed get_cpuinfo for SMP
 * 01/07/99 S.Eranian	added the support for command line argument
 * 06/24/99 W.Drummond	added boot_cpu_data.
 */
#include <linux/config.h>
#include <linux/init.h>

#include <linux/bootmem.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/reboot.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/threads.h>
#include <linux/console.h>

#include <asm/acpi-ext.h>
#include <asm/ia32.h>
#include <asm/page.h>
#include <asm/machvec.h>
#include <asm/processor.h>
#include <asm/sal.h>
#include <asm/system.h>
#include <asm/efi.h>
#include <asm/mca.h>

#ifdef CONFIG_BLK_DEV_RAM
# include <linux/blk.h>
#endif

extern char _end;

/* cpu_data[bootstrap_processor] is data for the bootstrap processor: */
struct cpuinfo_ia64 cpu_data[NR_CPUS];

unsigned long ia64_cycles_per_usec;
struct ia64_boot_param ia64_boot_param;
struct screen_info screen_info;
/* This tells _start which CPU is booting.  */
int cpu_now_booting = 0;

#ifdef CONFIG_SMP
volatile unsigned long cpu_online_map;
#endif

#define COMMAND_LINE_SIZE	512

char saved_command_line[COMMAND_LINE_SIZE]; /* used in proc filesystem */

static int
find_max_pfn (unsigned long start, unsigned long end, void *arg)
{
	unsigned long *max_pfn = arg, pfn;

	pfn = (PAGE_ALIGN(end - 1) - PAGE_OFFSET) >> PAGE_SHIFT;
	if (pfn > *max_pfn)
		*max_pfn = pfn;
	return 0;
}

static int
free_available_memory (unsigned long start, unsigned long end, void *arg)
{
#	define KERNEL_END	((unsigned long) &_end)
#	define MIN(a,b)		((a) < (b) ? (a) : (b))
#	define MAX(a,b)		((a) > (b) ? (a) : (b))
	unsigned long range_start, range_end;

	range_start = MIN(start, KERNEL_START);
	range_end   = MIN(end, KERNEL_START);

	/*
	 * XXX This should not be necessary, but the bootmem allocator
	 * is broken and fails to work correctly when the starting
	 * address is not properly aligned.
	 */
	range_start = PAGE_ALIGN(range_start);

	if (range_start < range_end)
		free_bootmem(__pa(range_start), range_end - range_start);

	range_start = MAX(start, KERNEL_END);
	range_end   = MAX(end, KERNEL_END);

	/*
	 * XXX This should not be necessary, but the bootmem allocator
	 * is broken and fails to work correctly when the starting
	 * address is not properly aligned.
	 */
	range_start = PAGE_ALIGN(range_start);

	if (range_start < range_end)
		free_bootmem(__pa(range_start), range_end - range_start);

	return 0;
}

void __init
setup_arch (char **cmdline_p)
{
	unsigned long max_pfn, bootmap_start, bootmap_size;

	unw_init();

	/*
	 * The secondary bootstrap loader passes us the boot
	 * parameters at the beginning of the ZERO_PAGE, so let's
	 * stash away those values before ZERO_PAGE gets cleared out.
	 */
	memcpy(&ia64_boot_param, (void *) ZERO_PAGE_ADDR, sizeof(ia64_boot_param));

	efi_init();

	max_pfn = 0;
	efi_memmap_walk(find_max_pfn, &max_pfn);

	/*
	 * This is wrong, wrong, wrong.  Darn it, you'd think if they
	 * change APIs, they'd do things for the better.  Grumble...
	 */
	bootmap_start = PAGE_ALIGN(__pa(&_end));
	if (ia64_boot_param.initrd_size)
		bootmap_start = PAGE_ALIGN(bootmap_start + ia64_boot_param.initrd_size);
	bootmap_size = init_bootmem(bootmap_start >> PAGE_SHIFT, max_pfn);

	efi_memmap_walk(free_available_memory, 0);

	reserve_bootmem(bootmap_start, bootmap_size);
#ifdef CONFIG_BLK_DEV_INITRD
	initrd_start = ia64_boot_param.initrd_start;
	if (initrd_start) {
		initrd_end = initrd_start+ia64_boot_param.initrd_size;
		printk("Initial ramdisk at: 0x%p (%lu bytes)\n",
		       (void *) initrd_start, ia64_boot_param.initrd_size);
		reserve_bootmem(virt_to_phys(initrd_start), ia64_boot_param.initrd_size);
	}
#endif
#if 0
	/* XXX fix me */
	init_mm.start_code = (unsigned long) &_stext;
	init_mm.end_code = (unsigned long) &_etext;
	init_mm.end_data = (unsigned long) &_edata;
	init_mm.brk = (unsigned long) &_end;

	code_resource.start = virt_to_bus(&_text);
	code_resource.end = virt_to_bus(&_etext) - 1;
	data_resource.start = virt_to_bus(&_etext);
	data_resource.end = virt_to_bus(&_edata) - 1;
#endif

	/* process SAL system table: */
	ia64_sal_init(efi.sal_systab);

	*cmdline_p = __va(ia64_boot_param.command_line);
	strncpy(saved_command_line, *cmdline_p, sizeof(saved_command_line));
	saved_command_line[COMMAND_LINE_SIZE-1] = '\0';		/* for safety */

	printk("args to kernel: %s\n", *cmdline_p);

#ifdef CONFIG_SMP
	bootstrap_processor = hard_smp_processor_id();
	current->processor = bootstrap_processor;
#endif
	cpu_init();	/* initialize the bootstrap CPU */

	if (efi.acpi) {
		/* Parse the ACPI tables */
		acpi_parse(efi.acpi);
	}

#ifdef CONFIG_IA64_GENERIC
	machvec_init(acpi_get_sysname());
#endif

#ifdef CONFIG_VT
# if defined(CONFIG_VGA_CONSOLE)
	conswitchp = &vga_con;
# elif defined(CONFIG_DUMMY_CONSOLE)
	conswitchp = &dummy_con;
# endif
#endif

#ifdef CONFIG_IA64_MCA
	/* enable IA-64 Machine Check Abort Handling */
	ia64_mca_init();
#endif
	paging_init();
	platform_setup(cmdline_p);
}

/*
 * Display cpu info for all cpu's.
 */
int
get_cpuinfo (char *buffer)
{
	char family[32], model[32], features[128], *cp, *p = buffer;
	struct cpuinfo_ia64 *c;
	unsigned long mask;

	for (c = cpu_data; c < cpu_data + NR_CPUS; ++c) {
#ifdef CONFIG_SMP
		if (!(cpu_online_map & (1UL << (c - cpu_data))))
			continue;
#endif

		mask = c->features;

		if (c->family == 7)
			memcpy(family, "IA-64", 6);
		else
			sprintf(family, "%u", c->family);

		switch (c->model) {
		      case 0:	strcpy(model, "Itanium"); break;
		      default:	sprintf(model, "%u", c->model); break;
		}

		/* build the feature string: */
		memcpy(features, " standard", 10);
		cp = features;
		if (mask & 1) {
			strcpy(cp, " branchlong");
			cp = strchr(cp, '\0');
			mask &= ~1UL;
		}
		if (mask)
			sprintf(cp, " 0x%lx", mask);

		p += sprintf(p,
			     "CPU# %lu\n"
			     "\tvendor     : %s\n"
			     "\tfamily     : %s\n"
			     "\tmodel      : %s\n"
			     "\trevision   : %u\n"
			     "\tarchrev    : %u\n"
			     "\tfeatures   :%s\n"	/* don't change this---it _is_ right! */
			     "\tcpu number : %lu\n"
			     "\tcpu regs   : %u\n"
			     "\tcpu MHz    : %lu.%06lu\n"
			     "\titc MHz    : %lu.%06lu\n"
			     "\tBogoMIPS   : %lu.%02lu\n\n",
			     c - cpu_data, c->vendor, family, model, c->revision, c->archrev,
			     features,
			     c->ppn, c->number, c->proc_freq / 1000000, c->proc_freq % 1000000,
			     c->itc_freq / 1000000, c->itc_freq % 1000000,
			     loops_per_sec() / 500000, (loops_per_sec() / 5000) % 100);
        }
	return p - buffer;
}

void
identify_cpu (struct cpuinfo_ia64 *c)
{
	union {
		unsigned long bits[5];
		struct {
			/* id 0 & 1: */
			char vendor[16];

			/* id 2 */
			u64 ppn;		/* processor serial number */

			/* id 3: */
			unsigned number		:  8;
			unsigned revision	:  8;
			unsigned model		:  8;
			unsigned family		:  8;
			unsigned archrev	:  8;
			unsigned reserved	: 24;

			/* id 4: */
			u64 features;
		} field;
	} cpuid;
	pal_vm_info_1_u_t vm1;
	pal_vm_info_2_u_t vm2;
	pal_status_t status;
	unsigned long impl_va_msb = 50, phys_addr_size = 44;	/* Itanium defaults */
	int i;

	for (i = 0; i < 5; ++i)
		cpuid.bits[i] = ia64_get_cpuid(i);

	memset(c, 0, sizeof(struct cpuinfo_ia64));

	memcpy(c->vendor, cpuid.field.vendor, 16);
	c->ppn = cpuid.field.ppn;
	c->number = cpuid.field.number;
	c->revision = cpuid.field.revision;
	c->model = cpuid.field.model;
	c->family = cpuid.field.family;
	c->archrev = cpuid.field.archrev;
	c->features = cpuid.field.features;

	status = ia64_pal_vm_summary(&vm1, &vm2);
	if (status == PAL_STATUS_SUCCESS) {
#if 1
		/*
		 * XXX the current PAL code returns IMPL_VA_MSB==60, which is dead-wrong.
		 * --davidm 00/05/26
		 s*/
		impl_va_msb = 50;
#else
		impl_va_msb = vm2.pal_vm_info_2_s.impl_va_msb;
#endif
		phys_addr_size = vm1.pal_vm_info_1_s.phys_add_size;
	}
	printk("processor implements %lu virtual and %lu physical address bits\n",
	       impl_va_msb + 1, phys_addr_size);
	c->unimpl_va_mask = ~((7L<<61) | ((1L << (impl_va_msb + 1)) - 1));
	c->unimpl_pa_mask = ~((1L<<63) | ((1L << phys_addr_size) - 1));

#ifdef CONFIG_IA64_SOFTSDV_HACKS
	/* BUG: SoftSDV doesn't support the cpuid registers. */
	if (c->vendor[0] == '\0') 
		memcpy(c->vendor, "Intel", 6);
#endif
}

/*
 * cpu_init() initializes state that is per-CPU.  This function acts
 * as a 'CPU state barrier', nothing should get across.
 */
void
cpu_init (void)
{
	extern void __init ia64_rid_init (void);
	extern void __init ia64_tlb_init (void);

	identify_cpu(&my_cpu_data);

	/* Clear the stack memory reserved for pt_regs: */
	memset(ia64_task_regs(current), 0, sizeof(struct pt_regs));

	/*
	 * Initialize default control register to defer speculative
	 * faults.  On a speculative load, we want to defer access
	 * right, key miss, and key permission faults.  We currently
	 * do NOT defer TLB misses, page-not-present, access bit, or
	 * debug faults but kernel code should not rely on any
	 * particular setting of these bits.
	 */
	ia64_set_dcr(IA64_DCR_DR | IA64_DCR_DK | IA64_DCR_DX | IA64_DCR_PP);
	ia64_set_fpu_owner(0);		/* initialize ar.k5 */

	atomic_inc(&init_mm.mm_count);
	current->active_mm = &init_mm;

	ia64_rid_init();
	ia64_tlb_init();

#ifdef	CONFIG_IA32_SUPPORT
	/* initialize global ia32 state - CR0 and CR4 */
	__asm__("mov ar.cflg = %0"
		: /* no outputs */
		: "r" (((ulong) IA32_CR4 << 32) | IA32_CR0));
#endif

#ifdef CONFIG_SMP
	normal_xtp();
#endif
}
