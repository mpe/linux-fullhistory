/*
 *  linux/arch/i386/kernel/setup.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *
 *  Enhanced CPU type detection by Mike Jagdis, Patrick St. Jean
 *  and Martin Mares, November 1997.
 */

/*
 * This file handles the architecture-dependent parts of initialization
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/tty.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/config.h>
#include <linux/init.h>
#ifdef CONFIG_APM
#include <linux/apm_bios.h>
#endif
#ifdef CONFIG_BLK_DEV_RAM
#include <linux/blk.h>
#endif
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/smp.h>

/*
 * Machine setup..
 */

char ignore_irq13 = 0;		/* set if exception 16 works */
struct cpuinfo_x86 boot_cpu_data = { 0, 0, 0, 0, -1, 1, 0, 0, -1 };

/*
 * Bus types ..
 */
int EISA_bus = 0;
int MCA_bus = 0;

/* for MCA, but anyone else can use it if they want */
unsigned int machine_id = 0;
unsigned int machine_submodel_id = 0;
unsigned int BIOS_revision = 0;

/*
 * Setup options
 */
struct drive_info_struct { char dummy[32]; } drive_info;
struct screen_info screen_info;
#ifdef CONFIG_APM
struct apm_bios_info apm_bios_info;
#endif
struct sys_desc_table_struct {
	unsigned short length;
	unsigned char table[0];
};

unsigned char aux_device_present;

#ifdef CONFIG_BLK_DEV_RAM
extern int rd_doload;		/* 1 = load ramdisk, 0 = don't load */
extern int rd_prompt;		/* 1 = prompt for ramdisk, 0 = don't prompt */
extern int rd_image_start;	/* starting block # of image */
#endif

extern int root_mountflags;
extern int _etext, _edata, _end;

extern char empty_zero_page[PAGE_SIZE];

/*
 * This is set up by the setup-routine at boot-time
 */
#define PARAM	empty_zero_page
#define EXT_MEM_K (*(unsigned short *) (PARAM+2))
#define ALT_MEM_K (*(unsigned long *) (PARAM+0x1e0))
#ifdef CONFIG_APM
#define APM_BIOS_INFO (*(struct apm_bios_info *) (PARAM+64))
#endif
#define DRIVE_INFO (*(struct drive_info_struct *) (PARAM+0x80))
#define SCREEN_INFO (*(struct screen_info *) (PARAM+0))
#define MOUNT_ROOT_RDONLY (*(unsigned short *) (PARAM+0x1F2))
#define RAMDISK_FLAGS (*(unsigned short *) (PARAM+0x1F8))
#define ORIG_ROOT_DEV (*(unsigned short *) (PARAM+0x1FC))
#define AUX_DEVICE_INFO (*(unsigned char *) (PARAM+0x1FF))
#define LOADER_TYPE (*(unsigned char *) (PARAM+0x210))
#define KERNEL_START (*(unsigned long *) (PARAM+0x214))
#define INITRD_START (*(unsigned long *) (PARAM+0x218))
#define INITRD_SIZE (*(unsigned long *) (PARAM+0x21c))
#define SYS_DESC_TABLE (*(struct sys_desc_table_struct*)(PARAM+0x220))
#define COMMAND_LINE ((char *) (PARAM+2048))
#define COMMAND_LINE_SIZE 256

#define RAMDISK_IMAGE_START_MASK  	0x07FF
#define RAMDISK_PROMPT_FLAG		0x8000
#define RAMDISK_LOAD_FLAG		0x4000	

static char command_line[COMMAND_LINE_SIZE] = { 0, };
       char saved_command_line[COMMAND_LINE_SIZE];

