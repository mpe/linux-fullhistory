/*
 *  linux/drivers/block/cmd640.c	Version 0.02  Nov 30, 1995
 *
 *  Copyright (C) 1995  Linus Torvalds & author (see below)
 */

/*
 *  Principal Author/Maintainer:  abramov@cecmow.enet.dec.com (Igor)
 *
 *  This file provides support for the advanced features and bugs
 *  of IDE interfaces using the CMD Technologies 0640 IDE interface chip.
 *
 *  Version 0.01	Initial version, hacked out of ide.c,
 *			and #include'd rather than compiled separately.
 *			This will get cleaned up in a subsequent release.
 *
 *  Version 0.02	Fixes for vlb initialization code, enable
 *			read-ahead for versions 'B' and 'C' of chip by
 *			default, some code cleanup.
 *
 */

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

/* Interface to access cmd640x registers */
static void (*put_cmd640_reg)(int key, int reg_no, int val);
static byte (*get_cmd640_reg)(int key, int reg_no);

enum { none, vlb, pci1, pci2 };
static int	bus_type = none;
static int	cmd640_chip_version;
static int	cmd640_key;
static byte	is_cmd640[MAX_HWIFS];
static int 	bus_speed; /* MHz */

/*
 * For some unknown reasons pcibios functions which read and write registers
 * do not work with cmd640. We use direct io instead.
 */

/* PCI method 1 access */

static void put_cmd640_reg_pci1(int key, int reg_no, int val)
{
	unsigned long flags;

	save_flags(flags);
	cli();
	outl_p((reg_no & 0xfc) | key, 0xcf8);
	outb_p(val, (reg_no & 3) + 0xcfc);
	restore_flags(flags);
}

static byte get_cmd640_reg_pci1(int key, int reg_no)
{
	byte b;
	unsigned long flags;

	save_flags(flags);
	cli();
	outl_p((reg_no & 0xfc) | key, 0xcf8);
	b = inb_p(0xcfc + (reg_no & 3));
	restore_flags(flags);
	return b;
}

/* PCI method 2 access (from CMD datasheet) */

static void put_cmd640_reg_pci2(int key, int reg_no, int val)
{
	unsigned long flags;

	save_flags(flags);
	cli();
	outb_p(0x10, 0xcf8);
	outb_p(val, key + reg_no);
	outb_p(0, 0xcf8);
	restore_flags(flags);
}

static byte get_cmd640_reg_pci2(int key, int reg_no)
{
	byte b;
	unsigned long flags;

	save_flags(flags);
	cli();
	outb_p(0x10, 0xcf8);
	b = inb_p(key + reg_no);
	outb_p(0, 0xcf8);
	restore_flags(flags);
	return b;
}

/* VLB access */

static void put_cmd640_reg_vlb(int key, int reg_no, int val)
{
	unsigned long flags;

	save_flags(flags);
	cli();
	outb_p(reg_no, key + 8);
	outb_p(val, key + 0xc);
	restore_flags(flags);
}

