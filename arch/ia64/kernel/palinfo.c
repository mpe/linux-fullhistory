/*
 * palinfo.c
 *
 * Prints processor specific information reported by PAL.
 * This code is based on specification of PAL as of the
 * Intel IA-64 Architecture Software Developer's Manual v1.0.
 *
 * 
 * Copyright (C) 2000 Hewlett-Packard Co
 * Copyright (C) 2000 Stephane Eranian <eranian@hpl.hp.com>
 * 
 * 05/26/2000	S.Eranian	initial release
 *
 * ISSUES:
 *	- because of some PAL bugs, some calls return invalid results or
 *	  are empty for now.
 *	- remove hack to avoid problem with <= 256M RAM for itr.
 */
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/mm.h>

#include <asm/pal.h>
#include <asm/sal.h>
#include <asm/efi.h>
#include <asm/page.h>
#include <asm/processor.h>

/*
 * Hope to get rid of these in a near future
*/
#define IA64_PAL_VERSION_BUG		1

#define PALINFO_VERSION "0.1"

typedef int (*palinfo_func_t)(char*);

typedef struct {
	const char		*name;		/* name of the proc entry */
	palinfo_func_t		proc_read;	/* function to call for reading */
	struct proc_dir_entry	*entry;		/* registered entry (removal) */
} palinfo_entry_t;

static struct proc_dir_entry *palinfo_dir;

/*
 *  A bunch of string array to get pretty printing
 */

static char *cache_types[] = {
	"",			/* not used */
	"Instruction",
	"Data",
	"Data/Instruction"	/* unified */
};

static const char *cache_mattrib[]={
	"WriteThrough",
	"WriteBack",
	"",		/* reserved */
	""		/* reserved */
};

static const char *cache_st_hints[]={
	"Temporal, level 1",
	"Reserved",
	"Reserved",
	"Non-temporal, all levels",
	"Reserved",
	"Reserved",
	"Reserved",	
	"Reserved"
};

static const char *cache_ld_hints[]={
	"Temporal, level 1",
	"Non-temporal, level 1",
	"Reserved",
	"Non-temporal, all levels",
	"Reserved",
	"Reserved",
	"Reserved",	
	"Reserved"
};

static const char *rse_hints[]={
	"enforced lazy",
	"eager stores",
	"eager loads",
	"eager loads and stores"
};

#define RSE_HINTS_COUNT (sizeof(rse_hints)/sizeof(const char *))

/*
 * The current resvision of the Volume 2 of 
 * IA-64 Architecture Software Developer's Manual is wrong.
 * Table 4-10 has invalid information concerning the ma field:
 * Correct table is:
 *      bit 0 - 001 - UC
 *      bit 4 - 100 - UC
 *      bit 5 - 101 - UCE
 *      bit 6 - 110 - WC
 *      bit 7 - 111 - NatPage 
 */
static const char *mem_attrib[]={
	"Write Back (WB)",		/* 000 */
	"Uncacheable (UC)",		/* 001 */
	"Reserved",			/* 010 */
	"Reserved",			/* 011 */
	"Uncacheable (UC)",		/* 100 */
	"Uncacheable Exported (UCE)",	/* 101 */
	"Write Coalescing (WC)",	/* 110 */
	"NaTPage"			/* 111 */
};



/*
 * Allocate a buffer suitable for calling PAL code in Virtual mode
 *
 * The documentation (PAL2.6) requires thius buffer to have a pinned
 * translation to avoid any DTLB faults. For this reason we allocate
 * a page (large enough to hold any possible reply) and use a DTC
 * to hold the translation during the call. A call the free_palbuffer()
 * is required to release ALL resources (page + translation).
 *
 * The size of the page allocated is based on the PAGE_SIZE defined
 * at compile time for the kernel, i.e.  >= 4Kb.
 *
 * Return: a pointer to the newly allocated page (virtual address)
 */
