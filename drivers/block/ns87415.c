/*
 * linux/drivers/block/ns87415.c	Version 1.00  December 7, 1997
 *
 * Copyright (C) 1997-1998  Mark Lord
 *
 * Inspired by an earlier effort from David S. Miller (davem@caipfs.rutgers.edu)
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/pci.h>
#include <linux/bios32.h>
#include <asm/io.h>
#include "ide.h"


#undef INCLUDE_OBSOLETE_NS87514_STUFF	/* define this if you absolutely *need* the timings stuff */


#ifdef INCLUDE_OBSOLETE_NS87514_STUFF
/*
 * This part adapted from code from David S. Miller (davem@caipfs.rutgers.edu)
 * which was in turn adapted from code from Mark Lord.
 *
 * Here as a temporary measure only.  Will be removed once /proc/ide/ is working.
 */
#include "ide_modes.h"

static void ns87415_program_modes(ide_drive_t *drive, byte active_count, byte recovery_count)
{
	ide_hwif_t *hwif = HWIF(drive);
	byte cfg_reg, regval;

	cfg_reg = (0x44 + (8 * HWIF(drive)->channel) + (4 * drive->select.b.unit));

	/* set identical PIO timings for read/write */
	regval  = (17 - active_count) | ((16 - recovery_count) << 4);
	pcibios_write_config_byte(hwif->pci_bus, hwif->pci_fn, cfg_reg, regval);
	pcibios_write_config_byte(hwif->pci_bus, hwif->pci_fn, cfg_reg + 1, regval);
}

static void set_ide_modes(ide_drive_t *drive, ide_pio_data_t *d, int bus_speed)
{
	int setup_time, active_time, cycle_time = d->cycle_time;
	byte setup_count, active_count, pio_mode = d->pio_mode;
	byte recovery_count, recovery_count2, cycle_count;
	int recovery_time, clock_time;

	if(pio_mode > 5)
		pio_mode = 5;

	setup_time = ide_pio_timings[pio_mode].setup_time;
	active_time = ide_pio_timings[pio_mode].active_time;

	recovery_time = cycle_time - (setup_time + active_time);
	clock_time = 1000 / bus_speed;

	cycle_count  = (cycle_time  + clock_time - 1) / clock_time;
	setup_count  = (setup_time  + clock_time - 1) / clock_time;
	active_count = (active_time + clock_time - 1) / clock_time;

	if(active_count < 2)
		active_count = 2;

	recovery_count  = (recovery_time + clock_time - 1) / clock_time;
	recovery_count2 = cycle_count - (setup_count + active_count);

	if(recovery_count2 > recovery_count)
		recovery_count = recovery_count2;
	if(recovery_count < 2)
		recovery_count = 2;
	if(recovery_count > 17) {
		active_count += recovery_count - 17;
		recovery_count = 17;
	}

	if(active_count > 16)
		active_count = 16;
	if(recovery_count > 16)
		recovery_count = 16;

	printk("active[%d CLKS] recovery[%d CLKS]\n", active_count, recovery_count);

	ns87415_program_modes(drive, active_count, recovery_count);
}

/* Configure for best PIO mode. */
static void ns87415_tuneproc (ide_drive_t *drive, byte mode_wanted)
{
	ide_pio_data_t d;
	int bus_speed = ide_system_bus_speed();

	switch(mode_wanted) {
	case 6:
	case 7:
		/* Changes to Fast-devsel are unsupported. */
		return;

	case 8:
	case 9:
		mode_wanted &= 1;
		/* XXX set_prefetch_mode(index, mode_wanted); */
		printk("%s: %sbled NS87415 prefetching...\n", drive->name, mode_wanted ? "en" : "dis");
		return;
	};

	(void) ide_get_best_pio_mode(drive, mode_wanted, 5, &d);

	printk("%s: selected NS87415 PIO mode%d (%dns)%s ",
	       drive->name, d.pio_mode, d.cycle_time,
	       d.overridden ? " (overriding vendor mode)" : "");

	set_ide_modes(drive, &d, bus_speed);
}
#endif	/* INCLUDE_OBSOLETE_NS87514_STUFF */

static unsigned int ns87415_count = 0, ns87415_control[MAX_HWIFS] = {0};

