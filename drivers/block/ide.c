/*
 *  linux/drivers/block/ide.c	Version 6.00  Dec   4, 1996
 *
 *  Copyright (C) 1994-1996  Linus Torvalds & authors (see below)
 */
#define _IDE_C		/* needed by <linux/blk.h> */

/*
 *  Maintained by Mark Lord  <mlord@pobox.com>
 *            and Gadi Oxman <gadio@netvision.net.il>
 *
 * This is the multiple IDE interface driver, as evolved from hd.c.
 * It supports up to four IDE interfaces, on one or more IRQs (usually 14 & 15).
 * There can be up to two drives per interface, as per the ATA-2 spec.
 *
 * Primary:    ide0, port 0x1f0; major=3;  hda is minor=0; hdb is minor=64
 * Secondary:  ide1, port 0x170; major=22; hdc is minor=0; hdd is minor=64
 * Tertiary:   ide2, port 0x???; major=33; hde is minor=0; hdf is minor=64
 * Quaternary: ide3, port 0x???; major=34; hdg is minor=0; hdh is minor=64
 *
 * It is easy to extend ide.c to handle more than four interfaces:
 *
 *	Change the MAX_HWIFS constant in ide.h.
 *
 *	Define some new major numbers (in major.h), and insert them into
 *	the ide_hwif_to_major table in ide.c.
 *
 *	Fill in the extra values for the new interfaces into the two tables
 *	inside ide.c:  default_io_base[]  and  default_irqs[].
 *
 *	Create the new request handlers by cloning "do_ide3_request()"
 *	for each new interface, and add them to the switch statement
 *	in the ide_init() function in ide.c.
 *
 *	Recompile, create the new /dev/ entries, and it will probably work.
 *
 *  From hd.c:
 *  |
 *  | It traverses the request-list, using interrupts to jump between functions.
 *  | As nearly all functions can be called within interrupts, we may not sleep.
 *  | Special care is recommended.  Have Fun!
 *  |
 *  | modified by Drew Eckhardt to check nr of hd's from the CMOS.
 *  |
 *  | Thanks to Branko Lankester, lankeste@fwi.uva.nl, who found a bug
 *  | in the early extended-partition checks and added DM partitions.
 *  |
 *  | Early work on error handling by Mika Liljeberg (liljeber@cs.Helsinki.FI).
 *  |
 *  | IRQ-unmask, drive-id, multiple-mode, support for ">16 heads",
 *  | and general streamlining by Mark Lord (mlord@pobox.com).
 *
 *  October, 1994 -- Complete line-by-line overhaul for linux 1.1.x, by:
 *
 *	Mark Lord	(mlord@pobox.com)		(IDE Perf.Pkg)
 *	Delman Lee	(delman@mipg.upenn.edu)		("Mr. atdisk2")
 *	Scott Snyder	(snyder@fnald0.fnal.gov)	(ATAPI IDE cd-rom)
 *
 *  This was a rewrite of just about everything from hd.c, though some original
 *  code is still sprinkled about.  Think of it as a major evolution, with
 *  inspiration from lots of linux users, esp.  hamish@zot.apana.org.au
 *
 *  Version 1.0 ALPHA	initial code, primary i/f working okay
 *  Version 1.3 BETA	dual i/f on shared irq tested & working!
 *  Version 1.4 BETA	added auto probing for irq(s)
 *  Version 1.5 BETA	added ALPHA (untested) support for IDE cd-roms,
 *  ...
 *  Version 3.5		correct the bios_cyl field if it's too small
 *  (linux 1.1.76)	 (to help fdisk with brain-dead BIOSs)
 *  Version 3.6		cosmetic corrections to comments and stuff
 *  (linux 1.1.77)	reorganise probing code to make it understandable
 *			added halfway retry to probing for drive identification
 *			added "hdx=noprobe" command line option
 *			allow setting multmode even when identification fails
 *  Version 3.7		move set_geometry=1 from do_identify() to ide_init()
 *			increase DRQ_WAIT to eliminate nuisance messages
 *			wait for DRQ_STAT instead of DATA_READY during probing
 *			  (courtesy of Gary Thomas gary@efland.UU.NET)
 *  Version 3.8		fixed byte-swapping for confused Mitsumi cdrom drives
 *			update of ide-cd.c from Scott, allows blocksize=1024
 *			cdrom probe fixes, inspired by jprang@uni-duisburg.de
 *  Version 3.9		don't use LBA if lba_capacity looks funny
 *			correct the drive capacity calculations
 *			fix probing for old Seagates without IDE_ALTSTATUS_REG
 *			fix byte-ordering for some NEC cdrom drives
 *  Version 3.10	disable multiple mode by default; was causing trouble
 *  Version 3.11	fix mis-identification of old WD disks as cdroms
 *  Version 3,12	simplify logic for selecting initial mult_count
 *			  (fixes problems with buggy WD drives)
 *  Version 3.13	remove excess "multiple mode disabled" messages
 *  Version 3.14	fix ide_error() handling of BUSY_STAT
 *			fix byte-swapped cdrom strings (again.. arghh!)
 *			ignore INDEX bit when checking the ALTSTATUS reg
 *  Version 3.15	add SINGLE_THREADED flag for use with dual-CMD i/f
 *			ignore WRERR_STAT for non-write operations
 *			added vlb_sync support for DC-2000A & others,
 *			 (incl. some Promise chips), courtesy of Frank Gockel
 *  Version 3.16	convert vlb_32bit and vlb_sync into runtime flags
 *			add ioctls to get/set VLB flags (HDIO_[SG]ET_CHIPSET)
 *			rename SINGLE_THREADED to SUPPORT_SERIALIZE,
 *			add boot flag to "serialize" operation for CMD i/f
 *			add optional support for DTC2278 interfaces,
 *			 courtesy of andy@cercle.cts.com (Dyan Wile).
 *			add boot flag to enable "dtc2278" probe
 *			add probe to avoid EATA (SCSI) interfaces,
 *			 courtesy of neuffer@goofy.zdv.uni-mainz.de.
 *  Version 4.00	tidy up verify_area() calls - heiko@colossus.escape.de
 *			add flag to ignore WRERR_STAT for some drives
 *			 courtesy of David.H.West@um.cc.umich.edu
 *			assembly syntax tweak to vlb_sync
 *			removable drive support from scuba@cs.tu-berlin.de
 *			add transparent support for DiskManager-6.0x "Dynamic
 *			 Disk Overlay" (DDO), most of this is in genhd.c
 *			eliminate "multiple mode turned off" message at boot
 *  Version 4.10	fix bug in ioctl for "hdparm -c3"
 *			fix DM6:DDO support -- now works with LILO, fdisk, ...
 *			don't treat some naughty WD drives as removable
 *  Version 4.11	updated DM6 support using info provided by OnTrack
 *  Version 5.00	major overhaul, multmode setting fixed, vlb_sync fixed
 *			added support for 3rd/4th/alternative IDE ports
 *			created ide.h; ide-cd.c now compiles separate from ide.c
 *			hopefully fixed infinite "unexpected_intr" from cdroms
 *			zillions of other changes and restructuring
 *			somehow reduced overall memory usage by several kB
 *			probably slowed things down slightly, but worth it
 *  Version 5.01	AT LAST!!  Finally understood why "unexpected_intr"
 *			 was happening at various times/places:  whenever the
 *			 ide-interface's ctl_port was used to "mask" the irq,
 *			 it also would trigger an edge in the process of masking
 *			 which would result in a self-inflicted interrupt!!
 *			 (such a stupid way to build a hardware interrupt mask).
 *			 This is now fixed (after a year of head-scratching).
 *  Version 5.02	got rid of need for {enable,disable}_irq_list()
 *  Version 5.03	tune-ups, comments, remove "busy wait" from drive resets
 *			removed PROBE_FOR_IRQS option -- no longer needed
 *			OOOPS!  fixed "bad access" bug for 2nd drive on an i/f
 *  Version 5.04	changed "ira %d" to "irq %d" in DEBUG message
 *			added more comments, cleaned up unexpected_intr()
 *			OOOPS!  fixed null pointer problem in ide reset code
 *			added autodetect for Triton chipset -- no effect yet
 *  Version 5.05	OOOPS!  fixed bug in revalidate_disk()
 *			OOOPS!  fixed bug in ide_do_request()
 *			added ATAPI reset sequence for cdroms
 *  Version 5.10	added Bus-Mastered DMA support for Triton Chipset
 *			some (mostly) cosmetic changes
 *  Version 5.11	added ht6560b support by malafoss@snakemail.hut.fi
 *			reworked PCI scanning code
 *			added automatic RZ1000 detection/support
 *			added automatic PCI CMD640 detection/support
 *			added option for VLB CMD640 support
 *			tweaked probe to find cdrom on hdb with disks on hda,hdc
 *  Version 5.12	some performance tuning
 *			added message to alert user to bad /dev/hd[cd] entries
 *			OOOPS!  fixed bug in atapi reset
 *			driver now forces "serialize" again for all cmd640 chips
 *			noticed REALLY_SLOW_IO had no effect, moved it to ide.c
 *			made do_drive_cmd() into public ide_do_drive_cmd()
 *  Version 5.13	fixed typo ('B'), thanks to houston@boyd.geog.mcgill.ca
 *			fixed ht6560b support
 *  Version 5.13b (sss)	fix problem in calling ide_cdrom_setup()
 *			don't bother invalidating nonexistent partitions
 *  Version 5.14	fixes to cmd640 support.. maybe it works now(?)
 *			added & tested full EZ-DRIVE support -- don't use LILO!
 *			don't enable 2nd CMD640 PCI port during init - conflict
 *  Version 5.15	bug fix in init_cmd640_vlb()
 *			bug fix in interrupt sharing code
 *  Version 5.16	ugh.. fix "serialize" support, broken in 5.15
 *			remove "Huh?" from cmd640 code
 *			added qd6580 interface speed select from Colten Edwards
 *  Version 5.17	kludge around bug in BIOS32 on Intel triton motherboards
 *  Version 5.18	new CMD640 code, moved to cmd640.c, #include'd for now
 *			new UMC8672 code, moved to umc8672.c, #include'd for now
 *			disallow turning on DMA when h/w not capable of DMA
 *  Version 5.19	fix potential infinite timeout on resets
 *			extend reset poll into a general purpose polling scheme
 *			add atapi tape drive support from Gadi Oxman
 *			simplify exit from _intr routines -- no IDE_DO_REQUEST
 *  Version 5.20	leave current rq on blkdev request list during I/O
 *			generalized ide_do_drive_cmd() for tape/cdrom driver use
 *  Version 5.21	fix nasty cdrom/tape bug (ide_preempt was messed up)
 *  Version 5.22	fix ide_xlate_1024() to work with/without drive->id
 *  Version 5.23	miscellaneous touch-ups
 *  Version 5.24	fix #if's for SUPPORT_CMD640
 *  Version 5.25	more touch-ups, fix cdrom resets, ...
 *			cmd640.c now configs/compiles separate from ide.c
 *  Version 5.26	keep_settings now maintains the using_dma flag
 *			fix [EZD] remap message to only output at boot time
 *			fix "bad /dev/ entry" message to say hdc, not hdc0
 *			fix ide_xlate_1024() to respect user specified CHS
 *			use CHS from partn table if it looks translated
 *			re-merged flags chipset,vlb_32bit,vlb_sync into io_32bit
 *			keep track of interface chipset type, when known
 *			add generic PIO mode "tuneproc" mechanism
 *			fix cmd640_vlb option
 *			fix ht6560b support (was completely broken)
 *			umc8672.c now configures/compiles separate from ide.c
 *			move dtc2278 support to dtc2278.c
 *			move ht6560b support to ht6560b.c
 *			move qd6580  support to qd6580.c
 *			add  ali14xx support in ali14xx.c
 * Version 5.27		add [no]autotune parameters to help cmd640
 *			move rz1000  support to rz1000.c
 * Version 5.28		#include "ide_modes.h"
 *			fix disallow_unmask: now per-interface "no_unmask" bit
 *			force io_32bit to be the same on drive pairs of dtc2278
 *			improved IDE tape error handling, and tape DMA support
 *			bugfix in ide_do_drive_cmd() for cdroms + serialize
 * Version 5.29		fixed non-IDE check for too many physical heads
 *			don't use LBA if capacity is smaller than CHS
 * Version 5.30		remove real_devices kludge, formerly used by genhd.c
 * Version 5.32		change "KB" to "kB"
 *			fix serialize (was broken in kernel 1.3.72)
 *			add support for "hdparm -I"
 *			use common code for disk/tape/cdrom IDE_DRIVE_CMDs
 *			add support for Promise DC4030VL caching card
 *			improved serialize support
 *			put partition check back into alphabetical order
 *			add config option for PCMCIA baggage
 *			try to make PCMCIA support safer to use
 *			improve security on ioctls(): all are suser() only
 * Version 5.33		improve handling of HDIO_DRIVE_CMDs that read data
 * Version 5.34		fix irq-sharing problem from 5.33
 *			fix cdrom ioctl problem from 5.33
 * Version 5.35		cosmetic changes
 *			fix cli() problem in try_to_identify()
 * Version 5.36		fixes to optional PCMCIA support
 * Version 5.37		don't use DMA when "noautotune" is specified
 * Version 5.37a (go)	fix shared irq probing (was broken in kernel 1.3.72)
 *			call unplug_device() from ide_do_drive_cmd()
 * Version 5.38		add "hdx=none" option, courtesy of Joel Maslak
 *			mask drive irq after use, if sharing with another hwif
 *			add code to help debug weird cmd640 problems
 * Version 5.39		fix horrible error in earlier irq sharing "fix"
 * Version 5.40		fix serialization -- was broken in 5.39
 *			help sharing by masking device irq after probing
 * Version 5.41		more fixes to irq sharing/serialize detection
 *			disable io_32bit by default on drive reset
 * Version 5.42		simplify irq-masking after probe
 *			fix NULL pointer deref in save_match()
 * Version 5.43		Ugh.. unexpected_intr is back: try to exterminate it
 * Version 5.44		Fix for "irq probe failed" on cmd640
 *			change path on message regarding MAKEDEV.ide
 *			add a throttle to the unexpected_intr() messages
 * Version 5.45		fix ugly parameter parsing bugs (thanks Derek)
 *			include Gadi's magic fix for cmd640 unexpected_intr
 *			include mc68000 patches from Geert Uytterhoeven
 *			add Gadi's fix for PCMCIA cdroms
 * Version 5.46		remove the mc68000 #ifdefs for 2.0.x
 * Version 5.47		fix set_tune race condition
 *			fix bug in earlier PCMCIA cdrom update
 * Version 5.48		if def'd, invoke CMD640_DUMP_REGS when irq probe fails
 *			lengthen the do_reset1() pulse, for laptops
 *			add idebus=xx parameter for cmd640 and ali chipsets
 *			no_unmask flag now per-drive instead of per-hwif
 *			fix tune_req so that it gets done immediately
 *			fix missing restore_flags() in ide_ioctl
 *			prevent use of io_32bit on cmd640 with no prefetch
 * Version 5.49		fix minor quirks in probing routines
 * Version 5.50		allow values as small as 20 for idebus=
 * Version 5.51		force non io_32bit in drive_cmd_intr()
 *			change delay_10ms() to delay_50ms() to fix problems
 * Version 5.52		fix incorrect invalidation of removable devices
 *			add "hdx=slow" command line option
 * Version 5.60		start to modularize the driver; the disk and ATAPI
 *			 drivers can be compiled as loadable modules.
 *			move IDE probe code to ide-probe.c
 *			move IDE disk code to ide-disk.c
 *			add support for generic IDE device subdrivers
 *			add m68k code from Geert Uytterhoeven
 *			probe all interfaces by default
 *			add ioctl to (re)probe an interface
 * Version 6.00		use per device request queues
 *			attempt to optimize shared hwgroup performance
 *			add ioctl to manually adjust bandwidth algorithms
 *			add kerneld support for the probe module
 *			fix bug in ide_error()
 *			fix bug in the first ide_get_lock() call for Atari
 *			don't flush leftover data for ATAPI devices
 *
 *  Some additional driver compile-time options are in ide.h
 *
 *  To do, in likely order of completion:
 *	- modify kernel to obtain BIOS geometry for drives on 2nd/3rd/4th i/f
*/

