/*
 * linux/drivers/ide/ide-pmac.c
 *
 * Support for IDE interfaces on PowerMacs.
 * These IDE interfaces are memory-mapped and have a DBDMA channel
 * for doing DMA.
 *
 *  Copyright (C) 1998-2001 Paul Mackerras & Ben. Herrenschmidt
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 * Some code taken from drivers/ide/ide-dma.c:
 *
 *  Copyright (c) 1995-1998  Mark Lord
 *
 */
#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/pci.h>

#include <asm/prom.h>
#include <asm/io.h>
#include <asm/dbdma.h>
#include <asm/ide.h>
#include <asm/mediabay.h>
#include <asm/pci-bridge.h>
#include <asm/machdep.h>
#include <asm/pmac_feature.h>
#include <asm/sections.h>
#include <asm/irq.h>
#ifdef CONFIG_PMAC_PBOOK
#include <linux/adb.h>
#include <linux/pmu.h>
#endif
#include "ide_modes.h"

extern void ide_do_request(ide_hwgroup_t *hwgroup, int masked_irq);

#define IDE_PMAC_DEBUG

#define DMA_WAIT_TIMEOUT	500

typedef struct pmac_ide_hwif {
	unsigned long			regbase;
	int				irq;
	int				kind;
	int				aapl_bus_id;
	struct device_node*		node;
	u32				timings[2];
	int				index;
#ifdef CONFIG_BLK_DEV_IDEDMA_PMAC
	/* Those fields are duplicating what is in hwif. We currently
	 * can't use the hwif ones because of some assumptions that are
	 * being done by the generic code about the kind of dma controller
	 * and format of the dma table. This will have to be fixed though.
	 */
	volatile struct dbdma_regs*	dma_regs;
	struct dbdma_cmd*		dma_table_cpu;
	dma_addr_t			dma_table_dma;
	struct scatterlist*		sg_table;
	int				sg_nents;
	int				sg_dma_direction;
#endif
	
} pmac_ide_hwif_t;

static pmac_ide_hwif_t pmac_ide[MAX_HWIFS] __pmacdata;
static int pmac_ide_count;

enum {
	controller_ohare,	/* OHare based */
	controller_heathrow,	/* Heathrow/Paddington */
	controller_kl_ata3,	/* KeyLargo ATA-3 */
	controller_kl_ata4,	/* KeyLargo ATA-4 */
	controller_kl_ata4_80	/* KeyLargo ATA-4 with 80 conductor cable */
};

/*
 * Extra registers, both 32-bit little-endian
 */
#define IDE_TIMING_CONFIG	0x200
#define IDE_INTERRUPT		0x300

/*
 * Timing configuration register definitions
 */

/* Number of IDE_SYSCLK_NS ticks, argument is in nanoseconds */
#define SYSCLK_TICKS(t)		(((t) + IDE_SYSCLK_NS - 1) / IDE_SYSCLK_NS)
#define SYSCLK_TICKS_66(t)	(((t) + IDE_SYSCLK_66_NS - 1) / IDE_SYSCLK_66_NS)
#define IDE_SYSCLK_NS		30	/* 33Mhz cell */
#define IDE_SYSCLK_66_NS	15	/* 66Mhz cell */

/* 66Mhz cell, found in KeyLargo. Can do ultra mode 0 to 2 on
 * 40 connector cable and to 4 on 80 connector one.
 * Clock unit is 15ns (66Mhz)
 * 
 * 3 Values can be programmed:
 *  - Write data setup, which appears to match the cycle time. They
 *    also call it DIOW setup.
 *  - Ready to pause time (from spec)
 *  - Address setup. That one is weird. I don't see where exactly
 *    it fits in UDMA cycles, I got it's name from an obscure piece
 *    of commented out code in Darwin. They leave it to 0, we do as
 *    well, despite a comment that would lead to think it has a
 *    min value of 45ns.
 * Apple also add 60ns to the write data setup (or cycle time ?) on
 * reads. I can't explain that, I tried it and it broke everything
 * here.
 */
#define TR_66_UDMA_MASK			0xfff00000
#define TR_66_UDMA_EN			0x00100000 /* Enable Ultra mode for DMA */
#define TR_66_UDMA_ADDRSETUP_MASK	0xe0000000 /* Address setup */
#define TR_66_UDMA_ADDRSETUP_SHIFT	29
#define TR_66_UDMA_RDY2PAUS_MASK	0x1e000000 /* Ready 2 pause time */
#define TR_66_UDMA_RDY2PAUS_SHIFT	25
#define TR_66_UDMA_WRDATASETUP_MASK	0x01e00000 /* Write data setup time */
#define TR_66_UDMA_WRDATASETUP_SHIFT	21
#define TR_66_MDMA_MASK			0x000ffc00
#define TR_66_MDMA_RECOVERY_MASK	0x000f8000
#define TR_66_MDMA_RECOVERY_SHIFT	15
#define TR_66_MDMA_ACCESS_MASK		0x00007c00
#define TR_66_MDMA_ACCESS_SHIFT		10
#define TR_66_PIO_MASK			0x000003ff
#define TR_66_PIO_RECOVERY_MASK		0x000003e0
#define TR_66_PIO_RECOVERY_SHIFT	5
#define TR_66_PIO_ACCESS_MASK		0x0000001f
#define TR_66_PIO_ACCESS_SHIFT		0

/* 33Mhz cell, found in OHare, Heathrow (& Paddington) and KeyLargo
 * Can do pio & mdma modes, clock unit is 30ns (33Mhz)
 * 
 * The access time and recovery time can be programmed. Some older
 * Darwin code base limit OHare to 150ns cycle time. I decided to do
 * the same here fore safety against broken old hardware ;)
 * The HalfTick bit, when set, adds half a clock (15ns) to the access
 * time and removes one from recovery. It's not supported on KeyLargo
 * implementation afaik. The E bit appears to be set for PIO mode 0 and
 * is used to reach long timings used in this mode.
 */
#define TR_33_MDMA_MASK			0x003ff800
#define TR_33_MDMA_RECOVERY_MASK	0x001f0000
#define TR_33_MDMA_RECOVERY_SHIFT	16
#define TR_33_MDMA_ACCESS_MASK		0x0000f800
#define TR_33_MDMA_ACCESS_SHIFT		11
#define TR_33_MDMA_HALFTICK		0x00200000
#define TR_33_PIO_MASK			0x000007ff
#define TR_33_PIO_E			0x00000400
#define TR_33_PIO_RECOVERY_MASK		0x000003e0
#define TR_33_PIO_RECOVERY_SHIFT	5
#define TR_33_PIO_ACCESS_MASK		0x0000001f
#define TR_33_PIO_ACCESS_SHIFT		0

/*
 * Interrupt register definitions
 */
#define IDE_INTR_DMA			0x80000000
#define IDE_INTR_DEVICE			0x40000000

#ifdef CONFIG_BLK_DEV_IDEDMA_PMAC

/* Rounded Multiword DMA timings
 * 
 * I gave up finding a generic formula for all controller
 * types and instead, built tables based on timing values
 * used by Apple in Darwin's implementation.
 */
struct mdma_timings_t {
	int	accessTime;
	int	recoveryTime;
	int	cycleTime;
};

struct mdma_timings_t mdma_timings_33[] __pmacdata =
{
    { 240, 240, 480 },
    { 180, 180, 360 },
    { 135, 135, 270 },
    { 120, 120, 240 },
    { 105, 105, 210 },
    {  90,  90, 180 },
    {  75,  75, 150 },
    {  75,  45, 120 },
    {   0,   0,   0 }
};

struct mdma_timings_t mdma_timings_33k[] __pmacdata =
{
    { 240, 240, 480 },
    { 180, 180, 360 },
    { 150, 150, 300 },
    { 120, 120, 240 },
    {  90, 120, 210 },
    {  90,  90, 180 },
    {  90,  60, 150 },
    {  90,  30, 120 },
    {   0,   0,   0 }
};

struct mdma_timings_t mdma_timings_66[] __pmacdata =
{
    { 240, 240, 480 },
    { 180, 180, 360 },
    { 135, 135, 270 },
    { 120, 120, 240 },
    { 105, 105, 210 },
    {  90,  90, 180 },
    {  90,  75, 165 },
    {  75,  45, 120 },
    {   0,   0,   0 }
};

