#include <linux/init.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/smp.h>
#include <asm/semaphore.h>
#include <asm/processor.h>
#include <asm/msr.h>
#include <asm/io.h>
#include <asm/mmu_context.h>

#include "cpu.h"

static int cachesize_override __initdata = -1;
static int disable_x86_fxsr __initdata = 0;

struct cpu_dev * cpu_devs[X86_VENDOR_NUM] = {};

static struct cpu_dev default_cpu;
static struct cpu_dev * this_cpu = &default_cpu;

extern void mcheck_init(struct cpuinfo_x86 *c);

static void default_init(struct cpuinfo_x86 * c)
{
	/* Not much we can do here... */
	/* Check if at least it has cpuid */
	if (c->cpuid_level == -1) {
		/* No cpuid. It must be an ancient CPU */
		if (c->x86 == 4)
			strcpy(c->x86_model_id, "486");
		else if (c->x86 == 3)
			strcpy(c->x86_model_id, "386");
	}
}

static struct cpu_dev default_cpu = {
	c_init:	default_init,
};

static int __init cachesize_setup(char *str)
{
	get_option (&str, &cachesize_override);
	return 1;
}
__setup("cachesize=", cachesize_setup);

#ifndef CONFIG_X86_TSC
static int tsc_disable __initdata = 0;

static int __init tsc_setup(char *str)
{
	tsc_disable = 1;
	return 1;
}

__setup("notsc", tsc_setup);
#endif

int __init get_model_name(struct cpuinfo_x86 *c)
{
	unsigned int *v;
	char *p, *q;

	if (cpuid_eax(0x80000000) < 0x80000004)
		return 0;

	v = (unsigned int *) c->x86_model_id;
	cpuid(0x80000002, &v[0], &v[1], &v[2], &v[3]);
	cpuid(0x80000003, &v[4], &v[5], &v[6], &v[7]);
	cpuid(0x80000004, &v[8], &v[9], &v[10], &v[11]);
	c->x86_model_id[48] = 0;

	/* Intel chips right-justify this string for some dumb reason;
	   undo that brain damage */
	p = q = &c->x86_model_id[0];
	while ( *p == ' ' )
	     p++;
	if ( p != q ) {
	     while ( *p )
		  *q++ = *p++;
	     while ( q <= &c->x86_model_id[48] )
		  *q++ = '\0';	/* Zero-pad the rest */
	}

	return 1;
}


void __init display_cacheinfo(struct cpuinfo_x86 *c)
{
	unsigned int n, dummy, ecx, edx, l2size;

	n = cpuid_eax(0x80000000);

	if (n >= 0x80000005) {
		cpuid(0x80000005, &dummy, &dummy, &ecx, &edx);
		printk(KERN_INFO "CPU: L1 I Cache: %dK (%d bytes/line), D cache %dK (%d bytes/line)\n",
			edx>>24, edx&0xFF, ecx>>24, ecx&0xFF);
		c->x86_cache_size=(ecx>>24)+(edx>>24);	
	}

	if (n < 0x80000006)	/* Some chips just has a large L1. */
		return;

	ecx = cpuid_ecx(0x80000006);
	l2size = ecx >> 16;
	
	/* do processor-specific cache resizing */
	if (this_cpu->c_size_cache)
		l2size = this_cpu->c_size_cache(c,l2size);

	/* Allow user to override all this if necessary. */
	if (cachesize_override != -1)
		l2size = cachesize_override;

	if ( l2size == 0 )
		return;		/* Again, no L2 cache is possible */

	c->x86_cache_size = l2size;

	printk(KERN_INFO "CPU: L2 Cache: %dK (%d bytes/line)\n",
	       l2size, ecx & 0xFF);
}

/* Naming convention should be: <Name> [(<Codename>)] */
/* This table only is used unless init_<vendor>() below doesn't set it; */
/* in particular, if CPUID levels 0x80000002..4 are supported, this isn't used */

