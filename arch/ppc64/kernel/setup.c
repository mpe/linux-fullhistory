/*
 * 
 * Common boot and setup code.
 *
 * Copyright (C) 2001 PPC64 Team, IBM Corp
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
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
#include <linux/seq_file.h>
#include <linux/ioport.h>
#include <linux/tty.h>
#include <asm/init.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/processor.h>
#include <asm/pgtable.h>
#include <asm/bootinfo.h>
#include <asm/smp.h>
#include <asm/elf.h>
#include <asm/machdep.h>
#include <asm/iSeries/LparData.h>
#include <asm/Naca.h>
#include <asm/Paca.h>
#include <asm/ppcdebug.h>
#include <asm/time.h>

extern unsigned long klimit;
/* extern void *stab; */
extern HTAB htab_data;
extern unsigned long loops_per_jiffy;

extern unsigned long embedded_sysmap_start;
extern unsigned long embedded_sysmap_end;

int have_of = 1;

extern void  chrp_init(unsigned long r3,
		       unsigned long r4,
		       unsigned long r5,
		       unsigned long r6,
		       unsigned long r7);

extern void chrp_init_map_io_space( void );
extern void iSeries_init( void );
extern void iSeries_init_early( void );
extern void pSeries_init_early( void );
extern void pSeriesLP_init_early(void);
extern void mm_init_ppc64( void ); 

unsigned long decr_overclock = 1;
unsigned long decr_overclock_proc0 = 1;
unsigned long decr_overclock_set = 0;
unsigned long decr_overclock_proc0_set = 0;

#ifdef CONFIG_XMON
extern void xmon_map_scc(void);
#endif

char saved_command_line[256];
unsigned char aux_device_present;

void parse_cmd_line(unsigned long r3, unsigned long r4, unsigned long r5,
		    unsigned long r6, unsigned long r7);
int parse_bootinfo(void);

int _machine = _MACH_unknown;

#ifdef CONFIG_MAGIC_SYSRQ
unsigned long SYSRQ_KEY;
#endif /* CONFIG_MAGIC_SYSRQ */

struct machdep_calls ppc_md;
struct Naca *naca;

/*
 * Perhaps we can put the pmac screen_info[] here
 * on pmac as well so we don't need the ifdef's.
 * Until we get multiple-console support in here
 * that is.  -- Cort
 * Maybe tie it to serial consoles, since this is really what
 * these processors use on existing boards.  -- Dan
 */ 
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

/*
 * These are used in binfmt_elf.c to put aux entries on the stack
 * for each elf executable being started.
 */
int dcache_bsize;
int icache_bsize;
int ucache_bsize;

/*
 * Initialize the PPCDBG state.  Called before relocation has been enabled.
 */
void ppcdbg_initialize(void) {
	unsigned long offset = reloc_offset();
	struct Naca *_naca = RELOC(naca);

	_naca->debug_switch = PPC_DEBUG_DEFAULT; /* | PPCDBG_BUSWALK | PPCDBG_PHBINIT | PPCDBG_MM | PPCDBG_MMINIT | PPCDBG_TCEINIT | PPCDBG_TCE */;
}

/* 
 * Initialize a set of PACA's, one for each processor.
 *
 * At this point, relocation is on, but we have not done any other
 * setup of the mm subsystem.
 */
void paca_init(void) {
#if 0
	int processorCount = naca->processorCount, i;
	struct Paca *paca[];

	/* Put the array of paca's on a page boundary & allocate 1/2 page of */
	/* storage for each.                                                 */  
	klimit += (PAGE_SIZE-1) & PAGE_MASK;
	naca->xPaca = paca[0] = klimit;
	klimit += ((PAGE_SIZE>>1) * processorCount); 

	for(i=0; i<processorCount; i++) {
		paca[0]->xPacaIndex = i;
	}
#endif
}

/*
 * Do some initial setup of the system.  The paramters are those which 
 * were passed in from the bootloader.
 */