#undef REALLY_SLOW_IO		/* most systems can safely undef this */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/major.h>
#include <linux/blkdev.h>
#include <linux/errno.h>
#include <linux/hdreg.h>
#include <linux/genhd.h>
#include <linux/malloc.h>
#include <linux/bios32.h>
#include <linux/pci.h>

#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/bitops.h>

#include "ide.h"
#include "ide_modes.h"

#ifdef CONFIG_KERNELD
#include <linux/kerneld.h>
#endif /* CONFIG_KERNELD */

static const byte	ide_hwif_to_major[] = {IDE0_MAJOR, IDE1_MAJOR, IDE2_MAJOR, IDE3_MAJOR};

static int	idebus_parameter; /* holds the "idebus=" parameter */
static int	system_bus_speed; /* holds what we think is VESA/PCI bus speed */
static int	initializing;     /* set while initializing built-in drivers */

/*
 * ide_lock is used by the Atari code to obtain access to the IDE interrupt,
 * which is shared between several drivers.
 */
static int	ide_lock = 0;

/*
 * ide_modules keeps track of the available IDE chipset/probe/driver modules.
 */
static ide_module_t *ide_modules = NULL;

/*
 * This is declared extern in ide.h, for access by other IDE modules:
 */
ide_hwif_t	ide_hwifs[MAX_HWIFS];	/* master data repository */

#if (DISK_RECOVERY_TIME > 0)
/*
 * For really screwy hardware (hey, at least it *can* be used with Linux)
 * we can enforce a minimum delay time between successive operations.
 */
static unsigned long read_timer(void)
{
	unsigned long t, flags;
	int i;

	save_flags(flags);
	cli();
	t = jiffies * 11932;
    	outb_p(0, 0x43);
	i = inb_p(0x40);
	i |= inb(0x40) << 8;
	restore_flags(flags);
	return (t - i);
}
#endif /* DISK_RECOVERY_TIME */

static inline void set_recovery_timer (ide_hwif_t *hwif)
{
#if (DISK_RECOVERY_TIME > 0)
	hwif->last_time = read_timer();
#endif /* DISK_RECOVERY_TIME */
}

/*
 * Do not even *think* about calling this!
 */
static void init_hwif_data (unsigned int index)
{
	byte *p;
	unsigned int unit;
	ide_hwif_t *hwif = &ide_hwifs[index];

	/* bulk initialize hwif & drive info with zeros */
	p = ((byte *) hwif) + sizeof(ide_hwif_t);
	do {
		*--p = 0;
	} while (p > (byte *) hwif);

	/* fill in any non-zero initial values */
	hwif->index     = index;
	ide_init_hwif_ports(hwif->io_ports, ide_default_io_base(index), &hwif->irq);
	hwif->noprobe	= !hwif->io_ports[IDE_DATA_OFFSET];
#ifdef CONFIG_BLK_DEV_HD
	if (hwif->io_ports[IDE_DATA_OFFSET] == HD_DATA)
		hwif->noprobe = 1; /* may be overridden by ide_setup() */
#endif /* CONFIG_BLK_DEV_HD */
	hwif->major	= ide_hwif_to_major[index];
	hwif->name[0]	= 'i';
	hwif->name[1]	= 'd';
	hwif->name[2]	= 'e';
	hwif->name[3]	= '0' + index;
	for (unit = 0; unit < MAX_DRIVES; ++unit) {
		ide_drive_t *drive = &hwif->drives[unit];

		drive->media			= ide_disk;
		drive->select.all		= (unit<<4)|0xa0;
		drive->hwif			= hwif;
		drive->ctl			= 0x08;
		drive->ready_stat		= READY_STAT;
		drive->bad_wstat		= BAD_W_STAT;
		drive->special.b.recalibrate	= 1;
		drive->special.b.set_geometry	= 1;
		drive->name[0]			= 'h';
		drive->name[1]			= 'd';
		drive->name[2]			= 'a' + (index * MAX_DRIVES) + unit;
	}
}

/*
 * init_ide_data() sets reasonable default values into all fields
 * of all instances of the hwifs and drives, but only on the first call.
 * Subsequent calls have no effect (they don't wipe out anything).
 *
 * This routine is normally called at driver initialization time,
 * but may also be called MUCH earlier during kernel "command-line"
 * parameter processing.  As such, we cannot depend on any other parts
 * of the kernel (such as memory allocation) to be functioning yet.
 *
 * This is too bad, as otherwise we could dynamically allocate the
 * ide_drive_t structs as needed, rather than always consuming memory
 * for the max possible number (MAX_HWIFS * MAX_DRIVES) of them.
 */
#define MAGIC_COOKIE 0x12345678
static void init_ide_data (void)
{
	unsigned int index;
	static unsigned long magic_cookie = MAGIC_COOKIE;

	if (magic_cookie != MAGIC_COOKIE)
		return;		/* already initialized */
	magic_cookie = 0;

	for (index = 0; index < MAX_HWIFS; ++index)
		init_hwif_data(index);

	idebus_parameter = 0;
	system_bus_speed = 0;
}

/*
 * ide_system_bus_speed() returns what we think is the system VESA/PCI
 * bus speed (in MHz).  This is used for calculating interface PIO timings.
 * The default is 40 for known PCI systems, 50 otherwise.
 * The "idebus=xx" parameter can be used to override this value.
 * The actual value to be used is computed/displayed the first time through.
 */
int ide_system_bus_speed (void)
{
	if (!system_bus_speed) {
		if (idebus_parameter)
			system_bus_speed = idebus_parameter;	/* user supplied value */
#ifdef CONFIG_PCI
		else if (pcibios_present())
			system_bus_speed = 40;	/* safe default value for PCI */
#endif /* CONFIG_PCI */
		else
			system_bus_speed = 50;	/* safe default value for VESA and PCI */
		printk("ide: Assuming %dMHz system bus speed for PIO modes%s\n", system_bus_speed,
			idebus_parameter ? "" : "; override with idebus=xx");
	}
	return system_bus_speed;
}

#if SUPPORT_VLB_SYNC
/*
 * Some localbus EIDE interfaces require a special access sequence
 * when using 32-bit I/O instructions to transfer data.  We call this
 * the "vlb_sync" sequence, which consists of three successive reads
 * of the sector count register location, with interrupts disabled
 * to ensure that the reads all happen together.
 */
static inline void do_vlb_sync (ide_ioreg_t port) {
	(void) inb (port);
	(void) inb (port);
	(void) inb (port);
}
#endif /* SUPPORT_VLB_SYNC */

/*
 * This is used for most PIO data transfers *from* the IDE interface
 */
void ide_input_data (ide_drive_t *drive, void *buffer, unsigned int wcount)
{
	byte io_32bit = drive->io_32bit;

	if (io_32bit) {
#if SUPPORT_VLB_SYNC
		if (io_32bit & 2) {
			cli();
			do_vlb_sync(IDE_NSECTOR_REG);
			insl(IDE_DATA_REG, buffer, wcount);
			if (drive->unmask)
				sti();
		} else
#endif /* SUPPORT_VLB_SYNC */
			insl(IDE_DATA_REG, buffer, wcount);
	} else {
#if SUPPORT_SLOW_DATA_PORTS
		if (drive->slow) {
			unsigned short *ptr = (unsigned short *) buffer;
			while (wcount--) {
				*ptr++ = inw_p(IDE_DATA_REG);
				*ptr++ = inw_p(IDE_DATA_REG);
			}
		} else
#endif /* SUPPORT_SLOW_DATA_PORTS */
			insw(IDE_DATA_REG, buffer, wcount<<1);
	}
}

/*
 * This is used for most PIO data transfers *to* the IDE interface
 */
void ide_output_data (ide_drive_t *drive, void *buffer, unsigned int wcount)
{
	byte io_32bit = drive->io_32bit;

	if (io_32bit) {
#if SUPPORT_VLB_SYNC
		if (io_32bit & 2) {
			cli();
			do_vlb_sync(IDE_NSECTOR_REG);
			outsl(IDE_DATA_REG, buffer, wcount);
			if (drive->unmask)
				sti();
		} else
#endif /* SUPPORT_VLB_SYNC */
			outsl(IDE_DATA_REG, buffer, wcount);
	} else {
#if SUPPORT_SLOW_DATA_PORTS
		if (drive->slow) {
			unsigned short *ptr = (unsigned short *) buffer;
			while (wcount--) {
				outw_p(*ptr++, IDE_DATA_REG);
				outw_p(*ptr++, IDE_DATA_REG);
			}
		} else
#endif /* SUPPORT_SLOW_DATA_PORTS */
			outsw(IDE_DATA_REG, buffer, wcount<<1);
	}
}

/*
 * The following routines are mainly used by the ATAPI drivers.
 *
 * These routines will round up any request for an odd number of bytes,
 * so if an odd bytecount is specified, be sure that there's at least one
 * extra byte allocated for the buffer.
 */
void atapi_input_bytes (ide_drive_t *drive, void *buffer, unsigned int bytecount)
{
	++bytecount;
#ifdef CONFIG_ATARI
	if (MACH_IS_ATARI) {
		/* Atari has a byte-swapped IDE interface */
		insw_swapw(IDE_DATA_REG, buffer, bytecount / 2);
		return;
	}
#endif /* CONFIG_ATARI */
	ide_input_data (drive, buffer, bytecount / 4);
	if ((bytecount & 0x03) >= 2)
		insw (IDE_DATA_REG, ((byte *)buffer) + (bytecount & ~0x03), 1);
}

void atapi_output_bytes (ide_drive_t *drive, void *buffer, unsigned int bytecount)
{
	++bytecount;
#ifdef CONFIG_ATARI
	if (MACH_IS_ATARI) {
		/* Atari has a byte-swapped IDE interface */
		outsw_swapw(IDE_DATA_REG, buffer, bytecount / 2);
		return;
	}
#endif /* CONFIG_ATARI */
	ide_output_data (drive, buffer, bytecount / 4);
	if ((bytecount & 0x03) >= 2)
		outsw (IDE_DATA_REG, ((byte *)buffer) + (bytecount & ~0x03), 1);
}

/*
 * This should get invoked any time we exit the driver to
 * wait for an interrupt response from a drive.  handler() points
 * at the appropriate code to handle the next interrupt, and a
 * timer is started to prevent us from waiting forever in case
 * something goes wrong (see the ide_timer_expiry() handler later on).
 */
void ide_set_handler (ide_drive_t *drive, ide_handler_t *handler, unsigned int timeout)
{
	ide_hwgroup_t *hwgroup = HWGROUP(drive);
#ifdef DEBUG
	if (hwgroup->handler != NULL) {
		printk("%s: ide_set_handler: handler not null; old=%p, new=%p\n",
			drive->name, hwgroup->handler, handler);
	}
#endif
	hwgroup->handler       = handler;
	hwgroup->timer.expires = jiffies + timeout;
	add_timer(&(hwgroup->timer));
}