/* Ultra DMA timings (rounded) */
struct {
	int	addrSetup; /* ??? */
	int	rdy2pause;
	int	wrDataSetup;
} udma_timings[] __pmacdata =
{
    {   0, 180,  120 },	/* Mode 0 */
    {   0, 150,  90 },	/*      1 */
    {   0, 120,  60 },	/*      2 */
    {   0, 90,   45 },	/*      3 */
    {   0, 90,   30 }	/*      4 */
};

/* allow up to 256 DBDMA commands per xfer */
#define MAX_DCMDS		256

/* Wait 2s for disk to answer on IDE bus after
 * enable operation.
 * NOTE: There is at least one case I know of a disk that needs about 10sec
 *       before anwering on the bus. I beleive we could add a kernel command
 *       line arg to override this delay for such cases.
 */
#define IDE_WAKEUP_DELAY_MS	2000

static void pmac_ide_setup_dma(struct device_node *np, int ix);
static int pmac_ide_build_dmatable(ide_drive_t *drive, int wr);
static int pmac_ide_tune_chipset(ide_drive_t *drive, u8 speed);
static void pmac_ide_tuneproc(ide_drive_t *drive, u8 pio);
static void pmac_ide_selectproc(ide_drive_t *drive);
static int pmac_ide_dma_begin (ide_drive_t *drive);

#endif /* CONFIG_BLK_DEV_IDEDMA_PMAC */

#ifdef CONFIG_PMAC_PBOOK
static int idepmac_notify_sleep(struct pmu_sleep_notifier *self, int when);
struct pmu_sleep_notifier idepmac_sleep_notifier = {
	idepmac_notify_sleep, SLEEP_LEVEL_BLOCK,
};
#endif /* CONFIG_PMAC_PBOOK */

/*
 * N.B. this can't be an initfunc, because the media-bay task can
 * call ide_[un]register at any time.
 */
void __pmac
pmac_ide_init_hwif_ports(hw_regs_t *hw,
			      unsigned long data_port, unsigned long ctrl_port,
			      int *irq)
{
	int i, ix;

	if (data_port == 0)
		return;

	for (ix = 0; ix < MAX_HWIFS; ++ix)
		if (data_port == pmac_ide[ix].regbase)
			break;

	if (ix >= MAX_HWIFS) {
		/* Probably a PCI interface... */
		for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; ++i)
			hw->io_ports[i] = data_port + i - IDE_DATA_OFFSET;
		hw->io_ports[IDE_CONTROL_OFFSET] = ctrl_port;
		return;
	}

	for (i = 0; i < 8; ++i)
		hw->io_ports[i] = data_port + i * 0x10;
	hw->io_ports[8] = data_port + 0x160;

	if (irq != NULL)
		*irq = pmac_ide[ix].irq;
}

/* Setup timings for the selected drive (master/slave). I still need to verify if this
 * is enough, I beleive selectproc will be called whenever an IDE command is started,
 * but... */
static void __pmac
pmac_ide_selectproc(ide_drive_t *drive)
{
	pmac_ide_hwif_t* pmif = (pmac_ide_hwif_t *)HWIF(drive)->hwif_data;

	if (pmif == NULL)
		return;

	if (drive->select.b.unit & 0x01)
		writel(pmif->timings[1],
			(unsigned *)(IDE_DATA_REG+IDE_TIMING_CONFIG));
	else
		writel(pmif->timings[0],
			(unsigned *)(IDE_DATA_REG+IDE_TIMING_CONFIG));
	(void)readl((unsigned *)(IDE_DATA_REG+IDE_TIMING_CONFIG));
}


static int __pmac
pmac_ide_do_setfeature(ide_drive_t *drive, u8 command)
{
	ide_hwif_t *hwif = HWIF(drive);
	int result = 1;
	
	disable_irq(hwif->irq);	/* disable_irq_nosync ?? */
	udelay(1);
	SELECT_DRIVE(drive);
	SELECT_MASK(drive, 0);
	udelay(1);
	/* Get rid of pending error state */
	(void) hwif->INB(IDE_STATUS_REG);
	/* Timeout bumped for some powerbooks */
	if (wait_for_ready(drive, 2000)) {
		/* Timeout bumped for some powerbooks */
		printk(KERN_ERR "pmac_ide_do_setfeature disk not ready "
			"before SET_FEATURE!\n");
		goto out;
	}
	udelay(10);
	hwif->OUTB(drive->ctl | 2, IDE_CONTROL_REG);
	hwif->OUTB(command, IDE_NSECTOR_REG);
	hwif->OUTB(SETFEATURES_XFER, IDE_FEATURE_REG);
	hwif->OUTB(WIN_SETFEATURES, IDE_COMMAND_REG);
	udelay(1);
	/* Timeout bumped for some powerbooks */
	result = wait_for_ready(drive, 2000);
	hwif->OUTB(drive->ctl, IDE_CONTROL_REG);
	if (result)
		printk(KERN_ERR "pmac_ide_do_setfeature disk not ready "
			"after SET_FEATURE !\n");
out:
	SELECT_MASK(drive, 0);
	if (result == 0) {
		drive->id->dma_ultra &= ~0xFF00;
		drive->id->dma_mword &= ~0x0F00;
		drive->id->dma_1word &= ~0x0F00;
		switch(command) {
			case XFER_UDMA_7:
				drive->id->dma_ultra |= 0x8080; break;
			case XFER_UDMA_6:
				drive->id->dma_ultra |= 0x4040; break;
			case XFER_UDMA_5:
				drive->id->dma_ultra |= 0x2020; break;
			case XFER_UDMA_4:
				drive->id->dma_ultra |= 0x1010; break;
			case XFER_UDMA_3:
				drive->id->dma_ultra |= 0x0808; break;
			case XFER_UDMA_2:
				drive->id->dma_ultra |= 0x0404; break;
			case XFER_UDMA_1:
				drive->id->dma_ultra |= 0x0202; break;
			case XFER_UDMA_0:
				drive->id->dma_ultra |= 0x0101; break;
			case XFER_MW_DMA_2:
				drive->id->dma_mword |= 0x0404; break;
			case XFER_MW_DMA_1:
				drive->id->dma_mword |= 0x0202; break;
			case XFER_MW_DMA_0:
				drive->id->dma_mword |= 0x0101; break;
			case XFER_SW_DMA_2:
				drive->id->dma_1word |= 0x0404; break;
			case XFER_SW_DMA_1:
				drive->id->dma_1word |= 0x0202; break;
			case XFER_SW_DMA_0:
				drive->id->dma_1word |= 0x0101; break;
			default: break;
		}
	}
	enable_irq(hwif->irq);
	return result;
}

/* Calculate PIO timings */
static void __pmac
pmac_ide_tuneproc(ide_drive_t *drive, u8 pio)
{
	ide_pio_data_t d;
	u32 *timings;
	unsigned accessTicks, recTicks;
	unsigned accessTime, recTime;
	pmac_ide_hwif_t* pmif = (pmac_ide_hwif_t *)HWIF(drive)->hwif_data;
	
	if (pmif == NULL)
		return;
		
	pio = ide_get_best_pio_mode(drive, pio, 4, &d);
	accessTicks = SYSCLK_TICKS(ide_pio_timings[pio].active_time);
	if (drive->select.b.unit & 0x01)
		timings = &pmif->timings[1];
	else
		timings = &pmif->timings[0];

	recTime = d.cycle_time - ide_pio_timings[pio].active_time
			- ide_pio_timings[pio].setup_time;
	recTime = max(recTime, 150U);
	accessTime = ide_pio_timings[pio].active_time;
	accessTime = max(accessTime, 150U);
	if (pmif->kind == controller_kl_ata4 ||
		pmif->kind == controller_kl_ata4_80) {
		/* 66Mhz cell */
		accessTicks = SYSCLK_TICKS_66(accessTime);
		accessTicks = min(accessTicks, 0x1fU);
		recTicks = SYSCLK_TICKS_66(recTime);
		recTicks = min(recTicks, 0x1fU);
		*timings = ((*timings) & ~TR_66_PIO_MASK) |
				(accessTicks << TR_66_PIO_ACCESS_SHIFT) |
				(recTicks << TR_66_PIO_RECOVERY_SHIFT);
	} else {
		/* 33Mhz cell */
		int ebit = 0;
		accessTicks = SYSCLK_TICKS(accessTime);
		accessTicks = min(accessTicks, 0x1fU);
		accessTicks = max(accessTicks, 4U);
		recTicks = SYSCLK_TICKS(recTime);
		recTicks = min(recTicks, 0x1fU);
		recTicks = max(recTicks, 5U) - 4;
		if (recTicks > 9) {
			recTicks--; /* guess, but it's only for PIO0, so... */
			ebit = 1;
		}
		*timings = ((*timings) & ~TR_33_PIO_MASK) |
				(accessTicks << TR_33_PIO_ACCESS_SHIFT) |
				(recTicks << TR_33_PIO_RECOVERY_SHIFT);
		if (ebit)
			*timings |= TR_33_PIO_E;
	}

#ifdef IDE_PMAC_DEBUG
	printk(KERN_ERR "ide_pmac: Set PIO timing for mode %d, reg: 0x%08x\n",
		pio,  *timings);
#endif	

	if (drive->select.all == HWIF(drive)->INB(IDE_SELECT_REG))
		pmac_ide_selectproc(drive);
}

