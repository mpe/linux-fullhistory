/*
 *  linux/arch/ppc/kernel/apus_setup.c
 *
 *  Copyright (C) 1998  Jesper Skov
 *
 *  Basically what is needed to replace functionality found in
 *  arch/m68k allowing Amiga drivers to work under APUS.
 *  Bits of code and/or ideas from arch/m68k and arch/ppc files.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kd.h>
#include <linux/init.h>

#include <asm/setup.h>
#include <asm/amigahw.h>
#include <asm/amigappc.h>
#include <asm/pgtable.h>
#include <asm/io.h>

unsigned long m68k_machtype;
char debug_device[6] = "";

void (*mach_sched_init) (void (*handler)(int, void *, struct pt_regs *)) __initdata;
/* machine dependent keyboard functions */
int (*mach_keyb_init) (void) __initdata;
int (*mach_kbdrate) (struct kbd_repeat *) = NULL;
void (*mach_kbd_leds) (unsigned int) = NULL;
/* machine dependent irq functions */
void (*mach_init_IRQ) (void) __initdata;
void (*(*mach_default_handler)[]) (int, void *, struct pt_regs *) = NULL;
void (*mach_get_model) (char *model) = NULL;
int (*mach_get_hardware_list) (char *buffer) = NULL;
int (*mach_get_irq_list) (char *) = NULL;
void (*mach_process_int) (int, struct pt_regs *) = NULL;
/* machine dependent timer functions */
unsigned long (*mach_gettimeoffset) (void);
void (*mach_gettod) (int*, int*, int*, int*, int*, int*);
int (*mach_hwclk) (int, struct hwclk_time*) = NULL;
int (*mach_set_clock_mmss) (unsigned long) = NULL;
void (*mach_reset)( void );
long mach_max_dma_address = 0x00ffffff; /* default set to the lower 16MB */
#if defined(CONFIG_AMIGA_FLOPPY) || defined(CONFIG_ATARI_FLOPPY)
void (*mach_floppy_setup) (char *, int *) __initdata = NULL;
void (*mach_floppy_eject) (void) = NULL;
#endif
#ifdef CONFIG_HEARTBEAT
void (*mach_heartbeat) (int) = NULL;
#endif

extern unsigned long amiga_model;
extern unsigned decrementer_count;/* count value for 1e6/HZ microseconds */
extern unsigned count_period_num; /* 1 decrementer count equals */
extern unsigned count_period_den; /* count_period_num / count_period_den us */

extern struct mem_info memory[NUM_MEMINFO];/* memory description */

extern void amiga_floppy_setup(char *, int *);
extern void config_amiga(void);

static int __60nsram = 0;

/*********************************************************** SETUP */
/* From arch/m68k/kernel/setup.c. */
__initfunc(void apus_setup_arch(unsigned long * memory_start_p,
				unsigned long * memory_end_p))
{
	extern char cmd_line[];
	int i;
	char *p, *q;

	/* Parse the command line for arch-specific options.
	 * For the m68k, this is currently only "debug=xxx" to enable printing
	 * certain kernel messages to some machine-specific device.  */
	for( p = cmd_line; p && *p; ) {
	    i = 0;
	    if (!strncmp( p, "debug=", 6 )) {
		    strncpy( debug_device, p+6, sizeof(debug_device)-1 );
		    debug_device[sizeof(debug_device)-1] = 0;
		    if ((q = strchr( debug_device, ' ' ))) *q = 0;
		    i = 1;
	    } else if (!strncmp( p, "60nsram", 7 )) {
		    APUS_WRITE (APUS_REG_WAITSTATE, 
				REGWAITSTATE_SETRESET
				|REGWAITSTATE_PPCR
				|REGWAITSTATE_PPCW);
		    __60nsram = 1;
		    i = 1;
	    }

	    if (i) {
		/* option processed, delete it */
		if ((q = strchr( p, ' ' )))
		    strcpy( p, q+1 );
		else
		    *p = 0;
	    } else {
		if ((p = strchr( p, ' ' ))) ++p;
	    }
	}

	config_amiga();
}


void get_current_tb(unsigned long long *time)
{
	__asm __volatile ("1:mftbu 4      \n\t"
			  "  mftb  5      \n\t"
			  "  mftbu 6      \n\t"
			  "  cmpw  4,6    \n\t"
			  "  bne   1b     \n\t"
			  "  stw   4,0(%0)\n\t"
			  "  stw   5,4(%0)\n\t"
			  : 
			  : "r" (time)
			  : "r4", "r5", "r6");
}


