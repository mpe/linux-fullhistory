/*
 * arch/alpha/boot/bootp.c
 *
 * Copyright (C) 1997 Jay Estabrook
 *
 * This file is used for creating a bootp file for the Linux/AXP kernel
 *
 * based significantly on the arch/alpha/boot/main.c of Linus Torvalds
 */
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/version.h>
#include <linux/mm.h>
#include <linux/config.h>

#include <asm/system.h>
#include <asm/console.h>
#include <asm/hwrpb.h>
#include <asm/pgtable.h>
#include <asm/io.h>

#include <stdarg.h>

#include "ksize.h"

extern int vsprintf(char *, const char *, va_list);
extern unsigned long switch_to_osf_pal(unsigned long nr,
	struct pcb_struct * pcb_va, struct pcb_struct * pcb_pa,
	unsigned long vptb, unsigned long *kstk);

int printk(const char * fmt, ...)
{
	va_list args;
	int i, j, written, remaining, num_nl;
	static char buf[1024];
	char * str;

	va_start(args, fmt);
	i = vsprintf(buf, fmt, args);
	va_end(args);

	/* expand \n into \r\n: */

	num_nl = 0;
	for (j = 0; j < i; ++j) {
	    if (buf[j] == '\n')
	    	++num_nl;
	}
	remaining = i + num_nl;
	for (j = i - 1; j >= 0; --j) {
	    buf[j + num_nl] = buf[j];
	    if (buf[j] == '\n') {
	    	--num_nl;
		buf[j + num_nl] = '\r';
	    }
	}

	str = buf;
	do {
	    written = puts(str, remaining);
	    remaining -= written;
	    str += written;
	} while (remaining > 0);
	return i;
}

#define hwrpb (*INIT_HWRPB)

/*
 * Find a physical address of a virtual object..
 *
 * This is easy using the virtual page table address.
 */
struct pcb_struct * find_pa(unsigned long *vptb, struct pcb_struct * pcb)
{
	unsigned long address = (unsigned long) pcb;
	unsigned long result;

	result = vptb[address >> 13];
	result >>= 32;
	result <<= 13;
	result |= address & 0x1fff;
	return (struct pcb_struct *) result;
}	

/*
 * This function moves into OSF/1 pal-code, and has a temporary
 * PCB for that. The kernel proper should replace this PCB with
 * the real one as soon as possible.
 *
 * The page table muckery in here depends on the fact that the boot
 * code has the L1 page table identity-map itself in the second PTE
 * in the L1 page table. Thus the L1-page is virtually addressable
 * itself (through three levels) at virtual address 0x200802000.
 *
 * As we don't want it there anyway, we also move the L1 self-map
 * up as high as we can, so that the last entry in the L1 page table
 * maps the page tables.
 *
 * As a result, the OSF/1 pal-code will instead use a virtual page table
 * map located at 0xffffffe00000000.
 */
#define pcb_va ((struct pcb_struct *) 0x20000000)
#define old_vptb (0x0000000200000000UL)
#define new_vptb (0xfffffffe00000000UL)
void pal_init(void)
{
	unsigned long i, rev, sum;
	unsigned long *L1, *l;
	struct percpu_struct * percpu;
	struct pcb_struct * pcb_pa;

	/* Find the level 1 page table and duplicate it in high memory */
	L1 = (unsigned long *) 0x200802000UL; /* (1<<33 | 1<<23 | 1<<13) */
	L1[1023] = L1[1];

	percpu = (struct percpu_struct *)
			(hwrpb.processor_offset + (unsigned long) &hwrpb),
		
	pcb_va->ksp = 0;
	pcb_va->usp = 0;
	pcb_va->ptbr = L1[1] >> 32;
	pcb_va->asn = 0;
	pcb_va->pcc = 0;
	pcb_va->unique = 0;
	pcb_va->flags = 1;
	pcb_pa = find_pa((unsigned long *) old_vptb, pcb_va);
	printk("Switching to OSF PAL-code .. ");
	/*
	 * a0 = 2 (OSF)
	 * a1 = return address, but we give the asm the vaddr of the PCB
	 * a2 = physical addr of PCB
	 * a3 = new virtual page table pointer
	 * a4 = KSP (but we give it 0, asm sets it)
	 */
	i = switch_to_osf_pal(
		2,
		pcb_va,
		pcb_pa,
		new_vptb,
		0);
	if (i) {
		printk("failed, code %ld\n", i);
		halt();
	}
	rev = percpu->pal_revision = percpu->palcode_avail[2];

	hwrpb.vptb = new_vptb;

	/* update checksum: */
	sum = 0;
	for (l = (unsigned long *) &hwrpb;
	     l < (unsigned long *) &hwrpb.chksum;
	     ++l)
		sum += *l;
	hwrpb.chksum = sum;

	printk("Ok (rev %lx)\n", rev);
	/* remove the old virtual page-table mapping */
	L1[1] = 0;

	tbia(); /* do it directly in case we are SMP */
}