#ifdef CONFIG_BLK_DEV_IDEDMA_PMAC
static int __pmac
set_timings_udma(u32 *timings, u8 speed)
{
	unsigned rdyToPauseTicks, wrDataSetupTicks, addrTicks;

	rdyToPauseTicks = SYSCLK_TICKS_66(udma_timings[speed & 0xf].rdy2pause);
	wrDataSetupTicks = SYSCLK_TICKS_66(udma_timings[speed & 0xf].wrDataSetup);
	addrTicks = SYSCLK_TICKS_66(udma_timings[speed & 0xf].addrSetup);

	*timings = ((*timings) & ~(TR_66_UDMA_MASK | TR_66_MDMA_MASK)) |
			(wrDataSetupTicks << TR_66_UDMA_WRDATASETUP_SHIFT) | 
			(rdyToPauseTicks << TR_66_UDMA_RDY2PAUS_SHIFT) |
			(addrTicks <<TR_66_UDMA_ADDRSETUP_SHIFT) |
			TR_66_UDMA_EN;
#ifdef IDE_PMAC_DEBUG
	printk(KERN_ERR "ide_pmac: Set UDMA timing for mode %d, reg: 0x%08x\n",
		speed & 0xf,  *timings);
#endif	

	return 0;
}

static int __pmac
set_timings_mdma(int intf_type, u32 *timings, u8 speed, int drive_cycle_time)
{
	int cycleTime, accessTime, recTime;
	unsigned accessTicks, recTicks;
	struct mdma_timings_t* tm;
	int i;

	/* Get default cycle time for mode */
	switch(speed & 0xf) {
		case 0: cycleTime = 480; break;
		case 1: cycleTime = 150; break;
		case 2: cycleTime = 120; break;
		default:
			return -1;
	}
	/* Adjust for drive */
	if (drive_cycle_time && drive_cycle_time > cycleTime)
		cycleTime = drive_cycle_time;
	/* OHare limits according to some old Apple sources */	
	if ((intf_type == controller_ohare) && (cycleTime < 150))
		cycleTime = 150;
	/* Get the proper timing array for this controller */
	switch(intf_type) {
		case controller_kl_ata4:
		case controller_kl_ata4_80:
			tm = mdma_timings_66;
			break;
		case controller_kl_ata3:
			tm = mdma_timings_33k;
			break;
		default:
			tm = mdma_timings_33;
			break;
	}
	/* Lookup matching access & recovery times */
	i = -1;
	for (;;) {
		if (tm[i+1].cycleTime < cycleTime)
			break;
		i++;
	}
	if (i < 0)
		return -1;
	cycleTime = tm[i].cycleTime;
	accessTime = tm[i].accessTime;
	recTime = tm[i].recoveryTime;

#ifdef IDE_PMAC_DEBUG
	printk(KERN_ERR "ide_pmac: MDMA, cycleTime: %d, accessTime: %d, recTime: %d\n",
		cycleTime, accessTime, recTime);
#endif	
	if (intf_type == controller_kl_ata4 || intf_type == controller_kl_ata4_80) {
		/* 66Mhz cell */
		accessTicks = SYSCLK_TICKS_66(accessTime);
		accessTicks = min(accessTicks, 0x1fU);
		accessTicks = max(accessTicks, 0x1U);
		recTicks = SYSCLK_TICKS_66(recTime);
		recTicks = min(recTicks, 0x1fU);
		recTicks = max(recTicks, 0x3U);
		/* Clear out mdma bits and disable udma */
		*timings = ((*timings) & ~(TR_66_MDMA_MASK | TR_66_UDMA_MASK)) |
			(accessTicks << TR_66_MDMA_ACCESS_SHIFT) |
			(recTicks << TR_66_MDMA_RECOVERY_SHIFT);
	} else if (intf_type == controller_kl_ata3) {
		/* 33Mhz cell on KeyLargo */
		accessTicks = SYSCLK_TICKS(accessTime);
		accessTicks = max(accessTicks, 1U);
		accessTicks = min(accessTicks, 0x1fU);
		accessTime = accessTicks * IDE_SYSCLK_NS;
		recTicks = SYSCLK_TICKS(recTime);
		recTicks = max(recTicks, 1U);
		recTicks = min(recTicks, 0x1fU);
		*timings = ((*timings) & ~TR_33_MDMA_MASK) |
				(accessTicks << TR_33_MDMA_ACCESS_SHIFT) |
				(recTicks << TR_33_MDMA_RECOVERY_SHIFT);
	} else {
		/* 33Mhz cell on others */
		int halfTick = 0;
		int origAccessTime = accessTime;
		int origRecTime = recTime;
		
		accessTicks = SYSCLK_TICKS(accessTime);
		accessTicks = max(accessTicks, 1U);
		accessTicks = min(accessTicks, 0x1fU);
		accessTime = accessTicks * IDE_SYSCLK_NS;
		recTicks = SYSCLK_TICKS(recTime);
		recTicks = max(recTicks, 2U) - 1;
		recTicks = min(recTicks, 0x1fU);
		recTime = (recTicks + 1) * IDE_SYSCLK_NS;
		if ((accessTicks > 1) &&
		    ((accessTime - IDE_SYSCLK_NS/2) >= origAccessTime) &&
		    ((recTime - IDE_SYSCLK_NS/2) >= origRecTime)) {
            		halfTick = 1;
			accessTicks--;
		}
		*timings = ((*timings) & ~TR_33_MDMA_MASK) |
				(accessTicks << TR_33_MDMA_ACCESS_SHIFT) |
				(recTicks << TR_33_MDMA_RECOVERY_SHIFT);
		if (halfTick)
			*timings |= TR_33_MDMA_HALFTICK;
	}
#ifdef IDE_PMAC_DEBUG
	printk(KERN_ERR "ide_pmac: Set MDMA timing for mode %d, reg: 0x%08x\n",
		speed & 0xf,  *timings);
#endif	
	return 0;
}
#endif /* #ifdef CONFIG_BLK_DEV_IDEDMA_PMAC */

/* You may notice we don't use this function on normal operation,
 * our, normal mdma function is supposed to be more precise
 */
