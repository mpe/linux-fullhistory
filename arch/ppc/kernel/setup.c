/*
 * BK Id: SCCS/s.setup.c 1.65 11/18/01 20:57:25 trini
 */
/*
 * Common prep/pmac/chrp boot and setup code.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/reboot.h>
#include <linux/delay.h>
#include <linux/blk.h>
#include <linux/ide.h>
#include <linux/tty.h>
#include <linux/bootmem.h>
#include <linux/seq_file.h>

#include <asm/residual.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/processor.h>
#include <asm/pgtable.h>
#include <asm/bootinfo.h>
#include <asm/setup.h>
#include <asm/amigappc.h>
#include <asm/smp.h>
#include <asm/elf.h>
#include <asm/cputable.h>
#ifdef CONFIG_8xx
#include <asm/mpc8xx.h>
#include <asm/8xx_immap.h>
#endif
#ifdef CONFIG_8260
#include <asm/mpc8260.h>
#include <asm/immap_8260.h>
#endif
#ifdef CONFIG_4xx
#include <asm/ppc4xx.h>
#endif
#include <asm/bootx.h>
#include <asm/btext.h>
#include <asm/machdep.h>
#include <asm/feature.h>
#include <asm/uaccess.h>
#include <asm/system.h>

extern void platform_init(unsigned long r3, unsigned long r4,
		unsigned long r5, unsigned long r6, unsigned long r7);
extern void bootx_init(unsigned long r4, unsigned long phys);
extern unsigned long reloc_offset(void);
extern void identify_cpu(unsigned long offset, unsigned long cpu);
extern void do_cpu_ftr_fixups(unsigned long offset);

#ifdef CONFIG_XMON
extern void xmon_map_scc(void);
#endif

extern boot_infos_t *boot_infos;
char saved_command_line[256];
unsigned char aux_device_present;
struct ide_machdep_calls ppc_ide_md;
char *sysmap;
unsigned long sysmap_size;

/* Used with the BI_MEMSIZE bootinfo parameter to store the memory
   size value reported by the boot loader. */ 
unsigned int boot_mem_size;

int parse_bootinfo(void);

unsigned long ISA_DMA_THRESHOLD;
unsigned long DMA_MODE_READ, DMA_MODE_WRITE;

#ifdef CONFIG_ALL_PPC
int _machine = 0;

extern void prep_init(unsigned long r3, unsigned long r4,
		unsigned long r5, unsigned long r6, unsigned long r7);
extern void pmac_init(unsigned long r3, unsigned long r4,
		unsigned long r5, unsigned long r6, unsigned long r7);
extern void chrp_init(unsigned long r3, unsigned long r4,
		unsigned long r5, unsigned long r6, unsigned long r7);
#endif /* CONFIG_ALL_PPC */

#ifdef CONFIG_MAGIC_SYSRQ
unsigned long SYSRQ_KEY;
#endif /* CONFIG_MAGIC_SYSRQ */

#ifdef CONFIG_VGA_CONSOLE
unsigned long vgacon_remap_base;
#endif

struct machdep_calls ppc_md;

/*
 * These are used in binfmt_elf.c to put aux entries on the stack
 * for each elf executable being started.
 */
int dcache_bsize;
int icache_bsize;
int ucache_bsize;

#ifdef CONFIG_VGA_CONSOLE
struct screen_info screen_info = {
	0, 25,			/* orig-x, orig-y */
	0,			/* unused */
	0,			/* orig-video-page */
	0,			/* orig-video-mode */
	80,			/* orig-video-cols */
	0,0,0,			/* ega_ax, ega_bx, ega_cx */
	25,			/* orig-video-lines */
	1,			/* orig-video-isVGA */
	16			/* orig-video-points */
};
#endif /* CONFIG_VGA_CONSOLE */

void machine_restart(char *cmd)
{
	ppc_md.restart(cmd);
}
  
void machine_power_off(void)
{
	ppc_md.power_off();
}
  
void machine_halt(void)
{
	ppc_md.halt();
}

#ifdef CONFIG_TAU
extern u32 cpu_temp(unsigned long cpu);
extern u32 cpu_temp_both(unsigned long cpu);
#endif /* CONFIG_TAU */

