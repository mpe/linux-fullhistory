/*
 *  linux/arch/ppc/kernel/apus_setup.c
 *
 *  Copyright (C) 1998, 1999  Jesper Skov
 *
 *  Basically what is needed to replace functionality found in
 *  arch/m68k allowing Amiga drivers to work under APUS.
 *  Bits of code and/or ideas from arch/m68k and arch/ppc files.
 *
 * TODO:
 *  This file needs a *really* good cleanup. Restructure and optimize.
 *  Make sure it can be compiled for non-APUS configs. Begin to move
 *  Amiga specific stuff into linux/machine/amiga.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kd.h>
#include <linux/init.h>
#include <linux/serial.h>
#include <linux/hdreg.h>
#include <linux/blk.h>
#include <linux/pci.h>

#ifdef CONFIG_APUS
#include <asm/logging.h>
#endif

/* Get the IDE stuff from the 68k file */
#include <linux/ide.h>
#define ide_init_hwif_ports m68k_ide_init_hwif_ports
#define ide_default_irq m68k_ide_default_irq
#define ide_request_irq m68k_ide_request_irq
#define ide_default_io_base m68k_ide_default_io_base
#define ide_check_region m68k_ide_check_region
#define ide_request_region m68k_ide_request_region
#define ide_release_region m68k_ide_release_region
#define ide_fix_driveid m68k_ide_fix_driveid
#define ide_init_default_hwifs m68k_ide_init_default_hwifs
#define select_t m68k_select_t
#define ide_free_irq m68k_ide_free_irq
//#include <asm/hdreg.h>
#include <asm-m68k/ide.h>
#undef ide_free_irq
#undef select_t
#undef ide_request_irq
#undef ide_init_default_hwifs
#undef ide_init_hwif_ports
#undef ide_default_irq
#undef ide_default_io_base
#undef ide_check_region
#undef ide_request_region
#undef ide_release_region
#undef ide_fix_driveid
/*-------------------------------------------*/

#include <asm/bootinfo.h>
#include <asm/setup.h>
#include <asm/amigahw.h>
#include <asm/amigappc.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/machdep.h>
#include <asm/init.h>

#include "local_irq.h"

unsigned long m68k_machtype __apusdata;
char debug_device[6] __apusdata = "";

void (*mach_sched_init) (void (*handler)(int, void *, struct pt_regs *)) __initdata;
/* machine dependent keyboard functions */
int (*mach_keyb_init) (void) __initdata;
int (*mach_kbdrate) (struct kbd_repeat *) __apusdata = NULL;
void (*mach_kbd_leds) (unsigned int) __apusdata = NULL;
/* machine dependent irq functions */
void (*mach_init_IRQ) (void) __initdata;
void (*(*mach_default_handler)[]) (int, void *, struct pt_regs *) __apusdata = NULL;
void (*mach_get_model) (char *model) __apusdata = NULL;
int (*mach_get_hardware_list) (char *buffer) __apusdata = NULL;
int (*mach_get_irq_list) (char *) __apusdata = NULL;
void (*mach_process_int) (int, struct pt_regs *) __apusdata = NULL;
/* machine dependent timer functions */
unsigned long (*mach_gettimeoffset) (void) __apusdata;
void (*mach_gettod) (int*, int*, int*, int*, int*, int*) __apusdata;
int (*mach_hwclk) (int, struct hwclk_time*) __apusdata = NULL;
int (*mach_set_clock_mmss) (unsigned long) __apusdata = NULL;
void (*mach_reset)( void ) __apusdata;
long mach_max_dma_address __apusdata = 0x00ffffff; /* default set to the lower 16MB */
#if defined(CONFIG_AMIGA_FLOPPY)
void (*mach_floppy_setup) (char *, int *) __initdata = NULL;
void (*mach_floppy_eject) (void) __apusdata = NULL;
#endif
#ifdef CONFIG_HEARTBEAT
void (*mach_heartbeat) (int) __apusdata = NULL;
extern void apus_heartbeat (void);
static int heartbeat_enabled = 1;

void enable_heartbeat(void)
{
	heartbeat_enabled = 1;
}

