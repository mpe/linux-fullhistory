/*
 *  linux/arch/m68k/kernel/setup.c
 *
 *  Copyright (C) 1995  Hamish Macdonald
 */

/*
 * This file handles the architecture-dependent parts of system setup
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/console.h>
#include <linux/genhd.h>
#include <linux/errno.h>
#include <linux/string.h>

#include <asm/bootinfo.h>
#include <asm/setup.h>
#include <asm/irq.h>
#include <asm/machdep.h>
#ifdef CONFIG_AMIGA
#include <asm/amigahw.h>
#endif
#ifdef CONFIG_ATARI
#include <asm/atarihw.h>
#endif

#ifdef CONFIG_BLK_DEV_INITRD
#include <linux/blk.h>
#include <asm/pgtable.h>
#endif

u_long m68k_machtype;
u_long m68k_cputype;
u_long m68k_fputype;
u_long m68k_mmutype;

int m68k_is040or060 = 0;

char m68k_debug_device[6] = "";

extern int end;
extern unsigned long availmem;

int m68k_num_memory = 0;
struct mem_info m68k_memory[NUM_MEMINFO];

static struct mem_info m68k_ramdisk = { 0, 0 };

static char m68k_command_line[CL_SIZE];
char saved_command_line[CL_SIZE];

/* setup some dummy routines */
static void dummy_waitbut(void)
{
}

void (*mach_sched_init) (void (*handler)(int, void *, struct pt_regs *));
/* machine dependent keyboard functions */
int (*mach_keyb_init) (void);
int (*mach_kbdrate) (struct kbd_repeat *) = NULL;
void (*mach_kbd_leds) (unsigned int) = NULL;
/* machine dependent irq functions */
void (*mach_init_IRQ) (void);
void (*(*mach_default_handler)[]) (int, void *, struct pt_regs *) = NULL;
int (*mach_request_irq) (unsigned int, void (*)(int, void *, struct pt_regs *),
                         unsigned long, const char *, void *);
int (*mach_free_irq) (unsigned int, void *);
void (*mach_enable_irq) (unsigned int) = NULL;
void (*mach_disable_irq) (unsigned int) = NULL;
void (*mach_get_model) (char *model) = NULL;
int (*mach_get_hardware_list) (char *buffer) = NULL;
int (*mach_get_irq_list) (char *) = NULL;
void (*mach_process_int) (int, struct pt_regs *) = NULL;
/* machine dependent timer functions */
unsigned long (*mach_gettimeoffset) (void);
void (*mach_gettod) (int*, int*, int*, int*, int*, int*);
int (*mach_hwclk) (int, struct hwclk_time*) = NULL;
int (*mach_set_clock_mmss) (unsigned long) = NULL;
void (*mach_mksound)( unsigned int count, unsigned int ticks );
void (*mach_reset)( void );
void (*waitbut)(void) = dummy_waitbut;
struct fb_info *(*mach_fb_init)(long *);
long mach_max_dma_address = 0x00ffffff; /* default set to the lower 16MB */
void (*mach_debug_init)(void);
void (*mach_video_setup) (char *, int *);
#ifdef CONFIG_BLK_DEV_FD
int (*mach_floppy_init) (void) = NULL;
void (*mach_floppy_setup) (char *, int *) = NULL;
void (*mach_floppy_eject) (void) = NULL;
#endif
void (*mach_syms_export)(void) = NULL;

extern int amiga_parse_bootinfo(const struct bi_record *);
extern int atari_parse_bootinfo(const struct bi_record *);

extern void config_amiga(void);
extern void config_atari(void);
extern void config_mac(void);
extern void config_sun3(void);
extern void config_apollo(void);

extern void register_console(void (*proc)(const char *));
extern void ami_serial_print(const char *str);
extern void ata_serial_print(const char *str);

#define MASK_256K 0xfffc0000


