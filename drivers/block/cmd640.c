/*
 *  linux/drivers/block/cmd640.c	Version 0.12  Jul 22, 1996
 *
 *  Copyright (C) 1995-1996  Linus Torvalds & authors (see below)
 */

/*
 *  Original author:	abramov@cecmow.enet.dec.com (Igor Abramov)
 *
 *  Maintained by:	s0033las@sun10.vsz.bme.hu (Laszlo Peter)
 *			mlord@pobox.com (Mark Lord)
 *
 *  This file provides support for the advanced features and bugs
 *  of IDE interfaces using the CMD Technologies 0640 IDE interface chip.
 *
 *  These chips are basically fucked by design, and getting this driver
 *  to work on every motherboard design that uses this screwed chip seems
 *  bloody well impossible.  However, we're still trying.
 *
 *  We think version 0.12 should work for most folks.
 *  User feedback is essential.
 *
 *
 *  Version 0.01	Initial version, hacked out of ide.c,
 *			and #include'd rather than compiled separately.
 *			This will get cleaned up in a subsequent release.
 *
 *  Version 0.02	Fixes for vlb initialization code, enable
 *			read-ahead for versions 'B' and 'C' of chip by
 *			default, some code cleanup.
 *
 *  Version 0.03	Added reset of secondary interface,
 *			and black list for devices which are not compatible
 *			with read ahead mode. Separate function for setting
 *			readahead is added, possibly it will be called some
 *			day from ioctl processing code.
 *
 *  Version 0.04	Now configs/compiles separate from ide.c  -ml
 *
 *  Version 0.05	Major rewrite of interface timing code.
 *			Added new function cmd640_set_mode to set PIO mode
 *			from ioctl call. New drives added to black list.
 *
 *  Version 0.06	More code cleanup. Readahead is enabled only for
 *			detected hard drives, not included in readahead
 *			black list.
 *
 *  Version 0.07	Changed to more conservative drive tuning policy.
 *			Unknown drives, which report PIO < 4 are set to
 *			(reported_PIO - 1) if it is supported, or to PIO0.
 *			List of known drives extended by info provided by
 *			CMD at their ftp site.
 *
 *  Version 0.08	Added autotune/noautotune support.  -ml
 *
 *  Version 0.09	Try to be smarter about 2nd port enabling.  -ml
 *  Version 0.10	Be nice and don't reset 2nd port.  -ml
 *  Version 0.11	Try to handle more wierd situations.  -ml
 *
 *  Version 0.12	Lots of bug fixes from Laszlo Peter
 *			irq unmasking disabled for reliability.  -lp
 *			try to be even smarter about the second port.  -lp
 *			tidy up source code formatting.  -ml
 */

#undef REALLY_SLOW_IO		/* most systems can safely undef this */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <asm/io.h>
#include "ide.h"
#include "ide_modes.h"

#define PARANOID_ABOUT_CMD640	1	/* used to tag superstitious code */

int cmd640_vlb = 0;

/*
 * CMD640 specific registers definition.
 */

#define VID		0x00
#define DID		0x02
#define PCMD		0x04
#define PSTTS		0x06
#define REVID		0x08
#define PROGIF		0x09
#define SUBCL		0x0a
#define BASCL		0x0b
#define BaseA0		0x10
#define BaseA1		0x14
#define BaseA2		0x18
#define BaseA3		0x1c
#define INTLINE		0x3c
#define INPINE		0x3d

#define	CFR		0x50
#define   CFR_DEVREV		0x03
#define   CFR_IDE01INTR		0x04
#define	  CFR_DEVID		0x18
#define	  CFR_AT_VESA_078h	0x20
#define	  CFR_DSA1		0x40
#define	  CFR_DSA0		0x80

#define CNTRL		0x51
#define	  CNTRL_DIS_RA0		0x40
#define   CNTRL_DIS_RA1		0x80
#define	  CNTRL_ENA_2ND		0x08

#define	CMDTIM		0x52
#define	ARTTIM0		0x53
#define	DRWTIM0		0x54
#define ARTTIM1 	0x55
#define DRWTIM1		0x56
#define ARTTIM23	0x57
#define   DIS_RA2		0x04
#define   DIS_RA3		0x08
#define DRWTIM23	0x58
#define BRST		0x59