static int __pmac
pmac_ide_tune_chipset (ide_drive_t *drive, u8 speed)
{
	int unit = (drive->select.b.unit & 0x01);
	int ret = 0;
	pmac_ide_hwif_t* pmif = (pmac_ide_hwif_t *)HWIF(drive)->hwif_data;
	u32 *timings;
	
	if (pmif == NULL)
		return 1;

	timings = &pmif->timings[unit];
	
	switch(speed) {
#ifdef CONFIG_BLK_DEV_IDEDMA_PMAC
		case XFER_UDMA_4:
		case XFER_UDMA_3:
			if (pmif->kind != controller_kl_ata4_80)
				return 1;		
		case XFER_UDMA_2:
		case XFER_UDMA_1:
		case XFER_UDMA_0:
			if (pmif->kind != controller_kl_ata4 &&
				pmif->kind != controller_kl_ata4_80)
				return 1;		
			ret = set_timings_udma(timings, speed);
			break;
		case XFER_MW_DMA_2:
		case XFER_MW_DMA_1:
		case XFER_MW_DMA_0:
			ret = set_timings_mdma(pmif->kind, timings, speed, 0);
			break;
		case XFER_SW_DMA_2:
		case XFER_SW_DMA_1:
		case XFER_SW_DMA_0:
			return 1;
#endif /* CONFIG_BLK_DEV_IDEDMA_PMAC */
		case XFER_PIO_4:
		case XFER_PIO_3:
		case XFER_PIO_2:
		case XFER_PIO_1:
		case XFER_PIO_0:
			pmac_ide_tuneproc(drive, speed & 0x07);
			break;
		default:
			ret = 1;
	}
	if (ret)
		return ret;

	ret = pmac_ide_do_setfeature(drive, speed);
	if (ret)
		return ret;
		
	pmac_ide_selectproc(drive);	
	drive->current_speed = speed;

	return 0;
}

static void __pmac
sanitize_timings(pmac_ide_hwif_t *pmif)
{
	unsigned value;
	
	switch(pmif->kind) {
		case controller_kl_ata4:
		case controller_kl_ata4_80:
			value = 0x0008438c;
			break;
		case controller_kl_ata3:
			value = 0x00084526;
			break;
		case controller_heathrow:
		case controller_ohare:
		default:
			value = 0x00074526;
			break;
	}
	pmif->timings[0] = pmif->timings[1] = value;
}

unsigned long __pmac
pmac_ide_get_base(int index)
{
	return pmac_ide[index].regbase;
}

int __pmac
pmac_ide_check_base(unsigned long base)
{
	int ix;
	
 	for (ix = 0; ix < MAX_HWIFS; ++ix)
		if (base == pmac_ide[ix].regbase)
			return ix;
	return -1;
}

int __pmac
pmac_ide_get_irq(unsigned long base)
{
	int ix;

	for (ix = 0; ix < MAX_HWIFS; ++ix)
		if (base == pmac_ide[ix].regbase)
			return pmac_ide[ix].irq;
	return 0;
}

static int ide_majors[]  __pmacdata = { 3, 22, 33, 34, 56, 57 };

dev_t __init
pmac_find_ide_boot(char *bootdevice, int n)
{
	int i;
	
	/*
	 * Look through the list of IDE interfaces for this one.
	 */
	for (i = 0; i < pmac_ide_count; ++i) {
		char *name;
		if (!pmac_ide[i].node || !pmac_ide[i].node->full_name)
			continue;
		name = pmac_ide[i].node->full_name;
		if (memcmp(name, bootdevice, n) == 0 && name[n] == 0) {
			/* XXX should cope with the 2nd drive as well... */
			return MKDEV(ide_majors[i], 0);
		}
	}

	return 0;
}

void __init
pmac_ide_probe(void)
{
	struct device_node *np;
	int i;
	struct device_node *atas;
	struct device_node *p, **pp, *removables, **rp;
	unsigned long base;
	int irq, big_delay;
	ide_hwif_t *hwif;

	if (_machine != _MACH_Pmac)
		return;
	pp = &atas;
	rp = &removables;
	p = find_devices("ATA");
	if (p == NULL)
		p = find_devices("IDE");
	if (p == NULL)
		p = find_type_devices("ide");
	if (p == NULL)
		p = find_type_devices("ata");
	/* Move removable devices such as the media-bay CDROM
	   on the PB3400 to the end of the list. */
	for (; p != NULL; p = p->next) {
		if (p->parent && p->parent->type
		    && strcasecmp(p->parent->type, "media-bay") == 0) {
			*rp = p;
			rp = &p->next;
		} else {
			*pp = p;
			pp = &p->next;
		}
	}
	*rp = NULL;
	*pp = removables;
	big_delay = 0;

	for (i = 0, np = atas; i < MAX_HWIFS && np != NULL; np = np->next) {
		struct device_node *tp;
		struct pmac_ide_hwif* pmif;
		int *bidp;
		int in_bay = 0;
		u8 pbus, pid;
		struct pci_dev *pdev = NULL;

		/*
		 * If this node is not under a mac-io or dbdma node,
		 * leave it to the generic PCI driver.
		 */
		for (tp = np->parent; tp != 0; tp = tp->parent)
			if (tp->type && (strcmp(tp->type, "mac-io") == 0
					 || strcmp(tp->type, "dbdma") == 0))
				break;
		if (tp == 0)
			continue;

		if (np->n_addrs == 0) {
			printk(KERN_WARNING "ide: no address for device %s\n",
			       np->full_name);
			continue;
		}

		/* We need to find the pci_dev of the mac-io holding the
		 * IDE interface
		 */
		if (pci_device_from_OF_node(tp, &pbus, &pid) == 0)
			pdev = pci_find_slot(pbus, pid);
		if (pdev == NULL)
			printk(KERN_WARNING "ide: no PCI host for device %s, DMA disabled\n",
			       np->full_name);

		/*
		 * If this slot is taken (e.g. by ide-pci.c) try the next one.
		 */
		while (i < MAX_HWIFS
		       && ide_hwifs[i].io_ports[IDE_DATA_OFFSET] != 0)
			++i;
		if (i >= MAX_HWIFS)
			break;
		pmif = &pmac_ide[i];

		/*
		 * Some older OFs have bogus sizes, causing request_OF_resource
		 * to fail. We fix them up here
		 */
		if (np->addrs[0].size > 0x1000)
			np->addrs[0].size = 0x1000;
		if (np->n_addrs > 1 && np->addrs[1].size > 0x100)
			np->addrs[1].size = 0x100;

		if (request_OF_resource(np, 0, "  (mac-io IDE IO)") == NULL) {
			printk(KERN_ERR "ide-pmac(%s): can't request IO resource !\n", np->name);
			continue;
		}

		base = (unsigned long) ioremap(np->addrs[0].address, 0x400);

		/* XXX This is bogus. Should be fixed in the registry by checking
		   the kind of host interrupt controller, a bit like gatwick
		   fixes in irq.c
		 */
		if (np->n_intrs == 0) {
			printk(KERN_WARNING "ide: no intrs for device %s, using 13\n",
			       np->full_name);
			irq = 13;
		} else {
			irq = np->intrs[0].line;
		}
		pmif->regbase = base;
		pmif->irq = irq;
		pmif->node = np;
		pmif->index = i;
		if (device_is_compatible(np, "keylargo-ata")) {
			if (strcmp(np->name, "ata-4") == 0)
				pmif->kind = controller_kl_ata4;
			else
				pmif->kind = controller_kl_ata3;
		} else if (device_is_compatible(np, "heathrow-ata"))
			pmif->kind = controller_heathrow;
		else
			pmif->kind = controller_ohare;

		bidp = (int *)get_property(np, "AAPL,bus-id", NULL);
		pmif->aapl_bus_id =  bidp ? *bidp : 0;

		if (pmif->kind == controller_kl_ata4) {
			char* cable = get_property(np, "cable-type", NULL);
			if (cable && !strncmp(cable, "80-", 3))
				pmif->kind = controller_kl_ata4_80;
		}

		/* Make sure we have sane timings */
		sanitize_timings(pmif);

		if (np->parent && np->parent->name
		    && strcasecmp(np->parent->name, "media-bay") == 0) {
#ifdef CONFIG_PMAC_PBOOK
			media_bay_set_ide_infos(np->parent,base,irq,i);
#endif /* CONFIG_PMAC_PBOOK */
			in_bay = 1;
			if (!bidp)
				pmif->aapl_bus_id = 1;
		} else if (pmif->kind == controller_ohare) {
			/* The code below is having trouble on some ohare machines
			 * (timing related ?). Until I can put my hand on one of these
			 * units, I keep the old way
			 */
			ppc_md.feature_call(PMAC_FTR_IDE_ENABLE, np, 0, 1);
		} else {
 			/* This is necessary to enable IDE when net-booting */
			printk(KERN_INFO "pmac_ide: enabling IDE bus ID %d\n",
				pmif->aapl_bus_id);
			ppc_md.feature_call(PMAC_FTR_IDE_RESET, np, pmif->aapl_bus_id, 1);
			ppc_md.feature_call(PMAC_FTR_IDE_ENABLE, np, pmif->aapl_bus_id, 1);
			mdelay(10);
			ppc_md.feature_call(PMAC_FTR_IDE_RESET, np, pmif->aapl_bus_id, 0);
			big_delay = 1;
		}

		hwif = &ide_hwifs[i];
		/* Setup MMIO ops */
		default_hwif_mmiops(hwif);
		/* Tell common code _not_ to mess with resources */
		hwif->mmio = 2;
		hwif->hwif_data = pmif;
		pmac_ide_init_hwif_ports(&hwif->hw, base, 0, &hwif->irq);
		memcpy(hwif->io_ports, hwif->hw.io_ports, sizeof(hwif->io_ports));
		hwif->chipset = ide_pmac;
		hwif->noprobe = !hwif->io_ports[IDE_DATA_OFFSET] || in_bay;
		hwif->udma_four = (pmif->kind == controller_kl_ata4_80);
		hwif->pci_dev = pdev;
		hwif->drives[0].unmask = 1;
		hwif->drives[1].unmask = 1;
		hwif->tuneproc = pmac_ide_tuneproc;
		hwif->selectproc = pmac_ide_selectproc;
		hwif->speedproc = pmac_ide_tune_chipset;
#ifdef CONFIG_PMAC_PBOOK
		if (in_bay && check_media_bay_by_base(base, MB_CD) == 0)
			hwif->noprobe = 0;
#endif /* CONFIG_PMAC_PBOOK */

#ifdef CONFIG_BLK_DEV_IDEDMA_PMAC
		if (np->n_addrs >= 2) {
			/* has a DBDMA controller channel */
			pmac_ide_setup_dma(np, i);
		}
		hwif->atapi_dma = 1;
		hwif->ultra_mask = 0x1f;
		hwif->mwdma_mask = 0x07;
		hwif->swdma_mask = 0x07;

#endif /* CONFIG_BLK_DEV_IDEDMA_PMAC */

		++i;
	}
	pmac_ide_count = i;
	if (big_delay)
		mdelay(IDE_WAKEUP_DELAY_MS);

#ifdef CONFIG_PMAC_PBOOK
	pmu_register_sleep_notifier(&idepmac_sleep_notifier);
#endif /* CONFIG_PMAC_PBOOK */
}