static void m68k_parse_bootinfo(const struct bi_record *record)
{
    while (record->tag != BI_LAST) {
	int unknown = 0;
	const u_long *data = record->data;
	switch (record->tag) {
	    case BI_MACHTYPE:
	    case BI_CPUTYPE:
	    case BI_FPUTYPE:
	    case BI_MMUTYPE:
		/* Already set up by head.S */
		break;

	    case BI_MEMCHUNK:
		if (m68k_num_memory < NUM_MEMINFO) {
		    m68k_memory[m68k_num_memory].addr = data[0];
		    m68k_memory[m68k_num_memory].size = data[1];
		    m68k_num_memory++;
		} else
		    printk("m68k_parse_bootinfo: too many memory chunks\n");
		break;

	    case BI_RAMDISK:
		m68k_ramdisk.addr = data[0];
		m68k_ramdisk.size = data[1];
		break;

	    case BI_COMMAND_LINE:
		strncpy(m68k_command_line, (const char *)data, CL_SIZE);
		m68k_command_line[CL_SIZE-1] = '\0';
		break;

	    default:
		if (MACH_IS_AMIGA)
		    unknown = amiga_parse_bootinfo(record);
		else if (MACH_IS_ATARI)
		    unknown = atari_parse_bootinfo(record);
		else
		    unknown = 1;
	}
	if (unknown)
	    printk("m68k_parse_bootinfo: unknown tag 0x%04x ignored\n",
		   record->tag);
	record = (struct bi_record *)((u_long)record+record->size);
    }
}