__initfunc(void setup_arch(char **cmdline_p,
	unsigned long * memory_start_p, unsigned long * memory_end_p))
{
	unsigned long memory_start, memory_end;
	char c = ' ', *to = command_line, *from = COMMAND_LINE;
	int len = 0;
	static unsigned char smptrap=0;

	if (smptrap)
		return;
	smptrap=1;

 	ROOT_DEV = to_kdev_t(ORIG_ROOT_DEV);
 	drive_info = DRIVE_INFO;
 	screen_info = SCREEN_INFO;
#ifdef CONFIG_APM
	apm_bios_info = APM_BIOS_INFO;
#endif
	if( SYS_DESC_TABLE.length != 0 ) {
		MCA_bus = SYS_DESC_TABLE.table[3] &0x2;
		machine_id = SYS_DESC_TABLE.table[0];
		machine_submodel_id = SYS_DESC_TABLE.table[1];
		BIOS_revision = SYS_DESC_TABLE.table[2];
	}
	aux_device_present = AUX_DEVICE_INFO;
	memory_end = (1<<20) + (EXT_MEM_K<<10);
#ifndef STANDARD_MEMORY_BIOS_CALL
	{
		unsigned long memory_alt_end = (1<<20) + (ALT_MEM_K<<10);
		/* printk(KERN_DEBUG "Memory sizing: %08x %08x\n", memory_end, memory_alt_end); */
		if (memory_alt_end > memory_end)
			memory_end = memory_alt_end;
	}
#endif
	memory_end &= PAGE_MASK;
#ifdef CONFIG_BLK_DEV_RAM
	rd_image_start = RAMDISK_FLAGS & RAMDISK_IMAGE_START_MASK;
	rd_prompt = ((RAMDISK_FLAGS & RAMDISK_PROMPT_FLAG) != 0);
	rd_doload = ((RAMDISK_FLAGS & RAMDISK_LOAD_FLAG) != 0);
#endif
	if (!MOUNT_ROOT_RDONLY)
		root_mountflags &= ~MS_RDONLY;
	memory_start = (unsigned long) &_end;
	init_task.mm->start_code = PAGE_OFFSET;
	init_task.mm->end_code = (unsigned long) &_etext;
	init_task.mm->end_data = (unsigned long) &_edata;
	init_task.mm->brk = (unsigned long) &_end;

	/* Save unparsed command line copy for /proc/cmdline */
	memcpy(saved_command_line, COMMAND_LINE, COMMAND_LINE_SIZE);
	saved_command_line[COMMAND_LINE_SIZE-1] = '\0';

	for (;;) {
		/*
		 * "mem=nopentium" disables the 4MB page tables.
		 * "mem=XXX[kKmM]" overrides the BIOS-reported
		 * memory size
		 */
		if (c == ' ' && *(const unsigned long *)from == *(const unsigned long *)"mem=") {
			if (to != command_line) to--;
			if (!memcmp(from+4, "nopentium", 9)) {
				from += 9+4;
				boot_cpu_data.x86_capability &= ~8;
			} else {
				memory_end = simple_strtoul(from+4, &from, 0);
				if ( *from == 'K' || *from == 'k' ) {
					memory_end = memory_end << 10;
					from++;
				} else if ( *from == 'M' || *from == 'm' ) {
					memory_end = memory_end << 20;
					from++;
				}
			}
		}
		c = *(from++);
		if (!c)
			break;
		if (COMMAND_LINE_SIZE <= ++len)
			break;
		*(to++) = c;
	}
	*to = '\0';
	*cmdline_p = command_line;
	memory_end += PAGE_OFFSET;
	*memory_start_p = memory_start;
	*memory_end_p = memory_end;

#ifdef CONFIG_BLK_DEV_INITRD
	if (LOADER_TYPE) {
		initrd_start = INITRD_START ? INITRD_START + PAGE_OFFSET : 0;
		initrd_end = initrd_start+INITRD_SIZE;
		if (initrd_end > memory_end) {
			printk("initrd extends beyond end of memory "
			    "(0x%08lx > 0x%08lx)\ndisabling initrd\n",
			    initrd_end,memory_end);
			initrd_start = 0;
		}
	}
#endif

	/* request io space for devices used on all i[345]86 PC'S */
	request_region(0x00,0x20,"dma1");
	request_region(0x40,0x20,"timer");
	request_region(0x80,0x10,"dma page reg");
	request_region(0xc0,0x20,"dma2");
	request_region(0xf0,0x10,"fpu");
}

/*
 *	Detection of CPU model.
 */

