/*
 * arch/alpha/boot/main.c
 *
 * Copyright (C) 1994, 1995 Linus Torvalds
 *
 * This file is the bootloader for the Linux/AXP kernel
 */
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/version.h>
#include <linux/mm.h>

#include <asm/system.h>
#include <asm/console.h>
#include <asm/hwrpb.h>

#include <stdarg.h>

extern int vsprintf(char *, const char *, va_list);
extern unsigned long switch_to_osf_pal(unsigned long nr,
	struct pcb_struct * pcb_va, struct pcb_struct * pcb_pa,
	unsigned long vptb, unsigned long *kstk);

int printk(const char * fmt, ...)
{
	va_list args;
	int i;
	static char buf[1024];

	va_start(args, fmt);
	i = vsprintf(buf, fmt, args);
	va_end(args);
	puts(buf,i);
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

	percpu = (struct percpu_struct *) (hwrpb.processor_offset + (unsigned long) &hwrpb),
		
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
	 * a1 = return address, but we give the asm the virtual addr of the PCB
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
	for (l = (unsigned long *) &hwrpb; l < (unsigned long *) &hwrpb.chksum; ++l)
		sum += *l;
	hwrpb.chksum = sum;

	printk("Ok (rev %lx)\n", rev);
	/* remove the old virtual page-table mapping */
	L1[1] = 0;
	invalidate_all();
}

extern int _end;

static inline long openboot(void)
{
	char bootdev[256];
	long result;

	result = dispatch(CCB_GET_ENV, ENV_BOOTED_DEV, bootdev, 255);
	if (result < 0)
		return result;
	return dispatch(CCB_OPEN, bootdev, result & 255);
}

static inline long close(long dev)
{
	return dispatch(CCB_CLOSE, dev);
}

static inline long load(long dev, unsigned long addr, unsigned long count)
{
	char bootfile[256];
	long result;

	result = dispatch(CCB_GET_ENV, ENV_BOOTED_FILE, bootfile, 255);
	if (result < 0)
		return result;
	result &= 255;
	bootfile[result] = '\0';
	if (result)
		printk("Boot file specification (%s) not implemented\n", bootfile);
	return dispatch(CCB_READ, dev, count, addr, BOOT_SIZE/512 + 1);
}

/*
 * Start the kernel.
 */
static void runkernel(void)
{
	__asm__ __volatile__(
		"bis %1,%1,$30\n\t"
		"bis %0,%0,$26\n\t"
		"ret ($26)"
		: /* no outputs: it doesn't even return */
		: "r" (START_ADDR),
		  "r" (PAGE_SIZE + INIT_STACK));
}

void start_kernel(void)
{
	long i;
	long dev;

	printk("Linux/AXP bootloader for Linux " UTS_RELEASE "\n");
	if (hwrpb.pagesize != 8192) {
		printk("Expected 8kB pages, got %ldkB\n", hwrpb.pagesize >> 10);
		return;
	}
	pal_init();
	dev = openboot();
	if (dev < 0) {
		printk("Unable to open boot device: %016lx\n", dev);
		return;
	}
	dev &= 0xffffffff;
	printk("Loading vmlinux ...");
	i = load(dev, START_ADDR, START_SIZE);
	close(dev);
	if (i != START_SIZE) {
		printk("Failed (%lx)\n", i);
		return;
	}
	printk(" Ok\nNow booting the kernel\n");
	runkernel();
	for (i = 0 ; i < 0x100000000 ; i++)
		/* nothing */;
	halt();
}
