/*
 * BK Id: SCCS/s.pmac_setup.c 1.43 11/13/01 21:26:07 paulus
 */
/*
 *  linux/arch/ppc/kernel/setup.c
 *
 *  PowerPC version 
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Adapted for Power Macintosh by Paul Mackerras
 *    Copyright (C) 1996 Paul Mackerras (paulus@cs.anu.edu.au)
 *
 *  Derived from "arch/alpha/kernel/setup.c"
 *    Copyright (C) 1995 Linus Torvalds
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

/*
 * bootup setup stuff..
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/tty.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/major.h>
#include <linux/blk.h>
#include <linux/vt_kern.h>
#include <linux/console.h>
#include <linux/ide.h>
#include <linux/pci.h>
#include <linux/adb.h>
#include <linux/cuda.h>
#include <linux/pmu.h>
#include <linux/seq_file.h>

#include <asm/processor.h>
#include <asm/sections.h>
#include <asm/prom.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/pci-bridge.h>
#include <asm/ohare.h>
#include <asm/mediabay.h>
#include <asm/feature.h>
#include <asm/machdep.h>
#include <asm/keyboard.h>
#include <asm/dma.h>
#include <asm/bootx.h>
#include <asm/cputable.h>
#include <asm/btext.h>

#include <asm/time.h>
#include "local_irq.h"
#include "pmac_pic.h"
#include "../mm/mem_pieces.h"

#undef SHOW_GATWICK_IRQS

extern long pmac_time_init(void);
extern unsigned long pmac_get_rtc_time(void);
extern int pmac_set_rtc_time(unsigned long nowtime);
extern void pmac_read_rtc_time(void);
extern void pmac_calibrate_decr(void);
extern void pmac_pcibios_fixup(void);
extern void pmac_find_bridges(void);

extern int mackbd_setkeycode(unsigned int scancode, unsigned int keycode);
extern int mackbd_getkeycode(unsigned int scancode);
extern int mackbd_translate(unsigned char keycode, unsigned char *keycodep,
		     char raw_mode);
extern char mackbd_unexpected_up(unsigned char keycode);
extern void mackbd_leds(unsigned char leds);
extern void __init mackbd_init_hw(void);
extern int mac_hid_kbd_translate(unsigned char scancode, unsigned char *keycode,
				 char raw_mode);
extern char mac_hid_kbd_unexpected_up(unsigned char keycode);
extern void mac_hid_init_hw(void);
extern unsigned char mac_hid_kbd_sysrq_xlate[];
extern unsigned char pckbd_sysrq_xlate[];
extern unsigned char mackbd_sysrq_xlate[];
extern int pckbd_setkeycode(unsigned int scancode, unsigned int keycode);
extern int pckbd_getkeycode(unsigned int scancode);
extern int pckbd_translate(unsigned char scancode, unsigned char *keycode,
			   char raw_mode);
extern char pckbd_unexpected_up(unsigned char keycode);
extern int keyboard_sends_linux_keycodes;
extern void pmac_nvram_update(void);

extern int pmac_pci_enable_device_hook(struct pci_dev *dev, int initial);
extern void pmac_pcibios_after_init(void);

struct device_node *memory_node;

unsigned char drive_info;

int ppc_override_l2cr = 0;
int ppc_override_l2cr_value;
int has_l2cache = 0;

static int current_root_goodness = -1;

extern char saved_command_line[];

extern int pmac_newworld;

#define DEFAULT_ROOT_DEVICE 0x0801	/* sda1 - slightly silly choice */

extern void zs_kgdb_hook(int tty_num);
static void ohare_init(void);
#ifdef CONFIG_BOOTX_TEXT
void pmac_progress(char *s, unsigned short hex);
#endif

sys_ctrler_t sys_ctrler = SYS_CTRLER_UNKNOWN;

#ifdef CONFIG_SMP
extern struct smp_ops_t psurge_smp_ops;
extern struct smp_ops_t core99_smp_ops;

volatile static long int core99_l2_cache;
void __init
core99_init_l2(void)
{
	int cpu = smp_processor_id();

	if (!(cur_cpu_spec[0]->cpu_features & CPU_FTR_L2CR))
		return;

	if (cpu == 0){
		core99_l2_cache = _get_L2CR();
		printk("CPU0: L2CR is %lx\n", core99_l2_cache);
	} else {
		printk("CPU%d: L2CR was %lx\n", cpu, _get_L2CR());
		_set_L2CR(0);
		_set_L2CR(core99_l2_cache);
		printk("CPU%d: L2CR set to %lx\n", cpu, core99_l2_cache);
	}
}
#endif /* CONFIG_SMP */