/*
 * current_capacity() returns the capacity (in sectors) of a drive
 * according to its current geometry/LBA settings.
 */
static unsigned long current_capacity (ide_drive_t *drive)
{
	if (!drive->present)
		return 0;
	if (drive->driver != NULL)
		return DRIVER(drive)->capacity(drive);
	return 0;
}

/*
 * ide_geninit() is called exactly *once* for each major, from genhd.c,
 * at the beginning of the initial partition check for the drives.
 */
void ide_geninit (struct gendisk *gd)
{
	unsigned int unit;
	ide_hwif_t *hwif = gd->real_devices;

	for (unit = 0; unit < gd->nr_real; ++unit) {
		ide_drive_t *drive = &hwif->drives[unit];

		drive->part[0].nr_sects = current_capacity(drive);
		if (!drive->present || drive->media != ide_disk || drive->driver == NULL)
			drive->part[0].start_sect = -1; /* skip partition check */
	}
}

static void do_reset1 (ide_drive_t *, int);		/* needed below */

/*
 * atapi_reset_pollfunc() gets invoked to poll the interface for completion every 50ms
 * during an atapi drive reset operation. If the drive has not yet responded,
 * and we have not yet hit our maximum waiting time, then the timer is restarted
 * for another 50ms.
 */
static void atapi_reset_pollfunc (ide_drive_t *drive)
{
	ide_hwgroup_t *hwgroup = HWGROUP(drive);
	byte stat;

	OUT_BYTE (drive->select.all, IDE_SELECT_REG);
	udelay (10);

	if (OK_STAT(stat=GET_STAT(), 0, BUSY_STAT)) {
		printk("%s: ATAPI reset complete\n", drive->name);
	} else {
		if (jiffies < hwgroup->poll_timeout) {
			ide_set_handler (drive, &atapi_reset_pollfunc, HZ/20);
			return;	/* continue polling */
		}
		hwgroup->poll_timeout = 0;	/* end of polling */
		printk("%s: ATAPI reset timed-out, status=0x%02x\n", drive->name, stat);
		do_reset1 (drive, 1);	/* do it the old fashioned way */
		return;
	}
	hwgroup->poll_timeout = 0;	/* done polling */
}

/*
 * reset_pollfunc() gets invoked to poll the interface for completion every 50ms
 * during an ide reset operation. If the drives have not yet responded,
 * and we have not yet hit our maximum waiting time, then the timer is restarted
 * for another 50ms.
 */
static void reset_pollfunc (ide_drive_t *drive)
{
	ide_hwgroup_t *hwgroup = HWGROUP(drive);
	ide_hwif_t *hwif = HWIF(drive);
	byte tmp;

	if (!OK_STAT(tmp=GET_STAT(), 0, BUSY_STAT)) {
		if (jiffies < hwgroup->poll_timeout) {
			ide_set_handler (drive, &reset_pollfunc, HZ/20);
			return;	/* continue polling */
		}
		printk("%s: reset timed-out, status=0x%02x\n", hwif->name, tmp);
	} else  {
		printk("%s: reset: ", hwif->name);
		if ((tmp = GET_ERR()) == 1)
			printk("success\n");
		else {
#if FANCY_STATUS_DUMPS
			printk("master: ");
			switch (tmp & 0x7f) {
				case 1: printk("passed");
					break;
				case 2: printk("formatter device error");
					break;
				case 3: printk("sector buffer error");
					break;
				case 4: printk("ECC circuitry error");
					break;
				case 5: printk("controlling MPU error");
					break;
				default:printk("error (0x%02x?)", tmp);
			}
			if (tmp & 0x80)
				printk("; slave: failed");
			printk("\n");
#else
			printk("failed\n");
#endif /* FANCY_STATUS_DUMPS */
		}
	}
	hwgroup->poll_timeout = 0;	/* done polling */
}

static void pre_reset (ide_drive_t *drive)
{
	if (!drive->keep_settings) {
		drive->unmask = 0;
		drive->io_32bit = 0;
		if (drive->using_dma) {
			drive->using_dma = 0;
			printk("%s: disabled DMA\n", drive->name);
		}
	}
	if (drive->driver != NULL)
		DRIVER(drive)->pre_reset(drive);
}

/*
 * do_reset1() attempts to recover a confused drive by resetting it.
 * Unfortunately, resetting a disk drive actually resets all devices on
 * the same interface, so it can really be thought of as resetting the
 * interface rather than resetting the drive.
 *
 * ATAPI devices have their own reset mechanism which allows them to be
 * individually reset without clobbering other devices on the same interface.
 *
 * Unfortunately, the IDE interface does not generate an interrupt to let
 * us know when the reset operation has finished, so we must poll for this.
 * Equally poor, though, is the fact that this may a very long time to complete,
 * (up to 30 seconds worstcase).  So, instead of busy-waiting here for it,
 * we set a timer to poll at 50ms intervals.
 */
static void do_reset1 (ide_drive_t *drive, int  do_not_try_atapi)
{
	unsigned int unit;
	unsigned long flags;
	ide_hwif_t *hwif = HWIF(drive);
	ide_hwgroup_t *hwgroup = HWGROUP(drive);

	save_flags(flags);
	cli();		/* Why ? */

	/* For an ATAPI device, first try an ATAPI SRST. */
	if (drive->media != ide_disk && !do_not_try_atapi) {
		pre_reset(drive);
		OUT_BYTE (drive->select.all, IDE_SELECT_REG);
		udelay (20);
		OUT_BYTE (WIN_SRST, IDE_COMMAND_REG);
		hwgroup->poll_timeout = jiffies + WAIT_WORSTCASE;
		ide_set_handler (drive, &atapi_reset_pollfunc, HZ/20);
		restore_flags (flags);
		return;
	}

	/*
	 * First, reset any device state data we were maintaining
	 * for any of the drives on this interface.
	 */
	for (unit = 0; unit < MAX_DRIVES; ++unit)
		pre_reset(&hwif->drives[unit]);

#if OK_TO_RESET_CONTROLLER
	/*
	 * Note that we also set nIEN while resetting the device,
	 * to mask unwanted interrupts from the interface during the reset.
	 * However, due to the design of PC hardware, this will cause an
	 * immediate interrupt due to the edge transition it produces.
	 * This single interrupt gives us a "fast poll" for drives that
	 * recover from reset very quickly, saving us the first 50ms wait time.
	 */
	OUT_BYTE(drive->ctl|6,IDE_CONTROL_REG);	/* set SRST and nIEN */
	udelay(10);			/* more than enough time */
	OUT_BYTE(drive->ctl|2,IDE_CONTROL_REG);	/* clear SRST, leave nIEN */
	udelay(10);			/* more than enough time */
	hwgroup->poll_timeout = jiffies + WAIT_WORSTCASE;
	ide_set_handler (drive, &reset_pollfunc, HZ/20);
#endif	/* OK_TO_RESET_CONTROLLER */

	restore_flags (flags);
}

/*
 * ide_do_reset() is the entry point to the drive/interface reset code.
 */
void ide_do_reset (ide_drive_t *drive)
{
	do_reset1 (drive, 0);
}

/*
 * Clean up after success/failure of an explicit drive cmd
 */
void ide_end_drive_cmd (ide_drive_t *drive, byte stat, byte err)
{
	unsigned long flags;
	struct request *rq = HWGROUP(drive)->rq;

	if (rq->cmd == IDE_DRIVE_CMD) {
		byte *args = (byte *) rq->buffer;
		rq->errors = !OK_STAT(stat,READY_STAT,BAD_STAT);
		if (args) {
			args[0] = stat;
			args[1] = err;
			args[2] = IN_BYTE(IDE_NSECTOR_REG);
		}
	}
	save_flags(flags);
	cli();
	drive->queue = rq->next;
	blk_dev[MAJOR(rq->rq_dev)].current_request = NULL;
	HWGROUP(drive)->rq = NULL;
	rq->rq_status = RQ_INACTIVE;
	if (rq->sem != NULL)
		up(rq->sem);
	restore_flags(flags);
}

/*
 * Error reporting, in human readable form (luxurious, but a memory hog).
 */
byte ide_dump_status (ide_drive_t *drive, const char *msg, byte stat)
{
	unsigned long flags;
	byte err = 0;

	save_flags (flags);
	ide_sti();
	printk("%s: %s: status=0x%02x", drive->name, msg, stat);
#if FANCY_STATUS_DUMPS
	printk(" { ");
	if (stat & BUSY_STAT)
		printk("Busy ");
	else {
		if (stat & READY_STAT)	printk("DriveReady ");
		if (stat & WRERR_STAT)	printk("DeviceFault ");
		if (stat & SEEK_STAT)	printk("SeekComplete ");
		if (stat & DRQ_STAT)	printk("DataRequest ");
		if (stat & ECC_STAT)	printk("CorrectedError ");
		if (stat & INDEX_STAT)	printk("Index ");
		if (stat & ERR_STAT)	printk("Error ");
	}
	printk("}");
#endif	/* FANCY_STATUS_DUMPS */
	printk("\n");
	if ((stat & (BUSY_STAT|ERR_STAT)) == ERR_STAT) {
		err = GET_ERR();
		printk("%s: %s: error=0x%02x", drive->name, msg, err);
#if FANCY_STATUS_DUMPS
		if (drive->media == ide_disk) {
			printk(" { ");
			if (err & BBD_ERR)	printk("BadSector ");
			if (err & ECC_ERR)	printk("UncorrectableError ");
			if (err & ID_ERR)	printk("SectorIdNotFound ");
			if (err & ABRT_ERR)	printk("DriveStatusError ");
			if (err & TRK0_ERR)	printk("TrackZeroNotFound ");
			if (err & MARK_ERR)	printk("AddrMarkNotFound ");
			printk("}");
			if (err & (BBD_ERR|ECC_ERR|ID_ERR|MARK_ERR)) {
				byte cur = IN_BYTE(IDE_SELECT_REG);
				if (cur & 0x40) {	/* using LBA? */
					printk(", LBAsect=%ld", (unsigned long)
					 ((cur&0xf)<<24)
					 |(IN_BYTE(IDE_HCYL_REG)<<16)
					 |(IN_BYTE(IDE_LCYL_REG)<<8)
					 | IN_BYTE(IDE_SECTOR_REG));
				} else {
					printk(", CHS=%d/%d/%d",
					 (IN_BYTE(IDE_HCYL_REG)<<8) +
					  IN_BYTE(IDE_LCYL_REG),
					  cur & 0xf,
					  IN_BYTE(IDE_SECTOR_REG));
				}
				if (HWGROUP(drive)->rq)
					printk(", sector=%ld", HWGROUP(drive)->rq->sector);
			}
		}
#endif	/* FANCY_STATUS_DUMPS */
		printk("\n");
	}
	restore_flags (flags);
	return err;
}

/*
 * try_to_flush_leftover_data() is invoked in response to a drive
 * unexpectedly having its DRQ_STAT bit set.  As an alternative to
 * resetting the drive, this routine tries to clear the condition
 * by read a sector's worth of data from the drive.  Of course,
 * this may not help if the drive is *waiting* for data from *us*.
 */
static void try_to_flush_leftover_data (ide_drive_t *drive)
{
	int i = (drive->mult_count ? drive->mult_count : 1) * SECTOR_WORDS;

	if (drive->media != ide_disk)
		return;
	while (i > 0) {
		unsigned long buffer[16];
		unsigned int wcount = (i > 16) ? 16 : i;
		i -= wcount;
		ide_input_data (drive, buffer, wcount);
	}
}

/*
 * ide_error() takes action based on the error returned by the controller.
 */
void ide_error (ide_drive_t *drive, const char *msg, byte stat)
{
	struct request *rq;
	byte err;

	err = ide_dump_status(drive, msg, stat);
	if ((rq = HWGROUP(drive)->rq) == NULL || drive == NULL)
		return;
	/* retry only "normal" I/O: */
	if (rq->cmd == IDE_DRIVE_CMD) {
		rq->errors = 1;
		ide_end_drive_cmd(drive, stat, err);
		return;
	}
	if (stat & BUSY_STAT) {		/* other bits are useless when BUSY */
		rq->errors |= ERROR_RESET;
	} else {
		if (drive->media == ide_disk && (stat & ERR_STAT)) {
			/* err has different meaning on cdrom and tape */
			if (err & (BBD_ERR | ECC_ERR))	/* retries won't help these */
				rq->errors = ERROR_MAX;
			else if (err & TRK0_ERR)	/* help it find track zero */
				rq->errors |= ERROR_RECAL;
		}
		if ((stat & DRQ_STAT) && rq->cmd != WRITE)
			try_to_flush_leftover_data(drive);
	}
	if (GET_STAT() & (BUSY_STAT|DRQ_STAT))
		rq->errors |= ERROR_RESET;	/* Mmmm.. timing problem */

	if (rq->errors >= ERROR_MAX) {
		if (drive->driver != NULL)
			DRIVER(drive)->end_request(0, HWGROUP(drive));
		else
	 		ide_end_request(0, HWGROUP(drive));
	} else {
		if ((rq->errors & ERROR_RESET) == ERROR_RESET) {
			++rq->errors;
			ide_do_reset(drive);
			return;
		} else if ((rq->errors & ERROR_RECAL) == ERROR_RECAL)
			drive->special.b.recalibrate = 1;
		++rq->errors;
	}
}