extern inline void cpuid(int op, int *eax, int *ebx, int *ecx, int *edx)
{
	__asm__("cpuid"
		: "=a" (*eax),
		  "=b" (*ebx),
		  "=c" (*ecx),
		  "=d" (*edx)
		: "a" (op)
		: "cc");
}

__initfunc(static int cyrix_model(struct cpuinfo_x86 *c))
{
	int nr = c->x86_model;
	char *buf = c->x86_model_id;

	/* Note that some of the possibilities this decoding allows
	 * have never actually been manufactured - but those that
	 * do actually exist are correctly decoded.
	 */
	if (nr < 0x20) {
		strcpy(buf, "Cx486");
		if (!(nr & 0x10)) {
			sprintf(buf+5, "%c%s%c",
				(nr & 0x01) ? 'D' : 'S',
				(nr & 0x04) ? "Rx" : "LC",
				(nr & 0x02) ? '2' : '\000');
		} else if (!(nr & 0x08)) {
			sprintf(buf+5, "S%s%c",
				(nr & 0x01) ? "2" : "",
				(nr & 0x02) ? 'e' : '\000');
		} else {
			sprintf(buf+5, "DX%c",
				nr == 0x1b ? '2'
					: (nr == 0x1f ? '4' : '\000'));
		}
	} else if (nr >= 0x20 && nr <= 0x4f) {	/* 5x86, 6x86 or Gx86 */
		char *s = "";
		if (nr >= 0x30 && nr < 0x40) {	/* 6x86 */
			if (c->x86 == 5 && (c->x86_capability & (1 << 8)))
				s = "L";	/* 6x86L */
			else if (c->x86 == 6)
				s = "MX";	/* 6x86MX */
			}
		sprintf(buf, "%cx86%s %cx Core/Bus Clock",
			"??56G"[nr>>4],
			s,
			"12??43"[nr & 0x05]);
	} else if (nr >= 0x50 && nr <= 0x5f) {	/* Cyrix 6x86MX */
		sprintf(buf, "6x86MX %c%sx Core/Bus Clock",
			"12233445"[nr & 0x07],
			(nr && !(nr&1)) ? ".5" : "");
	} else if (nr >= 0xfd && c->cpuid_level < 0) {
		/* Probably 0xfd (Cx486[SD]LC with no ID register)
		 * or 0xfe (Cx486 A step with no ID register).
		 */
		strcpy(buf, "Cx486");
	} else
		return 0;	/* Use CPUID if applicable */
	return 1;
}

__initfunc(static int amd_model(struct cpuinfo_x86 *c))
{
	unsigned int n, dummy, *v;

	/* Actually we must have cpuid or we could never have
	 * figured out that this was AMD from the vendor info :-).
	 */

	cpuid(0x80000000, &n, &dummy, &dummy, &dummy);
	if (n < 4)
		return 0;
	v = (unsigned int *) c->x86_model_id;
	cpuid(0x80000002, &v[0], &v[1], &v[2], &v[3]);
	cpuid(0x80000003, &v[4], &v[5], &v[6], &v[7]);
	cpuid(0x80000004, &v[8], &v[9], &v[10], &v[11]);
	c->x86_model_id[48] = 0;
	return 1;
}

__initfunc(void get_cpu_vendor(struct cpuinfo_x86 *c))
{
	char *v = c->x86_vendor_id;

	if (!strcmp(v, "GenuineIntel"))
		c->x86_vendor = X86_VENDOR_INTEL;
	else if (!strcmp(v, "AuthenticAMD"))
		c->x86_vendor = X86_VENDOR_AMD;
	else if (!strncmp(v, "Cyrix", 5))
		c->x86_vendor = X86_VENDOR_CYRIX;
	else if (!strcmp(v, "UMC UMC UMC "))
		c->x86_vendor = X86_VENDOR_UMC;
	else if (!strcmp(v, "CentaurHauls"))
		c->x86_vendor = X86_VENDOR_CENTAUR;
	else if (!strcmp(v, "NexGenDriven"))
		c->x86_vendor = X86_VENDOR_NEXGEN;
	else
		c->x86_vendor = X86_VENDOR_UNKNOWN;
}