/*
 * Assume here that all clock rates are the same in a
 * smp system.  -- Cort
 */
int __openfirmware
of_show_percpuinfo(struct seq_file *m, int i)
{
	struct device_node *cpu_node;
	int *fp, s;
			
	cpu_node = find_type_devices("cpu");
	if (!cpu_node)
		return 0;
	for (s = 0; s < i && cpu_node->next; s++)
		cpu_node = cpu_node->next;
	fp = (int *) get_property(cpu_node, "clock-frequency", NULL);
	if (fp)
		seq_printf(m, "clock\t\t: %dMHz\n", *fp / 1000000);
	return 0;
}

int __pmac
pmac_show_cpuinfo(struct seq_file *m)
{
	struct device_node *np;
	char *pp;
	int plen;

	/* find motherboard type */
	seq_printf(m, "machine\t\t: ");
	np = find_devices("device-tree");
	if (np != NULL) {
		pp = (char *) get_property(np, "model", NULL);
		if (pp != NULL)
			seq_printf(m, "%s\n", pp);
		else
			seq_printf(m, "PowerMac\n");
		pp = (char *) get_property(np, "compatible", &plen);
		if (pp != NULL) {
			seq_printf(m, "motherboard\t:");
			while (plen > 0) {
				int l = strlen(pp) + 1;
				seq_printf(m, " %s", pp);
				plen -= l;
				pp += l;
			}
			seq_printf(m, "\n");
		}
	} else
		seq_printf(m, "PowerMac\n");

	/* find l2 cache info */
	np = find_devices("l2-cache");
	if (np == 0)
		np = find_type_devices("cache");
	if (np != 0) {
		unsigned int *ic = (unsigned int *)
			get_property(np, "i-cache-size", NULL);
		unsigned int *dc = (unsigned int *)
			get_property(np, "d-cache-size", NULL);
		seq_printf(m, "L2 cache\t:");
		has_l2cache = 1;
		if (get_property(np, "cache-unified", NULL) != 0 && dc) {
			seq_printf(m, " %dK unified", *dc / 1024);
		} else {
			if (ic)
				seq_printf(m, " %dK instruction", *ic / 1024);
			if (dc)
				seq_printf(m, "%s %dK data",
					   (ic? " +": ""), *dc / 1024);
		}
		pp = get_property(np, "ram-type", NULL);
		if (pp)
			seq_printf(m, " %s", pp);
		seq_printf(m, "\n");
	}

	/* find ram info */
	np = find_devices("memory");
	if (np != 0) {
		int n;
		struct reg_property *reg = (struct reg_property *)
			get_property(np, "reg", &n);
		
		if (reg != 0) {
			unsigned long total = 0;

			for (n /= sizeof(struct reg_property); n > 0; --n)
				total += (reg++)->size;
			seq_printf(m, "memory\t\t: %luMB\n", total >> 20);
		}
	}

	/* Checks "l2cr-value" property in the registry */
	np = find_devices("cpus");		
	if (np == 0)
		np = find_type_devices("cpu");		
	if (np != 0) {
		unsigned int *l2cr = (unsigned int *)
			get_property(np, "l2cr-value", NULL);
		if (l2cr != 0) {
			seq_printf(m, "l2cr override\t: 0x%x\n", *l2cr);
		}
	}
	
	/* Indicate newworld/oldworld */
	seq_printf(m, "pmac-generation\t: %s\n",
		   pmac_newworld ? "NewWorld" : "OldWorld");
	

	return 0;
}

#ifdef CONFIG_SCSI
/* Find the device number for the disk (if any) at target tgt
   on host adaptor host.  We just need to get the prototype from
   sd.h */
#include <linux/blkdev.h>
#include "../../../drivers/scsi/scsi.h"
#include "../../../drivers/scsi/sd.h"

#endif

#ifdef CONFIG_VT
/*
 * Dummy mksound function that does nothing.
 * The real one is in the dmasound driver.
 */
static void __pmac
pmac_mksound(unsigned int hz, unsigned int ticks)
{
}
#endif /* CONFIG_VT */

static volatile u32 *sysctrl_regs;