/*
 * Issue a simple drive command
 * The drive must be selected beforehand.
 */
void ide_cmd(ide_drive_t *drive, byte cmd, byte nsect, ide_handler_t *handler)
{
	ide_set_handler (drive, handler, WAIT_CMD);
	OUT_BYTE(drive->ctl,IDE_CONTROL_REG);
	OUT_BYTE(nsect,IDE_NSECTOR_REG);
	OUT_BYTE(cmd,IDE_COMMAND_REG);
}

/*
 * drive_cmd_intr() is invoked on completion of a special DRIVE_CMD.
 */
static void drive_cmd_intr (ide_drive_t *drive)
{
	struct request *rq = HWGROUP(drive)->rq;
	byte *args = (byte *) rq->buffer;
	byte stat = GET_STAT();

	ide_sti();
	if ((stat & DRQ_STAT) && args && args[3]) {
		byte io_32bit = drive->io_32bit;
		drive->io_32bit = 0;
		ide_input_data(drive, &args[4], args[3] * SECTOR_WORDS);
		drive->io_32bit = io_32bit;
		stat = GET_STAT();
	}
	if (OK_STAT(stat,READY_STAT,BAD_STAT))
		ide_end_drive_cmd (drive, stat, GET_ERR());
	else
		ide_error(drive, "drive_cmd", stat); /* calls ide_end_drive_cmd */
}

/*
 * do_special() is used to issue WIN_SPECIFY, WIN_RESTORE, and WIN_SETMULT
 * commands to a drive.  It used to do much more, but has been scaled back.
 */
static inline void do_special (ide_drive_t *drive)
{
	special_t *s = &drive->special;

#ifdef DEBUG
	printk("%s: do_special: 0x%02x\n", drive->name, s->all);
#endif
	if (s->b.set_tune) {
		ide_tuneproc_t *tuneproc = HWIF(drive)->tuneproc;
		s->b.set_tune = 0;
		if (tuneproc != NULL)
			tuneproc(drive, drive->tune_req);
	} else if (drive->driver != NULL) {
		DRIVER(drive)->special(drive);
	} else if (s->all) {
		printk("%s: bad special flag: 0x%02x\n", drive->name, s->all);
		s->all = 0;
	}
}

/*
 * This routine busy-waits for the drive status to be not "busy".
 * It then checks the status for all of the "good" bits and none
 * of the "bad" bits, and if all is okay it returns 0.  All other
 * cases return 1 after invoking ide_error() -- caller should just return.
 *
 * This routine should get fixed to not hog the cpu during extra long waits..
 * That could be done by busy-waiting for the first jiffy or two, and then
 * setting a timer to wake up at half second intervals thereafter,
 * until timeout is achieved, before timing out.
 */
int ide_wait_stat (ide_drive_t *drive, byte good, byte bad, unsigned long timeout)
{
	byte stat;
	unsigned long flags;

test:
	udelay(1);	/* spec allows drive 400ns to change "BUSY" */
	if (OK_STAT((stat = GET_STAT()), good, bad))
		return 0;	/* fast exit for most frequent case */
	if (!(stat & BUSY_STAT)) {
		ide_error(drive, "status error", stat);
		return 1;
	}

	save_flags(flags);
	ide_sti();
	timeout += jiffies;
	do {
		if (!((stat = GET_STAT()) & BUSY_STAT)) {
			restore_flags(flags);
			goto test;
		}
	} while (jiffies <= timeout);

	restore_flags(flags);
	ide_error(drive, "status timeout", GET_STAT());
	return 1;
}

/*
 * execute_drive_cmd() issues a special drive command,
 * usually initiated by ioctl() from the external hdparm program.
 */
static void execute_drive_cmd (ide_drive_t *drive, struct request *rq)
{
	byte *args = rq->buffer;
	if (args) {
#ifdef DEBUG
		printk("%s: DRIVE_CMD cmd=0x%02x sc=0x%02x fr=0x%02x xx=0x%02x\n",
		 drive->name, args[0], args[1], args[2], args[3]);
#endif
		OUT_BYTE(args[2],IDE_FEATURE_REG);
		ide_cmd(drive, args[0], args[1], &drive_cmd_intr);
		return;
	} else {
		/*
		 * NULL is actually a valid way of waiting for
		 * all current requests to be flushed from the queue.
		 */
#ifdef DEBUG
		printk("%s: DRIVE_CMD (null)\n", drive->name);
#endif
		ide_end_drive_cmd(drive, GET_STAT(), GET_ERR());
		return;
	}
}

/*
 * do_request() initiates handling of a new I/O request
 */
static inline void do_request (ide_hwgroup_t *hwgroup, ide_hwif_t *hwif, ide_drive_t *drive)
{
	unsigned long block, blockend;
	struct request *rq = drive->queue;
	unsigned int minor = MINOR(rq->rq_dev), unit = minor >> PARTN_BITS;

	ide_sti();
#ifdef DEBUG
	printk("%s: do_request: current=0x%08lx\n", hwif->name, (unsigned long) rq);
#endif
	if (unit >= MAX_DRIVES) {
		printk("%s: bad device number: %s\n", hwif->name, kdevname(rq->rq_dev));
		goto kill_rq;
	}
#ifdef DEBUG
	if (rq->bh && !buffer_locked(rq->bh)) {
		printk("%s: block not locked\n", drive->name);
		goto kill_rq;
	}
#endif
	block    = rq->sector;
	blockend = block + rq->nr_sectors;
	if ((blockend < block) || (blockend > drive->part[minor&PARTN_MASK].nr_sects)) {
		printk("%s%c: bad access: block=%ld, count=%ld\n", drive->name,
		 (minor&PARTN_MASK)?'0'+(minor&PARTN_MASK):' ', block, rq->nr_sectors);
		goto kill_rq;
	}
	block += drive->part[minor&PARTN_MASK].start_sect + drive->sect0;
#if FAKE_FDISK_FOR_EZDRIVE
	if (block == 0 && drive->remap_0_to_1)
		block = 1;  /* redirect MBR access to EZ-Drive partn table */
#endif /* FAKE_FDISK_FOR_EZDRIVE */
#if (DISK_RECOVERY_TIME > 0)
	while ((read_timer() - hwif->last_time) < DISK_RECOVERY_TIME);
#endif

	hwgroup->hwif = hwif;
	hwgroup->drive = drive;
	SELECT_DRIVE(hwif, drive);
	if (ide_wait_stat(drive, drive->ready_stat, BUSY_STAT|DRQ_STAT, WAIT_READY)) {
		printk("%s: drive not ready for command\n", drive->name);
		return;
	}

	if (!drive->special.all) {
		if (rq->cmd == IDE_DRIVE_CMD) {
			execute_drive_cmd(drive, rq);
			return;
		}
		if (drive->driver != NULL) {
			DRIVER(drive)->do_request(drive, rq, block);
			return;
		}
		printk("%s: media type %d not supported\n", drive->name, drive->media);
		goto kill_rq;
	}
	do_special(drive);
	return;
kill_rq:
	if (drive->driver != NULL)
		DRIVER(drive)->end_request(0, HWGROUP(drive));
	else
		ide_end_request(0, hwgroup);
}

/*
 * ide_stall_queue() can be used by a drive to give excess bandwidth back
 * to the hwgroup by sleeping for timeout jiffies.
 */
void ide_stall_queue (ide_drive_t *drive, unsigned long timeout)
{
	if (timeout > WAIT_WORSTCASE)
		timeout = WAIT_WORSTCASE;
	drive->sleep = timeout + jiffies;
}

#define WAKEUP(drive)	((drive)->service_start + 2 * (drive)->service_time)

/*
 * choose_drive() selects the next drive which will be serviced.
 */
static inline ide_drive_t *choose_drive (ide_hwgroup_t *hwgroup)
{
	ide_drive_t *drive, *best;

repeat:	
	best = NULL;
	drive = hwgroup->drive;
	do {
		if (!drive->queue)
			continue;
		if (drive->sleep && drive->sleep > jiffies)
			continue;
		if (!best) {
			best = drive;
			continue;
		}
		if (drive->sleep && (!best->sleep || drive->sleep < best->sleep))
			best = drive;
		if (!best->sleep && WAKEUP(drive) < WAKEUP(best))
			best = drive;
	} while ((drive = drive->next) != hwgroup->drive);
	if (best != hwgroup->drive && best && best->service_time > WAIT_MIN_SLEEP && !best->sleep && best->nice1) {
		long t = (signed) (WAKEUP(best) - jiffies);
		if (t >= WAIT_MIN_SLEEP) {
			/*
			 * We *may* have some time to spare, but first let's see if
			 * someone can potentially benefit from our nice mood today..
			 */
			drive = best->next;
			do {
				if (drive->sleep)	/* this drive tried to be nice to us */
					continue;
				if (WAKEUP(drive) > jiffies - best->service_time && WAKEUP(drive) < jiffies + t) {
					ide_stall_queue(best, IDE_MIN(t, 10 * WAIT_MIN_SLEEP));
					goto repeat;
				}
			} while ((drive = drive->next) != best);
		}
	}
	return best;
}

static inline void ide_leave_hwgroup (ide_hwgroup_t *hwgroup)
{
	ide_drive_t *drive = hwgroup->drive;
	unsigned long sleep = 0;

	hwgroup->rq = NULL;
	do {
		blk_dev[HWIF(drive)->major].current_request = NULL;
		if (!drive->sleep)
			continue;
		if (!sleep) {
			sleep = drive->sleep;
			continue;
		}
		if (drive->sleep < sleep)
			sleep = drive->sleep;
	} while ((drive = drive->next) != hwgroup->drive);
	if (sleep) {
		if (sleep < jiffies + WAIT_MIN_SLEEP)
			sleep = jiffies + WAIT_MIN_SLEEP;
		hwgroup->timer.expires = sleep;
		add_timer(&hwgroup->timer);
	} else {	/* Ugly, but how can we sleep for the lock otherwise? perhaps from tq_scheduler? */
		ide_release_lock(&ide_lock);
		hwgroup->active = 0;
	}
}

/*
 * The driver enables interrupts as much as possible.  In order to do this,
 * (a) the device-interrupt is always masked before entry, and
 * (b) the timeout-interrupt is always disabled before entry.
 *
 * If we enter here from, say irq14, and then start a new request for irq15,
 * (possible with "serialize" option) then we cannot ensure that we exit
 * before the irq15 hits us. So, we must be careful not to let this bother us.
 *
 * Interrupts are still masked (by default) whenever we are exchanging
 * data/cmds with a drive, because some drives seem to have very poor
 * tolerance for latency during I/O.  For devices which don't suffer from
 * this problem (most don't), the unmask flag can be set using the "hdparm"
 * utility, to permit other interrupts during data/cmd transfers.
 */
void ide_do_request (ide_hwgroup_t *hwgroup)
{
	cli();	/* paranoia */
	if (hwgroup->handler != NULL) {
		printk("%s: EEeekk!! handler not NULL in ide_do_request()\n", hwgroup->hwif->name);
		return;
	}
	do {
		ide_drive_t *drive = choose_drive(hwgroup);
		if (drive != NULL) {
			ide_hwif_t *hwif = HWIF(drive);
			if (hwgroup->hwif->sharing_irq && hwif != hwgroup->hwif)
				OUT_BYTE(hwgroup->drive->ctl|2, hwgroup->hwif->io_ports[IDE_CONTROL_OFFSET]);
			drive->sleep = 0;
			blk_dev[hwif->major].current_request = hwgroup->rq = drive->queue;
			drive->service_start = jiffies;
			do_request(hwgroup, hwif, drive);
			cli();
		} else {
			ide_leave_hwgroup(hwgroup);	/* no work left for this hwgroup */
			return;
		}
	} while (hwgroup->handler == NULL);
}

/*
 * ide_get_queue() returns the queue which corresponds to a given device.
 */
struct request **ide_get_queue (kdev_t dev)
{
	struct blk_dev_struct *bdev = blk_dev + MAJOR(dev);
	ide_hwif_t *hwif = bdev->data;

	return &hwif->drives[DEVICE_NR(dev) & 1].queue;
}

/*
 * do_hwgroup_request() invokes ide_do_request() after first masking
 * all possible interrupts for the current hwgroup.  This prevents race
 * conditions in the event that an unexpected interrupt occurs while
 * we are in the driver.
 *
 * Note that when an interrupt is used to reenter the driver, the first level
 * handler will already have masked the irq that triggered, but any other ones
 * for the hwgroup will still be unmasked.  The driver tries to be careful
 * about such things.
 */
static void do_hwgroup_request (ide_hwgroup_t *hwgroup)
{
	if (hwgroup->handler == NULL) {
		ide_hwif_t *hgif = hwgroup->hwif;
		ide_hwif_t *hwif = hgif;

		del_timer(&hwgroup->timer);
		ide_get_lock(&ide_lock, ide_intr, hwgroup);
		hwgroup->active = 1;
		do {
			disable_irq(hwif->irq);
		} while ((hwif = hwif->next) != hgif);
		ide_do_request (hwgroup);
		do {
			enable_irq(hwif->irq);
		} while ((hwif = hwif->next) != hgif);
	}
}

void do_ide0_request (void)	/* invoked with cli() */
{
	do_hwgroup_request (ide_hwifs[0].hwgroup);
}

#if MAX_HWIFS > 1
void do_ide1_request (void)	/* invoked with cli() */
{
	do_hwgroup_request (ide_hwifs[1].hwgroup);
}
#endif /* MAX_HWIFS > 1 */