void disable_heartbeat(void)
{
	heartbeat_enabled = 0;
	mach_heartbeat(0);
}
#endif

extern unsigned long amiga_model;
extern unsigned decrementer_count;/* count value for 1e6/HZ microseconds */
extern unsigned count_period_num; /* 1 decrementer count equals */
extern unsigned count_period_den; /* count_period_num / count_period_den us */

int num_memory __apusdata = 0;
struct mem_info memory[NUM_MEMINFO] __apusdata;/* memory description */
/* FIXME: Duplicate memory data to avoid conflicts with m68k shared code. */
int m68k_realnum_memory __apusdata = 0;
struct mem_info m68k_memory[NUM_MEMINFO] __apusdata;/* memory description */

struct mem_info ramdisk __apusdata;

extern void amiga_floppy_setup(char *, int *);
extern void config_amiga(void);

static int __60nsram __apusdata = 0;

/* for cpuinfo */
static int __bus_speed __apusdata = 0;
static int __speed_test_failed __apusdata = 0;

/********************************************** COMPILE PROTECTION */
/* Provide some stubs that links to Amiga specific functions. 
 * This allows CONFIG_APUS to be removed from generic PPC files while
 * preventing link errors for other PPC targets.
 */
__apus
unsigned long apus_get_rtc_time(void)
{
#ifdef CONFIG_APUS
	extern unsigned long m68k_get_rtc_time(void);
	
	return m68k_get_rtc_time ();
#else
	return 0;
#endif
}

__apus
int apus_set_rtc_time(unsigned long nowtime)
{
#ifdef CONFIG_APUS
	extern int m68k_set_rtc_time(unsigned long nowtime);

	return m68k_set_rtc_time (nowtime);
#else
	return 0;
#endif
}

__apus
int  apus_request_irq(unsigned int irq, 
		      void (*handler)(int, void *, struct pt_regs *),
		      unsigned long flags, const char *devname, 
		      void *dev_id)
{
#ifdef CONFIG_APUS
	extern int  amiga_request_irq(unsigned int irq, 
				      void (*handler)(int, void *, 
						      struct pt_regs *),
				      unsigned long flags, 
				      const char *devname, 
				      void *dev_id);

	return amiga_request_irq (irq, handler, flags, devname, dev_id);
#else
	return 0;
#endif
}

__apus
void apus_free_irq(unsigned int irq, void *dev_id)
{
#ifdef CONFIG_APUS
	extern void amiga_free_irq(unsigned int irq, void *dev_id);

	amiga_free_irq (irq, dev_id);
#endif
}

__apus
void apus_process_int(unsigned long vec, void *fp)
{
#ifdef CONFIG_APUS
	extern void process_int(unsigned long vec, struct pt_regs *fp);

	process_int (vec, (struct pt_regs*)fp);
#endif
}

__apus
int apus_get_irq_list(char *buf)
{
#ifdef CONFIG_APUS
	extern int m68k_get_irq_list (char*);
	
	return m68k_get_irq_list (buf);
#else
	return 0;
#endif
}


/* Here some functions we don't support, but which the other ports reference */
int pckbd_setkeycode(unsigned int scancode, unsigned int keycode)
{
	printk("Bogus call to " __FILE__ ":" __FUNCTION__ "\n");
	return 0; 
}
int pckbd_getkeycode(unsigned int scancode) 
{ 
	printk("Bogus call to " __FILE__ ":" __FUNCTION__ "\n");
	return 0; 
}
int pckbd_translate(unsigned char scancode, unsigned char *keycode,
		    char raw_mode) 
{
	printk("Bogus call to " __FILE__ ":" __FUNCTION__ "\n");
	return 0; 
}
char pckbd_unexpected_up(unsigned char keycode)
{
	printk("Bogus call to " __FILE__ ":" __FUNCTION__ "\n");
	return 0;
}
void pckbd_leds(unsigned char leds)
{
	printk("Bogus call to " __FILE__ ":" __FUNCTION__ "\n");
}
void pckbd_init_hw(void)
{
	printk("Bogus call to " __FILE__ ":" __FUNCTION__ "\n");
}
unsigned char pckbd_sysrq_xlate[128];