#ifdef CONFIG_BLK_DEV_IDEDMA_PMAC

static int
pmac_ide_build_sglist(ide_drive_t *drive, struct request *rq)
{
	ide_hwif_t *hwif = HWIF(drive);
	pmac_ide_hwif_t *pmif = (pmac_ide_hwif_t *)hwif->hwif_data;
	struct scatterlist *sg = pmif->sg_table;
	int nents;

	if (hwif->sg_dma_active)
		BUG();
		
	nents = blk_rq_map_sg(&drive->queue, rq, sg);
		
	if (rq_data_dir(rq) == READ)
		pmif->sg_dma_direction = PCI_DMA_FROMDEVICE;
	else
		pmif->sg_dma_direction = PCI_DMA_TODEVICE;

	return pci_map_sg(hwif->pci_dev, sg, nents, pmif->sg_dma_direction);
}

static int
pmac_ide_raw_build_sglist(ide_drive_t *drive, struct request *rq)
{
	ide_hwif_t *hwif = HWIF(drive);
	pmac_ide_hwif_t *pmif = (pmac_ide_hwif_t *)hwif->hwif_data;
	struct scatterlist *sg = pmif->sg_table;
	int nents = 0;
	ide_task_t *args = rq->special;
	unsigned char *virt_addr = rq->buffer;
	int sector_count = rq->nr_sectors;

	if (args->command_type == IDE_DRIVE_TASK_RAW_WRITE)
		pmif->sg_dma_direction = PCI_DMA_TODEVICE;
	else
		pmif->sg_dma_direction = PCI_DMA_FROMDEVICE;
	
	if (sector_count > 127) {
		memset(&sg[nents], 0, sizeof(*sg));
		sg[nents].page = virt_to_page(virt_addr);
		sg[nents].offset = (unsigned long) virt_addr & ~PAGE_MASK;
		sg[nents].length = 127  * SECTOR_SIZE;
		nents++;
		virt_addr = virt_addr + (127 * SECTOR_SIZE);
		sector_count -= 127;
	}
	memset(&sg[nents], 0, sizeof(*sg));
	sg[nents].page = virt_to_page(virt_addr);
	sg[nents].offset = (unsigned long) virt_addr & ~PAGE_MASK;
	sg[nents].length =  sector_count  * SECTOR_SIZE;
	nents++;
   
	return pci_map_sg(hwif->pci_dev, sg, nents, pmif->sg_dma_direction);
}

/*
 * pmac_ide_build_dmatable builds the DBDMA command list
 * for a transfer and sets the DBDMA channel to point to it.
 */
static int
pmac_ide_build_dmatable(ide_drive_t *drive, int wr)
{
	struct dbdma_cmd *table;
	int i, count = 0;
	struct request *rq = HWGROUP(drive)->rq;
	ide_hwif_t *hwif = HWIF(drive);
	pmac_ide_hwif_t* pmif = (pmac_ide_hwif_t *)hwif->hwif_data;
	volatile struct dbdma_regs *dma = pmif->dma_regs;
	struct scatterlist *sg;

	/* DMA table is already aligned */
	table = (struct dbdma_cmd *) pmif->dma_table_cpu;

	/* Make sure DMA controller is stopped (necessary ?) */
	writel((RUN|PAUSE|FLUSH|WAKE|DEAD) << 16, &dma->control);
	while (readl(&dma->status) & RUN)
		udelay(1);

	/* Build sglist */
	if (HWGROUP(drive)->rq->flags & REQ_DRIVE_TASKFILE)
		pmif->sg_nents = i = pmac_ide_raw_build_sglist(drive, rq);
	else
		pmif->sg_nents = i = pmac_ide_build_sglist(drive, rq);
	if (!i)
		return 0;

	/* Build DBDMA commands list */
	sg = pmif->sg_table;
	while (i && sg_dma_len(sg)) {
		u32 cur_addr;
		u32 cur_len;

		cur_addr = sg_dma_address(sg);
		cur_len = sg_dma_len(sg);

		while (cur_len) {
			unsigned int tc = (cur_len < 0xfe00)? cur_len: 0xfe00;

			if (++count >= MAX_DCMDS) {
				printk(KERN_WARNING "%s: DMA table too small\n",
				       drive->name);
				goto use_pio_instead;
			}
			st_le16(&table->command, wr? OUTPUT_MORE: INPUT_MORE);
			st_le16(&table->req_count, tc);
			st_le32(&table->phy_addr, cur_addr);
			table->cmd_dep = 0;
			table->xfer_status = 0;
			table->res_count = 0;
			cur_addr += tc;
			cur_len -= tc;
			++table;
		}
		sg++;
		i--;
	}

	/* convert the last command to an input/output last command */
	if (count) {
		st_le16(&table[-1].command, wr? OUTPUT_LAST: INPUT_LAST);
		/* add the stop command to the end of the list */
		memset(table, 0, sizeof(struct dbdma_cmd));
		st_le16(&table->command, DBDMA_STOP);
		mb();
		writel(pmif->dma_table_dma, &dma->cmdptr);
		return 1;
	}

	printk(KERN_DEBUG "%s: empty DMA table?\n", drive->name);
 use_pio_instead:
	pci_unmap_sg(hwif->pci_dev,
		     pmif->sg_table,
		     pmif->sg_nents,
		     pmif->sg_dma_direction);
	hwif->sg_dma_active = 0;
	return 0; /* revert to PIO for this request */
}

/* Teardown mappings after DMA has completed.  */
static void
pmac_ide_destroy_dmatable (ide_drive_t *drive)
{
	struct pci_dev *dev = HWIF(drive)->pci_dev;
	pmac_ide_hwif_t* pmif = (pmac_ide_hwif_t *)HWIF(drive)->hwif_data;
	struct scatterlist *sg = pmif->sg_table;
	int nents = pmif->sg_nents;

	if (nents) {
		pci_unmap_sg(dev, sg, nents, pmif->sg_dma_direction);
		pmif->sg_nents = 0;
	}
}