void apus_calibrate_decr(void)
{
	int freq, divisor;

	/* This algorithm for determining the bus speed was
           contributed by Ralph Schmidt. */
	unsigned long long start, stop;
	int bus_speed;

	{
		unsigned long loop = amiga_eclock / 10;

		get_current_tb (&start);
		while (loop--) {
			unsigned char tmp;

			tmp = ciaa.pra;
		}
		get_current_tb (&stop);
	}

	bus_speed = (((unsigned long)(stop-start))*10*4) / 1000000;
	if (AMI_1200 == amiga_model)
		bus_speed /= 2;

	if ((bus_speed >= 47) && (bus_speed < 53)) {
		bus_speed = 50;
		freq = 12500000;
	} else if ((bus_speed >= 57) && (bus_speed < 63)) {
		bus_speed = 60;
		freq = 15000000;
	} else if ((bus_speed >= 63) && (bus_speed < 69)) {
		bus_speed = 66;
		freq = 16500000;
	} else {
		printk ("APUS: Unable to determine bus speed (%d). "
			"Defaulting to 50MHz", bus_speed);
		bus_speed = 50;
		freq = 12500000;
	}

	/* Ease diagnostics... */
	{
		extern int __map_without_bats;

		printk ("APUS: BATs=%d, BUS=%dMHz, RAM=%dns\n",
			(__map_without_bats) ? 0 : 1,
			bus_speed,
			(__60nsram) ? 60 : 70);

		/* print a bit more if asked politely... */
		if (!(ciaa.pra & 0x40)){
			extern unsigned int bat_addrs[4][3];
			int b;
			for (b = 0; b < 4; ++b) {
				printk ("APUS: BAT%d ", b);
				printk ("%08x-%08x -> %08x\n",
					bat_addrs[b][0],
					bat_addrs[b][1],
					bat_addrs[b][2]);
			}
		}

	}

	freq *= 60;	/* try to make freq/1e6 an integer */
        divisor = 60;
        printk("time_init: decrementer frequency = %d/%d\n", freq, divisor);
        decrementer_count = freq / HZ / divisor;
        count_period_num = divisor;
        count_period_den = freq / 1000000;
}

void arch_gettod(int *year, int *mon, int *day, int *hour,
		 int *min, int *sec)
{
	if (mach_gettod)
		mach_gettod(year, mon, day, hour, min, sec);
	else
		*year = *mon = *day = *hour = *min = *sec = 0;
}

/*********************************************************** FLOPPY */
#if defined(CONFIG_AMIGA_FLOPPY) || defined(CONFIG_ATARI_FLOPPY)
__initfunc(void floppy_setup(char *str, int *ints))
{
	if (mach_floppy_setup)
		mach_floppy_setup (str, ints);
}

void floppy_eject(void)
{
	if (mach_floppy_eject)
		mach_floppy_eject();
}
#endif

/*********************************************************** MEMORY */
extern void
map_page(struct task_struct *tsk, unsigned long va,
	 unsigned long pa, int flags);

#define KMAP_MAX 8
static unsigned long kmap_chunks[KMAP_MAX*3];
static int kmap_chunk_count = 0;

/* Based on arch/ppc/mm/init.c:ioremap() which maps the address range
   to the same virtual address as the physical address - which may
   cause problems since Z3 IO space is not the same as PCI/ISA.
   This should be rewritten to something more like the m68k version. */
unsigned long kernel_map (unsigned long phys_addr, unsigned long size,
			  int cacheflag, unsigned long *memavailp)
{
	unsigned long v_ret, end;
	/* Remap to 0x90000000. Related comment in ppc/mm/init.c. */
	static unsigned long virt = 0x90000000;
	v_ret = virt;

	if (kmap_chunk_count == KMAP_MAX*3)
		panic ("kernel_map: Can only map %d chunks.\n",
		       KMAP_MAX);

	kmap_chunks[kmap_chunk_count++] = phys_addr;
	kmap_chunks[kmap_chunk_count++] = size;
	kmap_chunks[kmap_chunk_count++] = v_ret;

	for (end = phys_addr + size ; phys_addr < end; 
	     phys_addr += PAGE_SIZE, virt += PAGE_SIZE) {
		map_page(&init_task, virt, phys_addr,
			 pgprot_val(PAGE_KERNEL_CI) | _PAGE_GUARDED);
	}
	return v_ret;
}