static void *
get_palcall_buffer(void)
{
	void *tmp;

	tmp = (void *)__get_free_page(GFP_KERNEL);
	if (tmp == 0) {
		printk(KERN_ERR "%s: can't get a buffer page\n", __FUNCTION__);
	} else if ( ((u64)tmp - PAGE_OFFSET) > (1<<_PAGE_SIZE_256M) )  { /* XXX: temporary hack */
		unsigned long flags;

		/* PSR.ic must be zero to insert new DTR */
		ia64_clear_ic(flags);

		/*
		 * we  only insert of DTR
		 *
		 * XXX: we need to figure out a way to "allocate" TR(s) to avoid
		 * conflicts. Maybe something in an include file like pgtable.h
		 * page.h or processor.h
		 *
		 * ITR0/DTR0: used for kernel code/data
		 * ITR1/DTR1: used by HP simulator
		 * ITR2/DTR2: used to map PAL code
		 */
		ia64_itr(0x2, 3, (u64)tmp,
			 pte_val(mk_pte_phys(__pa(tmp), __pgprot(__DIRTY_BITS|_PAGE_PL_0|_PAGE_AR_RW))), PAGE_SHIFT);

		ia64_srlz_d ();

		__restore_flags(flags);	
	}

	return tmp;
}

/*
 * Free a palcall buffer allocated with the previous call
 *
 * The translation is also purged.
 */
static void
free_palcall_buffer(void *addr)
{
	__free_page(addr);
	ia64_ptr(0x2, (u64)addr, PAGE_SHIFT);
	ia64_srlz_d ();
}

/*
 * Take a 64bit vector and produces a string such that
 * if bit n is set then 2^n in clear text is generated. The adjustment
 * to the right unit is also done.
 *
 * Input:
 *	- a pointer to a buffer to hold the string
 * 	- a 64-bit vector
 * Ouput:
 *	- a pointer to the end of the buffer
 *
 */
static char *
bitvector_process(char *p, u64 vector)
{
	int i,j;
	const char *units[]={ "", "K", "M", "G", "T" };

	for (i=0, j=0; i < 64; i++ , j=i/10) {
		if (vector & 0x1) {
			p += sprintf(p, "%d%s ", 1 << (i-j*10), units[j]);
		}
		vector >>= 1;
	}
	return p;
}

/*
 * Take a 64bit vector and produces a string such that
 * if bit n is set then register n is present. The function
 * takes into account consecutive registers and prints out ranges.
 *
 * Input:
 *	- a pointer to a buffer to hold the string
 * 	- a 64-bit vector
 * Ouput:
 *	- a pointer to the end of the buffer
 *
 */
static char *
bitregister_process(char *p, u64 *reg_info, int max)
{
	int i, begin, skip = 0;
	u64 value = reg_info[0];

	value >>= i = begin = ffs(value) - 1;

	for(; i < max; i++ ) {

		if (i != 0 && (i%64) == 0) value = *++reg_info;

		if ((value & 0x1) == 0 && skip == 0) {
			if (begin  <= i - 2) 
				p += sprintf(p, "%d-%d ", begin, i-1);
			else
				p += sprintf(p, "%d ", i-1);
			skip  = 1;
			begin = -1;
		} else if ((value & 0x1) && skip == 1) {
			skip = 0;
			begin = i;
		}
		value >>=1;
	}
	if (begin > -1) {
		if (begin < 127) 
			p += sprintf(p, "%d-127", begin);
		else
			p += sprintf(p, "127");
	}

	return p;
}

static int
power_info(char *page)
{
	s64 status;
	char *p = page;
	pal_power_mgmt_info_u_t *halt_info;
	int i;

	halt_info = get_palcall_buffer();
	if (halt_info == 0) return 0;

	status = ia64_pal_halt_info(halt_info);
	if (status != 0) {
		free_palcall_buffer(halt_info);
		return 0;
	}

	for (i=0; i < 8 ; i++ ) {
		if (halt_info[i].pal_power_mgmt_info_s.im == 1) {
			p += sprintf(p,	"Power level %d:\n" \
					"\tentry_latency       : %d cycles\n" \
				 	"\texit_latency        : %d cycles\n" \
					"\tpower consumption   : %d mW\n" \
					"\tCache+TLB coherency : %s\n", i,
				halt_info[i].pal_power_mgmt_info_s.entry_latency,
				halt_info[i].pal_power_mgmt_info_s.exit_latency,
				halt_info[i].pal_power_mgmt_info_s.power_consumption,
				halt_info[i].pal_power_mgmt_info_s.co ? "Yes" : "No");
		} else {
			p += sprintf(p,"Power level %d: not implemented\n",i);
		}
	}

	free_palcall_buffer(halt_info);

	return p - page;
}