static byte get_cmd640_reg_vlb(int key, int reg_no)
{
	byte b;
	unsigned long flags;

	save_flags(flags);
	cli();
	outb_p(reg_no, key + 8);
	b = inb_p(key + 0xc);
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

/*
 * Probe for Cmd640x and initialize it if found
 */

int ide_probe_for_cmd640x(void)
{
	int  i;
	int  second_port;
	int  read_ahead;
	byte b;

	for (i = 0; i < MAX_HWIFS; i++)
		is_cmd640[i] = 0;

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

	/*
	 * Undocumented magic. (There is no 0x5b port in specs)
	 */

	put_cmd640_reg(cmd640_key, 0x5b, 0xbd);
	if (get_cmd640_reg(cmd640_key, 0x5b) != 0xbd) {
		printk("ide: can't initialize cmd640 -- wrong value in 0x5b\n");
		return 0;
	}
	put_cmd640_reg(cmd640_key, 0x5b, 0);

	/*
	 * Documented magic.
	 */

	cmd640_chip_version = get_cmd640_reg(cmd640_key, CFR) & CFR_DEVREV;
	if (cmd640_chip_version == 0) {
		printk ("ide: wrong CMD640 version -- 0\n");
		return 0;
	}

	/*
	 * Do not initialize secondary controller for vlbus
	 */
	second_port = (bus_type != vlb);

	/*
	 * Set the maximum allowed bus speed (it is safest until we
	 * 				      find how detect bus speed)
	 * Normally PCI bus runs at 33MHz, but often works overclocked to 40
	 */
	bus_speed = (bus_type == vlb) ? 50 : 40; 

#if 0	/* don't know if this is reliable yet */
	/*
	 * Enable readahead for versions above 'A'
	 */
	read_ahead = (cmd640_chip_version > 1);
#else
	read_ahead = 0;
#endif
	/*
	 * Setup Control Register
	 */
	b = get_cmd640_reg(cmd640_key, CNTRL);	
	if (second_port)
		b |= CNTRL_ENA_2ND;
	else
		b &= ~CNTRL_ENA_2ND;
	if (read_ahead)
		b &= ~(CNTRL_DIS_RA0 | CNTRL_DIS_RA1);
	else
		b |= (CNTRL_DIS_RA0 | CNTRL_DIS_RA1);
	put_cmd640_reg(cmd640_key, CNTRL, b);

	/*
	 * Initialize 2nd IDE port, if required
	 */
	if (second_port) {
		/* We reset timings, and setup read-ahead */
		b = read_ahead ? 0 : (DIS_RA2 | DIS_RA3);
		put_cmd640_reg(cmd640_key, ARTTIM23, b);
		put_cmd640_reg(cmd640_key, DRWTIM23, 0);
	}

	serialized = 1;

	printk("ide: buggy CMD640%c interface at ", 
	       'A' - 1 + cmd640_chip_version);
	switch (bus_type) {
		case vlb :
			printk("local bus, port 0x%x", cmd640_key);
			break;
		case pci1:
			printk("pci, (0x%x)", cmd640_key);
			break;
		case pci2:
			printk("pci,(access method 2) (0x%x)", cmd640_key);
			break;
	}

	is_cmd640[0] = is_cmd640[1] = 1;

	/*
	 * Reset interface timings
	 */
	put_cmd640_reg(cmd640_key, CMDTIM, 0);

	printk("\n ... serialized, %s read-ahead, secondary interface %s\n",
	       read_ahead ? "enabled" : "disabled",
	       second_port ? "enabled" : "disabled");

	return 1;
}

static int as_clocks(int a) {
	switch (a & 0xc0) {
		case 0 :	return 4;
		case 0x40 :	return 2;
		case 0x80 :	return 3;
		case 0xc0 :	return 5;
		default :	return -1;
	}
}

/*
 * Tuning of drive parameters
 */

static void cmd640_set_timing(int if_num, int dr_num, int r1, int r2) {
	int  b_reg;
	byte b;
	int  r52;
	static int a = 0;

	b_reg = if_num ? ARTTIM23 : dr_num ? ARTTIM1 : ARTTIM0;

	if (if_num == 0) {
		put_cmd640_reg(cmd640_key, b_reg, r1);
		put_cmd640_reg(cmd640_key, b_reg + 1, r2);
	} else {
		b = get_cmd640_reg(cmd640_key, b_reg);
		if (a == 0 || as_clocks(b) < as_clocks(r1))
			put_cmd640_reg(cmd640_key, b_reg, (b & 0xc0) | r1);
		
		if (a == 0) {
			put_cmd640_reg(cmd640_key, b_reg + 1, r2);
		} else {
			b = get_cmd640_reg(cmd640_key, b_reg + 1);
			r52 =  (b&0x0f) < (r2&0x0f) ? (r2&0x0f) : (b&0x0f);
			r52 |= (b&0xf0) < (r2&0xf0) ? (r2&0xf0) : (b&0xf0);
			put_cmd640_reg(cmd640_key, b_reg+1, r52);
		}
		a = 1;
	}

	b = get_cmd640_reg(cmd640_key, CMDTIM);
	if (b == 0) {
		put_cmd640_reg(cmd640_key, CMDTIM, r2);
	} else {
		r52  = (b&0x0f) < (r2&0x0f) ? (r2&0x0f) : (b&0x0f);
		r52 |= (b&0xf0) < (r2&0xf0) ? (r2&0xf0) : (b&0xf0);
		put_cmd640_reg(cmd640_key, CMDTIM, r52);
	}
}

struct pio_timing {
	int	mc_time;	/* Minimal cycle time (ns) */
	int	av_time;	/* Address valid to DIOR-/DIOW- setup (ns) */
	int	ds_time;	/* DIOR data setup	(ns) */
} pio_timings[6] = {
	{ 70,	165,	600 },	/* PIO Mode 0 */
	{ 50,	125,	383 },	/* PIO Mode 1 */
	{ 30,	100,	240 },	/* PIO Mode 2 */
	{ 30,	80,	180 },	/* PIO Mode 3 */
	{ 25,	70,	125 },	/* PIO Mode 4 */
	{ 20,	50,	100 }	/* PIO Mode ? */
};

struct drive_pio_info {
	const char	*name;
	int		pio;
} drive_pios[] = {
	{ "Maxtor 7131 AT", 1 },
	{ "Maxtor 7171 AT", 1 },
	{ "Maxtor 7213 AT", 1 },
	{ "Maxtor 7245 AT", 1 },
	{ "SAMSUNG SHD-3122A", 1 },
	{ "QUANTUM ELS127A", 0 },
	{ "QUANTUM LPS240A", 0 },
	{ "QUANTUM LPS270A", 3 },
	{ "QUANTUM LPS540A", 3 },
	{ NULL,	0 }
};

static int known_drive_pio(char* name) {
	struct drive_pio_info* pi;

	for (pi = drive_pios; pi->name != NULL; pi++) {
		if (strcmp(pi->name, name) == 0)
			return pi->pio;
	}
	return -1;
}

static void cmd640_timings_to_regvals(int mc_time, int av_time, int ds_time,
				int clock_time,
				int* r1, int* r2)
{
	int a, b;

	a = (mc_time + clock_time - 1)/clock_time;
	if (a <= 2) *r1 = 0x40;
	else if (a == 3) *r1 = 0x80;
	else if (a == 4) *r1 = 0;
	else *r1 = 0xc0;

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
	if (a > 0xf)
		a = 0;
	if (cmd640_chip_version > 1)
		b -= 1;
	if (b > 0xf)
		b = 0;
	*r2 = (a << 4) | b;
}

static void set_pio_mode(int if_num, int drv_num, int mode_num) {
	int p_base;
	int i;

	p_base = if_num ? 0x170 : 0x1f0;
	outb_p(3, p_base + 1);
	outb_p(mode_num | 8, p_base + 2);
	outb_p((drv_num | 0xa) << 4, p_base + 6);
	outb_p(0xef, p_base + 7);
	for (i = 0; (i < 100) && (inb (p_base + 7) & 0x80); i++)
		delay_10ms();
}

void cmd640_tune_drive(ide_drive_t* drive) {
	int interface_number;
	int drive_number;
	int clock_time; /* ns */
	int max_pio;
	int mc_time, av_time, ds_time;
	struct hd_driveid* id;
	int r1, r2;

	/*
	 * Determine if drive is under cmd640 control
	 */
	interface_number = HWIF(drive) - ide_hwifs;
	if (!is_cmd640[interface_number])
		return;

	drive_number = drive - HWIF(drive)->drives;
	clock_time = 1000/bus_speed;
	id = drive->id;
	if ((max_pio = known_drive_pio(id->model)) != -1) {
		mc_time = pio_timings[max_pio].mc_time;
		av_time = pio_timings[max_pio].av_time;
		ds_time = pio_timings[max_pio].ds_time;
	} else {
		max_pio = id->tPIO;
		mc_time = pio_timings[max_pio].mc_time;
		av_time = pio_timings[max_pio].av_time;
		ds_time = pio_timings[max_pio].ds_time;
		if (id->field_valid & 2) {
			if ((id->capability & 8) && (id->eide_pio_modes & 7)) {
				if (id->eide_pio_modes & 4) max_pio = 5;
				else if (id->eide_pio_modes & 2) max_pio = 4;
				else max_pio = 3;
				ds_time = id->eide_pio_iordy;
				mc_time = pio_timings[max_pio].mc_time;
				av_time = pio_timings[max_pio].av_time;
			} else {
				ds_time = id->eide_pio;
			}
			if (ds_time == 0)
				ds_time = pio_timings[max_pio].ds_time;
		}
	}
	cmd640_timings_to_regvals(mc_time, av_time, ds_time, clock_time,
				&r1, &r2);
	set_pio_mode(interface_number, drive_number, max_pio);
	cmd640_set_timing(interface_number, drive_number, r1, r2);
	printk ("Mode and Timing set to PIO%d (0x%x 0x%x)\n", max_pio, r1, r2);
}