/* From pgtable.h */
extern __inline__ pte_t *my_find_pte(struct mm_struct *mm,unsigned long va)
{
	pgd_t *dir = 0;
	pmd_t *pmd = 0;
	pte_t *pte = 0;

	va &= PAGE_MASK;
	
	dir = pgd_offset( mm, va );
	if (dir)
	{
		pmd = pmd_offset(dir, va & PAGE_MASK);
		if (pmd && pmd_present(*pmd))
		{
			pte = pte_offset(pmd, va);
		}
	}
	return pte;
}


/* Again simulating an m68k/mm/kmap.c function. */
void kernel_set_cachemode( unsigned long address, unsigned long size,
			   unsigned int cmode )
{
	int mask, flags;

	switch (cmode)
	{
	case KERNELMAP_FULL_CACHING:
		mask = ~(_PAGE_NO_CACHE | _PAGE_GUARDED);
		flags = 0;
		break;
	case KERNELMAP_NOCACHE_SER:
		mask = ~0;
		flags = (_PAGE_NO_CACHE | _PAGE_GUARDED);
		break;
	default:
		panic ("kernel_set_cachemode() doesn't support mode %d\n", 
		       cmode);
		break;
	}
	
	size /= PAGE_SIZE;
	address &= PAGE_MASK;
	while (size--)
	{
		pte_t *pte;

		pte = my_find_pte(init_task.mm, address);
		if ( !pte )
		{
			printk("pte NULL in kernel_set_cachemode()\n");
			return;
		}

                pte_val (*pte) &= mask;
                pte_val (*pte) |= flags;
                flush_tlb_page(find_vma(init_task.mm,address),address);

		address += PAGE_SIZE;
	}
}

unsigned long mm_ptov (unsigned long paddr)
{
	unsigned long ret;
	if (paddr < 16*1024*1024)
		ret = ZTWO_VADDR(paddr);
	else {
		int i;

		for (i = 0; i < kmap_chunk_count;){
			unsigned long phys = kmap_chunks[i++];
			unsigned long size = kmap_chunks[i++];
			unsigned long virt = kmap_chunks[i++];
			if (paddr >= phys
			    && paddr < (phys + size)){
				ret = virt + paddr - phys;
				goto exit;
			}
		}
		
		ret = (unsigned long) __va(paddr);
	}
exit:
#ifdef DEBUGPV
	printk ("PTOV(%lx)=%lx\n", paddr, ret);
#endif
	return ret;
}

int mm_end_of_chunk (unsigned long addr, int len)
{
	if (memory[0].addr + memory[0].size == addr + len)
		return 1;
	return 0;
}

/*********************************************************** CACHE */

#define L1_CACHE_BYTES 32
#define MAX_CACHE_SIZE 8192
void cache_push(__u32 addr, int length)
{
	addr = mm_ptov(addr);

	if (MAX_CACHE_SIZE < length)
		length = MAX_CACHE_SIZE;

	while(length > 0){
		__asm ("dcbf 0,%0\n\t"
		       : : "r" (addr));
		addr += L1_CACHE_BYTES;
		length -= L1_CACHE_BYTES;
	}
	/* Also flush trailing block */
	__asm ("dcbf 0,%0\n\t"
	       "sync \n\t"
	       : : "r" (addr));
}
void cache_clear(__u32 addr, int length)
{
	if (MAX_CACHE_SIZE < length)
		length = MAX_CACHE_SIZE;

	addr = mm_ptov(addr);

	__asm ("dcbf 0,%0\n\t"
	       "sync \n\t"
	       "icbi 0,%0 \n\t"
	       "isync \n\t"
	       : : "r" (addr));
	
	addr += L1_CACHE_BYTES;
	length -= L1_CACHE_BYTES;

	while(length > 0){
		__asm ("dcbf 0,%0\n\t"
		       "sync \n\t"
		       "icbi 0,%0 \n\t"
		       "isync \n\t"
		       : : "r" (addr));
		addr += L1_CACHE_BYTES;
		length -= L1_CACHE_BYTES;
	}

	__asm ("dcbf 0,%0\n\t"
	       "sync \n\t"
	       "icbi 0,%0 \n\t"
	       "isync \n\t"
	       : : "r" (addr));
}