static ide_tuneproc_t cmd640_tune_drive;

/* Interface to access cmd640x registers */
void (*put_cmd640_reg)(int reg_no, int val);
byte (*get_cmd640_reg)(int reg_no);

enum { none, vlb, pci1, pci2 };
static int	bus_type = none;
static int	cmd640_chip_version;
static int	cmd640_key;
static int 	bus_speed; /* MHz */

/*
 * The CMD640x chip does not support DWORD config write cycles, but some
 * of the BIOSes use them to implement the config services.
 * We use direct IO instead.
 */

/* PCI method 1 access */

static void put_cmd640_reg_pci1(int reg_no, int val)
{
	unsigned long flags;

	save_flags(flags);
	cli();
	outl_p((reg_no & 0xfc) | cmd640_key, 0xcf8);
	outb_p(val, (reg_no & 3) + 0xcfc);
	restore_flags(flags);
}

static byte get_cmd640_reg_pci1(int reg_no)
{
	byte b;
	unsigned long flags;

	save_flags(flags);
	cli();
	outl_p((reg_no & 0xfc) | cmd640_key, 0xcf8);
	b = inb_p(0xcfc + (reg_no & 3));
	restore_flags(flags);
	return b;
}

/* PCI method 2 access (from CMD datasheet) */

static void put_cmd640_reg_pci2(int reg_no, int val)
{
	unsigned long flags;

	save_flags(flags);
	cli();
	outb_p(0x10, 0xcf8);
	outb_p(val, cmd640_key + reg_no);
	outb_p(0, 0xcf8);
	restore_flags(flags);
}

static byte get_cmd640_reg_pci2(int reg_no)
{
	byte b;
	unsigned long flags;

	save_flags(flags);
	cli();
	outb_p(0x10, 0xcf8);
	b = inb_p(cmd640_key + reg_no);
	outb_p(0, 0xcf8);
	restore_flags(flags);
	return b;
}

/* VLB access */

static void put_cmd640_reg_vlb(int reg_no, int val)
{
	unsigned long flags;

	save_flags(flags);
	cli();
	outb_p(reg_no, cmd640_key + 8);
	outb_p(val, cmd640_key + 0xc);
	restore_flags(flags);
}

static byte get_cmd640_reg_vlb(int reg_no)
{
	byte b;
	unsigned long flags;

	save_flags(flags);
	cli();
	outb_p(reg_no, cmd640_key + 8);
	b = inb_p(cmd640_key + 0xc);
	restore_flags(flags);
	return b;
}

/*
 * Probe for CMD640x -- pci method 1
 */

static int probe_for_cmd640_pci1(void)
{
	long id;
	int	k;

	for (k = 0x80000000; k <= 0x8000f800; k += 0x800) {
		outl(k, 0xcf8);
		id = inl(0xcfc);
		if (id != 0x06401095)
			continue;
		put_cmd640_reg = put_cmd640_reg_pci1;
		get_cmd640_reg = get_cmd640_reg_pci1;
		cmd640_key = k;
		return 1;
	}
	return 0;
}

/*
 * Probe for CMD640x -- pci method 2
 */

static int probe_for_cmd640_pci2(void)
{
	int i;
	int v_id;
	int d_id;

	for (i = 0xc000; i <= 0xcf00; i += 0x100) {
		outb(0x10, 0xcf8);
		v_id = inw(i);
		d_id = inw(i + 2);
		outb(0, 0xcf8);
		if (v_id != 0x1095 || d_id != 0x640)
			continue;
		put_cmd640_reg = put_cmd640_reg_pci2;
		get_cmd640_reg = get_cmd640_reg_pci2;
		cmd640_key = i;
		return 1;
	}
	return 0;
}

/*
 * Probe for CMD640x -- vlb
 */

static int probe_for_cmd640_vlb(void) {
	byte b;

	outb(CFR, 0x178);
	b = inb(0x17c);
	if (b == 0xff || b == 0 || (b & CFR_AT_VESA_078h)) {
		outb(CFR, 0x78);
		b = inb(0x7c);
		if (b == 0xff || b == 0 || !(b & CFR_AT_VESA_078h))
			return 0;
		cmd640_key = 0x70;
	} else {
		cmd640_key = 0x170;
	}
	put_cmd640_reg = put_cmd640_reg_vlb;
	get_cmd640_reg = get_cmd640_reg_vlb;
	return 1;
}