struct pci_bus * __init pci_scan_peer_bridge(int bus)
{
	printk("Bogus call to " __FILE__ ":" __FUNCTION__ "\n");
	return NULL;
}

/*********************************************************** SETUP */
/* From arch/m68k/kernel/setup.c. */
void __init apus_setup_arch(unsigned long * memory_start_p,
				unsigned long * memory_end_p)
{
#ifdef CONFIG_APUS
	extern char cmd_line[];
	int i;
	char *p, *q;

	/* Let m68k-shared code know it should do the Amiga thing. */
	m68k_machtype = MACH_AMIGA;

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

	{
#define LOG_SIZE 4096
		void* base;

		/* Throw away some memory - the P5 firmare stomps on top
		 * of CHIP memory during bootup.
		 */
		amiga_chip_alloc(0x1000);

		base = amiga_chip_alloc(LOG_SIZE+sizeof(klog_data_t));
		LOG_INIT(base, base+sizeof(klog_data_t), LOG_SIZE);
	}
#endif
}

__apus
int
apus_get_cpuinfo(char *buffer)
{
#ifdef CONFIG_APUS
	extern int __map_without_bats;
	extern unsigned long powerup_PCI_present;
	int len;

	len = sprintf(buffer, "machine\t\t: Amiga\n");
	len += sprintf(buffer+len, "bus speed\t: %d%s", __bus_speed,
		       (__speed_test_failed) ? " [failed]\n" : "\n");
	len += sprintf(buffer+len, "using BATs\t: %s\n",
		       (__map_without_bats) ? "No" : "Yes");
	len += sprintf(buffer+len, "ram speed\t: %dns\n", 
		       (__60nsram) ? 60 : 70);
	len += sprintf(buffer+len, "PCI bridge\t: %s\n",
		       (powerup_PCI_present) ? "Yes" : "No");
	return len;
#endif
}

__apus
static void get_current_tb(unsigned long long *time)
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


__apus
void apus_calibrate_decr(void)
{
#ifdef CONFIG_APUS
	int freq, divisor;

	/* This algorithm for determining the bus speed was
           contributed by Ralph Schmidt. */
	unsigned long long start, stop;
	int bus_speed;
	int speed_test_failed = 0;

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
		speed_test_failed = 1;
	}

	/* Ease diagnostics... */
	{
		extern int __map_without_bats;
		extern unsigned long powerup_PCI_present;

		printk ("APUS: BATs=%d, BUS=%dMHz",
			(__map_without_bats) ? 0 : 1,
			bus_speed);
		if (speed_test_failed)
			printk ("[FAILED - please report]");

		printk (", RAM=%dns, PCI bridge=%d\n",
			(__60nsram) ? 60 : 70,
			(powerup_PCI_present) ? 1 : 0);

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

	__bus_speed = bus_speed;
	__speed_test_failed = speed_test_failed;
#endif
}

__apus
void arch_gettod(int *year, int *mon, int *day, int *hour,
		 int *min, int *sec)
{
#ifdef CONFIG_APUS
	if (mach_gettod)
		mach_gettod(year, mon, day, hour, min, sec);
	else
		*year = *mon = *day = *hour = *min = *sec = 0;
#endif
}

/* for "kbd-reset" cmdline param */
__init
void kbd_reset_setup(char *str, int *ints)
{
}

#if defined(CONFIG_WHIPPET_SERIAL)||defined(CONFIG_MULTIFACE_III_TTY)||defined(CONFIG_GVPIOEXT)||defined(CONFIG_AMIGA_BUILTIN_SERIAL)

long m68k_rs_init(void);
int m68k_register_serial(struct serial_struct *);
void m68k_unregister_serial(int);
long m68k_serial_console_init(long, long );

int rs_init(void)
{
	return m68k_rs_init();
}
int register_serial(struct serial_struct *p)
{
	return m68k_register_serial(p);
}
void unregister_serial(int i)
{
	m68k_unregister_serial(i);
}
#ifdef CONFIG_SERIAL_CONSOLE
long serial_console_init(long kmem_start, long kmem_end)
{
	return m68k_serial_console_init(kmem_start, kmem_end);
}
#endif
#endif