#if MAX_HWIFS > 2
void do_ide2_request (void)	/* invoked with cli() */
{
	do_hwgroup_request (ide_hwifs[2].hwgroup);
}
#endif /* MAX_HWIFS > 2 */

#if MAX_HWIFS > 3
void do_ide3_request (void)	/* invoked with cli() */
{
	do_hwgroup_request (ide_hwifs[3].hwgroup);
}
#endif /* MAX_HWIFS > 3 */

void ide_timer_expiry (unsigned long data)
{
	ide_hwgroup_t *hwgroup = (ide_hwgroup_t *) data;
	ide_drive_t   *drive   = hwgroup->drive;
	ide_handler_t *handler;
	unsigned long flags;

	save_flags(flags);
	cli();

	if ((handler = hwgroup->handler) != NULL) {
		hwgroup->handler = NULL;
		if (hwgroup->poll_timeout != 0)	/* polling in progress? */
			handler(drive);
		else {				/* abort the operation */
			if (hwgroup->hwif->dmaproc)
				(void) hwgroup->hwif->dmaproc (ide_dma_abort, drive);
			ide_error(drive, "irq timeout", GET_STAT());
		}
		cli();
		if (hwgroup->handler == NULL) {
			set_recovery_timer(HWIF(drive));
			drive->service_time = jiffies - drive->service_start;
			do_hwgroup_request (hwgroup);
		}
	} else
		do_hwgroup_request (hwgroup);
	restore_flags(flags);
}

/*
 * There's nothing really useful we can do with an unexpected interrupt,
 * other than reading the status register (to clear it), and logging it.
 * There should be no way that an irq can happen before we're ready for it,
 * so we needn't worry much about losing an "important" interrupt here.
 *
 * On laptops (and "green" PCs), an unexpected interrupt occurs whenever the
 * drive enters "idle", "standby", or "sleep" mode, so if the status looks
 * "good", we just ignore the interrupt completely.
 *
 * This routine assumes cli() is in effect when called.
 *
 * If an unexpected interrupt happens on irq15 while we are handling irq14
 * and if the two interfaces are "serialized" (CMD640), then it looks like
 * we could screw up by interfering with a new request being set up for irq15.
 *
 * In reality, this is a non-issue.  The new command is not sent unless the
 * drive is ready to accept one, in which case we know the drive is not
 * trying to interrupt us.  And ide_set_handler() is always invoked before
 * completing the issuance of any new drive command, so we will not be
 * accidently invoked as a result of any valid command completion interrupt.
 *
 */
static void unexpected_intr (int irq, ide_hwgroup_t *hwgroup)
{
	byte stat;
	unsigned int unit;
	ide_hwif_t *hwif = hwgroup->hwif;

	/*
	 * handle the unexpected interrupt
	 */
	do {
		if (hwif->irq == irq) {
			for (unit = 0; unit < MAX_DRIVES; ++unit) {
				ide_drive_t *drive = &hwif->drives[unit];
				if (!drive->present)
					continue;
				SELECT_DRIVE(hwif,drive);
				udelay(100);  /* Ugly, but wait_stat() may not be safe here */
				if (!OK_STAT(stat=GET_STAT(), drive->ready_stat, BAD_STAT)) {
					/* Try to not flood the console with msgs */
					static unsigned long last_msgtime = 0;
					if ((last_msgtime + (HZ/2)) < jiffies) {
						last_msgtime = jiffies;
						(void) ide_dump_status(drive, "unexpected_intr", stat);
					}
				}
				if ((stat & DRQ_STAT))
					try_to_flush_leftover_data(drive);
			}
		}
	} while ((hwif = hwif->next) != hwgroup->hwif);
	SELECT_DRIVE(hwif,hwgroup->drive); /* Ugh.. probably interrupts current I/O */
	udelay(100);  /* Ugly, but wait_stat() may not be safe here */
}

/*
 * entry point for all interrupts, caller does cli() for us
 */
void ide_intr (int irq, void *dev_id, struct pt_regs *regs)
{
	ide_hwgroup_t *hwgroup = dev_id;
	ide_handler_t *handler;

	if (!ide_ack_intr (hwgroup->hwif->io_ports[IDE_DATA_OFFSET],
			   hwgroup->hwif->io_ports[IDE_IRQ_OFFSET]))
		return;

	if (irq == hwgroup->hwif->irq && (handler = hwgroup->handler) != NULL) {
		ide_drive_t *drive = hwgroup->drive;
		hwgroup->handler = NULL;
		del_timer(&(hwgroup->timer));
		if (drive->unmask)
			ide_sti();
		handler(drive);
		cli();	/* this is necessary, as next rq may be different irq */
		if (hwgroup->handler == NULL) {
			set_recovery_timer(HWIF(drive));
			drive->service_time = jiffies - drive->service_start;
			ide_do_request(hwgroup);
		}
	} else {
		unexpected_intr(irq, hwgroup);
	}
	cli();
}

/*
 * get_info_ptr() returns the (ide_drive_t *) for a given device number.
 * It returns NULL if the given device number does not match any present drives.
 */
static ide_drive_t *get_info_ptr (kdev_t i_rdev)
{
	int		major = MAJOR(i_rdev);
	unsigned int	h;

	for (h = 0; h < MAX_HWIFS; ++h) {
		ide_hwif_t  *hwif = &ide_hwifs[h];
		if (hwif->present && major == hwif->major) {
			unsigned unit = DEVICE_NR(i_rdev);
			if (unit < MAX_DRIVES) {
				ide_drive_t *drive = &hwif->drives[unit];
				if (drive->present)
					return drive;
			}
			break;
		}
	}
	return NULL;
}

/*
 * This function is intended to be used prior to invoking ide_do_drive_cmd().
 */
void ide_init_drive_cmd (struct request *rq)
{
	rq->buffer = NULL;
	rq->cmd = IDE_DRIVE_CMD;
	rq->sector = 0;
	rq->nr_sectors = 0;
	rq->current_nr_sectors = 0;
	rq->sem = NULL;
	rq->bh = NULL;
	rq->bhtail = NULL;
	rq->next = NULL;
}

/*
 * This function issues a special IDE device request
 * onto the request queue.
 *
 * If action is ide_wait, then then rq is queued at the end of
 * the request queue, and the function sleeps until it has been
 * processed.  This is for use when invoked from an ioctl handler.
 *
 * If action is ide_preempt, then the rq is queued at the head of
 * the request queue, displacing the currently-being-processed
 * request and this function returns immediately without waiting
 * for the new rq to be completed.  This is VERY DANGEROUS, and is
 * intended for careful use by the ATAPI tape/cdrom driver code.
 *
 * If action is ide_next, then the rq is queued immediately after
 * the currently-being-processed-request (if any), and the function
 * returns without waiting for the new rq to be completed.  As above,
 * This is VERY DANGEROUS, and is intended for careful use by the
 * ATAPI tape/cdrom driver code.
 *
 * If action is ide_end, then the rq is queued at the end of the
 * request queue, and the function returns immediately without waiting
 * for the new rq to be completed. This is again intended for careful
 * use by the ATAPI tape/cdrom driver code.
 */
int ide_do_drive_cmd (ide_drive_t *drive, struct request *rq, ide_action_t action)
{
	unsigned long flags;
	ide_hwgroup_t *hwgroup = HWGROUP(drive);
	unsigned int major = HWIF(drive)->major;
	struct request *cur_rq;
	struct semaphore sem = MUTEX_LOCKED;

	if (IS_PROMISE_DRIVE && rq->buffer != NULL)
		return -ENOSYS;  /* special drive cmds not supported */
	rq->errors = 0;
	rq->rq_status = RQ_ACTIVE;
	rq->rq_dev = MKDEV(major,(drive->select.b.unit)<<PARTN_BITS);
	if (action == ide_wait)
		rq->sem = &sem;

	save_flags(flags);
	cli();
	cur_rq = drive->queue;

	if (cur_rq == NULL || action == ide_preempt) {
		rq->next = cur_rq;
		drive->queue = rq;
		if (action == ide_preempt)
			hwgroup->rq = NULL;
	} else {
		if (action == ide_wait || action == ide_end) {
			while (cur_rq->next != NULL)	/* find end of list */
				cur_rq = cur_rq->next;
		}
		rq->next = cur_rq->next;
		cur_rq->next = rq;
	}
	if (!hwgroup->active) {
		do_hwgroup_request(hwgroup);
		cli();
	}
	if (action == ide_wait  && rq->rq_status != RQ_INACTIVE)
		down(&sem);	/* wait for it to be serviced */
	restore_flags(flags);
	return rq->errors ? -EIO : 0;	/* return -EIO if errors */
}

/*
 * This routine is called to flush all partitions and partition tables
 * for a changed disk, and then re-read the new partition table.
 * If we are revalidating a disk because of a media change, then we
 * enter with usage == 0.  If we are using an ioctl, we automatically have
 * usage == 1 (we need an open channel to use an ioctl :-), so this
 * is our limit.
 */
int ide_revalidate_disk(kdev_t i_rdev)
{
	ide_drive_t *drive;
	unsigned int p, major, minor;
	long flags;

	if ((drive = get_info_ptr(i_rdev)) == NULL)
		return -ENODEV;
	major = MAJOR(i_rdev);
	minor = drive->select.b.unit << PARTN_BITS;
	save_flags(flags);
	cli();
	if (drive->busy || (drive->usage > 1)) {
		restore_flags(flags);
		return -EBUSY;
	};
	drive->busy = 1;
	MOD_INC_USE_COUNT;
	restore_flags(flags);

	for (p = 0; p < (1<<PARTN_BITS); ++p) {
		if (drive->part[p].nr_sects > 0) {
			kdev_t devp = MKDEV(major, minor+p);
			fsync_dev          (devp);
			invalidate_inodes  (devp);
			invalidate_buffers (devp);
		}
		drive->part[p].start_sect = 0;
		drive->part[p].nr_sects   = 0;
	};

	drive->part[0].nr_sects = current_capacity(drive);
	if (drive->media != ide_disk || drive->driver == NULL)
		drive->part[0].start_sect = -1;
	resetup_one_dev(HWIF(drive)->gd, drive->select.b.unit);

	drive->busy = 0;
	wake_up(&drive->wqueue);
	MOD_DEC_USE_COUNT;
	return 0;
}

static void revalidate_drives (void)
{
	ide_hwif_t *hwif;
	ide_drive_t *drive;
	int index, unit;

	for (index = 0; index < MAX_HWIFS; ++index) {
		hwif = &ide_hwifs[index];
		for (unit = 0; unit < MAX_DRIVES; ++unit) {
			drive = &ide_hwifs[index].drives[unit];
			if (drive->revalidate) {
				drive->revalidate = 0;
				if (!initializing)
					(void) ide_revalidate_disk(MKDEV(hwif->major, unit<<PARTN_BITS));
			}
		}
	}
}

static void ide_init_module (int type)
{
	int found = 0;
	ide_module_t *module = ide_modules;
	
	while (module) {
		if (module->type == type) {
			found = 1;
			(void) module->init();
		}
		module = module->next;
	}
	revalidate_drives();
#ifdef CONFIG_KERNELD
	if (!found && type == IDE_PROBE_MODULE)
		(void) request_module("ide-probe");
#endif /* CONFIG_KERNELD */
}

static int ide_open(struct inode * inode, struct file * filp)
{
	ide_drive_t *drive;
	int rc;

	if ((drive = get_info_ptr(inode->i_rdev)) == NULL)
		return -ENXIO;
	MOD_INC_USE_COUNT;
	if (drive->driver == NULL)
		ide_init_module(IDE_DRIVER_MODULE);
#ifdef CONFIG_KERNELD
	if (drive->driver == NULL) {
		if (drive->media == ide_disk)
			(void) request_module("ide-disk");
		if (drive->media == ide_cdrom)
			(void) request_module("ide-cd");
		if (drive->media == ide_tape)
			(void) request_module("ide-tape");
		if (drive->media == ide_floppy)
			(void) request_module("ide-floppy");
	}
#endif /* CONFIG_KERNELD */
	while (drive->busy)
		sleep_on(&drive->wqueue);
	drive->usage++;
	if (drive->driver != NULL) {
		if ((rc = DRIVER(drive)->open(inode, filp, drive)))
			MOD_DEC_USE_COUNT;
		return rc;
	}
	printk ("%s: driver not present\n", drive->name);
	drive->usage--;
	MOD_DEC_USE_COUNT;
	return -ENXIO;
}

/*
 * Releasing a block device means we sync() it, so that it can safely
 * be forgotten about...
 */
static void ide_release(struct inode * inode, struct file * file)
{
	ide_drive_t *drive;

	if ((drive = get_info_ptr(inode->i_rdev)) != NULL) {
		fsync_dev(inode->i_rdev);
		drive->usage--;
		if (drive->driver != NULL)
			DRIVER(drive)->release(inode, file, drive);
		MOD_DEC_USE_COUNT;
	}
}

