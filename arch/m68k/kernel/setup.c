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

#include <asm/setup.h>
#include <asm/irq.h>
#include <asm/machdep.h>
#include <asm/amigatypes.h>
#include <asm/amigahw.h>

#ifdef CONFIG_BLK_DEV_INITRD
#include <linux/blk.h>
#include <asm/pgtable.h>
#endif

struct bootinfo boot_info = {0,};
int bisize = sizeof boot_info;

int m68k_is040or060 = 0;

char m68k_debug_device[6] = "";

extern int end;
extern unsigned long availmem;

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

extern void config_amiga(void);
extern void config_atari(void);
extern void config_mac(void);
extern void config_sun3(void);

extern void register_console(void (*proc)(const char *));
extern void ami_serial_print (const char *str);
extern void ata_serial_print (const char *str);

extern void (*kd_mksound)(unsigned int, unsigned int);

extern void amiga_get_model(char *model);
extern void atari_get_model(char *model);
extern void mac_get_model(char *model);
extern int amiga_get_hardware_list(char *buffer);
extern int atari_get_hardware_list(char *buffer);
extern int mac_get_hardware_list(char *buffer);

#define MASK_256K 0xfffc0000

void setup_arch(char **cmdline_p,
		unsigned long * memory_start_p, unsigned long * memory_end_p)
{
	unsigned long memory_start, memory_end;
	extern int _etext, _edata, _end;
	int i;
	char *p, *q;

	if (MACH_IS_AMIGA)
		register_console(ami_serial_print);

	if (MACH_IS_ATARI)
		register_console(ata_serial_print);

	if (CPU_IS_040)
		m68k_is040or060 = 4;
	else if (CPU_IS_060)
		m68k_is040or060 = 6;

	/* clear the fpu if we have one */
	if (boot_info.cputype & (FPU_68881|FPU_68882|FPU_68040|FPU_68060)) {
		volatile int zero = 0;
		asm __volatile__ ("frestore %0" : : "m" (zero));
	}

	memory_start = availmem;
	memory_end = 0;

	for (i = 0; i < boot_info.num_memory; i++)
		memory_end += boot_info.memory[i].size & MASK_256K;

	init_task.mm->start_code = 0;
	init_task.mm->end_code = (unsigned long) &_etext;
	init_task.mm->end_data = (unsigned long) &_edata;
	init_task.mm->brk = (unsigned long) &_end;

	*cmdline_p = boot_info.command_line;
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

	switch (boot_info.machtype) {
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
	    default:
		panic ("No configuration setup");
	}

#ifdef CONFIG_BLK_DEV_INITRD
	if (boot_info.ramdisk_size) {
		initrd_start = PTOV (boot_info.ramdisk_addr);
		initrd_end = initrd_start + boot_info.ramdisk_size * 1024;
	}
#endif
}

int get_cpuinfo(char * buffer)
{
    char *cpu, *mmu, *fpu;
    u_long clockfreq, clockfactor;

#define LOOP_CYCLES_68020	(8)
#define LOOP_CYCLES_68030	(8)
#define LOOP_CYCLES_68040	(3)
#define LOOP_CYCLES_68060	(1)

    if (CPU_IS_020) {
	cpu = "68020";
	mmu = "68851";
	clockfactor = LOOP_CYCLES_68020;
    } else if (CPU_IS_030) {
	cpu = mmu = "68030";
	clockfactor = LOOP_CYCLES_68030;
    } else if (CPU_IS_040) {
	cpu = mmu = "68040";
	clockfactor = LOOP_CYCLES_68040;
    } else if (CPU_IS_060) {
	cpu = mmu = "68060";
	clockfactor = LOOP_CYCLES_68060;
    } else {
	cpu = mmu = "680x0";
	clockfactor = 0;
    }

    if (boot_info.cputype & FPU_68881)
	fpu = "68881";
    else if (boot_info.cputype & FPU_68882)
	fpu = "68882";
    else if (boot_info.cputype & FPU_68040)
	fpu = "68040";
    else if (boot_info.cputype & FPU_68060)
	fpu = "68060";
    else
	fpu = "none";

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

    switch (boot_info.machtype) {
#ifdef CONFIG_AMIGA
	case MACH_AMIGA:
	    amiga_get_model(model);
	    break;
#endif
#ifdef CONFIG_ATARI
	case MACH_ATARI:
	    atari_get_model(model);
	    break;
#endif
#ifdef CONFIG_MAC
	case MACH_MAC:
	    mac_get_model(model);
	    break;
#endif
	default:
	    strcpy(model, "Unknown m68k");
    } /* boot_info.machtype */

    len += sprintf(buffer+len, "Model:\t\t%s\n", model);
    len += get_cpuinfo(buffer+len);
    for (mem = 0, i = 0; i < boot_info.num_memory; i++)
	mem += boot_info.memory[i].size;
    len += sprintf(buffer+len, "System Memory:\t%ldK\n", mem>>10);

    switch (boot_info.machtype) {
#ifdef CONFIG_AMIGA
	case MACH_AMIGA:
	    len += amiga_get_hardware_list(buffer+len);
	    break;
#endif
#ifdef CONFIG_ATARI
	case MACH_ATARI:
	    len += atari_get_hardware_list(buffer+len);
	    break;
#endif
#ifdef CONFIG_MAC
	case MACH_MAC:
	    break;
#endif
    } /* boot_info.machtype */

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