void __init
pmac_setup_arch(void)
{
	struct device_node *cpu;
	int *fp;
	unsigned long pvr;
	
	pvr = PVR_VER(mfspr(PVR));

	/* Set loops_per_jiffy to a half-way reasonable value,
	   for use until calibrate_delay gets called. */
	cpu = find_type_devices("cpu");
	if (cpu != 0) {
		fp = (int *) get_property(cpu, "clock-frequency", NULL);
		if (fp != 0) {
			if (pvr == 4 || pvr >= 8)
				/* 604, G3, G4 etc. */
				loops_per_jiffy = *fp / HZ;
			else
				/* 601, 603, etc. */
				loops_per_jiffy = *fp / (2*HZ);
		} else
			loops_per_jiffy = 50000000 / HZ;
	}

	/* this area has the CPU identification register
	   and some registers used by smp boards */
	sysctrl_regs = (volatile u32 *) ioremap(0xf8000000, 0x1000);
	ohare_init();

	/* Lookup PCI hosts */
	pmac_find_bridges();
	
	/* Checks "l2cr-value" property in the registry */
	if (cur_cpu_spec[0]->cpu_features & CPU_FTR_L2CR) {
		struct device_node *np = find_devices("cpus");		
		if (np == 0)
			np = find_type_devices("cpu");		
		if (np != 0) {
			unsigned int *l2cr = (unsigned int *)
				get_property(np, "l2cr-value", NULL);
			if (l2cr != 0) {
				ppc_override_l2cr = 1;
				ppc_override_l2cr_value = *l2cr;
				_set_L2CR(0);
				_set_L2CR(ppc_override_l2cr_value);
			}
		}
	}

	if (ppc_override_l2cr)
		printk(KERN_INFO "L2CR overriden (0x%x), backside cache is %s\n",
			ppc_override_l2cr_value, (ppc_override_l2cr_value & 0x80000000)
				? "enabled" : "disabled");

#ifdef CONFIG_SMP
	/* somewhat of a hack */
	core99_init_l2();
#endif
	
#ifdef CONFIG_KGDB
	zs_kgdb_hook(0);
#endif

#ifdef CONFIG_ADB_CUDA
	find_via_cuda();
#else
	if (find_devices("via-cuda")) {
		printk("WARNING ! Your machine is Cuda based but your kernel\n");
		printk("          wasn't compiled with CONFIG_ADB_CUDA option !\n");
	}
#endif	
#ifdef CONFIG_ADB_PMU
	find_via_pmu();
#else
	if (find_devices("via-pmu")) {
		printk("WARNING ! Your machine is PMU based but your kernel\n");
		printk("          wasn't compiled with CONFIG_ADB_PMU option !\n");
	}
#endif	
#ifdef CONFIG_NVRAM
	pmac_nvram_init();
#endif
#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#endif
#ifdef CONFIG_VT
	kd_mksound = pmac_mksound;
#endif
#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start)
		ROOT_DEV = MKDEV(RAMDISK_MAJOR, 0);
	else
#endif
		ROOT_DEV = to_kdev_t(DEFAULT_ROOT_DEVICE);

#ifdef CONFIG_SMP
	/* Check for Core99 */
	if (find_devices("uni-n"))
		ppc_md.smp_ops = &core99_smp_ops;
	else
		ppc_md.smp_ops = &psurge_smp_ops;
#endif /* CONFIG_SMP */
}

static void __init ohare_init(void)
{
	/*
	 * Turn on the L2 cache.
	 * We assume that we have a PSX memory controller iff
	 * we have an ohare I/O controller.
	 */
	if (find_devices("ohare") != NULL) {
		if (((sysctrl_regs[2] >> 24) & 0xf) >= 3) {
			if (sysctrl_regs[4] & 0x10)
				sysctrl_regs[4] |= 0x04000020;
			else
				sysctrl_regs[4] |= 0x04000000;
			if(has_l2cache)
				printk(KERN_INFO "Level 2 cache enabled\n");
		}
	}
}

extern char *bootpath;
extern char *bootdevice;
void *boot_host;
int boot_target;
int boot_part;
extern kdev_t boot_dev;

void __init
pmac_init2(void)
{
#ifdef CONFIG_ADB_PMU
	via_pmu_start();
#endif
#ifdef CONFIG_ADB_CUDA
	via_cuda_start();
#endif
#ifdef CONFIG_PMAC_PBOOK
	media_bay_init();
#endif	
}