struct cpu_model_info {
	int vendor;
	int x86;
	char *model_names[16];
};

static struct cpu_model_info cpu_models[] __initdata = {
	{ X86_VENDOR_INTEL,	4,
	  { "486 DX-25/33", "486 DX-50", "486 SX", "486 DX/2", "486 SL", 
	    "486 SX/2", NULL, "486 DX/2-WB", "486 DX/4", "486 DX/4-WB", NULL, 
	    NULL, NULL, NULL, NULL, NULL }},
	{ X86_VENDOR_INTEL,	5,
	  { "Pentium 60/66 A-step", "Pentium 60/66", "Pentium 75+",
	    "OverDrive PODP5V83", "Pentium MMX", NULL, NULL,
	    "Mobile Pentium 75+", "Mobile Pentium MMX", NULL, NULL, NULL,
	    NULL, NULL, NULL, NULL }},
	{ X86_VENDOR_INTEL,	6,
	  { "Pentium Pro A-step", "Pentium Pro", NULL, "Pentium II (Klamath)", 
	    NULL, "Pentium II (Deschutes)", NULL, NULL, NULL, NULL, NULL, NULL,
	    NULL, NULL, NULL, NULL }},
	{ X86_VENDOR_CYRIX,	4,
	  { NULL, NULL, NULL, NULL, "MediaGX", NULL, NULL, NULL, NULL, "5x86",
	    NULL, NULL, NULL, NULL, NULL, NULL }},
	{ X86_VENDOR_CYRIX,	5,
	  { NULL, NULL, "6x86", NULL, "GXm", NULL, NULL, NULL, NULL,
	    NULL, NULL, NULL, NULL, NULL, NULL, NULL }},
	{ X86_VENDOR_CYRIX,	6,
	  { "6x86MX", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 
	    NULL, NULL, NULL, NULL, NULL, NULL }},
	{ X86_VENDOR_AMD,	4,
	  { NULL, NULL, NULL, "DX/2", NULL, NULL, NULL, "DX/2-WB", "DX/4",
	    "DX/4-WB", NULL, NULL, NULL, NULL, "Am5x86-WT", "Am5x86-WB" }},
	{ X86_VENDOR_AMD,	5,
	  { "K5/SSA5 (PR-75, PR-90, PR-100)", "K5 (PR-120, PR-133)",
	    "K5 (PR-166)", "K5 (PR-200)", NULL, NULL,
	    "K6 (166 - 266)", "K6 (166 - 300)", "K6-3D (200 - 450)",
	    "K6-3D-Plus (200 - 450)", NULL, NULL, NULL, NULL, NULL, NULL }},
	{ X86_VENDOR_UMC,	4,
	  { NULL, "U5D", "U5S", NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	    NULL, NULL, NULL, NULL, NULL, NULL }},
	{ X86_VENDOR_CENTAUR,	5,
	  { NULL, NULL, NULL, NULL, "C6", NULL, NULL, NULL, NULL, NULL, NULL,
	    NULL, NULL, NULL, NULL, NULL }},
	{ X86_VENDOR_NEXGEN,	5,
	  { "Nx586", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	    NULL, NULL, NULL, NULL, NULL, NULL, NULL }},
};

__initfunc(void identify_cpu(struct cpuinfo_x86 *c))
{
	int i;
	char *p = NULL;

	c->loops_per_sec = loops_per_sec;

	get_cpu_vendor(c);

	if (c->x86_vendor == X86_VENDOR_UNKNOWN &&
	    c->cpuid_level < 0)
		return;

	if (c->x86_vendor == X86_VENDOR_CYRIX && cyrix_model(c))
		return;

	if (c->x86_model < 16)
		for (i=0; i<sizeof(cpu_models)/sizeof(struct cpu_model_info); i++)
			if (cpu_models[i].vendor == c->x86_vendor &&
			    cpu_models[i].x86 == c->x86) {
				p = cpu_models[i].model_names[c->x86_model];
				break;
			}
	if (p) {
		strcpy(c->x86_model_id, p);
		return;
	}

	if (c->x86_vendor == X86_VENDOR_AMD && amd_model(c))
		return;

	sprintf(c->x86_model_id, "%02x/%02x", c->x86_vendor, c->x86_model);
}