static __inline__ unsigned char
dma_bits_to_command(unsigned char bits)
{
	if(bits & 0x04)
		return XFER_MW_DMA_2;
	if(bits & 0x02)
		return XFER_MW_DMA_1;
	if(bits & 0x01)
		return XFER_MW_DMA_0;
	return 0;
}

static __inline__ unsigned char
udma_bits_to_command(unsigned char bits, int high_speed)
{
	if (high_speed) {
		if(bits & 0x10)
			return XFER_UDMA_4;
		if(bits & 0x08)
			return XFER_UDMA_3;
	}
	if(bits & 0x04)
		return XFER_UDMA_2;
	if(bits & 0x02)
		return XFER_UDMA_1;
	if(bits & 0x01)
		return XFER_UDMA_0;
	return 0;
}

/* Calculate MultiWord DMA timings */
static int __pmac
pmac_ide_mdma_enable(ide_drive_t *drive)
{
	u8 bits = drive->id->dma_mword & 0x07;
	u8 feature = dma_bits_to_command(bits);
	u32 *timings;
	int drive_cycle_time;
	struct hd_driveid *id = drive->id;
	pmac_ide_hwif_t* pmif = (pmac_ide_hwif_t *)HWIF(drive)->hwif_data;
	int ret;

	/* Set feature on drive */
    	printk(KERN_INFO "%s: Enabling MultiWord DMA %d\n", drive->name, feature & 0xf);
	ret = pmac_ide_do_setfeature(drive, feature);
	if (ret) {
	    	printk(KERN_WARNING "%s: Failed !\n", drive->name);
	    	return 0;
	}

	if (!drive->init_speed)
		drive->init_speed = feature;
	
	/* which drive is it ? */
	if (drive->select.b.unit & 0x01)
		timings = &pmif->timings[1];
	else
		timings = &pmif->timings[0];

	/* Check if drive provide explicit cycle time */
	if ((id->field_valid & 2) && (id->eide_dma_time))
		drive_cycle_time = id->eide_dma_time;
	else
		drive_cycle_time = 0;

	/* Calculate controller timings */
	set_timings_mdma(pmif->kind, timings, feature, drive_cycle_time);

	drive->current_speed = feature;	
	return 1;
}

/* Calculate Ultra DMA timings */
static int __pmac
pmac_ide_udma_enable(ide_drive_t *drive, int high_speed)
{
	u8 bits = drive->id->dma_ultra & 0x1f;
	u8 feature = udma_bits_to_command(bits, high_speed);
	pmac_ide_hwif_t* pmif = (pmac_ide_hwif_t *)HWIF(drive)->hwif_data;
	u32 *timings;
	int ret;

	/* Set feature on drive */
    	printk(KERN_INFO "%s: Enabling Ultra DMA %d\n", drive->name, feature & 0xf);
	ret = pmac_ide_do_setfeature(drive, feature);
	if (ret) {
		printk(KERN_WARNING "%s: Failed !\n", drive->name);
		return 0;
	}

	if (!drive->init_speed)
		drive->init_speed = feature;

	/* which drive is it ? */
	if (drive->select.b.unit & 0x01)
		timings = &pmif->timings[1];
	else
		timings = &pmif->timings[0];

	set_timings_udma(timings, feature);

	drive->current_speed = feature;	
	return 1;
}

int pmac_ide_dma_check(ide_drive_t *drive)
{
	int ata4, udma;
	struct hd_driveid *id = drive->id;
	ide_hwif_t *hwif = HWIF(drive);
	pmac_ide_hwif_t* pmif = (pmac_ide_hwif_t *)hwif->hwif_data;
	int enable = 1;

	drive->using_dma = 0;
	
	if (pmif == NULL)
		return 0;
		
	if (drive->media == ide_floppy)
		enable = 0;
	if (((id->capability & 1) == 0) &&
	    !HWIF(drive)->ide_dma_good_drive(drive))
		enable = 0;
	if (HWIF(drive)->ide_dma_bad_drive(drive))
		enable = 0;
	udma = 0;
	ata4 = (pmif->kind == controller_kl_ata4 ||
		pmif->kind == controller_kl_ata4_80);
			
	if(enable) {
		if (ata4 && (drive->media == ide_disk) &&
		    (id->field_valid & 0x0004) && (id->dma_ultra & 0x1f)) {
			/* UltraDMA modes. */
			drive->using_dma = pmac_ide_udma_enable(drive,
				pmif->kind == controller_kl_ata4_80);
		}
		if (!drive->using_dma && (id->dma_mword & 0x0007)) {
			/* Normal MultiWord DMA modes. */
			drive->using_dma = pmac_ide_mdma_enable(drive);
		}
		hwif->OUTB(0, IDE_CONTROL_REG);
		/* Apply settings to controller */
		pmac_ide_selectproc(drive);
	}
	return 0;
}

static int
pmac_ide_dma_read (ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);
	pmac_ide_hwif_t* pmif = (pmac_ide_hwif_t *)hwif->hwif_data;
	struct request *rq = HWGROUP(drive)->rq;
//	ide_task_t *args = rq->special;
	u8 unit = (drive->select.b.unit & 0x01);
	u8 ata4;
	u8 lba48 = (drive->addressing == 1) ? 1 : 0;
	task_ioreg_t command = WIN_NOP;

	if (pmif == NULL)
		return 1;

	ata4 = (pmif->kind == controller_kl_ata4 ||
		pmif->kind == controller_kl_ata4_80);

	if (!pmac_ide_build_dmatable(drive, 0))
		return 1;
	/* Apple adds 60ns to wrDataSetup on reads */
	if (ata4 && (pmif->timings[unit] & TR_66_UDMA_EN)) {
		writel(pmif->timings[unit]+0x00800000UL,
			(unsigned *)(IDE_DATA_REG+IDE_TIMING_CONFIG));
		(void)readl((unsigned *)(IDE_DATA_REG + IDE_TIMING_CONFIG));
	}
	drive->waiting_for_dma = 1;
	if (drive->media != ide_disk)
		return 0;
	if (HWGROUP(drive)->handler != NULL)	/* paranoia check */
		BUG();
	ide_set_handler(drive, &ide_dma_intr, WAIT_CMD, NULL);
	/*
	 * FIX ME to use only ACB ide_task_t args Struct
	 */
#if 0
	{
		ide_task_t *args = rq->special;
		command = args->tfRegister[IDE_COMMAND_OFFSET];
	}
#else
	command = (lba48) ? WIN_READDMA_EXT : WIN_READDMA;
	if (rq->flags & REQ_DRIVE_TASKFILE) {
		ide_task_t *args = rq->special;
		command = args->tfRegister[IDE_COMMAND_OFFSET];
	}
#endif
	/* issue cmd to drive */
	hwif->OUTB(command, IDE_COMMAND_REG);

	return pmac_ide_dma_begin(drive);
}

static int
pmac_ide_dma_write (ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);
	pmac_ide_hwif_t* pmif = (pmac_ide_hwif_t *)hwif->hwif_data;
	struct request *rq = HWGROUP(drive)->rq;
//	ide_task_t *args = rq->special;
	u8 unit = (drive->select.b.unit & 0x01);
	u8 ata4;
	u8 lba48 = (drive->addressing == 1) ? 1 : 0;
	task_ioreg_t command = WIN_NOP;

	if (pmif == NULL)
		return 1;

	ata4 = (pmif->kind == controller_kl_ata4 ||
		pmif->kind == controller_kl_ata4_80);

	if (!pmac_ide_build_dmatable(drive, 1))
		return 1;
	/* Apple adds 60ns to wrDataSetup on reads */
	if (ata4 && (pmif->timings[unit] & TR_66_UDMA_EN)) {
		writel(pmif->timings[unit],
			(unsigned *)(IDE_DATA_REG+IDE_TIMING_CONFIG));
		(void)readl((unsigned *)(IDE_DATA_REG + IDE_TIMING_CONFIG));
	}
	drive->waiting_for_dma = 1;
	if (drive->media != ide_disk)
		return 0;
	if (HWGROUP(drive)->handler != NULL)	/* paranoia check */
		BUG();
	ide_set_handler(drive, &ide_dma_intr, WAIT_CMD, NULL);
	/*
	 * FIX ME to use only ACB ide_task_t args Struct
	 */