#ifdef CONFIG_SCSI
void __init
note_scsi_host(struct device_node *node, void *host)
{
	int l;
	char *p;

	l = strlen(node->full_name);
	if (bootpath != NULL && bootdevice != NULL
	    && strncmp(node->full_name, bootdevice, l) == 0
	    && (bootdevice[l] == '/' || bootdevice[l] == 0)) {
		boot_host = host;
		/*
		 * There's a bug in OF 1.0.5.  (Why am I not surprised.)
		 * If you pass a path like scsi/sd@1:0 to canon, it returns
		 * something like /bandit@F2000000/gc@10/53c94@10000/sd@0,0
		 * That is, the scsi target number doesn't get preserved.
		 * So we pick the target number out of bootpath and use that.
		 */
		p = strstr(bootpath, "/sd@");
		if (p != NULL) {
			p += 4;
			boot_target = simple_strtoul(p, NULL, 10);
			p = strchr(p, ':');
			if (p != NULL)
				boot_part = simple_strtoul(p + 1, NULL, 10);
		}
	}
}
#endif

#if defined(CONFIG_BLK_DEV_IDE) && defined(CONFIG_BLK_DEV_IDE_PMAC)
kdev_t __init
find_ide_boot(void)
{
	char *p;
	int n;
	kdev_t __init pmac_find_ide_boot(char *bootdevice, int n);

	if (bootdevice == NULL)
		return 0;
	p = strrchr(bootdevice, '/');
	if (p == NULL)
		return 0;
	n = p - bootdevice;

	return pmac_find_ide_boot(bootdevice, n);
}
#endif /* CONFIG_BLK_DEV_IDE && CONFIG_BLK_DEV_IDE_PMAC */

void __init
find_boot_device(void)
{
#if defined(CONFIG_SCSI) && defined(CONFIG_BLK_DEV_SD)
	if (boot_host != NULL) {
		boot_dev = sd_find_target(boot_host, boot_target);
		if (boot_dev != 0)
			return;
	}
#endif
#if defined(CONFIG_BLK_DEV_IDE) && defined(CONFIG_BLK_DEV_IDE_PMAC)
	boot_dev = find_ide_boot();
#endif
}

/* can't be __init - can be called whenever a disk is first accessed */
void __pmac
note_bootable_part(kdev_t dev, int part, int goodness)
{
	static int found_boot = 0;
	char *p;

	/* Do nothing if the root has been mounted already. */
	if (init_task.fs->rootmnt != NULL)
		return;
	if ((goodness <= current_root_goodness) &&
	    (ROOT_DEV != to_kdev_t(DEFAULT_ROOT_DEVICE)))
		return;
	p = strstr(saved_command_line, "root=");
	if (p != NULL && (p == saved_command_line || p[-1] == ' '))
		return;

	if (!found_boot) {
		find_boot_device();
		found_boot = 1;
	}
	if (boot_dev == 0 || dev == boot_dev) {
		ROOT_DEV = MKDEV(MAJOR(dev), MINOR(dev) + part);
		boot_dev = NODEV;
		current_root_goodness = goodness;
	}
}

void __pmac
pmac_restart(char *cmd)
{
#ifdef CONFIG_ADB_CUDA
	struct adb_request req;
#endif /* CONFIG_ADB_CUDA */

#ifdef CONFIG_NVRAM
	pmac_nvram_update();
#endif
	
	switch (sys_ctrler) {
#ifdef CONFIG_ADB_CUDA
	case SYS_CTRLER_CUDA:
		cuda_request(&req, NULL, 2, CUDA_PACKET,
			     CUDA_RESET_SYSTEM);
		for (;;)
			cuda_poll();
		break;
#endif /* CONFIG_ADB_CUDA */
#ifdef CONFIG_ADB_PMU		
	case SYS_CTRLER_PMU:
		pmu_restart();
		break;
#endif /* CONFIG_ADB_PMU */		
	default: ;
	}
}

void __pmac
pmac_power_off(void)
{
#ifdef CONFIG_ADB_CUDA
	struct adb_request req;
#endif /* CONFIG_ADB_CUDA */

#ifdef CONFIG_NVRAM
	pmac_nvram_update();
#endif
	
	switch (sys_ctrler) {
#ifdef CONFIG_ADB_CUDA
	case SYS_CTRLER_CUDA:
		cuda_request(&req, NULL, 2, CUDA_PACKET,
			     CUDA_POWERDOWN);
		for (;;)
			cuda_poll();
		break;
#endif /* CONFIG_ADB_CUDA */
#ifdef CONFIG_ADB_PMU
	case SYS_CTRLER_PMU:
		pmu_shutdown();
		break;
#endif /* CONFIG_ADB_PMU */
	default: ;
	}
}

void __pmac
pmac_halt(void)
{
   pmac_power_off();
}