static int 
cache_info(char *page)
{
	char *p = page;
	u64 levels, unique_caches;
	pal_cache_config_info_t cci;
	int i,j, k;
	s64 status;

	if ((status=ia64_pal_cache_summary(&levels, &unique_caches)) != 0) {
			printk("ia64_pal_cache_summary=%ld\n", status);
			return 0;
	}

	p += sprintf(p, "Cache levels  : %ld\n" \
			"Unique caches : %ld\n\n",
			levels,
			unique_caches);

	for (i=0; i < levels; i++) {

		for (j=2; j >0 ; j--) {

			/* even without unification some level may not be present */
			if ((status=ia64_pal_cache_config_info(i,j, &cci)) != 0) {
				continue;
			}
			p += sprintf(p, "%s Cache level %d:\n" \
					"\tSize           : %ld bytes\n" \
					"\tAttributes     : ",
					cache_types[j+cci.pcci_unified], i+1,
					cci.pcci_cache_size);

			if (cci.pcci_unified) p += sprintf(p, "Unified ");

			p += sprintf(p, "%s\n", cache_mattrib[cci.pcci_cache_attr]);

			p += sprintf(p, "\tAssociativity  : %d\n" \
					"\tLine size      : %d bytes\n" \
					"\tStride         : %d bytes\n",
					cci.pcci_assoc,
					1<<cci.pcci_line_size,
					1<<cci.pcci_stride);
			if (j == 1)
				p += sprintf(p, "\tStore latency  : N/A\n");
			else
				p += sprintf(p, "\tStore latency  : %d cycle(s)\n",
						cci.pcci_st_latency);

			p += sprintf(p, "\tLoad latency   : %d cycle(s)\n" \
					"\tStore hints    : ",
					cci.pcci_ld_latency);

			for(k=0; k < 8; k++ ) {
				if ( cci.pcci_st_hints & 0x1) p += sprintf(p, "[%s]", cache_st_hints[k]);
				cci.pcci_st_hints >>=1; 
			}
			p += sprintf(p, "\n\tLoad hints     : ");

			for(k=0; k < 8; k++ ) {
				if ( cci.pcci_ld_hints & 0x1) p += sprintf(p, "[%s]", cache_ld_hints[k]);
				cci.pcci_ld_hints >>=1; 
			}
			p += sprintf(p, "\n\tAlias boundary : %d byte(s)\n" \
					"\tTag LSB        : %d\n" \
					"\tTag MSB        : %d\n",
					1<<cci.pcci_alias_boundary,
					cci.pcci_tag_lsb,
					cci.pcci_tag_msb);

			/* when unified, data(j=2) is enough */
			if (cci.pcci_unified) break;
		}
	}
	return p - page;
}