int show_cpuinfo(struct seq_file *m, void *v)
{
	int i = (int) v - 1;
	int err = 0;
	unsigned int pvr;
	unsigned short maj, min;
	unsigned long lpj;

	if (i >= NR_CPUS) {
		/* Show summary information */
#ifdef CONFIG_SMP
		unsigned long bogosum = 0;
		for (i = 0; i < smp_num_cpus; ++i)
			if (cpu_online_map & (1 << i))
				bogosum += cpu_data[i].loops_per_jiffy;
		seq_printf(m, "total bogomips\t: %lu.%02lu\n",
			   bogosum/(500000/HZ), bogosum/(5000/HZ) % 100);
#endif /* CONFIG_SMP */

		if (ppc_md.show_cpuinfo != NULL)
			err = ppc_md.show_cpuinfo(m);
		return err;
	}

#ifdef CONFIG_SMP
	if (!(cpu_online_map & (1 << i)))
		return 0;
	pvr = cpu_data[i].pvr;
	lpj = cpu_data[i].loops_per_jiffy;
	seq_printf(m, "processor\t: %lu\n", i);
#else
	pvr = mfspr(PVR);
	lpj = loops_per_jiffy;
#endif

	seq_printf(m, "cpu\t\t: ");

	if (cur_cpu_spec[i]->pvr_mask)
		seq_printf(m, "%s", cur_cpu_spec[i]->cpu_name);
	else
		seq_printf(m, "unknown (%08x)", pvr);
#ifdef CONFIG_ALTIVEC
	if (cur_cpu_spec[i]->cpu_features & CPU_FTR_ALTIVEC)
		seq_printf(m, ", altivec supported");
#endif
	seq_printf(m, "\n");

#ifdef CONFIG_TAU
	if (cur_cpu_spec[i]->cpu_features & CPU_FTR_TAU) {
#ifdef CONFIG_TAU_AVERAGE
		/* more straightforward, but potentially misleading */
		seq_printf(m,  "temperature \t: %u C (uncalibrated)\n",
			   cpu_temp(i));
#else
		/* show the actual temp sensor range */
		u32 temp;
		temp = cpu_temp_both(i);
		seq_printf(m, "temperature \t: %u-%u C (uncalibrated)\n",
			   temp & 0xff, temp >> 16);
#endif
	}
#endif /* CONFIG_TAU */

	if (ppc_md.show_percpuinfo != NULL) {
		err = ppc_md.show_percpuinfo(m, i);
		if (err)
			return err;
	}

	switch (PVR_VER(pvr)) {
	case 0x0020:	/* 403 family */
		maj = PVR_MAJ(pvr) + 1;
		min = PVR_MIN(pvr);
		break;
	case 0x1008:	/* 740P/750P ?? */
		maj = ((pvr >> 8) & 0xFF) - 1;
		min = pvr & 0xFF;
		break;
	default:
		maj = (pvr >> 8) & 0xFF;
		min = pvr & 0xFF;
		break;
	}

	seq_printf(m, "revision\t: %hd.%hd (pvr %04x %04x)\n", 
		   maj, min, PVR_VER(pvr), PVR_REV(pvr));

	seq_printf(m, "bogomips\t: %lu.%02lu\n",
		   lpj / (500000/HZ), (lpj / (5000/HZ)) % 100);

#ifdef CONFIG_SMP
	seq_printf(m, "\n");
#endif

	return 0;
}


static void *c_start(struct seq_file *m, loff_t *pos)
{
	int i = *pos;

	return i <= NR_CPUS? (void *) (i + 1): NULL;
}

static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return c_start(m, pos);
}

static void c_stop(struct seq_file *m, void *v)
{
}

struct seq_operations cpuinfo_op = {
	start:	c_start,
	next:	c_next,
	stop:	c_stop,
	show:	show_cpuinfo,
};

/*
 * We're called here very early in the boot.  We determine the machine
 * type and call the appropriate low-level setup functions.
 *  -- Cort <cort@fsmlabs.com>
 *
 * Note that the kernel may be running at an address which is different
 * from the address that it was linked at, so we must use RELOC/PTRRELOC
 * to access static data (including strings).  -- paulus
 */