void ide_unregister (unsigned int index)
{
	struct gendisk *gd, **gdp;
	ide_drive_t *drive, *d;
	ide_hwif_t *hwif, *g;
	ide_hwgroup_t *hwgroup;
	int irq_count = 0, unit;
	unsigned long flags;

	if (index >= MAX_HWIFS)
		return;
	save_flags(flags);
	cli();
	hwif = &ide_hwifs[index];
	if (!hwif->present)
		goto abort;
	for (unit = 0; unit < MAX_DRIVES; ++unit) {
		drive = &hwif->drives[unit];
		if (!drive->present)
			continue;
		if (drive->busy || drive->usage)
			goto abort;
		if (drive->driver != NULL && DRIVER(drive)->cleanup(drive))
			goto abort;
	}
	hwif->present = 0;
	hwgroup = hwif->hwgroup;

	/*
	 * free the irq if we were the only hwif using it
	 */
	g = hwgroup->hwif;
	do {
		if (g->irq == hwif->irq)
			++irq_count;
		g = g->next;
	} while (g != hwgroup->hwif);
	if (irq_count == 1)
		free_irq(hwif->irq, hwgroup);

	/*
	 * Note that we only release the standard ports,
	 * and do not even try to handle any extra ports
	 * allocated for weird IDE interface chipsets.
	 */
	ide_release_region(hwif->io_ports[IDE_DATA_OFFSET], 8);
	ide_release_region(hwif->io_ports[IDE_CONTROL_OFFSET], 1);

	/*
	 * Remove us from the hwgroup, and free
	 * the hwgroup if we were the only member
	 */
	d = hwgroup->drive;
	for (index = 0; index < MAX_DRIVES; ++index) {
		drive = &hwif->drives[index];
		if (!drive->present)
			continue;
		while (hwgroup->drive->next != drive)
			hwgroup->drive = hwgroup->drive->next;
		hwgroup->drive->next = drive->next;
		if (hwgroup->drive == drive)
			hwgroup->drive = NULL;
		if (drive->id != NULL) {
			kfree(drive->id);
			drive->id = NULL;
		}
		drive->present = 0;
	}
	if (d->present)
		hwgroup->drive = d;
	while (hwgroup->hwif->next != hwif)
		hwgroup->hwif = hwgroup->hwif->next;
	hwgroup->hwif->next = hwif->next;
	if (hwgroup->hwif == hwif)
		kfree(hwgroup);
	else
		hwgroup->hwif = HWIF(hwgroup->drive);

	/*
	 * Remove us from the kernel's knowledge
	 */
	unregister_blkdev(hwif->major, hwif->name);
	kfree(blksize_size[hwif->major]);
	blk_dev[hwif->major].request_fn = NULL;
	blk_dev[hwif->major].data = NULL;
	blk_dev[hwif->major].queue = NULL;
	blksize_size[hwif->major] = NULL;
	for (gdp = &gendisk_head; *gdp; gdp = &((*gdp)->next))
		if (*gdp == hwif->gd)
			break;
	if (*gdp == NULL)
		printk("gd not in disk chain!\n");
	else {
		gd = *gdp; *gdp = gd->next;
		kfree(gd->sizes);
		kfree(gd->part);
		kfree(gd);
	}
	init_hwif_data (index);	/* restore hwif data to pristine status */
abort:
	restore_flags(flags);
}

int ide_register (int arg1, int arg2, int irq)
{
	int index, retry = 1;
	ide_hwif_t *hwif;
	ide_ioreg_t data_port = (ide_ioreg_t) arg1, ctl_port = (ide_ioreg_t) arg2;

	do {
		for (index = 0; index < MAX_HWIFS; ++index) {
			hwif = &ide_hwifs[index];
			if (hwif->io_ports[IDE_DATA_OFFSET] == data_port)
				goto found;
		}
		for (index = 0; index < MAX_HWIFS; ++index) {
			hwif = &ide_hwifs[index];
			if (!hwif->present)
				goto found;
		}
		for (index = 0; index < MAX_HWIFS; index++)
			ide_unregister(index);
	} while (retry--);
	return -1;
found:
	if (hwif->present)
		ide_unregister(index);
	if (hwif->present)
		return -1;
	ide_init_hwif_ports(hwif->io_ports, data_port, &hwif->irq);
	if (ctl_port)
		hwif->io_ports[IDE_CONTROL_OFFSET] = ctl_port;
	hwif->irq = irq;
	hwif->noprobe = 0;
	ide_init_module(IDE_PROBE_MODULE);
	ide_init_module(IDE_DRIVER_MODULE);
	return hwif->present ? index : -1;
}

static int ide_ioctl (struct inode *inode, struct file *file,
			unsigned int cmd, unsigned long arg)
{
	int err;
	ide_drive_t *drive;
	unsigned long flags;
	struct request rq;

	if (!inode || !(inode->i_rdev))
		return -EINVAL;
	if ((drive = get_info_ptr(inode->i_rdev)) == NULL)
		return -ENODEV;
	ide_init_drive_cmd (&rq);
	switch (cmd) {
		case HDIO_GETGEO:
		{
			struct hd_geometry *loc = (struct hd_geometry *) arg;
			if (!loc || drive->media != ide_disk) return -EINVAL;
			if (put_user(drive->bios_head, (byte *) &loc->heads)) return -EFAULT;
			if (put_user(drive->bios_sect, (byte *) &loc->sectors)) return -EFAULT;
			if (put_user(drive->bios_cyl, (unsigned short *) &loc->cylinders)) return -EFAULT;
			if (put_user((unsigned)drive->part[MINOR(inode->i_rdev)&PARTN_MASK].start_sect,
				(unsigned long *) &loc->start)) return -EFAULT;
			return 0;
		}
		case BLKFLSBUF:
			if (!suser()) return -EACCES;
			fsync_dev(inode->i_rdev);
			invalidate_buffers(inode->i_rdev);
			return 0;

		case BLKRASET:
			if (!suser()) return -EACCES;
			if(arg > 0xff) return -EINVAL;
			read_ahead[MAJOR(inode->i_rdev)] = arg;
			return 0;

		case BLKRAGET:
			return put_user(read_ahead[MAJOR(inode->i_rdev)], (long *) arg);

	 	case BLKGETSIZE:   /* Return device size */
			return put_user(drive->part[MINOR(inode->i_rdev)&PARTN_MASK].nr_sects, (long *) arg);

		case BLKRRPART: /* Re-read partition tables */
			if (!suser()) return -EACCES;
			return ide_revalidate_disk(inode->i_rdev);

		case HDIO_GET_KEEPSETTINGS:
			return put_user(drive->keep_settings, (long *) arg);

		case HDIO_GET_UNMASKINTR:
			return put_user(drive->unmask, (long *) arg);

		case HDIO_GET_DMA:
			return put_user(drive->using_dma, (long *) arg);

		case HDIO_GET_32BIT:
			return put_user(drive->io_32bit, (long *) arg);

		case HDIO_GET_MULTCOUNT:
			return put_user(drive->mult_count, (long *) arg);

		case HDIO_GET_IDENTITY:
			if (MINOR(inode->i_rdev) & PARTN_MASK)
				return -EINVAL;
			if (drive->id == NULL)
				return -ENOMSG;
			if (copy_to_user((char *)arg, (char *)drive->id, sizeof(*drive->id)))
				return -EFAULT;
			return 0;

		case HDIO_GET_NOWERR:
			return put_user(drive->bad_wstat == BAD_R_STAT, (long *) arg);

		case HDIO_GET_NICE:
		{
			long nice = 0;

			nice |= drive->dsc_overlap << IDE_NICE_DSC_OVERLAP;
			nice |= drive->atapi_overlap << IDE_NICE_ATAPI_OVERLAP;
			nice |= drive->nice0 << IDE_NICE_0;
			nice |= drive->nice1 << IDE_NICE_1;
			nice |= drive->nice2 << IDE_NICE_2;
			return put_user(nice, (long *) arg);
		}

		case HDIO_SET_DMA:
			if (!suser()) return -EACCES;
			if (drive->driver != NULL && !DRIVER(drive)->supports_dma)
				return -EPERM;
			if (!drive->id || !(drive->id->capability & 1) || !HWIF(drive)->dmaproc)
				return -EPERM;
		case HDIO_SET_KEEPSETTINGS:
		case HDIO_SET_UNMASKINTR:
		case HDIO_SET_NOWERR:
			if (arg > 1)
				return -EINVAL;
		case HDIO_SET_32BIT:
			if (!suser()) return -EACCES;
			if ((MINOR(inode->i_rdev) & PARTN_MASK))
				return -EINVAL;
			save_flags(flags);
			cli();
			switch (cmd) {
				case HDIO_SET_DMA:
					if (!(HWIF(drive)->dmaproc)) {
						restore_flags(flags);
						return -EPERM;
					}
					drive->using_dma = arg;
					break;
				case HDIO_SET_KEEPSETTINGS:
					drive->keep_settings = arg;
					break;
				case HDIO_SET_UNMASKINTR:
					if (arg && drive->no_unmask) {
						restore_flags(flags);
						return -EPERM;
					}
					drive->unmask = arg;
					break;
				case HDIO_SET_NOWERR:
					drive->bad_wstat = arg ? BAD_R_STAT : BAD_W_STAT;
					break;
				case HDIO_SET_32BIT:
					if (arg > (1 + (SUPPORT_VLB_SYNC<<1))) {
						restore_flags(flags);
						return -EINVAL;
					}
					if (arg && drive->no_io_32bit) {
						restore_flags(flags);
						return -EPERM;
					}
					drive->io_32bit = arg;
#ifdef CONFIG_BLK_DEV_DTC2278
					if (HWIF(drive)->chipset == ide_dtc2278)
						HWIF(drive)->drives[!drive->select.b.unit].io_32bit = arg;
#endif /* CONFIG_BLK_DEV_DTC2278 */
					break;
			}
			restore_flags(flags);
			return 0;

		case HDIO_SET_MULTCOUNT:
			if (!suser()) return -EACCES;
			if (MINOR(inode->i_rdev) & PARTN_MASK)
				return -EINVAL;
			if (drive->id && arg > drive->id->max_multsect)
				return -EINVAL;
			save_flags(flags);
			cli();
			if (drive->special.b.set_multmode) {
				restore_flags(flags);
				return -EBUSY;
			}
			drive->mult_req = arg;
			drive->special.b.set_multmode = 1;
			restore_flags(flags);
			(void) ide_do_drive_cmd (drive, &rq, ide_wait);
			return (drive->mult_count == arg) ? 0 : -EIO;

		case HDIO_DRIVE_CMD:
		{
			byte args[4], *argbuf = args;
			int argsize = 4;
			if (!suser()) return -EACCES;
			if (NULL == (void *) arg)
				return ide_do_drive_cmd(drive, &rq, ide_wait);
			if (copy_from_user(args, (void *)arg, 4))
				return -EFAULT;
			if (args[3]) {
				argsize = 4 + (SECTOR_WORDS * 4 * args[3]);
				argbuf = kmalloc(argsize, GFP_KERNEL);
				if (argbuf == NULL)
					return -ENOMEM;
				memcpy(argbuf, args, 4);
			}
			rq.buffer = argbuf;
			err = ide_do_drive_cmd(drive, &rq, ide_wait);
			if (copy_to_user((void *)arg, argbuf, argsize))
				err = -EFAULT;
			if (argsize > 4)
				kfree(argbuf);
			return err;
		}
		case HDIO_SET_PIO_MODE:
			if (!suser()) return -EACCES;
			if (MINOR(inode->i_rdev) & PARTN_MASK)
				return -EINVAL;
			if (!HWIF(drive)->tuneproc)
				return -ENOSYS;
			save_flags(flags);
			cli();
			if (drive->special.b.set_tune) {
				restore_flags(flags);
				return -EBUSY;
			}
			drive->tune_req = (byte) arg;
			drive->special.b.set_tune = 1;
			restore_flags(flags);
			(void) ide_do_drive_cmd (drive, &rq, ide_wait);
			return 0;

		case HDIO_SCAN_HWIF:
		{
			int args[3];
			if (!suser()) return -EACCES;
			if (copy_from_user(args, (void *)arg, 3 * sizeof(int)))
				return -EFAULT;
			if (ide_register(args[0], args[1], args[2]) == -1)
				return -EIO;
			return 0;
		}
		case HDIO_SET_NICE:
			if (!suser()) return -EACCES;
			if (drive->driver == NULL)
				return -EPERM;
			if (arg != (arg & ((1 << IDE_NICE_DSC_OVERLAP) | (1 << IDE_NICE_1))))
				return -EPERM;
			drive->dsc_overlap = (arg >> IDE_NICE_DSC_OVERLAP) & 1;
			if (drive->dsc_overlap && !DRIVER(drive)->supports_dsc_overlap) {
				drive->dsc_overlap = 0;
				return -EPERM;
			}
			drive->nice1 = (arg >> IDE_NICE_1) & 1;
			return 0;

		RO_IOCTLS(inode->i_rdev, arg);

		default:
			if (drive->driver != NULL)
				return DRIVER(drive)->ioctl(drive, inode, file, cmd, arg);
			return -EPERM;
	}
}

static int ide_check_media_change (kdev_t i_rdev)
{
	ide_drive_t *drive;

	if ((drive = get_info_ptr(i_rdev)) == NULL)
		return -ENODEV;
	if (drive->driver != NULL)
		return DRIVER(drive)->media_change(drive);
	return 0;
}

void ide_fixstring (byte *s, const int bytecount, const int byteswap)
{
	byte *p = s, *end = &s[bytecount & ~1]; /* bytecount must be even */

	if (byteswap) {
		/* convert from big-endian to host byte order */
		for (p = end ; p != s;) {
			unsigned short *pp = (unsigned short *) (p -= 2);
			*pp = ntohs(*pp);
		}
	}

	/* strip leading blanks */
	while (s != end && *s == ' ')
		++s;

	/* compress internal blanks and strip trailing blanks */
	while (s != end && *s) {
		if (*s++ != ' ' || (s != end && *s && *s != ' '))
			*p++ = *(s-1);
	}

	/* wipe out trailing garbage */
	while (p != end)
		*p++ = '\0';
}