/*********************************************************** FLOPPY */
#if defined(CONFIG_AMIGA_FLOPPY)
__init 
void floppy_setup(char *str, int *ints)
{
	if (mach_floppy_setup)
		mach_floppy_setup (str, ints);
}

__apus
void floppy_eject(void)
{
	if (mach_floppy_eject)
		mach_floppy_eject();
}
#endif

/*********************************************************** MEMORY */
#define KMAP_MAX 32
unsigned long kmap_chunks[KMAP_MAX*3] __apusdata;
int kmap_chunk_count __apusdata = 0;

/* From pgtable.h */
__apus
static __inline__ pte_t *my_find_pte(struct mm_struct *mm,unsigned long va)
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
__apus
void kernel_set_cachemode( unsigned long address, unsigned long size,
			   unsigned int cmode )
{
	unsigned long mask, flags;

	switch (cmode)
	{
	case IOMAP_FULL_CACHING:
		mask = ~(_PAGE_NO_CACHE | _PAGE_GUARDED);
		flags = 0;
		break;
	case IOMAP_NOCACHE_SER:
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

		pte = my_find_pte(&init_mm, address);
		if ( !pte )
		{
			printk("pte NULL in kernel_set_cachemode()\n");
			return;
		}

                pte_val (*pte) &= mask;
                pte_val (*pte) |= flags;
                flush_tlb_page(find_vma(&init_mm,address),address);

		address += PAGE_SIZE;
	}
}

__apus
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

__apus
int mm_end_of_chunk (unsigned long addr, int len)
{
	if (memory[0].addr + memory[0].size == addr + len)
		return 1;
	return 0;
}

/*********************************************************** CACHE */

#define L1_CACHE_BYTES 32
#define MAX_CACHE_SIZE 8192
__apus
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

__apus
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

/****************************************************** from setup.c */
void
apus_restart(char *cmd)
{
	cli();

	APUS_WRITE(APUS_REG_LOCK, 
		   REGLOCK_BLACKMAGICK1|REGLOCK_BLACKMAGICK2);
	APUS_WRITE(APUS_REG_LOCK, 
		   REGLOCK_BLACKMAGICK1|REGLOCK_BLACKMAGICK3);
	APUS_WRITE(APUS_REG_LOCK, 
		   REGLOCK_BLACKMAGICK2|REGLOCK_BLACKMAGICK3);
	APUS_WRITE(APUS_REG_SHADOW, REGSHADOW_SELFRESET);
	APUS_WRITE(APUS_REG_RESET, REGRESET_AMIGARESET);
	for(;;);
}

void
apus_power_off(void)
{
	for (;;);
}

void
apus_halt(void)
{
   apus_restart(NULL);
}

/****************************************************** from setup.c/IDE */
#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE)
/*
 * IDE stuff.
 */
void ide_insw(ide_ioreg_t port, void *buf, int ns);
void ide_outsw(ide_ioreg_t port, void *buf, int ns);
void
apus_ide_insw(ide_ioreg_t port, void *buf, int ns)
{
	ide_insw(port, buf, ns);
}

void
apus_ide_outsw(ide_ioreg_t port, void *buf, int ns)
{
	ide_outsw(port, buf, ns);
}

int
apus_ide_default_irq(ide_ioreg_t base)
{
        return m68k_ide_default_irq(base);
}

ide_ioreg_t
apus_ide_default_io_base(int index)
{
        return 0;
}

int
apus_ide_check_region(ide_ioreg_t from, unsigned int extent)
{
        return m68k_ide_check_region(from, extent);
}

void
apus_ide_request_region(ide_ioreg_t from,
			unsigned int extent,
			const char *name)
{
        m68k_ide_request_region(from, extent, name);
}

void
apus_ide_release_region(ide_ioreg_t from,
			unsigned int extent)
{
        m68k_ide_release_region(from, extent);
}