__init
unsigned long
early_init(int r3, int r4, int r5)
{
	extern char __bss_start, _end;
 	unsigned long phys;
	unsigned long offset = reloc_offset();

 	/* Default */
 	phys = offset + KERNELBASE;

	/* First zero the BSS -- use memset, some arches don't have
	 * caches on yet */
	memset_io(PTRRELOC(&__bss_start), 0, &_end - &__bss_start);

	/*
	 * Identify the CPU type and fix up code sections
	 * that depend on which cpu we have.
	 */
	identify_cpu(offset, 0);
	do_cpu_ftr_fixups(offset);

#if defined(CONFIG_ALL_PPC)
	/* If we came here from BootX, clear the screen,
	 * set up some pointers and return. */
	if ((r3 == 0x426f6f58) && (r5 == 0)) {
		bootx_init(r4, phys);
		return phys;
	}

	/* check if we're prep, return if we are */
	if ( *(unsigned long *)(0) == 0xdeadc0de )
		return phys;

	/* 
	 * for now, don't use bootinfo because it breaks yaboot 0.5
	 * and assume that if we didn't find a magic number, we have OF
	 */
	phys = prom_init(r3, r4, (prom_entry)r5);
#endif

	return phys;
}

#ifdef CONFIG_ALL_PPC
void __init
intuit_machine_type(void)
{
	char *model;
	struct device_node *root;
			
	/* ask the OF info if we're a chrp or pmac */
	root = find_path_device("/");
	if (root != 0) {
		/* assume pmac unless proven to be chrp -- Cort */
		_machine = _MACH_Pmac;
		model = get_property(root, "device_type", NULL);
		if (model && !strncmp("chrp", model, 4))
			_machine = _MACH_chrp;
		else {
			model = get_property(root, "model", NULL);
			if (model && !strncmp(model, "IBM", 3))
				_machine = _MACH_chrp;
		}
	}
}

/*
 * The ALL_PPC version of platform_init...
 */
void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	      unsigned long r6, unsigned long r7)
{
#ifdef CONFIG_BOOTX_TEXT
	extern boot_infos_t *disp_bi;

	if (disp_bi) {
		btext_clearscreen();
		btext_welcome(disp_bi);
	}
#endif	

	/* if we didn't get any bootinfo telling us what we are... */
	if (_machine == 0) {
		/* prep boot loader tells us if we're prep or not */
		if ( *(unsigned long *)(KERNELBASE) == (0xdeadc0de) )
			_machine = _MACH_prep;
	}

	/* not much more to do here, if prep */
	if (_machine == _MACH_prep) {
		prep_init(r3, r4, r5, r6, r7);
		return;
	}

	/* prom_init has already been called from __start */
	if (boot_infos)
		relocate_nodes();

	/* If we aren't PReP, we can find out if we're Pmac
	 * or CHRP with this. */
	if (_machine == 0)
		intuit_machine_type();

	/* finish_device_tree may need _machine defined. */
	finish_device_tree();

	/*
	 * If we were booted via quik, r3 points to the physical
	 * address of the command-line parameters.
	 * If we were booted from an xcoff image (i.e. netbooted or
	 * booted from floppy), we get the command line from the
	 * bootargs property of the /chosen node.
	 * If an initial ramdisk is present, r3 and r4
	 * are used for initrd_start and initrd_size,
	 * otherwise they contain 0xdeadbeef.  
	 */
	if (r3 >= 0x4000 && r3 < 0x800000 && r4 == 0) {
		cmd_line[0] = 0;
		strncpy(cmd_line, (char *)r3 + KERNELBASE,
			sizeof(cmd_line));
	} else if (boot_infos != 0) {
		/* booted by BootX - check for ramdisk */
		if (boot_infos->kernelParamsOffset != 0)
			strncpy(cmd_line, (char *) boot_infos
				+ boot_infos->kernelParamsOffset,
				sizeof(cmd_line));
#ifdef CONFIG_BLK_DEV_INITRD
		if (boot_infos->ramDisk) {
			initrd_start = (unsigned long) boot_infos
				+ boot_infos->ramDisk;
			initrd_end = initrd_start + boot_infos->ramDiskSize;
			initrd_below_start_ok = 1;
		}
#endif
	} else {
		struct device_node *chosen;
		char *p;
			
#ifdef CONFIG_BLK_DEV_INITRD
		if (r3 && r4 && r4 != 0xdeadbeef)
			{
				if (r3 < KERNELBASE)
					r3 += KERNELBASE;
				initrd_start = r3;
				initrd_end = r3 + r4;
				ROOT_DEV = MKDEV(RAMDISK_MAJOR, 0);
				initrd_below_start_ok = 1;
			}
#endif
		chosen = find_devices("chosen");
		if (chosen != NULL) {
			p = get_property(chosen, "bootargs", NULL);
			if (p && *p) {
				cmd_line[0] = 0;
				strncpy(cmd_line, p, sizeof(cmd_line));
			}
		}
	}
	cmd_line[sizeof(cmd_line) - 1] = 0;

	switch (_machine) {
	case _MACH_Pmac:
		pmac_init(r3, r4, r5, r6, r7);
		break;
	case _MACH_chrp:
		chrp_init(r3, r4, r5, r6, r7);
		break;
	}
}
#endif /* CONFIG_ALL_PPC */