/*
 * stridx() returns the offset of c within s,
 * or -1 if c is '\0' or not found within s.
 */
static int stridx (const char *s, char c)
{
	char *i = strchr(s, c);
	return (i && c) ? i - s : -1;
}

/*
 * match_parm() does parsing for ide_setup():
 *
 * 1. the first char of s must be '='.
 * 2. if the remainder matches one of the supplied keywords,
 *     the index (1 based) of the keyword is negated and returned.
 * 3. if the remainder is a series of no more than max_vals numbers
 *     separated by commas, the numbers are saved in vals[] and a
 *     count of how many were saved is returned.  Base10 is assumed,
 *     and base16 is allowed when prefixed with "0x".
 * 4. otherwise, zero is returned.
 */
static int match_parm (char *s, const char *keywords[], int vals[], int max_vals)
{
	static const char *decimal = "0123456789";
	static const char *hex = "0123456789abcdef";
	int i, n;

	if (*s++ == '=') {
		/*
		 * Try matching against the supplied keywords,
		 * and return -(index+1) if we match one
		 */
		if (keywords != NULL) {
			for (i = 0; *keywords != NULL; ++i) {
				if (!strcmp(s, *keywords++))
					return -(i+1);
			}
		}
		/*
		 * Look for a series of no more than "max_vals"
		 * numeric values separated by commas, in base10,
		 * or base16 when prefixed with "0x".
		 * Return a count of how many were found.
		 */
		for (n = 0; (i = stridx(decimal, *s)) >= 0;) {
			vals[n] = i;
			while ((i = stridx(decimal, *++s)) >= 0)
				vals[n] = (vals[n] * 10) + i;
			if (*s == 'x' && !vals[n]) {
				while ((i = stridx(hex, *++s)) >= 0)
					vals[n] = (vals[n] * 0x10) + i;
			}
			if (++n == max_vals)
				break;
			if (*s == ',' || *s == ';')
				++s;
		}
		if (!*s)
			return n;
	}
	return 0;	/* zero = nothing matched */
}

/*
 * ide_setup() gets called VERY EARLY during initialization,
 * to handle kernel "command line" strings beginning with "hdx="
 * or "ide".  Here is the complete set currently supported:
 *
 * "hdx="  is recognized for all "x" from "a" to "h", such as "hdc".
 * "idex=" is recognized for all "x" from "0" to "3", such as "ide1".
 *
 * "hdx=noprobe"	: drive may be present, but do not probe for it
 * "hdx=none"		: drive is NOT present, ignore cmos and do not probe
 * "hdx=nowerr"		: ignore the WRERR_STAT bit on this drive
 * "hdx=cdrom"		: drive is present, and is a cdrom drive
 * "hdx=cyl,head,sect"	: disk drive is present, with specified geometry
 * "hdx=autotune"	: driver will attempt to tune interface speed
 *				to the fastest PIO mode supported,
 *				if possible for this drive only.
 *				Not fully supported by all chipset types,
 *				and quite likely to cause trouble with
 *				older/odd IDE drives.
 *
 * "idebus=xx"		: inform IDE driver of VESA/PCI bus speed in MHz,
 *				where "xx" is between 20 and 66 inclusive,
 *				used when tuning chipset PIO modes.
 *				For PCI bus, 25 is correct for a P75 system,
 *				30 is correct for P90,P120,P180 systems,
 *				and 33 is used for P100,P133,P166 systems.
 *				If in doubt, use idebus=33 for PCI.
 *				As for VLB, it is safest to not specify it.
 *
 * "idex=noprobe"	: do not attempt to access/use this interface
 * "idex=base"		: probe for an interface at the addr specified,
 *				where "base" is usually 0x1f0 or 0x170
 *				and "ctl" is assumed to be "base"+0x206
 * "idex=base,ctl"	: specify both base and ctl
 * "idex=base,ctl,irq"	: specify base, ctl, and irq number
 * "idex=autotune"	: driver will attempt to tune interface speed
 *				to the fastest PIO mode supported,
 *				for all drives on this interface.
 *				Not fully supported by all chipset types,
 *				and quite likely to cause trouble with
 *				older/odd IDE drives.
 * "idex=noautotune"	: driver will NOT attempt to tune interface speed
 *				This is the default for most chipsets,
 *				except the cmd640.
 * "idex=serialize"	: do not overlap operations on idex and ide(x^1)
 *
 * The following are valid ONLY on ide0,
 * and the defaults for the base,ctl ports must not be altered.
 *
 * "ide0=dtc2278"	: probe/support DTC2278 interface
 * "ide0=ht6560b"	: probe/support HT6560B interface
 * "ide0=cmd640_vlb"	: *REQUIRED* for VLB cards with the CMD640 chip
 *			  (not for PCI -- automatically detected)
 * "ide0=qd6580"	: probe/support qd6580 interface
 * "ide0=ali14xx"	: probe/support ali14xx chipsets (ALI M1439, M1443, M1445)
 * "ide0=umc8672"	: probe/support umc8672 chipsets
 */
void ide_setup (char *s)
{
	int i, vals[3];
	ide_hwif_t *hwif;
	ide_drive_t *drive;
	unsigned int hw, unit;
	const char max_drive = 'a' + ((MAX_HWIFS * MAX_DRIVES) - 1);
	const char max_hwif  = '0' + (MAX_HWIFS - 1);

	printk("ide_setup: %s", s);
	init_ide_data ();

	/*
	 * Look for drive options:  "hdx="
	 */
	if (s[0] == 'h' && s[1] == 'd' && s[2] >= 'a' && s[2] <= max_drive) {
		const char *hd_words[] = {"none", "noprobe", "nowerr", "cdrom",
				"serialize", "autotune", "noautotune",
				"slow", "swapdata", NULL};
		unit = s[2] - 'a';
		hw   = unit / MAX_DRIVES;
		unit = unit % MAX_DRIVES;
		hwif = &ide_hwifs[hw];
		drive = &hwif->drives[unit];
		switch (match_parm(&s[3], hd_words, vals, 3)) {
			case -1: /* "none" */
				drive->nobios = 1;  /* drop into "noprobe" */
			case -2: /* "noprobe" */
				drive->noprobe = 1;
				goto done;
			case -3: /* "nowerr" */
				drive->bad_wstat = BAD_R_STAT;
				hwif->noprobe = 0;
				goto done;
			case -4: /* "cdrom" */
				drive->present = 1;
				drive->media = ide_cdrom;
				hwif->noprobe = 0;
				goto done;
			case -5: /* "serialize" */
				printk(" -- USE \"ide%d=serialize\" INSTEAD", hw);
				goto do_serialize;
			case -6: /* "autotune" */
				drive->autotune = 1;
				goto done;
			case -7: /* "noautotune" */
				drive->autotune = 2;
				goto done;
			case -8: /* "slow" */
				drive->slow = 1;
				goto done;
			case -9: /* swapdata */
				drive->bswap = 1;
				goto done;
			case 3: /* cyl,head,sect */
				drive->media	= ide_disk;
				drive->cyl	= drive->bios_cyl  = vals[0];
				drive->head	= drive->bios_head = vals[1];
				drive->sect	= drive->bios_sect = vals[2];
				drive->present	= 1;
				drive->forced_geom = 1;
				hwif->noprobe = 0;
				goto done;
			default:
				goto bad_option;
		}
	}

	if (s[0] != 'i' || s[1] != 'd' || s[2] != 'e')
		goto bad_option;
	/*
	 * Look for bus speed option:  "idebus="
	 */
	if (s[3] == 'b' && s[4] == 'u' && s[5] == 's') {
		if (match_parm(&s[6], NULL, vals, 1) != 1)
			goto bad_option;
		if (vals[0] >= 20 && vals[0] <= 66)
			idebus_parameter = vals[0];
		else
			printk(" -- BAD BUS SPEED! Expected value from 20 to 66");
		goto done;
	}
	/*
	 * Look for interface options:  "idex="
	 */
	if (s[3] >= '0' && s[3] <= max_hwif) {
		/*
		 * Be VERY CAREFUL changing this: note hardcoded indexes below
		 */
		const char *ide_words[] = {"noprobe", "serialize", "autotune", "noautotune",
			"qd6580", "ht6560b", "cmd640_vlb", "dtc2278", "umc8672", "ali14xx", "dc4030", "reset", NULL};
		hw = s[3] - '0';
		hwif = &ide_hwifs[hw];
		i = match_parm(&s[4], ide_words, vals, 3);

		/*
		 * Cryptic check to ensure chipset not already set for hwif:
		 */
		if (i > 0 || (i <= -5 && i != -12)) {
			if (hwif->chipset != ide_unknown)
				goto bad_option;
			if (i <= -5) {
				if (ide_hwifs[1].chipset != ide_unknown)
					goto bad_option;
				/*
				 * Interface keywords work only for ide0:
				 */
				if (hw != 0)
					goto bad_hwif;
				printk("\n");
			}
		}

		switch (i) {
			case -12: /* "reset" */
				hwif->reset = 1;
				goto done;
#ifdef CONFIG_BLK_DEV_PROMISE
			case -11: /* "dc4030" */
			{
				setup_dc4030(hwif);
				goto done;
			}
#endif /* CONFIG_BLK_DEV_PROMISE */
#ifdef CONFIG_BLK_DEV_ALI14XX
			case -10: /* "ali14xx" */
			{
				extern void init_ali14xx (void);
				init_ali14xx();
				goto done;
			}
#endif /* CONFIG_BLK_DEV_ALI14XX */
#ifdef CONFIG_BLK_DEV_UMC8672
			case -9: /* "umc8672" */
			{
				extern void init_umc8672 (void);
				init_umc8672();
				goto done;
			}
#endif /* CONFIG_BLK_DEV_UMC8672 */
#ifdef CONFIG_BLK_DEV_DTC2278
			case -8: /* "dtc2278" */
			{
				extern void init_dtc2278 (void);
				init_dtc2278();
				goto done;
			}
#endif /* CONFIG_BLK_DEV_DTC2278 */
#ifdef CONFIG_BLK_DEV_CMD640
			case -7: /* "cmd640_vlb" */
			{
				extern int cmd640_vlb; /* flag for cmd640.c */
				cmd640_vlb = 1;
				goto done;
			}
#endif /* CONFIG_BLK_DEV_CMD640 */
#ifdef CONFIG_BLK_DEV_HT6560B
			case -6: /* "ht6560b" */
			{
				extern void init_ht6560b (void);
				init_ht6560b();
				goto done;
			}
#endif /* CONFIG_BLK_DEV_HT6560B */
#if CONFIG_BLK_DEV_QD6580
			case -5: /* "qd6580" (has secondary i/f) */
			{
				extern void init_qd6580 (void);
				init_qd6580();
				goto done;
			}
#endif /* CONFIG_BLK_DEV_QD6580 */
			case -4: /* "noautotune" */
				hwif->drives[0].autotune = 2;
				hwif->drives[1].autotune = 2;
				goto done;
			case -3: /* "autotune" */
				hwif->drives[0].autotune = 1;
				hwif->drives[1].autotune = 1;
				goto done;
			case -2: /* "serialize" */
			do_serialize:
				ide_hwifs[hw].serialized = 1;   /* serialize */
				ide_hwifs[hw^1].serialized = 1; /* with mate */
				goto done;

			case -1: /* "noprobe" */
				hwif->noprobe = 1;
				goto done;

			case 1:	/* base */
				vals[1] = vals[0] + 0x206; /* default ctl */
			case 2: /* base,ctl */
				vals[2] = 0;	/* default irq = probe for it */
			case 3: /* base,ctl,irq */
				ide_init_hwif_ports(hwif->io_ports, (ide_ioreg_t) vals[0], &hwif->irq);
				hwif->io_ports[IDE_CONTROL_OFFSET] = (ide_ioreg_t) vals[1];
				hwif->irq      = vals[2];
				hwif->noprobe  = 0;
				hwif->chipset  = ide_generic;
				goto done;

			case 0: goto bad_option;
			default:
				printk(" -- SUPPORT NOT CONFIGURED IN THIS KERNEL\n");
				return;
		}
	}
bad_option:
	printk(" -- BAD OPTION\n");
	return;
bad_hwif:
	printk("-- NOT SUPPORTED ON ide%d", hw);
done:
	printk("\n");
}

/*
 * This routine is called from the partition-table code in genhd.c
 * to "convert" a drive to a logical geometry with fewer than 1024 cyls.
 *
 * The second parameter, "xparm", determines exactly how the translation 
 * will be handled:
 *		 0 = convert to CHS with fewer than 1024 cyls
 *			using the same method as Ontrack DiskManager.
 *		 1 = same as "0", plus offset everything by 63 sectors.
 *		-1 = similar to "0", plus redirect sector 0 to sector 1.
 *		>1 = convert to a CHS geometry with "xparm" heads.
 *
 * Returns 0 if the translation was not possible, if the device was not 
 * an IDE disk drive, or if a geometry was "forced" on the commandline.
 * Returns 1 if the geometry translation was successful.
 */