#if 0
/*
 * Low level reset for controller, actually it has nothing specific for
 * CMD640, but I don't know how to use standard reset routine before
 * we recognized any drives.
 */
static void cmd640_reset_controller(int iface_no)
{
	int retry_count = 600;
	int base_port = iface_no ? 0x170 : 0x1f0;

	outb_p(4, base_port + 7);
	udelay(5);
	outb_p(0, base_port + 7);

	do {
		udelay(5);
		retry_count -= 1;
	} while ((inb_p(base_port + 7) & 0x80) && retry_count);

	if (retry_count == 0)
		printk("cmd640: failed to reset controller %d\n", iface_no);
}
#endif /* 0 */

/*
 *  Returns 1 if an IDE interface/drive exists at 0x170,
 *  Returns 0 otherwise.
 */
static int secondary_port_responding (void)
{
	/*
	 * Test for hardware at 0x170 (secondary IDE port).
	 */
	outb_p(0xa0, 0x170 + IDE_SELECT_OFFSET);	/* select drive0 */
	udelay(100);
	if (inb_p(0x170 + IDE_SELECT_OFFSET) != 0xa0) {
		outb_p(0xb0, 0x170 + IDE_SELECT_OFFSET); /* select drive1 */
		udelay(100);
		if (inb_p(0x170 + IDE_SELECT_OFFSET) != 0xb0)
			return 0;	/* nothing is there */
	}
	return 1;			/* something is there */
}

/*
 * Probe for Cmd640x and initialize it if found
 */