#if 0
	{
		ide_task_t *args = rq->special;
		command = args->tfRegister[IDE_COMMAND_OFFSET];
	}
#else
	command = (lba48) ? WIN_WRITEDMA_EXT : WIN_WRITEDMA;
	if (rq->flags & REQ_DRIVE_TASKFILE) {
		ide_task_t *args = rq->special;
		command = args->tfRegister[IDE_COMMAND_OFFSET];
	}
#endif
	/* issue cmd to drive */
	hwif->OUTB(command, IDE_COMMAND_REG);

	return pmac_ide_dma_begin(drive);
}

static int
pmac_ide_dma_count (ide_drive_t *drive)
{
	return HWIF(drive)->ide_dma_begin(drive);
}

static int
pmac_ide_dma_begin (ide_drive_t *drive)
{
	pmac_ide_hwif_t* pmif = (pmac_ide_hwif_t *)HWIF(drive)->hwif_data;
	volatile struct dbdma_regs *dma;

	if (pmif == NULL)
		return 1;
	dma = pmif->dma_regs;

	writel((RUN << 16) | RUN, &dma->control);
	/* Make sure it gets to the controller right now */
	(void)readl(&dma->control);
	return 0;
}

static int
pmac_ide_dma_end (ide_drive_t *drive)
{
	pmac_ide_hwif_t* pmif = (pmac_ide_hwif_t *)HWIF(drive)->hwif_data;
	volatile struct dbdma_regs *dma;
	u32 dstat;
	
	if (pmif == NULL)
		return 0;
	dma = pmif->dma_regs;

	drive->waiting_for_dma = 0;
	dstat = readl(&dma->status);
	writel(((RUN|WAKE|DEAD) << 16), &dma->control);
	pmac_ide_destroy_dmatable(drive);
	/* verify good dma status */
	return (dstat & (RUN|DEAD|ACTIVE)) != RUN;
}

static int
pmac_ide_dma_test_irq (ide_drive_t *drive)
{
	pmac_ide_hwif_t* pmif = (pmac_ide_hwif_t *)HWIF(drive)->hwif_data;
	volatile struct dbdma_regs *dma;
	unsigned long status;

	if (pmif == NULL)
		return 0;
	dma = pmif->dma_regs;

	/* We have to things to deal with here:
	 * 
	 * - The dbdma won't stop if the command was started
	 * but completed with an error without transferring all
	 * datas. This happens when bad blocks are met during
	 * a multi-block transfer.
	 * 
	 * - The dbdma fifo hasn't yet finished flushing to
	 * to system memory when the disk interrupt occurs.
	 * 
	 * The trick here is to increment drive->waiting_for_dma,
	 * and return as if no interrupt occurred. If the counter
	 * reach a certain timeout value, we then return 1. If
	 * we really got the interrupt, it will happen right away
	 * again.
	 * Apple's solution here may be more elegant. They issue
	 * a DMA channel interrupt (a separate irq line) via a DBDMA
	 * NOP command just before the STOP, and wait for both the
	 * disk and DBDMA interrupts to have completed.
	 */
 
	/* If ACTIVE is cleared, the STOP command have passed and
	 * transfer is complete.
	 */
	status = readl(&dma->status);
	if (!(status & ACTIVE))
		return 1;
	if (!drive->waiting_for_dma)
		printk(KERN_WARNING "ide%d, ide_dma_test_irq \
			called while not waiting\n", pmif->index);

	/* If dbdma didn't execute the STOP command yet, the
	 * active bit is still set */
	drive->waiting_for_dma++;
	if (drive->waiting_for_dma >= DMA_WAIT_TIMEOUT) {
		printk(KERN_WARNING "ide%d, timeout waiting \
			for dbdma command stop\n", pmif->index);
		return 1;
	}
	udelay(1);
	return 0;
}

static int
pmac_ide_dma_host_off (ide_drive_t *drive)
{
	return 0;
}

static int
pmac_ide_dma_host_on (ide_drive_t *drive)
{
	return 0;
}

static int
pmac_ide_dma_lostirq (ide_drive_t *drive)
{
	pmac_ide_hwif_t* pmif = (pmac_ide_hwif_t *)HWIF(drive)->hwif_data;
	volatile struct dbdma_regs *dma;
	unsigned long status;

	if (pmif == NULL)
		return 0;
	dma = pmif->dma_regs;

	status = readl(&dma->status);
	printk(KERN_ERR "ide-pmac lost interrupt, dma status: %lx\n", status);
	return 0;
}

static void __init 
pmac_ide_setup_dma(struct device_node *np, int ix)
{
	struct pmac_ide_hwif *pmif = &pmac_ide[ix];

	if (request_OF_resource(np, 1, " (mac-io IDE DMA)") == NULL) {
		printk(KERN_ERR "ide-pmac(%s): can't request DMA resource !\n", np->name);
		return;
	}

	pmif->dma_regs =
		(volatile struct dbdma_regs*)ioremap(np->addrs[1].address, 0x200);

	/*
	 * Allocate space for the DBDMA commands.
	 * The +2 is +1 for the stop command and +1 to allow for
	 * aligning the start address to a multiple of 16 bytes.
	 */
	pmif->dma_table_cpu = (struct dbdma_cmd*)pci_alloc_consistent(
		ide_hwifs[ix].pci_dev,
		(MAX_DCMDS + 2) * sizeof(struct dbdma_cmd),
		&pmif->dma_table_dma);
	if (pmif->dma_table_cpu == NULL) {
		printk(KERN_ERR "%s: unable to allocate DMA command list\n",
		       ide_hwifs[ix].name);
		return;
	}

	pmif->sg_table = kmalloc(sizeof(struct scatterlist) * MAX_DCMDS,
				 GFP_KERNEL);
	if (pmif->sg_table == NULL) {
		pci_free_consistent(	ide_hwifs[ix].pci_dev,
					(MAX_DCMDS + 2) * sizeof(struct dbdma_cmd),
				    	pmif->dma_table_cpu, pmif->dma_table_dma);
		return;
	}
	ide_hwifs[ix].ide_dma_off = &__ide_dma_off;
	ide_hwifs[ix].ide_dma_off_quietly = &__ide_dma_off_quietly;
	ide_hwifs[ix].ide_dma_on = &__ide_dma_on;
	ide_hwifs[ix].ide_dma_check = &pmac_ide_dma_check;
	ide_hwifs[ix].ide_dma_read = &pmac_ide_dma_read;
	ide_hwifs[ix].ide_dma_write = &pmac_ide_dma_write;
	ide_hwifs[ix].ide_dma_count = &pmac_ide_dma_count;
	ide_hwifs[ix].ide_dma_begin = &pmac_ide_dma_begin;
	ide_hwifs[ix].ide_dma_end = &pmac_ide_dma_end;
	ide_hwifs[ix].ide_dma_test_irq = &pmac_ide_dma_test_irq;
	ide_hwifs[ix].ide_dma_host_off = &pmac_ide_dma_host_off;
	ide_hwifs[ix].ide_dma_host_on = &pmac_ide_dma_host_on;
	ide_hwifs[ix].ide_dma_good_drive = &__ide_dma_good_drive;
	ide_hwifs[ix].ide_dma_bad_drive = &__ide_dma_bad_drive;
	ide_hwifs[ix].ide_dma_verbose = &__ide_dma_verbose;
	ide_hwifs[ix].ide_dma_timeout = &__ide_dma_timeout;
	ide_hwifs[ix].ide_dma_retune = &__ide_dma_retune;
	ide_hwifs[ix].ide_dma_lostirq = &pmac_ide_dma_lostirq;
	ide_hwifs[ix].ide_dma_queued_on = &__ide_dma_queued_on;
	ide_hwifs[ix].ide_dma_queued_off = &__ide_dma_queued_off;

#ifdef CONFIG_BLK_DEV_IDEDMA_PMAC_AUTO
	if (!noautodma)
		ide_hwifs[ix].autodma = 1;
#endif
	ide_hwifs[ix].drives[0].autodma = ide_hwifs[ix].autodma;
	ide_hwifs[ix].drives[1].autodma = ide_hwifs[ix].autodma;
}