int ide_xlate_1024 (kdev_t i_rdev, int xparm, const char *msg)
{
	ide_drive_t *drive;
	static const byte head_vals[] = {4, 8, 16, 32, 64, 128, 255, 0};
	const byte *heads = head_vals;
	unsigned long tracks;

	if ((drive = get_info_ptr(i_rdev)) == NULL || drive->forced_geom)
		return 0;

	if (xparm > 1 && xparm <= drive->bios_head && drive->bios_sect == 63)
		return 0;		/* we already have a translation */

	printk("%s ", msg);

	if (drive->id) {
		drive->cyl  = drive->id->cyls;
		drive->head = drive->id->heads;
		drive->sect = drive->id->sectors;
	}
	drive->bios_cyl  = drive->cyl;
	drive->bios_head = drive->head;
	drive->bios_sect = drive->sect;
	drive->special.b.set_geometry = 1;

	tracks = drive->bios_cyl * drive->bios_head * drive->bios_sect / 63;
	drive->bios_sect = 63;
	if (xparm > 1) {
		drive->bios_head = xparm;
		drive->bios_cyl = tracks / drive->bios_head;
	} else {
		while (drive->bios_cyl >= 1024) {
			drive->bios_head = *heads;
			drive->bios_cyl = tracks / drive->bios_head;
			if (0 == *++heads)
				break;
		}
#if FAKE_FDISK_FOR_EZDRIVE
		if (xparm == -1) {
			drive->remap_0_to_1 = 1;
			msg = "0->1";
		} else
#endif /* FAKE_FDISK_FOR_EZDRIVE */
		if (xparm == 1) {
			drive->sect0 = 63;
			drive->bios_cyl = (tracks - 1) / drive->bios_head;
			msg = "+63";
		}
		printk("[remap %s] ", msg);
	}
	drive->part[0].nr_sects = current_capacity(drive);
	printk("[%d/%d/%d]", drive->bios_cyl, drive->bios_head, drive->bios_sect);
	return 1;
}

#ifdef CONFIG_PCI
#if defined(CONFIG_BLK_DEV_RZ1000) || defined(CONFIG_BLK_DEV_TRITON) || defined(CONFIG_BLK_DEV_OPTI621)

typedef void (ide_pci_init_proc_t)(byte, byte);

/*
 * ide_probe_pci() scans PCI for a specific vendor/device function,
 * and invokes the supplied init routine for each instance detected.
 */
static void ide_probe_pci (unsigned short vendor, unsigned short device, ide_pci_init_proc_t *init, int func_adj)
{
	unsigned long flags;
	unsigned index;
	byte fn, bus;

	save_flags(flags);
	cli();
	for (index = 0; !pcibios_find_device (vendor, device, index, &bus, &fn); ++index) {
		init (bus, fn + func_adj);
	}
	restore_flags(flags);
}

#endif /* defined(CONFIG_BLK_DEV_RZ1000) || defined(CONFIG_BLK_DEV_TRITON) || defined(CONFIG_BLK_DEV_OPTI621) */
#endif /* CONFIG_PCI */

/*
 * probe_for_hwifs() finds/initializes "known" IDE interfaces
 *
 * This routine should ideally be using pcibios_find_class() to find all
 * PCI IDE interfaces, but that function causes some systems to "go weird".
 */
static void probe_for_hwifs (void)
{
#ifdef CONFIG_PCI
	/*
	 * Find/initialize PCI IDE interfaces
	 */
	if (pcibios_present()) {
#ifdef CONFIG_BLK_DEV_RZ1000
		ide_pci_init_proc_t init_rz1000;
		ide_probe_pci (PCI_VENDOR_ID_PCTECH, PCI_DEVICE_ID_PCTECH_RZ1000, &init_rz1000, 0);
		ide_probe_pci (PCI_VENDOR_ID_PCTECH, PCI_DEVICE_ID_PCTECH_RZ1001, &init_rz1000, 0);
#endif /* CONFIG_BLK_DEV_RZ1000 */
#ifdef CONFIG_BLK_DEV_TRITON
		/*
		 * Apparently the BIOS32 services on Intel motherboards are
		 * buggy and won't find the PCI_DEVICE_ID_INTEL_82371_1 for us.
		 * So instead, we search for PCI_DEVICE_ID_INTEL_82371_0,
		 * and then add 1.
		 */
		ide_probe_pci (PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82371_0, &ide_init_triton, 1);
		ide_probe_pci (PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82371SB_1, &ide_init_triton, 0);
#endif /* CONFIG_BLK_DEV_TRITON */
#ifdef CONFIG_BLK_DEV_OPTI621
		ide_probe_pci (PCI_VENDOR_ID_OPTI, PCI_DEVICE_ID_OPTI_82C621, &ide_init_opti621, 0);
#endif /* CONFIG_BLK_DEV_OPTI621 */
	}
#endif /* CONFIG_PCI */
#ifdef CONFIG_BLK_DEV_CMD640
	{
		extern void ide_probe_for_cmd640x (void);
		ide_probe_for_cmd640x();
	}
#endif
#ifdef CONFIG_BLK_DEV_PROMISE
	init_dc4030();
#endif
}

void ide_init_builtin_drivers (void)
{
	/*
	 * Probe for special "known" interface chipsets
	 */
	probe_for_hwifs ();

	/*
	 * Probe for devices
	 */
#ifdef CONFIG_BLK_DEV_IDE
#ifdef __mc68000__
	if (ide_hwifs[0].io_ports[IDE_DATA_OFFSET]) {
		ide_get_lock(&ide_lock, NULL, NULL);
		disable_irq(ide_hwifs[0].irq);
	}
#endif /* __mc68000__ */

	(void) ideprobe_init();

#ifdef __mc68000__
	if (ide_hwifs[0].io_ports[IDE_DATA_OFFSET]) {
		enable_irq(ide_hwifs[0].irq);
		ide_release_lock(&ide_lock);
	}
#endif /* __mc68000__ */
#endif /* CONFIG_BLK_DEV_IDE */

	/*
	 * Attempt to match drivers for the available drives
	 */
#ifdef CONFIG_BLK_DEV_IDEDISK
	(void) idedisk_init();
#endif /* CONFIG_BLK_DEV_IDEDISK */
#ifdef CONFIG_BLK_DEV_IDECD
	(void) ide_cdrom_init();
#endif /* CONFIG_BLK_DEV_IDECD */
#ifdef CONFIG_BLK_DEV_IDETAPE
	(void) idetape_init();
#endif /* CONFIG_BLK_DEV_IDETAPE */
#ifdef CONFIG_BLK_DEV_IDEFLOPPY
	(void) idefloppy_init();
#endif /* CONFIG_BLK_DEV_IDEFLOPPY */
#ifdef CONFIG_BLK_DEV_IDESCSI
	(void) idescsi_init();
#endif /* CONFIG_BLK_DEV_IDESCSI */
}

static int default_cleanup (ide_drive_t *drive)
{
	return ide_unregister_subdriver(drive);
}

static void default_do_request(ide_drive_t *drive, struct request *rq, unsigned long block)
{
	ide_end_request(0, HWGROUP(drive));
}
 
static void default_end_request (byte uptodate, ide_hwgroup_t *hwgroup)
{
	ide_end_request(uptodate, hwgroup);
}
  
static int default_ioctl (ide_drive_t *drive, struct inode *inode, struct file *file,
			  unsigned int cmd, unsigned long arg)
{
	return -EIO;
}

static int default_open (struct inode *inode, struct file *filp, ide_drive_t *drive)
{
	drive->usage--;
	return -EIO;
}

static void default_release (struct inode *inode, struct file *filp, ide_drive_t *drive)
{
}

static int default_check_media_change (ide_drive_t *drive)
{
	return 1;
}

static void default_pre_reset (ide_drive_t *drive)
{
}

static unsigned long default_capacity (ide_drive_t *drive)
{
	return 0x7fffffff;	/* cdrom or tape */
}

static void default_special (ide_drive_t *drive)
{
	special_t *s = &drive->special;

	s->all = 0;
	drive->mult_req = 0;
}

static void setup_driver_defaults (ide_drive_t *drive)
{
	ide_driver_t *d = drive->driver;

	if (d->cleanup == NULL)		d->cleanup = default_cleanup;
	if (d->do_request == NULL)	d->do_request = default_do_request;
	if (d->end_request == NULL)	d->end_request = default_end_request;
	if (d->ioctl == NULL)		d->ioctl = default_ioctl;
	if (d->open == NULL)		d->open = default_open;
	if (d->release == NULL)		d->release = default_release;
	if (d->media_change == NULL)	d->media_change = default_check_media_change;
	if (d->pre_reset == NULL)	d->pre_reset = default_pre_reset;
	if (d->capacity == NULL)	d->capacity = default_capacity;
	if (d->special == NULL)		d->special = default_special;
}

ide_drive_t *ide_scan_devices (byte media, ide_driver_t *driver, int n)
{
	unsigned int unit, index, i;
	ide_drive_t *drive;

	for (index = 0; index < MAX_HWIFS; ++index)
		if (ide_hwifs[index].present) goto search;
	ide_init_module(IDE_PROBE_MODULE);
search:
	for (index = 0, i = 0; index < MAX_HWIFS; ++index) {
		for (unit = 0; unit < MAX_DRIVES; ++unit) {
			drive = &ide_hwifs[index].drives[unit];
			if (drive->present && drive->media == media &&
			    drive->driver == driver && ++i > n)
				return drive;
		}
	}
	return NULL;
}

int ide_register_subdriver (ide_drive_t *drive, ide_driver_t *driver, int version)
{
	unsigned long flags;
	
	save_flags(flags);
	cli();
	if (version != IDE_SUBDRIVER_VERSION || !drive->present || drive->driver != NULL ||
	    drive->busy || drive->usage) {
	    	restore_flags(flags);
		return 1;
	}
	drive->driver = driver;
	setup_driver_defaults(drive);
	restore_flags(flags);
	if (drive->autotune != 2) {
		if (driver->supports_dma && HWIF(drive)->dmaproc != NULL)
			(void) (HWIF(drive)->dmaproc(ide_dma_check, drive));
		drive->dsc_overlap = (drive->next != drive && driver->supports_dsc_overlap);
		drive->nice1 = 1;
	}
	drive->revalidate = 1;
	return 0;
}

int ide_unregister_subdriver (ide_drive_t *drive)
{
	unsigned long flags;
	
	save_flags(flags);
	cli();
	if (drive->usage || drive->busy || drive->driver == NULL || DRIVER(drive)->busy) {
		restore_flags(flags);
		return 1;
	}
	drive->driver = NULL;
	restore_flags(flags);
	return 0;
}

int ide_register_module (ide_module_t *module)
{
	ide_module_t *p = ide_modules;

	while (p) {
		if (p == module)
			return 1;
		p = p->next;
	}
	module->next = ide_modules;
	ide_modules = module;
	revalidate_drives();
	return 0;
}

void ide_unregister_module (ide_module_t *module)
{
	ide_module_t **p;

	for (p = &ide_modules; (*p) && (*p) != module; p = &((*p)->next));
	if (*p)
		*p = (*p)->next;
}

struct file_operations ide_fops[] = {{
	NULL,			/* lseek - default */
	block_read,		/* read - general block-dev read */
	block_write,		/* write - general block-dev write */
	NULL,			/* readdir - bad */
	NULL,			/* select */
	ide_ioctl,		/* ioctl */
	NULL,			/* mmap */
	ide_open,		/* open */
	ide_release,		/* release */
	block_fsync,		/* fsync */
	NULL,			/* fasync */
	ide_check_media_change,	/* check_media_change */
	ide_revalidate_disk	/* revalidate */
}};

static struct symbol_table ide_syms = {
#include <linux/symtab_begin.h>
	X(ide_hwifs),
	X(ide_register_module),		X(ide_unregister_module),

	/*
	 * Probe module
	 */
	X(ide_timer_expiry),		X(ide_intr),
	X(ide_geninit),			X(ide_fops),
	X(ide_get_queue),
	X(do_ide0_request),
#if MAX_HWIFS > 1
	X(do_ide1_request),
#endif /* MAX_HWIFS > 1 */
#if MAX_HWIFS > 2
	X(do_ide2_request),
#endif /* MAX_HWIFS > 2 */
#if MAX_HWIFS > 3
	X(do_ide3_request),
#endif /* MAX_HWIFS > 3 */

	/*
	 * Driver module
	 */
	X(ide_scan_devices),		X(ide_register_subdriver),
	X(ide_unregister_subdriver),	X(ide_input_data),
	X(ide_output_data),		X(atapi_input_bytes),
	X(atapi_output_bytes),		X(ide_set_handler),
	X(ide_dump_status),		X(ide_error),
	X(ide_fixstring),		X(ide_wait_stat),
	X(ide_do_reset),		X(ide_init_drive_cmd),
	X(ide_do_drive_cmd),		X(ide_end_drive_cmd),
	X(ide_end_request),		X(ide_revalidate_disk),
	X(ide_cmd),			X(ide_stall_queue),

	X(ide_register),		X(ide_unregister),
#include <linux/symtab_end.h>
};

/*
 * This is gets invoked once during initialization, to set *everything* up
 */
int ide_init (void)
{
	init_ide_data ();

	initializing = 1;
	ide_init_builtin_drivers();
	initializing = 0;

	(void) register_symtab(&ide_syms);
	return 0;
}

#ifdef MODULE
char *options = NULL;

static void parse_options (char *line)
{
	char *next = line;

	if (line == NULL || !*line)
		return;
	while ((line = next) != NULL) {
 		if ((next = strchr(line,' ')) != NULL)
			*next++ = 0;
		if (!strncmp(line,"ide",3) || (!strncmp(line,"hd",2) && line[2] != '='))
			ide_setup(line);
	}
}

int init_module (void)
{
	parse_options(options);
	return ide_init();
}

void cleanup_module (void)
{
	int index;

	for (index = 0; index < MAX_HWIFS; ++index)
		ide_unregister(index);
}
#endif /* MODULE */