void setup_system(unsigned long r3, unsigned long r4, unsigned long r5,
		  unsigned long r6, unsigned long r7)
{
	/* This should be fixed properly in kernel/resource.c */
	iomem_resource.end = MEM_SPACE_LIMIT;

	/* pSeries systems are identified in prom.c via OF. */
	if ( itLpNaca.xLparInstalled == 1 )
		_machine = _MACH_iSeries;
	switch (_machine) {
	case _MACH_iSeries:
		iSeries_init_early();
		break;

#ifdef CONFIG_PPC_PSERIES
	case _MACH_pSeries:
		pSeries_init_early();
#ifdef CONFIG_BLK_DEV_INITRD
		initrd_start = initrd_end = 0;
#endif
		parse_bootinfo();
		break;

	case _MACH_pSeriesLP:
		pSeriesLP_init_early();
#ifdef CONFIG_BLK_DEV_INITRD
		initrd_start = initrd_end = 0;
#endif
		parse_bootinfo();
		break;
#endif
	}

	udbg_puts("\n-----------------------------------------------------\n");
	udbg_puts("Naca Info...\n\n");
	udbg_puts("naca                       = 0x");
	udbg_puthex((unsigned long)naca);
	udbg_putc('\n');

	udbg_puts("naca->processorCount       = 0x");
	udbg_puthex(naca->processorCount);
	udbg_putc('\n');

	udbg_puts("naca->physicalMemorySize   = 0x");
	udbg_puthex(naca->physicalMemorySize);
	udbg_putc('\n');

	udbg_puts("naca->dCacheL1LineSize     = 0x");
	udbg_puthex(naca->dCacheL1LineSize);
	udbg_putc('\n');

	udbg_puts("naca->dCacheL1LogLineSize  = 0x");
	udbg_puthex(naca->dCacheL1LogLineSize);
	udbg_putc('\n');

	udbg_puts("naca->dCacheL1LinesPerPage = 0x");
	udbg_puthex(naca->dCacheL1LinesPerPage);
	udbg_putc('\n');

	udbg_puts("naca->iCacheL1LineSize     = 0x");
	udbg_puthex(naca->iCacheL1LineSize);
	udbg_putc('\n');

	udbg_puts("naca->iCacheL1LogLineSize  = 0x");
	udbg_puthex(naca->iCacheL1LogLineSize);
	udbg_putc('\n');

	udbg_puts("naca->iCacheL1LinesPerPage = 0x");
	udbg_puthex(naca->iCacheL1LinesPerPage);
	udbg_putc('\n');

	udbg_puts("naca->pftSize              = 0x");
	udbg_puthex(naca->pftSize);
	udbg_putc('\n');

	udbg_puts("naca->serialPortAddr       = 0x");
	udbg_puthex(naca->serialPortAddr);
	udbg_putc('\n');

	udbg_puts("naca->interrupt_controller = 0x");
	udbg_puthex(naca->interrupt_controller);
	udbg_putc('\n');

	udbg_printf("\nHTAB Info ...\n\n"); 
	udbg_puts("htab_data.htab             = 0x");
	udbg_puthex((unsigned long)htab_data.htab);
	udbg_putc('\n');
	udbg_puts("htab_data.num_ptegs        = 0x");
	udbg_puthex(htab_data.htab_num_ptegs);
	udbg_putc('\n');

	udbg_puts("\n-----------------------------------------------------\n");


	if ( _machine & _MACH_pSeries ) {
		finish_device_tree();
		chrp_init(r3, r4, r5, r6, r7);
	}

	mm_init_ppc64();

	switch (_machine) {
	case _MACH_iSeries:
		iSeries_init();
		break;
	default:
		/* The following relies on the device tree being */
		/* fully configured.                             */
		parse_cmd_line(r3, r4, r5, r6, r7);
	}
}

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

unsigned long ppc_proc_freq;
unsigned long ppc_tb_freq;