static int
vm_info(char *page)
{
	char *p = page;
	u64 tr_pages =0, vw_pages=0, tc_pages;
	u64 attrib;
	pal_vm_info_1_u_t vm_info_1;
	pal_vm_info_2_u_t vm_info_2;
	pal_tc_info_u_t	tc_info;
	ia64_ptce_info_t ptce;
	int i, j;
	s64 status;

	if ((status=ia64_pal_vm_summary(&vm_info_1, &vm_info_2)) !=0) {
		printk("ia64_pal_vm_summary=%ld\n", status);
		return 0;
	}


	p += sprintf(p, "Physical Address Space         : %d bits\n" \
			"Virtual Address Space          : %d bits\n" \
			"Protection Key Registers(PKR)  : %d\n" \
			"Implemented bits in PKR.key    : %d\n" \
			"Hash Tag ID                    : 0x%x\n" \
			"Size of RR.rid                 : %d\n",
			vm_info_1.pal_vm_info_1_s.phys_add_size,
			vm_info_2.pal_vm_info_2_s.impl_va_msb+1,
			vm_info_1.pal_vm_info_1_s.max_pkr+1,
			vm_info_1.pal_vm_info_1_s.key_size,
			vm_info_1.pal_vm_info_1_s.hash_tag_id,
			vm_info_2.pal_vm_info_2_s.rid_size);

	if (ia64_pal_mem_attrib(&attrib) != 0) return 0;

	p += sprintf(p, "Supported memory attributes    : %s\n", mem_attrib[attrib&0x7]);

	if ((status=ia64_pal_vm_page_size(&tr_pages, &vw_pages)) !=0) {
		printk("ia64_pal_vm_page_size=%ld\n", status);
		return 0;
	}

	p += sprintf(p, "\nTLB walker                     : %s implemented\n" \
			"Number of DTR                  : %d\n" \
			"Number of ITR                  : %d\n" \
			"TLB insertable page sizes      : ",
			vm_info_1.pal_vm_info_1_s.vw ? "\b":"not",
			vm_info_1.pal_vm_info_1_s.max_dtr_entry+1,
			vm_info_1.pal_vm_info_1_s.max_itr_entry+1);


	p = bitvector_process(p, tr_pages);

	p += sprintf(p, "\nTLB purgeable page sizes       : ");

	p = bitvector_process(p, vw_pages);

	if ((status=ia64_get_ptce(&ptce)) != 0) {
		printk("ia64_get_ptce=%ld\n",status);
		return 0;
	}

	p += sprintf(p, "\nPurge base address             : 0x%016lx\n" \
			"Purge outer loop count         : %d\n" \
			"Purge inner loop count         : %d\n" \
			"Purge outer loop stride        : %d\n" \
			"Purge inner loop stride        : %d\n",
			ptce.base,
			ptce.count[0],
			ptce.count[1],
			ptce.stride[0],
			ptce.stride[1]);

	p += sprintf(p, "TC Levels                      : %d\n" \
			"Unique TC(s)                   : %d\n", 
			vm_info_1.pal_vm_info_1_s.num_tc_levels,
			vm_info_1.pal_vm_info_1_s.max_unique_tcs);

	for(i=0; i < vm_info_1.pal_vm_info_1_s.num_tc_levels; i++) {
		for (j=2; j>0 ; j--) {
			tc_pages = 0; /* just in case */

		
			/* even without unification, some levels may not be present */
			if ((status=ia64_pal_vm_info(i,j, &tc_info, &tc_pages)) != 0) {
				continue;
			}

			p += sprintf(p, "\n%s Translation Cache Level %d:\n" \
					"\tHash sets           : %d\n" \
					"\tAssociativity       : %d\n" \
					"\tNumber of entries   : %d\n" \
					"\tFlags               : ",
					cache_types[j+tc_info.tc_unified], i+1,
					tc_info.tc_num_sets,
					tc_info.tc_associativity,
					tc_info.tc_num_entries);

			if (tc_info.tc_pf) p += sprintf(p, "PreferredPageSizeOptimized ");
			if (tc_info.tc_unified) p += sprintf(p, "Unified ");
			if (tc_info.tc_reduce_tr) p += sprintf(p, "TCReduction");

			p += sprintf(p, "\n\tSupported page sizes: ");

			p = bitvector_process(p, tc_pages);

			/* when unified date (j=2) is enough */
			if (tc_info.tc_unified) break;
		}
	}
	p += sprintf(p, "\n");

	return p - page;	
}


static int
register_info(char *page)
{
	char *p = page;
	u64 reg_info[2];
	u64 info;
	u64 phys_stacked;
	pal_hints_u_t hints;
	u64 iregs, dregs;
	char *info_type[]={
		"Implemented AR(s)",
		"AR(s) with read side-effects",
		"Implemented CR(s)",
		"CR(s) with read side-effects",
	};

	for(info=0; info < 4; info++) {

		if (ia64_pal_register_info(info, &reg_info[0], &reg_info[1]) != 0) return 0;

	 	p += sprintf(p, "%-32s : ", info_type[info]);

		p = bitregister_process(p, reg_info, 128);

		p += sprintf(p, "\n");
	}

	if (ia64_pal_rse_info(&phys_stacked, &hints) != 0) return 0;

	p += sprintf(p, "RSE stacked physical registers   : %ld\n" \
			"RSE load/store hints             : %ld (%s)\n",
			phys_stacked,
			hints.ph_data, 
		     	hints.ph_data < RSE_HINTS_COUNT ? rse_hints[hints.ph_data]: "(??)");

	if (ia64_pal_debug_info(&iregs, &dregs)) return 0;

	p += sprintf(p, "Instruction debug register pairs : %ld\n" \
			"Data debug register pairs        : %ld\n",
			iregs, dregs);

	return p - page;
}