int parse_bootinfo(void)
{
	struct bi_record *rec;
	extern char __bss_start[];

	rec = (struct bi_record *)_ALIGN((ulong)__bss_start+(1<<20)-1,(1<<20));
	if ( rec->tag != BI_FIRST ) {
		/*
		 * This 0x10000 offset is a terrible hack but it will go away when
		 * we have the bootloader handle all the relocation and
		 * prom calls -- Cort
		 */
		rec = (struct bi_record *)_ALIGN((ulong)__bss_start+0x10000+(1<<20)-1,(1<<20));
		if ( rec->tag != BI_FIRST )
			return -1;
	}
	for ( ; rec->tag != BI_LAST ;
	      rec = (struct bi_record *)((ulong)rec + rec->size) )
	{
		ulong *data = rec->data;
		switch (rec->tag) {
		case BI_CMD_LINE:
			memcpy(cmd_line, (void *)data, rec->size);
			break;
		case BI_SYSMAP:
			sysmap = (char *)((data[0] >= (KERNELBASE)) ? data[0] :
					  (data[0]+KERNELBASE));
			sysmap_size = data[1];
			break;
#ifdef CONFIG_BLK_DEV_INITRD
		case BI_INITRD:
			initrd_start = data[0] + KERNELBASE;
			initrd_end = data[0] + data[1] + KERNELBASE;
			break;
#endif /* CONFIG_BLK_DEV_INITRD */
#ifdef CONFIG_ALL_PPC
		case BI_MACHTYPE:
			_machine = data[0];
			break;
#endif /* CONFIG_ALL_PPC */
		case BI_MEMSIZE:
			boot_mem_size = data[0];
			break;
		}
	}

	return 0;
}

/*
 * Find out what kind of machine we're on and save any data we need
 * from the early boot process (devtree is copied on pmac by prom_init()).
 * This is called very early on the boot process, after a minimal
 * MMU environment has been set up but before MMU_init is called.
 */
void __init
machine_init(unsigned long r3, unsigned long r4, unsigned long r5,
	     unsigned long r6, unsigned long r7)
{
#ifdef CONFIG_CMDLINE
	strcpy(cmd_line, CONFIG_CMDLINE);
#endif /* CONFIG_CMDLINE */

	parse_bootinfo();

	platform_init(r3, r4, r5, r6, r7);

	if (ppc_md.progress)
		ppc_md.progress("id mach(): done", 0x200);
}

/* Checks "l2cr=xxxx" command-line option */
int __init ppc_setup_l2cr(char *str)
{
	if (cur_cpu_spec[0]->cpu_features & CPU_FTR_L2CR) {
		unsigned long val = simple_strtoul(str, NULL, 0);
		printk(KERN_INFO "l2cr set to %lx\n", val);
                _set_L2CR(0);           /* force invalidate by disable cache */
                _set_L2CR(val);         /* and enable it */
	}
	return 1;
}
__setup("l2cr=", ppc_setup_l2cr);

void __init ppc_init(void)
{
	/* clear the progress line */
	if ( ppc_md.progress ) ppc_md.progress("             ", 0xffff);
	
	if (ppc_md.init != NULL) {
		ppc_md.init();
	}
}