int ide_probe_for_cmd640x(void)
{
	int second_port_toggled = 0;
	int second_port_cmd640 = 0;
	byte b;

	if (probe_for_cmd640_pci1()) {
		bus_type = pci1;
	} else if (probe_for_cmd640_pci2()) {
		bus_type = pci2;
	} else if (cmd640_vlb && probe_for_cmd640_vlb()) {
		/* May be remove cmd640_vlb at all, and probe in any case */
		bus_type = vlb;
	} else {
		return 0;
	}

#ifdef PARANOID_ABOUT_CMD640
	ide_hwifs[0].serialized = 1;	/* ensure this *always* gets set */
	ide_hwifs[1].serialized = 1;	/* ensure this *always* gets set */
#endif

#if 0
	/* Dump initial state of chip registers */
	for (b = 0; b != 0xff; b++) {
		printk(" %2x%c", get_cmd640_reg(b),
				((b&0xf) == 0xf) ? '\n' : ',');
	}
#endif

	/*
	 * Undocumented magic. (There is no 0x5b port in specs)
	 */

	put_cmd640_reg(0x5b, 0xbd);
	if (get_cmd640_reg(0x5b) != 0xbd) {
		printk("ide: can't initialize cmd640 -- wrong value in 0x5b\n");
		return 0;
	}
	put_cmd640_reg(0x5b, 0);

	/*
	 * Documented magic.
	 */

	cmd640_chip_version = get_cmd640_reg(CFR) & CFR_DEVREV;
	if (cmd640_chip_version == 0) {
		printk ("ide: wrong CMD640 version -- 0\n");
		return 0;
	}

	/*
	 * Setup the most conservative timings for all drives,
	 */

	put_cmd640_reg(CMDTIM, 0);

	put_cmd640_reg(ARTTIM0, 0xc0);
	put_cmd640_reg(ARTTIM1, 0xc0);
	put_cmd640_reg(ARTTIM23, 0xcc); /* disable read-ahead for drives 2&3 */

	put_cmd640_reg(DRWTIM0, 0);
	put_cmd640_reg(DRWTIM1, 0);
	put_cmd640_reg(DRWTIM23, 0);

	/*
	 * Set the maximum allowed bus speed (it is safest until we
	 * 				      find how to detect bus speed)
	 * Normally PCI bus runs at 33MHz, but often works overclocked to 40
	 */
	bus_speed = (bus_type == vlb) ? 50 : 40;

	/*
	 * Setup Control Register
	 */
	b = get_cmd640_reg(CNTRL);

	/*
	 * Disable readahead for drives at primary interface
	 */
	b |= (CNTRL_DIS_RA0 | CNTRL_DIS_RA1);
	put_cmd640_reg(CNTRL, b);

	if (!ide_hwifs[1].noprobe) {

		if (secondary_port_responding()) {

			if ((b & CNTRL_ENA_2ND) || (bus_type == vlb))
				second_port_cmd640 = 1;

		} else if (!(b & CNTRL_ENA_2ND) && (bus_type != vlb)) {

			second_port_toggled = 1;
			put_cmd640_reg(CNTRL, b | CNTRL_ENA_2ND); /* Enable second interface */

			if (secondary_port_responding())
				second_port_cmd640 = 1;
			else {
				second_port_toggled = 0;
				put_cmd640_reg(CNTRL, b); /* Disable second interface */
			}
		}
	}

	/*
	 * Note that we assume that the first interface is at 0x1f0,
	 * and that the second interface, if enabled, is at 0x170.
	 */
	ide_hwifs[0].chipset = ide_cmd640;
	ide_hwifs[0].tuneproc = &cmd640_tune_drive;
	ide_hwifs[0].no_unmask = 1;

	if (ide_hwifs[0].drives[0].autotune == 0)
		ide_hwifs[0].drives[0].autotune = 1;
	if (ide_hwifs[0].drives[1].autotune == 0)
		ide_hwifs[0].drives[1].autotune = 1;

	/*
	 * Initialize 2nd IDE port, if required
	 */
	if (second_port_cmd640) {

#ifndef PARANOID_ABOUT_CMD640
		ide_hwifs[0].serialized = 1;
		ide_hwifs[1].serialized = 1;
#endif

		ide_hwifs[1].chipset = ide_cmd640;
		ide_hwifs[1].tuneproc = &cmd640_tune_drive;
		ide_hwifs[1].no_unmask = 1;

		if (ide_hwifs[1].drives[0].autotune == 0)
			ide_hwifs[1].drives[0].autotune = 1;
		if (ide_hwifs[1].drives[1].autotune == 0)
			ide_hwifs[1].drives[1].autotune = 1;

#if 0
		/* reset the secondary interface */
		cmd640_reset_controller(1);
#endif
	}

	printk("ide: buggy CMD640%c interface on ",
	       'A' - 1 + cmd640_chip_version);
	switch (bus_type) {
		case vlb :
			printk("vlb (0x%x)", cmd640_key);
			break;
		case pci1:
			printk("pci (0x%x)", cmd640_key);
			break;
		case pci2:
			printk("pci (access method 2) (0x%x)", cmd640_key);
			break;
	}

	/*
	 * Tell everyone what we did to their system
	 */
	printk(":%s serialized, second port %s\n",
		second_port_cmd640 ? "" : " not",
		second_port_toggled ? "toggled" : "untouched");
	return 1;
}

#if 0  /* not used anywhere */
int cmd640_off(void) {
	static int a = 0;
	byte b;

	if (bus_type == none || a == 1)
		return 0;
	a = 1;
	b = get_cmd640_reg(CNTRL);
	b &= ~CNTRL_ENA_2ND;
	put_cmd640_reg(CNTRL, b);
	return 1;
}
#endif /* 0 */

/*
 * Sets readahead mode for specific drive
 *  in the future it could be called from ioctl
 */

static void set_readahead_mode(ide_drive_t* drive, int mode)
{
	static int masks[2][2] =
		{
			{CNTRL_DIS_RA0, CNTRL_DIS_RA1},
			{DIS_RA2, 	DIS_RA3}
		};
	byte b;

	int interface_number = HWIF(drive)->index;
	int drive_number = drive->select.b.unit;

	int port = (interface_number == 0) ? CNTRL : ARTTIM23;
	int mask = masks[interface_number][drive_number];

	b = get_cmd640_reg(port);

	/*
	 * I don't know why it is necessary, but without this my machine
	 * locks up, if bus_speed is not correct. And it even allows me
	 * to use 32 bit transfers on the primary port (hdparm -c1).
	 */
	if ((interface_number == 0) && mode)
		b|=0x27;

	if (mode)
		b &= ~mask;	/* Enable readahead for specific drive */
	else
		b |= mask;	/* Disable readahead for specific drive */

	put_cmd640_reg(port, b);
}