static const char *proc_features[]={
	NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,
	NULL,NULL,NULL,NULL,NULL,NULL,NULL, NULL,NULL,
	NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,
	NULL,NULL,NULL,NULL,NULL, NULL,NULL,NULL,NULL,
	NULL,NULL,NULL,NULL,NULL,
	"XIP,XPSR,XFS implemented",
	"XR1-XR3 implemented",
	"Disable dynamic predicate prediction",
	"Disable processor physical number",
	"Disable dynamic data cache prefetch",
	"Disable dynamic inst cache prefetch",
	"Disable dynamic branch prediction",
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	"Disable BINIT on processor time-out",
	"Disable dynamic power management (DPM)",
	"Disable coherency", 
	"Disable cache", 
	"Enable CMCI promotion",
	"Enable MCA to BINIT promotion",
	"Enable MCA promotion",
	"Enable BEER promotion"
};

	
static int
processor_info(char *page)
{
	char *p = page;
	const char **v = proc_features;
	u64 avail=1, status=1, control=1;
	int i;
	s64 ret;

	/* must be in physical mode */
	if ((ret=ia64_pal_proc_get_features(&avail, &status, &control)) != 0) return 0;

	for(i=0; i < 64; i++, v++,avail >>=1, status >>=1, control >>=1) {
		if ( ! *v ) continue;
		p += sprintf(p, "%-40s : %s%s %s\n", *v, 
				avail & 0x1 ? "" : "NotImpl",
				avail & 0x1 ? (status & 0x1 ? "On" : "Off"): "",
				avail & 0x1 ? (control & 0x1 ? "Ctrl" : "NoCtrl"): "");
	}
	return p - page;
}

/*
 * physical mode call for PAL_VERSION is working fine.
 * This function is meant to go away once PAL get fixed.
 */
static inline s64 
ia64_pal_version_phys(pal_version_u_t *pal_min_version, pal_version_u_t *pal_cur_version) 
{	
	struct ia64_pal_retval iprv;
	PAL_CALL_PHYS(iprv, PAL_VERSION, 0, 0, 0);
	if (pal_min_version)
		pal_min_version->pal_version_val = iprv.v0;
	if (pal_cur_version)
		pal_cur_version->pal_version_val = iprv.v1;
	return iprv.status; 
}

static int
version_info(char *page)
{
	s64 status;
	pal_version_u_t min_ver, cur_ver;
	char *p = page;

#ifdef IA64_PAL_VERSION_BUG
	/* The virtual mode call is buggy. But the physical mode call seems
	 * to be ok. Until they fix virtual mode, we do physical.
	 */
	status = ia64_pal_version_phys(&min_ver, &cur_ver);
#else
	/* The system crashes if you enable this code with the wrong PAL 
	 * code
	 */
	status = ia64_pal_version(&min_ver, &cur_ver);
#endif
	if (status != 0) return 0;

	p += sprintf(p, "PAL_vendor     : 0x%x (min=0x%x)\n" \
			"PAL_A revision : 0x%x (min=0x%x)\n" \
			"PAL_A model    : 0x%x (min=0x%x)\n" \
			"PAL_B mode     : 0x%x (min=0x%x)\n" \
			"PAL_B revision : 0x%x (min=0x%x)\n",
	     		cur_ver.pal_version_s.pv_pal_vendor,
	     		min_ver.pal_version_s.pv_pal_vendor,
	     		cur_ver.pal_version_s.pv_pal_a_rev,
	     		cur_ver.pal_version_s.pv_pal_a_rev,
	     		cur_ver.pal_version_s.pv_pal_a_model,
	     		min_ver.pal_version_s.pv_pal_a_model,
	     		cur_ver.pal_version_s.pv_pal_b_rev,
	     		min_ver.pal_version_s.pv_pal_b_rev,
	     		cur_ver.pal_version_s.pv_pal_b_model,
	     		min_ver.pal_version_s.pv_pal_b_model);

	return p - page;
}