/* Look up CPU names by table lookup. */
static char __init *table_lookup_model(struct cpuinfo_x86 *c)
{
	struct cpu_model_info *info;

	if ( c->x86_model >= 16 )
		return NULL;	/* Range check */

	if (!this_cpu)
		return NULL;

	info = this_cpu->c_models;

	while (info && info->family) {
		if (info->family == c->x86)
			return info->model_names[c->x86_model];
		info++;
	}
	return NULL;		/* Not found */
}



void __init get_cpu_vendor(struct cpuinfo_x86 *c)
{
	char *v = c->x86_vendor_id;
	int i;

	for (i = 0; i < X86_VENDOR_NUM; i++) {
		if (cpu_devs[i]) {
			if (!strcmp(v,cpu_devs[i]->c_ident[0]) ||
			    (cpu_devs[i]->c_ident[1] && 
			     !strcmp(v,cpu_devs[i]->c_ident[1]))) {
				c->x86_vendor = i;
				this_cpu = cpu_devs[i];
				break;
			}
		}
	}
}


static int __init x86_fxsr_setup(char * s)
{
	disable_x86_fxsr = 1;
	return 1;
}
__setup("nofxsr", x86_fxsr_setup);


/* Standard macro to see if a specific flag is changeable */
static inline int flag_is_changeable_p(u32 flag)
{
	u32 f1, f2;

	asm("pushfl\n\t"
	    "pushfl\n\t"
	    "popl %0\n\t"
	    "movl %0,%1\n\t"
	    "xorl %2,%0\n\t"
	    "pushl %0\n\t"
	    "popfl\n\t"
	    "pushfl\n\t"
	    "popl %0\n\t"
	    "popfl\n\t"
	    : "=&r" (f1), "=&r" (f2)
	    : "ir" (flag));

	return ((f1^f2) & flag) != 0;
}


/* Probe for the CPUID instruction */
int __init have_cpuid_p(void)
{
	return flag_is_changeable_p(X86_EFLAGS_ID);
}

void __init generic_identify(struct cpuinfo_x86 * c)
{
	u32 tfms;
	int junk;

	if (have_cpuid_p()) {
		/* Get vendor name */
		cpuid(0x00000000, &c->cpuid_level,
		      (int *)&c->x86_vendor_id[0],
		      (int *)&c->x86_vendor_id[8],
		      (int *)&c->x86_vendor_id[4]);
		
		get_cpu_vendor(c);
		/* Initialize the standard set of capabilities */
		/* Note that the vendor-specific code below might override */
	
		/* Intel-defined flags: level 0x00000001 */
		if ( c->cpuid_level >= 0x00000001 ) {
			u32 capability;
			cpuid(0x00000001, &tfms, &junk, &junk, &capability);
			c->x86_capability[0] = capability;
			c->x86 = (tfms >> 8) & 15;
			c->x86_model = (tfms >> 4) & 15;
			c->x86_mask = tfms & 15;
		} else {
			/* Have CPUID level 0 only - unheard of */
			c->x86 = 4;
		}
	}
}

/*
 * This does the hard work of actually picking apart the CPU stuff...
 */