static const struct readahead_black_list {
	const char* 	name;
	int		mode;
} drives_ra[] = {
	{ "QUANTUM LIGHTNING 540A", 0 },
	{ "ST3655A",	0 },
	{ "SAMSUNG",	0 },	/* Be conservative */
	{ NULL,		0 }
};

static int strmatch(const char* pattern, const char* name) {
	char c1, c2;

	while (1) {
		c1 = *pattern++;
		c2 = *name++;
		if (c1 == 0) {
			return 0;
		}
		if (c1 != c2)
			return 1;
	}
}

static int known_drive_readahead(char* name) {
	int i;

	for (i = 0; drives_ra[i].name != NULL; i++) {
		if (strmatch(drives_ra[i].name, name) == 0) {
			return drives_ra[i].mode;
		}
	}
	return -1;
}

static int arttim[4]  = {2, 2, 2, 2};	/* Address setup count (in clocks) */
static int a_count[4] = {1, 1, 1, 1};	/* Active count   (encoded) */
static int r_count[4] = {1, 1, 1, 1};	/* Recovery count (encoded) */

/*
 * Convert address setup count from number of clocks
 * to representation used by controller
 */

inline static int pack_arttim(int clocks)
{
	if (clocks <= 2) return 0x40;
	else if (clocks == 3) return 0x80;
	else if (clocks == 4) return 0x00;
	else return 0xc0;
}

/*
 * Pack active and recovery counts into single byte representation
 * used by controller
 */

inline static int pack_counts(int act_count, int rec_count)
{
	return ((act_count & 0x0f)<<4) | (rec_count & 0x0f);
}

inline int max(int a, int b) { return a > b ? a : b; }
inline int max4(int *p) { return max(p[0], max(p[1], max(p[2], p[3]))); }

/*
 * Set timing parameters
 */

static void cmd640_set_timing(int if_num, int dr_num)
{
	int	b_reg;
	int	ac, rc, at;
	byte b;

	/*
	 * Set address setup count and drive read/write timing registers.
	 * Primary interface has individual count/timing registers for
	 * each drive. Secondary interface has common set of registers, and
	 * we should set timings for the slowest drive.
	 */

	if (if_num == 0) {
		b_reg = dr_num ? ARTTIM1 : ARTTIM0;
		at = arttim[dr_num];
		ac = a_count[dr_num];
		rc = r_count[dr_num];
		b = pack_arttim(at);
	} else {
		b_reg = ARTTIM23;
		at = max(arttim[2], arttim[3]);
		ac = max(a_count[2], a_count[3]);
		rc = max(r_count[2], r_count[3]);

		/*
		 * Protect the readahead bits
		 */
		b = pack_arttim(at) | (get_cmd640_reg(ARTTIM23) & (DIS_RA2 | DIS_RA3));
	}

	put_cmd640_reg(b_reg, b);
	put_cmd640_reg(b_reg + 1, pack_counts(ac, rc));

	/*
	 * Update CMDTIM (IDE Command Block Timing Register)
	 */

	ac = max4(a_count);
	rc = max4(r_count);
	put_cmd640_reg(CMDTIM, pack_counts(ac, rc));
}

/*
 * Standard timings for PIO modes
 */

static const struct pio_timing {
	int	mc_time;	/* Address setup (ns) min */
	int	av_time;	/* Active pulse (ns) min */
	int	ds_time;	/* Cycle time (ns) min = Active pulse + Recovery pulse */
} pio_timings[6] = {
	{ 70,	165,	600 },	/* PIO Mode 0 */
	{ 50,	125,	383 },	/* PIO Mode 1 */
	{ 30,	100,	240 },	/* PIO Mode 2 */
	{ 30,	80,	180 },	/* PIO Mode 3 w/IORDY */
	{ 25,	70,	125 },	/* PIO Mode 4 w/IORDY -- should be 120, not 125 */
	{ 20,	50,	100 }	/* PIO Mode 5 w/IORDY (nonstandard) */
};

static void cmd640_timings_to_clocks(int mc_time, int av_time, int ds_time,
				int clock_time, int drv_idx)
{
	int a, b;

	arttim[drv_idx] = (mc_time + clock_time - 1)/clock_time;

	a = (av_time + clock_time - 1)/clock_time;
	if (a < 2)
		a = 2;
	b = (ds_time + clock_time - 1)/clock_time - a;
	if (b < 2)
		b = 2;
	if (b > 0x11) {
		a += b - 0x11;
		b = 0x11;
	}
	if (a > 0x10)
		a = 0x10;
	if (cmd640_chip_version > 1)
		b -= 1;
	if (b > 0x10)
		b = 0x10;

	a_count[drv_idx] = a;
	r_count[drv_idx] = b;
}