/*
 * This routine either enables/disables (according to drive->present)
 * the IRQ associated with the port (HWIF(drive)),
 * and selects either PIO or DMA handshaking for the next I/O operation.
 */
static void ns87415_prepare_drive (ide_drive_t *drive, unsigned int use_dma)
{
	ide_hwif_t *hwif = HWIF(drive);
	unsigned int bit, new, *old = (unsigned int *) hwif->select_data;
	unsigned int flags;

	save_flags(flags);
	cli();

	new = *old;

	/* adjust IRQ enable bit */
	bit = 1 << (8 + hwif->channel);
	new = drive->present ? (new | bit) : (new & ~bit);

	/* select PIO or DMA */
	bit = 1 << (20 + drive->select.b.unit + (hwif->channel << 1));
	new = use_dma ? (new | bit) : (new & ~bit);

	if (new != *old) {
		*old = new;
		(void) pcibios_write_config_dword(hwif->pci_bus, hwif->pci_fn, 0x40, new);
	}
	restore_flags(flags);
}

static void ns87415_selectproc (ide_drive_t *drive)
{
	ns87415_prepare_drive (drive, drive->using_dma);
}

static int ns87415_dmaproc(ide_dma_action_t func, ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);

	switch (func) {
		case ide_dma_end: /* returns 1 on error, 0 otherwise */
		{
			byte dma_stat = inb(hwif->dma_base+2);
			int rc = (dma_stat & 7) != 4;
			outb(7, hwif->dma_base); /* from errata: stop DMA, clear INTR & ERROR */
			outb(dma_stat|6, hwif->dma_base+2);	/* clear the INTR & ERROR bits */
			return rc;	/* verify good DMA status */
		}
		case ide_dma_write:
		case ide_dma_read:
			ns87415_prepare_drive(drive, 1); /* select DMA xfer */
			if (!ide_dmaproc(func, drive))	 /* use standard DMA stuff */
				return 0;
			ns87415_prepare_drive(drive, 0); /* DMA failed: select PIO xfer */
			return 1;
		default:
			return ide_dmaproc(func, drive); /* use standard DMA stuff */
	}
}

__initfunc(void ide_init_ns87415 (ide_hwif_t *hwif))
{
	unsigned int ctrl, progif, using_inta;

	/*
	 * We cannot probe for IRQ: both ports share common IRQ on INTA.
	 * Also, leave IRQ masked during drive probing, to prevent infinite
	 * interrupts from a potentially floating INTA..
	 *
	 * IRQs get unmasked in selectproc when drive is first used.
	 */
	(void) pcibios_read_config_dword(hwif->pci_bus, hwif->pci_fn, 0x40, &ctrl);
	(void) pcibios_read_config_dword(hwif->pci_bus, hwif->pci_fn, 0x40, &progif);
	/* is irq in "native" mode? */
	using_inta = progif & (1 << (hwif->channel << 1));
	if (!using_inta)
		using_inta = ctrl & (1 << (4 + hwif->channel));
	(void) pcibios_write_config_dword(hwif->pci_bus, hwif->pci_fn, 0x40, ctrl);
	if (hwif->mate) {
		hwif->select_data = hwif->mate->select_data;
	} else {
		hwif->select_data = (unsigned int) &ns87415_control[ns87415_count++];
		ctrl |= (1 << 8) | (1 << 9);		/* mask both IRQs */
		if (using_inta)
			ctrl &= ~(1 << 6);		/* unmask INTA */
		*((unsigned int *)hwif->select_data) = ctrl;
		/*
		 * Set prefetch size to 512 bytes for both ports,
		 * but don't turn on/off prefetching here.
		 */
		pcibios_write_config_byte(hwif->pci_bus, hwif->pci_fn, 0x55, 0xee);
	}
	if (!using_inta)
		hwif->irq = hwif->channel ? 15 : 14;	/* legacy mode */
	else if (!hwif->irq && hwif->mate && hwif->mate->irq)
		hwif->irq = hwif->mate->irq;		/* share IRQ with mate */

	hwif->dmaproc = &ns87415_dmaproc;
	hwif->selectproc = &ns87415_selectproc;
#ifdef INCLUDE_OBSOLETE_NS87514_STUFF
	hwif->tuneproc = &ns87415_tuneproc;
#endif	/* INCLUDE_OBSOLETE_NS87514_STUFF */
}