/* Warning, IO base is not yet inited */
void __init setup_arch(char **cmdline_p)
{
	extern int panic_timeout;
	extern char _etext[], _edata[];
	extern char *klimit;
	extern void do_init_bootmem(void);

	/* so udelay does something sensible, assume <= 1000 bogomips */
	loops_per_jiffy = 500000000 / HZ;

#ifdef CONFIG_ALL_PPC
	feature_init();
#endif
#ifdef CONFIG_XMON
	xmon_map_scc();
	if (strstr(cmd_line, "xmon"))
		xmon(0);
#endif /* CONFIG_XMON */
	if ( ppc_md.progress ) ppc_md.progress("setup_arch: enter", 0x3eab);

#if defined(CONFIG_KGDB)
	kgdb_map_scc();
	set_debug_traps();
	breakpoint();
#endif

	/*
	 * Set cache line size based on type of cpu as a default.
	 * Systems with OF can look in the properties on the cpu node(s)
	 * for a possibly more accurate value.
	 */
	if (cur_cpu_spec[0]->cpu_features & CPU_FTR_SPLIT_ID_CACHE) {
		dcache_bsize = cur_cpu_spec[0]->dcache_bsize;
		icache_bsize = cur_cpu_spec[0]->icache_bsize;
		ucache_bsize = 0;
	} else
		ucache_bsize = dcache_bsize = icache_bsize
			= cur_cpu_spec[0]->dcache_bsize;

	/* reboot on panic */
	panic_timeout = 180;

	init_mm.start_code = PAGE_OFFSET;
	init_mm.end_code = (unsigned long) _etext;
	init_mm.end_data = (unsigned long) _edata;
	init_mm.brk = (unsigned long) klimit;
	
	/* Save unparsed command line copy for /proc/cmdline */
	strcpy(saved_command_line, cmd_line);
	*cmdline_p = cmd_line;

	/* set up the bootmem stuff with available memory */
	do_init_bootmem();
	if ( ppc_md.progress ) ppc_md.progress("setup_arch: bootmem", 0x3eab);

	ppc_md.setup_arch();
	if ( ppc_md.progress ) ppc_md.progress("arch: exit", 0x3eab);

#if defined(CONFIG_PCI) && defined(CONFIG_ALL_PPC)
	/* We create the "pci-OF-bus-map" property now so it appear in the
	 * /proc device tree
	 */
	if (have_of) {
		struct property* of_prop;
		
		of_prop = (struct property*)alloc_bootmem(sizeof(struct property) + 256);
		if (of_prop && find_path_device("/")) {
			memset(of_prop, -1, sizeof(struct property) + 256);
			of_prop->name = "pci-OF-bus-map";
			of_prop->length = 256;
			of_prop->value = (unsigned char *)&of_prop[1];
			prom_add_property(find_path_device("/"), of_prop);
		}
	}
#endif /* CONFIG_PCI && CONFIG_ALL_PPC */

	paging_init();
	sort_exception_table();

	/* this is for modules since _machine can be a define -- Cort */
	ppc_md.ppc_machine = _machine;
}

/* Convert the shorts/longs in hd_driveid from little to big endian;
 * chars are endian independant, of course, but strings need to be flipped.
 * (Despite what it says in drivers/block/ide.h, they come up as little
 * endian...)
 *
 * Changes to linux/hdreg.h may require changes here. */