void setup_arch(char **cmdline_p, unsigned long * memory_start_p,
		unsigned long * memory_end_p)
{
	unsigned long memory_start, memory_end;
	extern int _etext, _edata, _end;
	int i;
	char *p, *q;

	/* machtype is set up by head.S, thus we know our gender */
	if (MACH_IS_AMIGA)
		register_console(ami_serial_print);
	if (MACH_IS_ATARI)
		register_console(ata_serial_print);

	/* The bootinfo is located right after the kernel bss */
	m68k_parse_bootinfo((const struct bi_record *)&_end);

	if (CPU_IS_040)
		m68k_is040or060 = 4;
	else if (CPU_IS_060)
		m68k_is040or060 = 6;

	/* clear the fpu if we have one */
	if (m68k_fputype & (FPU_68881|FPU_68882|FPU_68040|FPU_68060)) {
		volatile int zero = 0;
		asm __volatile__ ("frestore %0" : : "m" (zero));
	}

	memory_start = availmem;
	memory_end = 0;

	for (i = 0; i < m68k_num_memory; i++)
		memory_end += m68k_memory[i].size & MASK_256K;

	init_task.mm->start_code = 0;
	init_task.mm->end_code = (unsigned long) &_etext;
	init_task.mm->end_data = (unsigned long) &_edata;
	init_task.mm->brk = (unsigned long) &_end;

	*cmdline_p = m68k_command_line;
	memcpy(saved_command_line, *cmdline_p, CL_SIZE);

	/* Parse the command line for arch-specific options.
	 * For the m68k, this is currently only "debug=xxx" to enable printing
	 * certain kernel messages to some machine-specific device.
	 */
	for( p = *cmdline_p; p && *p; ) {
	    i = 0;
	    if (!strncmp( p, "debug=", 6 )) {
		strncpy( m68k_debug_device, p+6, sizeof(m68k_debug_device)-1 );
		m68k_debug_device[sizeof(m68k_debug_device)-1] = 0;
		if ((q = strchr( m68k_debug_device, ' ' ))) *q = 0;
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

	*memory_start_p = memory_start;
	*memory_end_p = memory_end;

	switch (m68k_machtype) {
#ifdef CONFIG_AMIGA
	    case MACH_AMIGA:
		config_amiga();
		break;
#endif
#ifdef CONFIG_ATARI
	    case MACH_ATARI:
		config_atari();
		break;
#endif
#ifdef CONFIG_MAC
	    case MACH_MAC:
		config_mac();
		break;
#endif
#ifdef CONFIG_SUN3
	    case MACH_SUN3:
	    	config_sun3();
	    	break;
#endif
#ifdef CONFIG_APOLLO
	    case MACH_APOLLO:
	    	config_apollo();
	    	break;
#endif
	    default:
		panic ("No configuration setup");
	}

#ifdef CONFIG_BLK_DEV_INITRD
	if (m68k_ramdisk.size) {
		initrd_start = PTOV (m68k_ramdisk.addr);
		initrd_end = initrd_start + m68k_ramdisk.size;
	}
#endif
}

int get_cpuinfo(char * buffer)
{
    const char *cpu, *mmu, *fpu;
    u_long clockfreq, clockfactor;

#define LOOP_CYCLES_68020	(8)
#define LOOP_CYCLES_68030	(8)
#define LOOP_CYCLES_68040	(3)
#define LOOP_CYCLES_68060	(1)

    if (CPU_IS_020) {
	cpu = "68020";
	clockfactor = LOOP_CYCLES_68020;
    } else if (CPU_IS_030) {
	cpu = "68030";
	clockfactor = LOOP_CYCLES_68030;
    } else if (CPU_IS_040) {
	cpu = "68040";
	clockfactor = LOOP_CYCLES_68040;
    } else if (CPU_IS_060) {
	cpu = "68060";
	clockfactor = LOOP_CYCLES_68060;
    } else {
	cpu = "680x0";
	clockfactor = 0;
    }

    if (m68k_fputype & FPU_68881)
	fpu = "68881";
    else if (m68k_fputype & FPU_68882)
	fpu = "68882";
    else if (m68k_fputype & FPU_68040)
	fpu = "68040";
    else if (m68k_fputype & FPU_68060)
	fpu = "68060";
    else if (m68k_fputype & FPU_SUNFPA)
	fpu = "Sun FPA";
    else
	fpu = "none";

    if (m68k_mmutype & MMU_68851)
	mmu = "68851";
    else if (m68k_mmutype & MMU_68030)
	mmu = "68030";
    else if (m68k_mmutype & MMU_68040)
	mmu = "68040";
    else if (m68k_mmutype & MMU_68060)
	mmu = "68060";
    else if (m68k_mmutype & MMU_SUN3)
	mmu = "Sun-3";
    else if (m68k_mmutype & MMU_APOLLO)
	mmu = "Apollo";
    else
	mmu = "unknown";

    clockfreq = loops_per_sec*clockfactor;

    return(sprintf(buffer, "CPU:\t\t%s\n"
		   "MMU:\t\t%s\n"
		   "FPU:\t\t%s\n"
		   "Clocking:\t%lu.%1luMHz\n"
		   "BogoMips:\t%lu.%02lu\n"
		   "Calibration:\t%lu loops\n",
		   cpu, mmu, fpu,
		   clockfreq/1000000,(clockfreq/100000)%10,
		   loops_per_sec/500000,(loops_per_sec/5000)%100,
		   loops_per_sec));

}

int get_hardware_list(char *buffer)
{
    int len = 0;
    char model[80];
    u_long mem;
    int i;

    if (mach_get_model)
	mach_get_model(model);
    else
	strcpy(model, "Unknown m68k");

    len += sprintf(buffer+len, "Model:\t\t%s\n", model);
    len += get_cpuinfo(buffer+len);
    for (mem = 0, i = 0; i < m68k_num_memory; i++)
	mem += m68k_memory[i].size;
    len += sprintf(buffer+len, "System Memory:\t%ldK\n", mem>>10);

    if (mach_get_hardware_list)
	len += mach_get_hardware_list(buffer+len);

    return(len);
}

#ifdef CONFIG_BLK_DEV_FD
int floppy_init(void)
{
	if (mach_floppy_init)
		return mach_floppy_init();
	else
		return 0;
}

void floppy_setup(char *str, int *ints)
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

unsigned long arch_kbd_init(void)
{
	return mach_keyb_init();
}

void arch_gettod(int *year, int *mon, int *day, int *hour,
		 int *min, int *sec)
{
	if (mach_gettod)
		mach_gettod(year, mon, day, hour, min, sec);
	else
		*year = *mon = *day = *hour = *min = *sec = 0;
}

void video_setup (char *options, int *ints)
{
	if (mach_video_setup)
		mach_video_setup (options, ints);
}