void
apus_ide_fix_driveid(struct hd_driveid *id)
{
        m68k_ide_fix_driveid(id);
}

__init
void apus_ide_init_hwif_ports (hw_regs_t *hw, ide_ioreg_t data_port, 
			       ide_ioreg_t ctrl_port, int *irq)
{
        m68k_ide_init_hwif_ports(hw, data_port, ctrl_port, irq);
}
#endif
/****************************************************** from irq.c */
#define VEC_SPUR    (24)

void
apus_do_IRQ(struct pt_regs *regs,
	    int            cpu,
            int            isfake)
{
	int old_level, new_level;

	new_level = (~(regs->mq) >> 3) & IPLEMU_IPLMASK;
		
	if (0 != new_level && 7 != new_level) {
		old_level = ~(regs->mq) & IPLEMU_IPLMASK;

		apus_process_int (VEC_SPUR+new_level, regs);
		
		APUS_WRITE(APUS_IPL_EMU, IPLEMU_SETRESET | IPLEMU_DISABLEINT);
		APUS_WRITE(APUS_IPL_EMU, IPLEMU_IPLMASK);
		APUS_WRITE(APUS_IPL_EMU, (IPLEMU_SETRESET
					  | (~(old_level) & IPLEMU_IPLMASK)));
		APUS_WRITE(APUS_IPL_EMU, IPLEMU_DISABLEINT);
	}
}

__apus
static void apus_save_flags(unsigned long* flags)
{
	unsigned short __f;
	APUS_READ(APUS_IPL_EMU, __f);
	return ((~__f) & IPLEMU_IPLMASK) << 8;
}

__apus
static void apus_restore_flags(unsigned long flags)
{
	APUS_WRITE(APUS_IPL_EMU, IPLEMU_SETRESET | IPLEMU_DISABLEINT);
	APUS_WRITE(APUS_IPL_EMU, IPLEMU_IPLMASK);
	APUS_WRITE(APUS_IPL_EMU, IPLEMU_SETRESET
                   | (~(flags >> 8) & IPLEMU_IPLMASK));
	APUS_WRITE(APUS_IPL_EMU, IPLEMU_DISABLEINT);
}

__apus
static void apus_sti(void)
{
	APUS_WRITE(APUS_IPL_EMU, IPLEMU_SETRESET | IPLEMU_DISABLEINT);
	APUS_WRITE(APUS_IPL_EMU, IPLEMU_IPLMASK);
	APUS_WRITE(APUS_IPL_EMU, IPLEMU_SETRESET | IPLEMU_IPLMASK);
	APUS_WRITE(APUS_IPL_EMU, IPLEMU_DISABLEINT);
}

__apus
static void apus_cli(void)
{
	APUS_WRITE(APUS_IPL_EMU, IPLEMU_SETRESET | IPLEMU_DISABLEINT);
	APUS_WRITE(APUS_IPL_EMU, IPLEMU_IPLMASK);
	APUS_WRITE(APUS_IPL_EMU, IPLEMU_SETRESET);
	APUS_WRITE(APUS_IPL_EMU, IPLEMU_DISABLEINT);
}

/****************************************************** keyboard */
__apus
static int apus_kbd_setkeycode(unsigned int scancode, unsigned int keycode)
{
	return -EOPNOTSUPP;
}

__apus
static int apus_kbd_getkeycode(unsigned int scancode)
{
	return scancode > 127 ? -EINVAL : scancode;
}

__apus
static int apus_kbd_translate(unsigned char keycode, unsigned char *keycodep,
			      char raw_mode)
{
	*keycodep = keycode;
	return 1;
}

__apus
static char apus_kbd_unexpected_up(unsigned char keycode)
{
	return 0200;
}

__apus  
static void apus_kbd_leds(unsigned char leds)
{
}

__apus  
static void apus_kbd_init_hw(void)
{
#ifdef CONFIG_APUS
	extern int amiga_keyb_init(void);

printk("**** " __FUNCTION__ "\n");
	amiga_keyb_init();
#endif
}


/****************************************************** init */
extern void amiga_disable_irq(unsigned int irq);
extern void amiga_enable_irq(unsigned int irq);
extern void m68k_init_IRQ (void);