#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE)
/*
 * IDE stuff.
 */
static int __pmac
pmac_ide_check_region(ide_ioreg_t from, unsigned int extent)
{
	/*
	 * We only do the check_region if `from' looks like a genuine
	 * I/O port number.  If it actually refers to a memory-mapped
	 * register, it should be OK.
	 */
	if (from < ~_IO_BASE)
		return check_region(from, extent);
	return 0;
}

static void __pmac
pmac_ide_request_region(ide_ioreg_t from,
			unsigned int extent,
			const char *name)
{
	if (from < ~_IO_BASE)
		request_region(from, extent, name);
}

static void __pmac
pmac_ide_release_region(ide_ioreg_t from,
			unsigned int extent)
{
	if (from < ~_IO_BASE)
		release_region(from, extent);
}

/*
 * This is only used if we have a PCI IDE controller, not
 * for the IDE controller in the ohare/paddington/heathrow/keylargo.
 */
static void __pmac
pmac_ide_init_hwif_ports(hw_regs_t *hw, ide_ioreg_t data_port,
		ide_ioreg_t ctrl_port, int *irq)
{
	ide_ioreg_t reg = data_port;
	int i;

	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++) {
		hw->io_ports[i] = reg;
		reg += 1;
	}
	hw->io_ports[IDE_CONTROL_OFFSET] = ctrl_port;
}
#endif

/*
 * Read in a property describing some pieces of memory.
 */

static void __init
get_mem_prop(char *name, struct mem_pieces *mp)
{
	struct reg_property *rp;
	int i, s;
	unsigned int *ip;
	int nac = prom_n_addr_cells(memory_node);
	int nsc = prom_n_size_cells(memory_node);

	ip = (unsigned int *) get_property(memory_node, name, &s);
	if (ip == NULL) {
		printk(KERN_ERR "error: couldn't get %s property on /memory\n",
		       name);
		abort();
	}
	s /= (nsc + nac) * 4;
	rp = mp->regions;
	for (i = 0; i < s; ++i, ip += nac+nsc) {
		if (nac >= 2 && ip[nac-2] != 0)
			continue;
		rp->address = ip[nac-1];
		if (nsc >= 2 && ip[nac+nsc-2] != 0)
			rp->size = ~0U;
		else
			rp->size = ip[nac+nsc-1];
		++rp;
	}
	mp->n_regions = rp - mp->regions;

	/* Make sure the pieces are sorted. */
	mem_pieces_sort(mp);
	mem_pieces_coalesce(mp);
}

/*
 * On systems with Open Firmware, collect information about
 * physical RAM and which pieces are already in use.
 * At this point, we have (at least) the first 8MB mapped with a BAT.
 * Our text, data, bss use something over 1MB, starting at 0.
 * Open Firmware may be using 1MB at the 4MB point.
 */
unsigned long __init
pmac_find_end_of_memory(void)
{
	unsigned long a, total;
	struct mem_pieces phys_mem;

	memory_node = find_devices("memory");
	if (memory_node == NULL) {
		printk(KERN_ERR "can't find memory node\n");
		abort();
	}

	/*
	 * Find out where physical memory is, and check that it
	 * starts at 0 and is contiguous.  It seems that RAM is
	 * always physically contiguous on Power Macintoshes.
	 *
	 * Supporting discontiguous physical memory isn't hard,
	 * it just makes the virtual <-> physical mapping functions
	 * more complicated (or else you end up wasting space
	 * in mem_map).
	 */
	get_mem_prop("reg", &phys_mem);
	if (phys_mem.n_regions == 0)
		panic("No RAM??");
	a = phys_mem.regions[0].address;
	if (a != 0)
		panic("RAM doesn't start at physical address 0");
	total = phys_mem.regions[0].size;

	if (phys_mem.n_regions > 1) {
		printk("RAM starting at 0x%x is not contiguous\n",
		       phys_mem.regions[1].address);
		printk("Using RAM from 0 to 0x%lx\n", total-1);
	}

	return total;
}

