/*
 * drivers/block/sl82c105.c
 *
 * SL82C105/Winbond 553 IDE driver
 *
 * Maintainer unknown.
 *
 * Drive tuning added from Corel Computer's kernel sources
 *  -- Russell King (15/11/98) linux@arm.linux.org.uk
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/pci.h>
#include <linux/ide.h>

#include <asm/io.h>
#include <asm/dma.h>

#include "ide_modes.h"

#ifdef CONFIG_ARCH_NETWINDER
/*
 * Convert a PIO mode and cycle time to the required on/off
 * times for the interface.  This has protection against run-away
 * timings.
 */
static unsigned int get_timing_sl82c105(ide_pio_data_t *p)
{
	unsigned int cmd_on;
	unsigned int cmd_off;

	cmd_on = (ide_pio_timings[p->pio_mode].active_time + 29) / 30;
	cmd_off = (p->cycle_time - 30 * cmd_on + 29) / 30;

	if (cmd_on > 32)
		cmd_on = 32;
	if (cmd_on == 0)
		cmd_on = 1;

	if (cmd_off > 32)
		cmd_off = 32;
	if (cmd_off == 0)
		cmd_off = 1;

	return (cmd_on - 1) << 8 | (cmd_off - 1);
}

/*
 * Tell the drive to enable the specified PIO mode.
 * This should be in ide.c, maybe as a special command
 * (see do_special).
 */
static int ide_set_drive_pio_mode(ide_drive_t *drive, byte pio)
{
	ide_hwif_t *hwif = HWIF(drive);

	if (pio > 2) {
		/* FIXME: I don't believe that this SELECT_DRIVE is required,
		 * since ide.c only calls tuneproc from do_special, after
		 * the correct drive has been selected.
		 */
		SELECT_DRIVE(hwif, drive);
		OUT_BYTE(drive->ctl | 2, IDE_CONTROL_REG);
		OUT_BYTE(0x08 | pio,  IDE_NSECTOR_REG);
		OUT_BYTE(0x03, IDE_FEATURE_REG);
		OUT_BYTE(WIN_SETFEATURES, IDE_COMMAND_REG);

		if (ide_wait_stat(drive, DRIVE_READY,
				  BUSY_STAT|DRQ_STAT|ERR_STAT, WAIT_CMD)) {
			printk("%s: drive not ready for command\n",
			       drive->name);
			return 1;
		}

		OUT_BYTE(drive->ctl, IDE_CONTROL_REG);
	}

	return 0;
}

/*
 * We only deal with PIO mode here - DMA mode 'using_dma' is not
 * initialised at the point that this function is called.
 */
static void tune_sl82c105(ide_drive_t *drive, byte pio)
{
	ide_hwif_t *hwif = HWIF(drive);
	struct pci_dev *dev = hwif->pci_dev;
	ide_pio_data_t p;
	unsigned int drv_ctrl = 0x909;

	pio = ide_get_best_pio_mode(drive, pio, 5, &p);

	if (!ide_set_drive_pio_mode(drive, pio)) {
		drv_ctrl = get_timing_sl82c105(&p);

		if (p.use_iordy)
			drv_ctrl |= 0x40;
	}

	pci_write_config_word(dev,
			      (hwif->channel ? 0x4c : 0x44)
			       + (drive->select.b.unit ? 4 : 0),
			      drv_ctrl);

	printk("%s: selected PIO mode %d (%dns)\n",
		drive->name, p.pio_mode, p.cycle_time);
}
#endif

void ide_init_sl82c105(ide_hwif_t *hwif)
{
	struct pci_dev *dev = hwif->pci_dev;

#ifdef CONFIG_ARCH_NETWINDER
	unsigned char ctrl_stat;

	pci_read_config_byte(dev, 0x40, &ctrl_stat);
	pci_write_config_byte(dev, 0x40, ctrl_stat | 0x33);

	hwif->tuneproc = tune_sl82c105;
#else
	unsigned short t16;
	unsigned int t32;
	pci_read_config_word(dev, PCI_COMMAND, &t16);
	printk("SL82C105 command word: %x\n",t16);
        t16 |= PCI_COMMAND_IO;
        pci_write_config_word(dev, PCI_COMMAND, t16);
	/* IDE timing */
	pci_read_config_dword(dev, 0x44, &t32);
	printk("IDE timing: %08x, resetting to PIO0 timing\n",t32);
	pci_write_config_dword(dev, 0x44, 0x03e4);
#ifndef CONFIG_MBX
	pci_read_config_dword(dev, 0x40, &t32);
	printk("IDE control/status register: %08x\n",t32);
	pci_write_config_dword(dev, 0x40, 0x10ff08a1);
#endif /* CONFIG_MBX */
#endif
}