void __init identify_cpu(struct cpuinfo_x86 *c)
{
	int i;

	c->loops_per_jiffy = loops_per_jiffy;
	c->x86_cache_size = -1;
	c->x86_vendor = X86_VENDOR_UNKNOWN;
	c->cpuid_level = -1;	/* CPUID not detected */
	c->x86_model = c->x86_mask = 0;	/* So far unknown... */
	c->x86_vendor_id[0] = '\0'; /* Unset */
	c->x86_model_id[0] = '\0';  /* Unset */
	memset(&c->x86_capability, 0, sizeof c->x86_capability);

	if (!have_cpuid_p()) {
		/* First of all, decide if this is a 486 or higher */
		/* It's a 486 if we can modify the AC flag */
		if ( flag_is_changeable_p(X86_EFLAGS_AC) )
			c->x86 = 4;
		else
			c->x86 = 3;
	}

	if (this_cpu->c_identify)
		this_cpu->c_identify(c);
	else
		generic_identify(c);

	printk(KERN_DEBUG "CPU: Before vendor init, caps: %08lx %08lx %08lx, vendor = %d\n",
	       c->x86_capability[0],
	       c->x86_capability[1],
	       c->x86_capability[2],
	       c->x86_vendor);

	/*
	 * Vendor-specific initialization.  In this section we
	 * canonicalize the feature flags, meaning if there are
	 * features a certain CPU supports which CPUID doesn't
	 * tell us, CPUID claiming incorrect flags, or other bugs,
	 * we handle them here.
	 *
	 * At the end of this section, c->x86_capability better
	 * indicate the features this CPU genuinely supports!
	 */
	if (this_cpu->c_init)
		this_cpu->c_init(c);

	printk(KERN_DEBUG "CPU: After vendor init, caps: %08lx %08lx %08lx %08lx\n",
	       c->x86_capability[0],
	       c->x86_capability[1],
	       c->x86_capability[2],
	       c->x86_capability[3]);

	/*
	 * The vendor-specific functions might have changed features.  Now
	 * we do "generic changes."
	 */

	/* TSC disabled? */
#ifndef CONFIG_X86_TSC
	if ( tsc_disable )
		clear_bit(X86_FEATURE_TSC, c->x86_capability);
#endif

	/* FXSR disabled? */
	if (disable_x86_fxsr) {
		clear_bit(X86_FEATURE_FXSR, c->x86_capability);
		clear_bit(X86_FEATURE_XMM, c->x86_capability);
	}

	/* Init Machine Check Exception if available. */
	mcheck_init(c);

	/* If the model name is still unset, do table lookup. */
	if ( !c->x86_model_id[0] ) {
		char *p;
		p = table_lookup_model(c);
		if ( p )
			strcpy(c->x86_model_id, p);
		else
			/* Last resort... */
			sprintf(c->x86_model_id, "%02x/%02x",
				c->x86_vendor, c->x86_model);
	}

	/* Now the feature flags better reflect actual CPU features! */

	printk(KERN_DEBUG "CPU:     After generic, caps: %08lx %08lx %08lx %08lx\n",
	       c->x86_capability[0],
	       c->x86_capability[1],
	       c->x86_capability[2],
	       c->x86_capability[3]);

	/*
	 * On SMP, boot_cpu_data holds the common feature set between
	 * all CPUs; so make sure that we indicate which features are
	 * common between the CPUs.  The first time this routine gets
	 * executed, c == &boot_cpu_data.
	 */
	if ( c != &boot_cpu_data ) {
		/* AND the already accumulated flags with these */
		for ( i = 0 ; i < NCAPINTS ; i++ )
			boot_cpu_data.x86_capability[i] &= c->x86_capability[i];
	}

	printk(KERN_DEBUG "CPU:             Common caps: %08lx %08lx %08lx %08lx\n",
	       boot_cpu_data.x86_capability[0],
	       boot_cpu_data.x86_capability[1],
	       boot_cpu_data.x86_capability[2],
	       boot_cpu_data.x86_capability[3]);
}
/*
 *	Perform early boot up checks for a valid TSC. See arch/i386/kernel/time.c
 */
 
void __init dodgy_tsc(void)
{
	get_cpu_vendor(&boot_cpu_data);
	if (( boot_cpu_data.x86_vendor == X86_VENDOR_CYRIX ) ||
	    ( boot_cpu_data.x86_vendor == X86_VENDOR_NSC   ))
		cpu_devs[X86_VENDOR_CYRIX]->c_init(&boot_cpu_data);
}