static char *cpu_vendor_names[] __initdata = {
	"Intel", "Cyrix", "AMD", "UMC", "NexGen", "Centaur" };

__initfunc(void print_cpu_info(struct cpuinfo_x86 *c))
{
	char *vendor = NULL;

	if (c->x86_vendor < sizeof(cpu_vendor_names)/sizeof(char *))
		vendor = cpu_vendor_names[c->x86_vendor];
	else if (c->cpuid_level >= 0)
		vendor = c->x86_vendor_id;

	if (vendor)
		printk("%s ", vendor);

	if (!c->x86_model_id[0])
		printk("%d86", c->x86);
	else
		printk("%s", c->x86_model_id);

	if (c->x86_mask)
		printk(" stepping %02x", c->x86_mask);

	printk("\n");
}

/*
 *	Get CPU information for use by the procfs.
 */

int get_cpuinfo(char * buffer)
{
	char *p = buffer;
	int sep_bug;
        static const char *x86_cap_flags[] = {
                "fpu", "vme", "de", "pse", "tsc", "msr", "pae", "mce",
                "cx8", "apic", "10", "sep", "mtrr", "pge", "mca", "cmov",
                "fcmov", "17", "18", "19", "20", "21", "22", "mmx",
                "osfxsr", "25", "26", "27", "28", "29", "30", "amd3d"
        };
	struct cpuinfo_x86 *c = cpu_data;
	int i, n;

	for(n=0; n<NR_CPUS; n++, c++) {
#ifdef __SMP__
		if (!(cpu_present_map & (1<<n)))
			continue;
#endif
		p += sprintf(p, "processor\t: %d\n"
			       "cpu family\t: %c\n"
			       "model\t\t: %s\n"
			       "vendor_id\t: %s\n",
			       n,
			       c->x86 + '0',
			       c->x86_model_id[0] ? c->x86_model_id : "unknown",
			       c->x86_vendor_id[0] ? c->x86_vendor_id : "unknown");
		if (c->x86_mask) {
			if (c->x86_vendor == X86_VENDOR_CYRIX)
				p += sprintf(p, "stepping\t: %d rev %d\n",
					     c->x86_mask >> 4,
					     c->x86_mask & 0x0f);
			else
				p += sprintf(p, "stepping\t: %d\n", c->x86_mask);
		} else
			p += sprintf(p, "stepping\t: unknown\n");

		sep_bug = c->x86_vendor == X86_VENDOR_INTEL &&
			  c->x86 == 0x06 &&
			  c->cpuid_level >= 0 &&
			  (c->x86_capability & 0x800) &&
			  c->x86_model < 3 &&
			  c->x86_mask < 3;
        
		p += sprintf(p, "fdiv_bug\t: %s\n"
			        "hlt_bug\t\t: %s\n"
			        "sep_bug\t\t: %s\n"
			        "f00f_bug\t: %s\n"
			        "fpu\t\t: %s\n"
			        "fpu_exception\t: %s\n"
			        "cpuid level\t: %d\n"
			        "wp\t\t: %s\n"
			        "flags\t\t:",
			     c->fdiv_bug ? "yes" : "no",
			     c->hlt_works_ok ? "no" : "yes",
			     sep_bug ? "yes" : "no",
			     c->f00f_bug ? "yes" : "no",
			     c->hard_math ? "yes" : "no",
			     (c->hard_math && ignore_irq13) ? "yes" : "no",
			     c->cpuid_level,
			     c->wp_works_ok ? "yes" : "no");

		for ( i = 0 ; i < 32 ; i++ )
			if ( c->x86_capability & (1 << i) )
				p += sprintf(p, " %s", x86_cap_flags[i]);
		p += sprintf(p, "\nbogomips\t: %lu.%02lu\n\n",
			     (c->loops_per_sec+2500)/500000,
			     ((c->loops_per_sec+2500)/5000) % 100);
	}
        return p - buffer;
}