static void set_pio_mode(int if_num, int drv_num, int mode_num) {
	int p_base;
	int i;

	p_base = if_num ? 0x170 : 0x1f0;

	outb_p(3, p_base + IDE_FEATURE_OFFSET);
	outb_p(mode_num | 0x08, p_base + IDE_NSECTOR_OFFSET);
	outb_p((drv_num | 0x0a) << 4, p_base + IDE_SELECT_OFFSET);
	outb_p(WIN_SETFEATURES, p_base + IDE_COMMAND_OFFSET);

	for (i = 0; (i < 100) && (inb(p_base + IDE_STATUS_OFFSET) & BUSY_STAT); i++)
		udelay(10000);
}

/*
 * Set a specific pio_mode for a drive
 */

static void cmd640_set_mode(ide_drive_t* drive, byte pio_mode, int ds_time) {
	int interface_number;
	int drive_number;
	int clock_time; /* ns */
	int mc_time, av_time;

	if (pio_mode > 5) return;

	interface_number = HWIF(drive)->index;
	drive_number = drive->select.b.unit;
	clock_time = 1000/bus_speed;

	mc_time = pio_timings[pio_mode].mc_time;
	av_time = pio_timings[pio_mode].av_time;
	ds_time = (ds_time != 0) ? ds_time : pio_timings[pio_mode].ds_time;

	cmd640_timings_to_clocks(mc_time, av_time, ds_time, clock_time,
				interface_number*2 + drive_number);
	set_pio_mode(interface_number, drive_number, pio_mode);
	cmd640_set_timing(interface_number, drive_number);
}

/*
 * Drive PIO mode "autoconfiguration".
 */

static void cmd640_tune_drive(ide_drive_t *drive, byte pio_mode) {
	int max_pio;
	int ds_time;
	int readahead;	/* there is a global named read_ahead */
	int overridden;
	int iordy;
	struct hd_driveid* id;

	if (pio_mode != 255) {
		cmd640_set_mode(drive, pio_mode, 0);
		return;
	}

	overridden = 0;
	iordy = 0;
	id = drive->id;

	if ((max_pio = ide_scan_pio_blacklist(id->model)) != -1) {
		ds_time = pio_timings[max_pio].ds_time;
		overridden = 1;
		iordy = (max_pio > 2);
	} else {
		max_pio = id->tPIO;
		ds_time = pio_timings[max_pio].ds_time;
		if (id->field_valid & 2) {
			if ((id->capability & 8) && (id->eide_pio_modes & 7)) {
				if (id->eide_pio_modes & 4) max_pio = 5;
				else if (id->eide_pio_modes & 2) max_pio = 4;
				else max_pio = 3;
				ds_time = id->eide_pio_iordy;
				iordy = 1;
			} else {
				ds_time = id->eide_pio;
			}
			if (ds_time == 0) {
				ds_time = pio_timings[max_pio].ds_time;
				iordy = (max_pio > 2);
			}
		}

		/*
		 * Conservative "downgrade"
		 */
		if (max_pio < 4 && max_pio != 0) {
			max_pio--;
			overridden = 1;
			iordy = (max_pio > 2);
			ds_time = pio_timings[max_pio].ds_time;
		}
	}

	cmd640_set_mode(drive, max_pio, ds_time);

	/*
	 * Disable (or set) readahead mode
	 */

	readahead = 0;
	if (cmd640_chip_version > 1) {	/* Mmmm.. probably should be > 2 ?? */
		readahead = known_drive_readahead(id->model);
		if (readahead == -1)
	        	readahead = 1;	/* Mmmm.. probably be 0 ?? */
		set_readahead_mode(drive, readahead);
	}

	printk ("Drive Timing: PIO Mode %d (%dns) %s/IORDY%s, Read-ahead: %s\n",
		max_pio,
		ds_time,
		iordy ? "w" : "wo",
		overridden ? " (overriding vendor mode)" : "",
		readahead ? "enabled" : "disabled");
}