void __init print_cpu_info(struct cpuinfo_x86 *c)
{
	char *vendor = NULL;

	if (c->x86_vendor < X86_VENDOR_NUM)
		vendor = this_cpu->c_vendor;
	else if (c->cpuid_level >= 0)
		vendor = c->x86_vendor_id;

	if (vendor && strncmp(c->x86_model_id, vendor, strlen(vendor)))
		printk("%s ", vendor);

	if (!c->x86_model_id[0])
		printk("%d86", c->x86);
	else
		printk("%s", c->x86_model_id);

	if (c->x86_mask || c->cpuid_level >= 0) 
		printk(" stepping %02x\n", c->x86_mask);
	else
		printk("\n");
}

unsigned long cpu_initialized __initdata = 0;

/* This is hacky. :)
 * We're emulating future behavior.
 * In the future, the cpu-specific init functions will be called implicitly
 * via the magic of initcalls.
 * They will insert themselves into the cpu_devs structure.
 * Then, when cpu_init() is called, we can just iterate over that array.
 */

extern int intel_cpu_init(void);
extern int cyrix_init_cpu(void);
extern int nsc_init_cpu(void);
extern int amd_init_cpu(void);
extern int centaur_init_cpu(void);
extern int transmeta_init_cpu(void);
extern int rise_init_cpu(void);
extern int nexgen_init_cpu(void);
extern int umc_init_cpu(void);

void __init early_cpu_init(void)
{
	intel_cpu_init();
	cyrix_init_cpu();
	nsc_init_cpu();
	amd_init_cpu();
	centaur_init_cpu();
	transmeta_init_cpu();
	rise_init_cpu();
	nexgen_init_cpu();
	umc_init_cpu();
}
/*
 * cpu_init() initializes state that is per-CPU. Some data is already
 * initialized (naturally) in the bootstrap process, such as the GDT
 * and IDT. We reload them nevertheless, this function acts as a
 * 'CPU state barrier', nothing should get across.
 */
void __init cpu_init (void)
{
	int nr = smp_processor_id();
	struct tss_struct * t = &init_tss[nr];

	if (test_and_set_bit(nr, &cpu_initialized)) {
		printk(KERN_WARNING "CPU#%d already initialized!\n", nr);
		for (;;) __sti();
	}
	printk(KERN_INFO "Initializing CPU#%d\n", nr);

	if (cpu_has_vme || cpu_has_tsc || cpu_has_de)
		clear_in_cr4(X86_CR4_VME|X86_CR4_PVI|X86_CR4_TSD|X86_CR4_DE);
#ifndef CONFIG_X86_TSC
	if (tsc_disable && cpu_has_tsc) {
		printk(KERN_NOTICE "Disabling TSC...\n");
		/**** FIX-HPA: DOES THIS REALLY BELONG HERE? ****/
		clear_bit(X86_FEATURE_TSC, boot_cpu_data.x86_capability);
		set_in_cr4(X86_CR4_TSD);
	}
#endif

	__asm__ __volatile__("lgdt %0": "=m" (gdt_descr));
	__asm__ __volatile__("lidt %0": "=m" (idt_descr));

	/*
	 * Delete NT
	 */
	__asm__("pushfl ; andl $0xffffbfff,(%esp) ; popfl");

	/*
	 * set up and load the per-CPU TSS and LDT
	 */
	atomic_inc(&init_mm.mm_count);
	current->active_mm = &init_mm;
	if(current->mm)
		BUG();
	enter_lazy_tlb(&init_mm, current, nr);

	t->esp0 = current->thread.esp0;
	set_tss_desc(nr,t);
	gdt_table[__TSS(nr)].b &= 0xfffffdff;
	load_TR(nr);
	load_LDT(&init_mm.context);

	/* Clear %fs and %gs. */
	asm volatile ("xorl %eax, %eax; movl %eax, %fs; movl %eax, %gs");

	/* Clear all 6 debug registers: */

#define CD(register) __asm__("movl %0,%%db" #register ::"r"(0) );

	CD(0); CD(1); CD(2); CD(3); /* no db4 and db5 */; CD(6); CD(7);

#undef CD

	/*
	 * Force FPU initialization:
	 */
	clear_thread_flag(TIF_USEDFPU);
	current->used_math = 0;
	stts();
}