static int
perfmon_info(char *page)
{
	char *p = page;
	u64 *pm_buffer;
	pal_perf_mon_info_u_t pm_info;

	pm_buffer = (u64 *)get_palcall_buffer();
	if (pm_buffer == 0) return 0;

	if (ia64_pal_perf_mon_info(pm_buffer, &pm_info) != 0) {
		free_palcall_buffer(pm_buffer);
		return 0;
	}

#ifdef IA64_PAL_PERF_MON_INFO_BUG
	pm_buffer[5]=0x3;
	pm_info.pal_perf_mon_info_s.cycles  = 0x12;
	pm_info.pal_perf_mon_info_s.retired = 0x08;
#endif

	p += sprintf(p, "PMC/PMD pairs                 : %d\n" \
			"Counter width                 : %d bits\n" \
			"Cycle event number            : %d\n" \
			"Retired event number          : %d\n" \
			"Implemented PMC               : ", 
			pm_info.pal_perf_mon_info_s.generic,
			pm_info.pal_perf_mon_info_s.width,
			pm_info.pal_perf_mon_info_s.cycles,
			pm_info.pal_perf_mon_info_s.retired);

	p = bitregister_process(p, pm_buffer, 256);

	p += sprintf(p, "\nImplemented PMD               : ");
	
	p = bitregister_process(p, pm_buffer+4, 256);

	p += sprintf(p, "\nCycles count capable          : ");
	
	p = bitregister_process(p, pm_buffer+8, 256);

	p += sprintf(p, "\nRetired bundles count capable : ");
	
	p = bitregister_process(p, pm_buffer+12, 256);

	p += sprintf(p, "\n");

	free_palcall_buffer(pm_buffer);

	return p - page;
}

static int
frequency_info(char *page)
{
	char *p = page;
	struct pal_freq_ratio proc, itc, bus;
	u64 base;

	if (ia64_pal_freq_base(&base) == -1)
		p += sprintf(p, "Output clock            : not implemented\n"); 
	else
		p += sprintf(p, "Output clock            : %ld ticks/s\n", base);

	if (ia64_pal_freq_ratios(&proc, &bus, &itc) != 0) return 0;

	p += sprintf(p, "Processor/Clock ratio   : %ld/%ld\n" \
			"Bus/Clock ratio         : %ld/%ld\n" \
			"ITC/Clock ratio         : %ld/%ld\n",
			proc.num, proc.den,
			bus.num, bus.den,
			itc.num, itc.den);

	return p - page;
}


/*
 * Entry point routine: all calls go trhough this function
 */
static int
palinfo_read_entry(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	palinfo_func_t info = (palinfo_func_t)data;
        int len = info(page);

        if (len <= off+count) *eof = 1;

        *start = page + off;
        len   -= off;

        if (len>count) len = count;
        if (len<0) len = 0;

        return len;
}

/*
 * List names,function pairs for every entry in /proc/palinfo
 * Must be terminated with the NULL,NULL entry.
 */
static palinfo_entry_t palinfo_entries[]={
	{ "version_info",	version_info, },
	{ "vm_info", 		vm_info, },
	{ "cache_info",		cache_info, },
	{ "power_info",		power_info, },
	{ "register_info",	register_info, },
	{ "processor_info",	processor_info, },
	{ "perfmon_info",	perfmon_info, },
	{ "frequency_info",	frequency_info, },
	{ NULL,			NULL,}
};


static int __init 
palinfo_init(void)
{
	palinfo_entry_t *p;

	printk(KERN_INFO "PAL Information Facility v%s\n", PALINFO_VERSION);

	palinfo_dir = create_proc_entry("palinfo",  S_IFDIR | S_IRUGO | S_IXUGO, NULL);

	for (p = palinfo_entries; p->name ; p++){
		p->entry = create_proc_read_entry (p->name, 0, palinfo_dir, 
						   palinfo_read_entry, p->proc_read);
	}

	return 0;
}

static int __exit
palinfo_exit(void)
{
	palinfo_entry_t *p;

	for (p = palinfo_entries; p->name ; p++){
		remove_proc_entry (p->name, palinfo_dir);
	}
	remove_proc_entry ("palinfo", 0);

	return 0;
}

module_init(palinfo_init);
module_exit(palinfo_exit);