static inline long load(unsigned long dst,
			unsigned long src,
			unsigned long count)
{
	extern void * memcpy(void *, const void *, size_t);

	memcpy((void *)dst, (void *)src, count);
	return count;
}

/*
 * Start the kernel.
 */
static void runkernel(void)
{
	__asm__ __volatile__(
		"bis %1,%1,$30\n\t"
		"bis %0,%0,$27\n\t"
		"jmp ($27)"
		: /* no outputs: it doesn't even return */
		: "r" (START_ADDR),
		  "r" (PAGE_SIZE + INIT_STACK));
}

extern char _end;
#define KERNEL_ORIGIN \
	((((unsigned long)&_end) + 511) & ~511)

void start_kernel(void)
{
	static long i;
	static int nbytes;
	/*
	 * note that this crufty stuff with static and envval and envbuf
	 * is because:
	 *
	 * 1. frequently, the stack is is short, and we don't want to overrun;
	 * 2. frequently the stack is where we are going to copy the kernel to;
	 * 3. a certain SRM console required the GET_ENV output to stack.
	 */
	static char envval[256];
	char envbuf[256];

	printk("Linux/AXP bootp loader for Linux " UTS_RELEASE "\n");
	if (hwrpb.pagesize != 8192) {
		printk("Expected 8kB pages, got %ldkB\n",
		       hwrpb.pagesize >> 10);
		return;
	}
	pal_init();

	nbytes = dispatch(CCB_GET_ENV, ENV_BOOTED_OSFLAGS,
			  envbuf, sizeof(envbuf));
	if (nbytes < 0 || nbytes >= sizeof(envbuf)) {
		nbytes = 0;
	}
	envbuf[nbytes] = '\0';
	memcpy(envval, envbuf, nbytes+1);
	printk("Loading the kernel...'%s'\n", envval);

	/* NOTE: *no* callbacks or printouts from here on out!!! */

#if 1
	/*
	 * this is a hack, as some consoles seem to get virtual 20000000
	 * (ie where the SRM console puts the kernel bootp image) memory
	 * overlapping physical 310000 memory, which causes real problems
	 * when attempting to copy the former to the latter... :-(
	 *
	 * so, we first move the kernel virtual-to-physical way above where
	 * we physically want the kernel to end up, then copy it from there
	 * to its final resting place... ;-}
	 *
	 * sigh...
	 */

        i = load(START_ADDR+(4*KERNEL_SIZE), KERNEL_ORIGIN, KERNEL_SIZE);
        i = load(START_ADDR, START_ADDR+(4*KERNEL_SIZE), KERNEL_SIZE);
#else
	i = load(START_ADDR, KERNEL_ORIGIN, KERNEL_SIZE);
#endif

	strcpy((char*)ZERO_PAGE, envval);

	runkernel();

	for (i = 0 ; i < 0x100000000 ; i++)
		/* nothing */;
	halt();
}