#endif /* CONFIG_BLK_DEV_IDEDMA_PMAC */

static void __pmac
idepmac_sleep_device(ide_drive_t *drive, unsigned base)
{
	ide_hwif_t *hwif = HWIF(drive);
	int j;
	
	/* FIXME: We only handle the master IDE disk, we shoud
	 *        try to fix CD-ROMs here
	 */
	switch (drive->media) {
	case ide_disk:
		/* Spin down the drive */
		hwif->OUTB(drive->select.all, base+0x60);
		(void) hwif->INB(base+0x60);
		udelay(100);
		hwif->OUTB(0x0, base+0x30);
		hwif->OUTB(0x0, base+0x20);
		hwif->OUTB(0x0, base+0x40);
		hwif->OUTB(0x0, base+0x50);
		hwif->OUTB(0xe0, base+0x70);
		hwif->OUTB(0x2, base+0x160);   
		for (j = 0; j < 10; j++) {
			u8 status;
			mdelay(100);
			status = hwif->INB(base+0x70);
			if (!(status & BUSY_STAT) && (status & DRQ_STAT))
				break;
		}
		break;
	case ide_cdrom:
		// todo
		break;
	case ide_floppy:
		// todo
		break;
	}
}

#ifdef CONFIG_PMAC_PBOOK
static void __pmac
idepmac_wake_device(ide_drive_t *drive, int used_dma)
{
	/* We force the IDE subdriver to check for a media change
	 * This must be done first or we may lost the condition
	 *
	 * Problem: This can schedule. I moved the block device
	 * wakeup almost late by priority because of that.
	 */
	//if (DRIVER(drive))
	//	check_disk_change(MKDEV(drive->disk->major, drive->disk->first_minor));
	
#ifdef CONFIG_BLK_DEV_IDEDMA_PMAC
	/* We re-enable DMA on the drive if it was active. */
	/* This doesn't work with the CD-ROM in the media-bay, probably
	 * because of a pending unit attention. The problem if that if I
	 * clear the error, the filesystem dies.
	 */
	if (used_dma && !ide_spin_wait_hwgroup(drive)) {
		/* Lock HW group */
		HWGROUP(drive)->busy = 1;
		pmac_ide_dma_check(drive);
		HWGROUP(drive)->busy = 0;
		if (!list_empty(&drive->queue.queue_head))
			ide_do_request(HWGROUP(drive), 0);
		spin_unlock_irq(&ide_lock);
	}
#endif /* CONFIG_BLK_DEV_IDEDMA_PMAC */
}

static void __pmac
idepmac_sleep_interface(pmac_ide_hwif_t *pmif, unsigned base, int mediabay)
{
	struct device_node* np = pmif->node;

	/* We clear the timings */
	pmif->timings[0] = 0;
	pmif->timings[1] = 0;
	
	/* The media bay will handle itself just fine */
	if (mediabay)
		return;
	
	/* Disable the bus */
	ppc_md.feature_call(PMAC_FTR_IDE_ENABLE, np, pmif->aapl_bus_id, 0);
}

static void __pmac
idepmac_wake_interface(pmac_ide_hwif_t *pmif, unsigned long base, int mediabay)
{
	struct device_node* np = pmif->node;

	if (!mediabay) {
		/* Revive IDE disk and controller */
		ppc_md.feature_call(PMAC_FTR_IDE_RESET, np, pmif->aapl_bus_id, 1);
		ppc_md.feature_call(PMAC_FTR_IDE_ENABLE, np, pmif->aapl_bus_id, 1);
		mdelay(10);
		ppc_md.feature_call(PMAC_FTR_IDE_RESET, np, pmif->aapl_bus_id, 0);
	}
}

static void
idepmac_sleep_drive(ide_drive_t *drive, unsigned long base)
{
	int unlock = 0;

	/* Wait for HW group to complete operations */
	if (ide_spin_wait_hwgroup(drive)) {
		// What can we do here ? Wake drive we had already
		// put to sleep and return an error ?
	} else {
		unlock = 1;
		/* Lock HW group */
		HWGROUP(drive)->busy = 1;
		/* Stop the device */
		idepmac_sleep_device(drive, base);
	}
	if (unlock)
		spin_unlock_irq(&ide_lock);
}

static void
idepmac_wake_drive(ide_drive_t *drive, unsigned long base)
{
	ide_hwif_t *hwif = HWIF(drive);
	unsigned long flags;
	int j;
	
	/* Reset timings */
	pmac_ide_selectproc(drive);
	mdelay(10);
	
	/* Wait up to 20 seconds for the drive to be ready */
	for (j = 0; j < 200; j++) {
		u8 status = 0;
		mdelay(100);
		hwif->OUTB(drive->select.all, base + 0x60);
		if ((hwif->INB(base + 0x60)) != drive->select.all)
			continue;
		status = hwif->INB(base + 0x70);
		if (!(status & BUSY_STAT))
			break;
	}

	/* We resume processing on the HW group */
	spin_lock_irqsave(&ide_lock, flags);
	HWGROUP(drive)->busy = 0;
	if (!list_empty(&drive->queue.queue_head))
		ide_do_request(HWGROUP(drive), 0);
	spin_unlock_irqrestore(&ide_lock, flags);
}

/* Note: We support only master drives for now. This will have to be
 * improved if we want to handle sleep on the iMacDV where the CD-ROM
 * is a slave
 */
static int __pmac
idepmac_notify_sleep(struct pmu_sleep_notifier *self, int when)
{
	int i, ret;
	unsigned long base;
	int big_delay;
 
	switch (when) {
	case PBOOK_SLEEP_REQUEST:
		break;
	case PBOOK_SLEEP_REJECT:
		break;
	case PBOOK_SLEEP_NOW:
		for (i = 0; i < pmac_ide_count; ++i) {
			ide_hwif_t *hwif;
			int dn;

			if ((base = pmac_ide[i].regbase) == 0)
				continue;

			hwif = &ide_hwifs[i];
			for (dn=0; dn<MAX_DRIVES; dn++) {
				if (!hwif->drives[dn].present)
					continue;
				idepmac_sleep_drive(&hwif->drives[dn], base);
			}
			/* Disable irq during sleep */
			disable_irq(pmac_ide[i].irq);
			
			/* Check if this is a media bay with an IDE device or not
			 * a media bay.
			 */
			ret = check_media_bay_by_base(base, MB_CD);
			if ((ret == 0) || (ret == -ENODEV))
				idepmac_sleep_interface(&pmac_ide[i], base, (ret == 0));
		}
		break;
	case PBOOK_WAKE:
		big_delay = 0;
		for (i = 0; i < pmac_ide_count; ++i) {

			if ((base = pmac_ide[i].regbase) == 0)
				continue;
				
			/* Make sure we have sane timings */		
			sanitize_timings(&pmac_ide[i]);

			/* Check if this is a media bay with an IDE device or not
			 * a media bay
			 */
			ret = check_media_bay_by_base(base, MB_CD);
			if ((ret == 0) || (ret == -ENODEV)) {
				idepmac_wake_interface(&pmac_ide[i], base, (ret == 0));				
				big_delay = 1;
			}

		}
		/* Let hardware get up to speed */
		if (big_delay)
			mdelay(IDE_WAKEUP_DELAY_MS);
	
		for (i = 0; i < pmac_ide_count; ++i) {
			ide_hwif_t *hwif;
			int used_dma, dn;
			int irq_on = 0;
			
			if ((base = pmac_ide[i].regbase) == 0)
				continue;
				
			hwif = &ide_hwifs[i];
			for (dn=0; dn<MAX_DRIVES; dn++) {
				ide_drive_t *drive = &hwif->drives[dn];
				if (!drive->present)
					continue;
				/* We don't have re-configured DMA yet */
				used_dma = drive->using_dma;
				drive->using_dma = 0;
				idepmac_wake_drive(drive, base);
				if (!irq_on) {
					enable_irq(pmac_ide[i].irq);
					irq_on = 1;
				}
				idepmac_wake_device(drive, used_dma);
			}
			if (!irq_on)
				enable_irq(pmac_ide[i].irq);
		}
		break;
	}
	return PBOOK_SLEEP_OK;
}
#endif /* CONFIG_PMAC_PBOOK */