void ppc_generic_ide_fix_driveid(struct hd_driveid *id)
{
        int i;
	unsigned short *stringcast;

	id->config         = __le16_to_cpu(id->config);
	id->cyls           = __le16_to_cpu(id->cyls);
	id->reserved2      = __le16_to_cpu(id->reserved2);
	id->heads          = __le16_to_cpu(id->heads);
	id->track_bytes    = __le16_to_cpu(id->track_bytes);
	id->sector_bytes   = __le16_to_cpu(id->sector_bytes);
	id->sectors        = __le16_to_cpu(id->sectors);
	id->vendor0        = __le16_to_cpu(id->vendor0);
	id->vendor1        = __le16_to_cpu(id->vendor1);
	id->vendor2        = __le16_to_cpu(id->vendor2);
	stringcast = (unsigned short *)&id->serial_no[0];
	for (i = 0; i < (20/2); i++)
	        stringcast[i] = __le16_to_cpu(stringcast[i]);
	id->buf_type       = __le16_to_cpu(id->buf_type);
	id->buf_size       = __le16_to_cpu(id->buf_size);
	id->ecc_bytes      = __le16_to_cpu(id->ecc_bytes);
	stringcast = (unsigned short *)&id->fw_rev[0];
	for (i = 0; i < (8/2); i++)
	        stringcast[i] = __le16_to_cpu(stringcast[i]);
	stringcast = (unsigned short *)&id->model[0];
	for (i = 0; i < (40/2); i++)
	        stringcast[i] = __le16_to_cpu(stringcast[i]);
	id->dword_io       = __le16_to_cpu(id->dword_io);
	id->reserved50     = __le16_to_cpu(id->reserved50);
	id->field_valid    = __le16_to_cpu(id->field_valid);
	id->cur_cyls       = __le16_to_cpu(id->cur_cyls);
	id->cur_heads      = __le16_to_cpu(id->cur_heads);
	id->cur_sectors    = __le16_to_cpu(id->cur_sectors);
	id->cur_capacity0  = __le16_to_cpu(id->cur_capacity0);
	id->cur_capacity1  = __le16_to_cpu(id->cur_capacity1);
	id->lba_capacity   = __le32_to_cpu(id->lba_capacity);
	id->dma_1word      = __le16_to_cpu(id->dma_1word);
	id->dma_mword      = __le16_to_cpu(id->dma_mword);
	id->eide_pio_modes = __le16_to_cpu(id->eide_pio_modes);
	id->eide_dma_min   = __le16_to_cpu(id->eide_dma_min);
	id->eide_dma_time  = __le16_to_cpu(id->eide_dma_time);
	id->eide_pio       = __le16_to_cpu(id->eide_pio);
	id->eide_pio_iordy = __le16_to_cpu(id->eide_pio_iordy);
	for (i = 0; i < 2; i++)
		id->words69_70[i] = __le16_to_cpu(id->words69_70[i]);
        for (i = 0; i < 4; i++)
                id->words71_74[i] = __le16_to_cpu(id->words71_74[i]);
	id->queue_depth	   = __le16_to_cpu(id->queue_depth);
	for (i = 0; i < 4; i++)
		id->words76_79[i] = __le16_to_cpu(id->words76_79[i]);
	id->major_rev_num  = __le16_to_cpu(id->major_rev_num);
	id->minor_rev_num  = __le16_to_cpu(id->minor_rev_num);
	id->command_set_1  = __le16_to_cpu(id->command_set_1);
	id->command_set_2  = __le16_to_cpu(id->command_set_2);
	id->cfsse          = __le16_to_cpu(id->cfsse);
	id->cfs_enable_1   = __le16_to_cpu(id->cfs_enable_1);
	id->cfs_enable_2   = __le16_to_cpu(id->cfs_enable_2);
	id->csf_default    = __le16_to_cpu(id->csf_default);
	id->dma_ultra      = __le16_to_cpu(id->dma_ultra);
	id->word89         = __le16_to_cpu(id->word89);
	id->word90         = __le16_to_cpu(id->word90);
	id->CurAPMvalues   = __le16_to_cpu(id->CurAPMvalues);
	id->word92         = __le16_to_cpu(id->word92);
	id->hw_config      = __le16_to_cpu(id->hw_config);
	for (i = 0; i < 32; i++)
		id->words94_125[i]  = __le16_to_cpu(id->words94_125[i]);
	id->last_lun       = __le16_to_cpu(id->last_lun);
	id->word127        = __le16_to_cpu(id->word127);
	id->dlf            = __le16_to_cpu(id->dlf);
	id->csfo           = __le16_to_cpu(id->csfo);
	for (i = 0; i < 26; i++)
		id->words130_155[i] = __le16_to_cpu(id->words130_155[i]);
	id->word156        = __le16_to_cpu(id->word156);
	for (i = 0; i < 3; i++)
		id->words157_159[i] = __le16_to_cpu(id->words157_159[i]);
	for (i = 0; i < 96; i++)
		id->words160_255[i] = __le16_to_cpu(id->words160_255[i]);
}