struct hw_interrupt_type amiga_irq_ctl = {
	" Amiga    ",
        NULL,
        NULL,
        NULL,
        amiga_enable_irq,
        amiga_disable_irq,
        NULL,
        0
};

__init
void apus_init_IRQ(void)
{
	int i;

	for (i = 0; i < NR_IRQS; i++)
		irq_desc[i].ctl = &amiga_irq_ctl;
	m68k_init_IRQ ();

	int_control.int_sti = apus_sti;
	int_control.int_cli = apus_cli;
	int_control.int_save_flags = apus_save_flags;
	int_control.int_restore_flags = apus_restore_flags;
}

__init
void apus_init(unsigned long r3, unsigned long r4, unsigned long r5,
	       unsigned long r6, unsigned long r7)
{
	extern int parse_bootinfo(const struct bi_record *);
	extern char _end[];
	
	/* Parse bootinfo. The bootinfo is located right after
           the kernel bss */
	parse_bootinfo((const struct bi_record *)&_end);
#ifdef CONFIG_BLK_DEV_INITRD
	/* Take care of initrd if we have one. Use data from
	   bootinfo to avoid the need to initialize PPC
	   registers when kernel is booted via a PPC reset. */
	if ( ramdisk.addr ) {
		initrd_start = (unsigned long) __va(ramdisk.addr);
		initrd_end = (unsigned long) 
			__va(ramdisk.size + ramdisk.addr);
	}
#endif /* CONFIG_BLK_DEV_INITRD */

	ISA_DMA_THRESHOLD = 0x00ffffff;

	ppc_md.setup_arch     = apus_setup_arch;
	ppc_md.setup_residual = NULL;
	ppc_md.get_cpuinfo    = apus_get_cpuinfo;
	ppc_md.irq_cannonicalize = NULL;
	ppc_md.init_IRQ       = apus_init_IRQ;
	ppc_md.do_IRQ         = apus_do_IRQ;
#ifdef CONFIG_HEARTBEAT
	ppc_md.heartbeat      = apus_heartbeat;
	ppc_md.heartbeat_count = 1;
#endif
	ppc_md.init           = NULL;

	ppc_md.restart        = apus_restart;
	ppc_md.power_off      = apus_power_off;
	ppc_md.halt           = apus_halt;

	ppc_md.time_init      = NULL;
	ppc_md.set_rtc_time   = apus_set_rtc_time;
	ppc_md.get_rtc_time   = apus_get_rtc_time;
	ppc_md.calibrate_decr = apus_calibrate_decr;

	ppc_md.nvram_read_val = NULL;
	ppc_md.nvram_write_val = NULL;

	/* These should not be used for the APUS yet, since it uses
	   the M68K keyboard now. */
	ppc_md.kbd_setkeycode    = apus_kbd_setkeycode;
	ppc_md.kbd_getkeycode    = apus_kbd_getkeycode;
	ppc_md.kbd_translate     = apus_kbd_translate;
	ppc_md.kbd_unexpected_up = apus_kbd_unexpected_up;
	ppc_md.kbd_leds          = apus_kbd_leds;
	ppc_md.kbd_init_hw       = apus_kbd_init_hw;
#ifdef CONFIG_MAGIC_SYSRQ
	ppc_md.kbd_sysrq_xlate	 = NULL;
#endif

#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE)
        ppc_ide_md.insw = apus_ide_insw;
        ppc_ide_md.outsw = apus_ide_outsw;
        ppc_ide_md.default_irq = apus_ide_default_irq;
        ppc_ide_md.default_io_base = apus_ide_default_io_base;
        ppc_ide_md.ide_check_region = apus_ide_check_region;
        ppc_ide_md.ide_request_region = apus_ide_request_region;
        ppc_ide_md.ide_release_region = apus_ide_release_region;
        ppc_ide_md.fix_driveid = apus_ide_fix_driveid;
        ppc_ide_md.ide_init_hwif = apus_ide_init_hwif_ports;

        ppc_ide_md.io_base = _IO_BASE;
#endif		
}