void __init
select_adb_keyboard(void)
{
#ifdef CONFIG_VT
#ifdef CONFIG_INPUT
	ppc_md.kbd_init_hw       = mac_hid_init_hw;
	ppc_md.kbd_translate     = mac_hid_kbd_translate;
	ppc_md.kbd_unexpected_up = mac_hid_kbd_unexpected_up;
	ppc_md.kbd_setkeycode    = 0;
	ppc_md.kbd_getkeycode    = 0;
	ppc_md.kbd_leds		 = 0;
#ifdef CONFIG_MAGIC_SYSRQ
#ifdef CONFIG_MAC_ADBKEYCODES
	if (!keyboard_sends_linux_keycodes) {
		ppc_md.ppc_kbd_sysrq_xlate = mac_hid_kbd_sysrq_xlate;
		SYSRQ_KEY = 0x69;
	} else
#endif /* CONFIG_MAC_ADBKEYCODES */
	{
		ppc_md.ppc_kbd_sysrq_xlate = pckbd_sysrq_xlate;
		SYSRQ_KEY = 0x54;
	}
#endif /* CONFIG_MAGIC_SYSRQ */
#elif defined(CONFIG_ADB_KEYBOARD)
	ppc_md.kbd_setkeycode       = mackbd_setkeycode;
	ppc_md.kbd_getkeycode       = mackbd_getkeycode;
	ppc_md.kbd_translate        = mackbd_translate;
	ppc_md.kbd_unexpected_up    = mackbd_unexpected_up;
	ppc_md.kbd_leds             = mackbd_leds;
	ppc_md.kbd_init_hw          = mackbd_init_hw;
#ifdef CONFIG_MAGIC_SYSRQ
	ppc_md.ppc_kbd_sysrq_xlate  = mackbd_sysrq_xlate;
	SYSRQ_KEY = 0x69;
#endif /* CONFIG_MAGIC_SYSRQ */
#endif /* CONFIG_INPUT_ADBHID/CONFIG_ADB_KEYBOARD */
#endif /* CONFIG_VT */
}

void __init
pmac_init(unsigned long r3, unsigned long r4, unsigned long r5,
	  unsigned long r6, unsigned long r7)
{
	/* isa_io_base gets set in pmac_find_bridges */
	isa_mem_base = PMAC_ISA_MEM_BASE;
	pci_dram_offset = PMAC_PCI_DRAM_OFFSET;
	ISA_DMA_THRESHOLD = ~0L;
	DMA_MODE_READ = 1;
	DMA_MODE_WRITE = 2;

	ppc_md.setup_arch     = pmac_setup_arch;
	ppc_md.show_cpuinfo   = pmac_show_cpuinfo;
	ppc_md.show_percpuinfo = of_show_percpuinfo;
	ppc_md.irq_cannonicalize = NULL;
	ppc_md.init_IRQ       = pmac_pic_init;
	ppc_md.get_irq        = pmac_get_irq; /* Changed later on ... */
	ppc_md.init           = pmac_init2;
	
	ppc_md.pcibios_fixup  = pmac_pcibios_fixup;
	ppc_md.pcibios_enable_device_hook = pmac_pci_enable_device_hook;
	ppc_md.pcibios_after_init = pmac_pcibios_after_init;

	ppc_md.restart        = pmac_restart;
	ppc_md.power_off      = pmac_power_off;
	ppc_md.halt           = pmac_halt;

	ppc_md.time_init      = pmac_time_init;
	ppc_md.set_rtc_time   = pmac_set_rtc_time;
	ppc_md.get_rtc_time   = pmac_get_rtc_time;
	ppc_md.calibrate_decr = pmac_calibrate_decr;

	ppc_md.find_end_of_memory = pmac_find_end_of_memory;

	select_adb_keyboard();

#if defined(CONFIG_BLK_DEV_IDE) && defined(CONFIG_BLK_DEV_IDE_PMAC)
        ppc_ide_md.ide_check_region	= pmac_ide_check_region;
        ppc_ide_md.ide_request_region	= pmac_ide_request_region;
        ppc_ide_md.ide_release_region	= pmac_ide_release_region;
        ppc_ide_md.ide_init_hwif	= pmac_ide_init_hwif_ports;
#endif /* CONFIG_BLK_DEV_IDE && CONFIG_BLK_DEV_IDE_PMAC */

#ifdef CONFIG_BOOTX_TEXT
	ppc_md.progress = pmac_progress;
#endif /* CONFIG_BOOTX_TEXT */

	if (ppc_md.progress) ppc_md.progress("pmac_init(): exit", 0);
	
}

#ifdef CONFIG_BOOTX_TEXT
extern void drawchar(char c);
extern void drawstring(const char *c);
extern boot_infos_t *disp_bi;
void __init
pmac_progress(char *s, unsigned short hex)
{
	if (disp_bi == 0)
		return;
	btext_drawstring(s);
	btext_drawchar('\n');
}
#endif /* CONFIG_BOOTX_TEXT */