static int show_cpuinfo(struct seq_file *m, void *v)
{
	unsigned long cpu_id = (unsigned long)v - 1;
	unsigned int pvr;
	unsigned short maj;
	unsigned short min;

#ifdef CONFIG_SMP
	if (cpu_id == NR_CPUS) {

		if (ppc_md.get_cpuinfo != NULL)
			ppc_md.get_cpuinfo(m);

		return 0;
	}

	if (!(cpu_online_map & (1<<cpu_id)))
		return 0;
#endif

	pvr = xPaca[cpu_id].pvr;
	maj = (pvr >> 8) & 0xFF;
	min = pvr & 0xFF;

	seq_printf(m, "processor\t: %lu\n", cpu_id);
	seq_printf(m, "cpu\t\t: ");

	pvr = xPaca[cpu_id].pvr;

	switch (PVR_VER(pvr)) {
	case PV_PULSAR:
		seq_printf(m, "RS64-III (pulsar)\n");
		break;
	case PV_POWER4:
		seq_printf(m, "POWER4 (gp)\n");
		break;
	case PV_ICESTAR:
		seq_printf(m, "RS64-III (icestar)\n");
		break;
	case PV_SSTAR:
		seq_printf(m, "RS64-IV (sstar)\n");
		break;
	case PV_630:
		seq_printf(m, "POWER3 (630)\n");
		break;
	case PV_630p:
		seq_printf(m, "POWER3 (630+)\n");
		break;
	default:
		seq_printf(m, "Unknown (%08x)\n", pvr);
		break;
	}

	/*
	 * Assume here that all clock rates are the same in a
	 * smp system.  -- Cort
	 */
	if (_machine != _MACH_iSeries) {
		struct device_node *cpu_node;
		int *fp;

		cpu_node = find_type_devices("cpu");
		if (cpu_node) {
			fp = (int *) get_property(cpu_node, "clock-frequency",
						  NULL);
			if (fp)
				seq_printf(m, "clock\t\t: %dMHz\n",
					   *fp / 1000000);
		}
	}

	if (ppc_md.setup_residual != NULL)
		ppc_md.setup_residual(m, cpu_id);

	seq_printf(m, "revision\t: %hd.%hd\n", maj, min);
	
	return 0;
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	return *pos <= NR_CPUS ? (void *)((*pos)+1) : NULL;
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
 * Fetch the cmd_line from open firmware. */
void parse_cmd_line(unsigned long r3, unsigned long r4, unsigned long r5,
		  unsigned long r6, unsigned long r7)
{
	struct device_node *chosen;
	char *p;

#ifdef CONFIG_BLK_DEV_INITRD
	if ((initrd_start == 0) && r3 && r4 && r4 != 0xdeadbeef) {
		initrd_start = (r3 >= KERNELBASE) ? r3 : (unsigned long)__va(r3);
		initrd_end = initrd_start + r4;
		ROOT_DEV = mk_kdev(RAMDISK_MAJOR, 0);
		initrd_below_start_ok = 1;
	}
#endif

	cmd_line[0] = 0;
	chosen = find_devices("chosen");
	if (chosen != NULL) {
		p = get_property(chosen, "bootargs", NULL);
		if (p != NULL)
			strncpy(cmd_line, p, sizeof(cmd_line));
	}
	cmd_line[sizeof(cmd_line) - 1] = 0;

	/* Look for mem= option on command line */
	if (strstr(cmd_line, "mem=")) {
		char *p, *q;
		unsigned long maxmem = 0;
		extern unsigned long __max_memory;

		for (q = cmd_line; (p = strstr(q, "mem=")) != 0; ) {
			q = p + 4;
			if (p > cmd_line && p[-1] != ' ')
				continue;
			maxmem = simple_strtoul(q, &q, 0);
			if (*q == 'k' || *q == 'K') {
				maxmem <<= 10;
				++q;
			} else if (*q == 'm' || *q == 'M') {
				maxmem <<= 20;
				++q;
			}
		}
		__max_memory = maxmem;
	}
	ppc_md.progress("id mach: done", 0x200);
}


char *bi_tag2str(unsigned long tag)
{
	switch (tag) {
	case BI_FIRST:
		return "BI_FIRST";
	case BI_LAST:
		return "BI_LAST";
	case BI_CMD_LINE:
		return "BI_CMD_LINE";
	case BI_BOOTLOADER_ID:
		return "BI_BOOTLOADER_ID";
	case BI_INITRD:
		return "BI_INITRD";
	case BI_SYSMAP:
		return "BI_SYSMAP";
	case BI_MACHTYPE:
		return "BI_MACHTYPE";
	default:
		return "BI_UNKNOWN";
	}
}

int parse_bootinfo(void)
{
	struct bi_record *rec;
	extern char *sysmap;
	extern unsigned long sysmap_size;

	rec = prom.bi_recs;

	if ( rec == NULL || rec->tag != BI_FIRST )
		return -1;

	for ( ; rec->tag != BI_LAST ; rec = bi_rec_next(rec) ) {
		switch (rec->tag) {
		case BI_CMD_LINE:
			memcpy(cmd_line, (void *)rec->data, rec->size);
			break;
		case BI_SYSMAP:
			sysmap = (char *)((rec->data[0] >= (KERNELBASE))
					? rec->data[0] : (unsigned long)__va(rec->data[0]));
			sysmap_size = rec->data[1];
			break;
#ifdef CONFIG_BLK_DEV_INITRD
		case BI_INITRD:
			initrd_start = (unsigned long)__va(rec->data[0]);
			initrd_end = initrd_start + rec->data[1];
			ROOT_DEV = mk_kdev(RAMDISK_MAJOR, 0);
			initrd_below_start_ok = 1;
			break;
#endif /* CONFIG_BLK_DEV_INITRD */
		}
	}

	return 0;
}

int __init ppc_init(void)
{
	/* clear the progress line */
	ppc_md.progress(" ", 0xffff);

	if (ppc_md.init != NULL) {
		ppc_md.init();
	}
	return 0;
}

arch_initcall(ppc_init);

void __init ppc64_calibrate_delay(void)
{
	loops_per_jiffy = tb_ticks_per_jiffy;

	printk("Calibrating delay loop... %lu.%02lu BogoMips\n",
			       loops_per_jiffy/(500000/HZ),
			       loops_per_jiffy/(5000/HZ) % 100);

}	

extern void (*calibrate_delay)(void);

/*
 * Called into from start_kernel, after lock_kernel has been called.
 * Initializes bootmem, which is unsed to manage page allocation until
 * mem_init is called.
 */
void __init setup_arch(char **cmdline_p)
{
	extern int panic_timeout;
	extern char _etext[], _edata[];
	extern void do_init_bootmem(void);

	calibrate_delay = ppc64_calibrate_delay;

#ifdef CONFIG_XMON
	xmon_map_scc();
	if (strstr(cmd_line, "xmon"))
		xmon(0);
#endif /* CONFIG_XMON */

	ppc_md.progress("setup_arch:enter", 0x3eab);

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
	dcache_bsize = naca->dCacheL1LineSize; 
	icache_bsize = naca->iCacheL1LineSize; 

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
	ppc_md.progress("setup_arch:bootmem", 0x3eab);

	ppc_md.setup_arch();

	paging_init();
	ppc_md.progress("setup_arch: exit", 0x3eab);
}

#ifdef CONFIG_IDE

/* Convert the shorts/longs in hd_driveid from little to big endian;
 * chars are endian independant, of course, but strings need to be flipped.
 * (Despite what it says in drivers/block/ide.h, they come up as little
 * endian...)
 *
 * Changes to linux/hdreg.h may require changes here. */
void ppc64_ide_fix_driveid(struct hd_driveid *id)
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
#endif


void exception_trace(unsigned long trap)
{
	unsigned long x, srr0, srr1, reg20, reg1, reg21;

	asm("mflr %0" : "=r" (x) :);
	asm("mfspr %0,0x1a" : "=r" (srr0) :);
	asm("mfspr %0,0x1b" : "=r" (srr1) :);
	asm("mr %0,1" : "=r" (reg1) :);
	asm("mr %0,20" : "=r" (reg20) :);
	asm("mr %0,21" : "=r" (reg21) :);

	udbg_puts("\n");
	udbg_puts("Took an exception : "); udbg_puthex(x); udbg_puts("\n");
	udbg_puts("   "); udbg_puthex(reg1); udbg_puts("\n");
	udbg_puts("   "); udbg_puthex(reg20); udbg_puts("\n");
	udbg_puts("   "); udbg_puthex(reg21); udbg_puts("\n");
	udbg_puts("   "); udbg_puthex(srr0); udbg_puts("\n");
	udbg_puts("   "); udbg_puthex(srr1); udbg_puts("\n");
}

int set_spread_lpevents( char * str )
{
	/* The parameter is the number of processors to share in processing lp events */
	unsigned long i;
	unsigned long val = simple_strtoul( str, NULL, 0 );
	if ( ( val > 0 ) && ( val <= maxPacas ) ) {
		for ( i=1; i<val; ++i )
			xPaca[i].lpQueuePtr = xPaca[0].lpQueuePtr;
		printk("lpevent processing spread over %ld processors\n", val);
	}
	else
		printk("invalid spreaqd_lpevents %ld\n", val);
	return 1;
}	

/* This should only be called on processor 0 during calibrate decr */
void setup_default_decr(void)
{
	struct Paca * paca = (struct Paca *)mfspr(SPRG3);

	if ( decr_overclock_set && !decr_overclock_proc0_set )
		decr_overclock_proc0 = decr_overclock;

	paca->default_decr = tb_ticks_per_jiffy / decr_overclock_proc0;	
	paca->next_jiffy_update_tb = get_tb() + tb_ticks_per_jiffy;
}

int set_decr_overclock_proc0( char * str )
{
	unsigned long val = simple_strtoul( str, NULL, 0 );
	if ( ( val >= 1 ) && ( val <= 48 ) ) {
		decr_overclock_proc0_set = 1;
		decr_overclock_proc0 = val;
		printk("proc 0 decrementer overclock factor of %ld\n", val);
	}
	else
		printk("invalid proc 0 decrementer overclock factor of %ld\n", val);
	return 1;
}

int set_decr_overclock( char * str )
{
	unsigned long val = simple_strtoul( str, NULL, 0 );
	if ( ( val >= 1 ) && ( val <= 48 ) ) {
		decr_overclock_set = 1;
		decr_overclock = val;
		printk("decrementer overclock factor of %ld\n", val);
	}
	else
		printk("invalid decrementer overclock factor of %ld\n", val);
	return 1;

}

__setup("spread_lpevents=", set_spread_lpevents );
__setup("decr_overclock_proc0=", set_decr_overclock_proc0 );
__setup("decr_overclock=", set_decr_overclock );
