/*
 * linux/drivers/ide/icside.c
 *
 * Copyright (c) 1996,1997 Russell King.
 *
 * Changelog:
 *  08-Jun-1996	RMK	Created
 *  12-Sep-1997	RMK	Added interrupt enable/disable
 *  17-Apr-1999	RMK	Added support for V6 EASI
 *  22-May-1999	RMK	Added support for V6 DMA
 */

#include <linux/config.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/errno.h>
#include <linux/hdreg.h>
#include <linux/ide.h>
#include <linux/pci.h>
#include <linux/init.h>

#include <asm/dma.h>
#include <asm/ecard.h>
#include <asm/io.h>

extern char *ide_xfer_verbose (byte xfer_rate);

/*
 * Maximum number of interfaces per card
 */
#define MAX_IFS	2

#define ICS_IDENT_OFFSET		0x8a0

#define ICS_ARCIN_V5_INTRSTAT		0x000
#define ICS_ARCIN_V5_INTROFFSET		0x001
#define ICS_ARCIN_V5_IDEOFFSET		0xa00
#define ICS_ARCIN_V5_IDEALTOFFSET	0xae0
#define ICS_ARCIN_V5_IDESTEPPING	4

#define ICS_ARCIN_V6_IDEOFFSET_1	0x800
#define ICS_ARCIN_V6_INTROFFSET_1	0x880
#define ICS_ARCIN_V6_INTRSTAT_1		0x8a4
#define ICS_ARCIN_V6_IDEALTOFFSET_1	0x8e0
#define ICS_ARCIN_V6_IDEOFFSET_2	0xc00
#define ICS_ARCIN_V6_INTROFFSET_2	0xc80
#define ICS_ARCIN_V6_INTRSTAT_2		0xca4
#define ICS_ARCIN_V6_IDEALTOFFSET_2	0xce0
#define ICS_ARCIN_V6_IDESTEPPING	4

struct cardinfo {
	unsigned int dataoffset;
	unsigned int ctrloffset;
	unsigned int stepping;
};

static struct cardinfo icside_cardinfo_v5 = {
	ICS_ARCIN_V5_IDEOFFSET,
	ICS_ARCIN_V5_IDEALTOFFSET,
	ICS_ARCIN_V5_IDESTEPPING
};

static struct cardinfo icside_cardinfo_v6_1 = {
	ICS_ARCIN_V6_IDEOFFSET_1,
	ICS_ARCIN_V6_IDEALTOFFSET_1,
	ICS_ARCIN_V6_IDESTEPPING
};

static struct cardinfo icside_cardinfo_v6_2 = {
	ICS_ARCIN_V6_IDEOFFSET_2,
	ICS_ARCIN_V6_IDEALTOFFSET_2,
	ICS_ARCIN_V6_IDESTEPPING
};

static const card_ids icside_cids[] = {
	{ MANU_ICS,  PROD_ICS_IDE  },
	{ MANU_ICS2, PROD_ICS2_IDE },
	{ 0xffff, 0xffff }
};

typedef enum {
	ics_if_unknown,
	ics_if_arcin_v5,
	ics_if_arcin_v6
} iftype_t;

/* ---------------- Version 5 PCB Support Functions --------------------- */
/* Prototype: icside_irqenable_arcin_v5 (struct expansion_card *ec, int irqnr)
 * Purpose  : enable interrupts from card
 */
static void icside_irqenable_arcin_v5 (struct expansion_card *ec, int irqnr)
{
	unsigned int memc_port = (unsigned int)ec->irq_data;
	outb (0, memc_port + ICS_ARCIN_V5_INTROFFSET);
}

/* Prototype: icside_irqdisable_arcin_v5 (struct expansion_card *ec, int irqnr)
 * Purpose  : disable interrupts from card
 */
static void icside_irqdisable_arcin_v5 (struct expansion_card *ec, int irqnr)
{
	unsigned int memc_port = (unsigned int)ec->irq_data;
	inb (memc_port + ICS_ARCIN_V5_INTROFFSET);
}

static const expansioncard_ops_t icside_ops_arcin_v5 = {
	icside_irqenable_arcin_v5,
	icside_irqdisable_arcin_v5,
	NULL,
	NULL,
	NULL,
	NULL
};


/* ---------------- Version 6 PCB Support Functions --------------------- */
/* Prototype: icside_irqenable_arcin_v6 (struct expansion_card *ec, int irqnr)
 * Purpose  : enable interrupts from card
 */
static void icside_irqenable_arcin_v6 (struct expansion_card *ec, int irqnr)
{
	unsigned int ide_base_port = (unsigned int)ec->irq_data;

	outb (0, ide_base_port + ICS_ARCIN_V6_INTROFFSET_1);
	outb (0, ide_base_port + ICS_ARCIN_V6_INTROFFSET_2);
}

/* Prototype: icside_irqdisable_arcin_v6 (struct expansion_card *ec, int irqnr)
 * Purpose  : disable interrupts from card
 */
static void icside_irqdisable_arcin_v6 (struct expansion_card *ec, int irqnr)
{
	unsigned int ide_base_port = (unsigned int)ec->irq_data;

	inb (ide_base_port + ICS_ARCIN_V6_INTROFFSET_1);
	inb (ide_base_port + ICS_ARCIN_V6_INTROFFSET_2);
}

/* Prototype: icside_irqprobe(struct expansion_card *ec)
 * Purpose  : detect an active interrupt from card
 */
static int icside_irqpending_arcin_v6(struct expansion_card *ec)
{
	unsigned int ide_base_port = (unsigned int)ec->irq_data;

	return inb(ide_base_port + ICS_ARCIN_V6_INTRSTAT_1) & 1 ||
	       inb(ide_base_port + ICS_ARCIN_V6_INTRSTAT_2) & 1;
}

static const expansioncard_ops_t icside_ops_arcin_v6 = {
	icside_irqenable_arcin_v6,
	icside_irqdisable_arcin_v6,
	icside_irqpending_arcin_v6,
	NULL,
	NULL,
	NULL
};

/* Prototype: icside_identifyif (struct expansion_card *ec)
 * Purpose  : identify IDE interface type
 * Notes    : checks the description string
 */
static iftype_t __init icside_identifyif (struct expansion_card *ec)
{
	unsigned int addr;
	iftype_t iftype;
	int id = 0;

	iftype = ics_if_unknown;

	addr = ecard_address (ec, ECARD_IOC, ECARD_FAST) + ICS_IDENT_OFFSET;

	id = inb (addr) & 1;
	id |= (inb (addr + 1) & 1) << 1;
	id |= (inb (addr + 2) & 1) << 2;
	id |= (inb (addr + 3) & 1) << 3;

	switch (id) {
	case 0: /* A3IN */
		printk("icside: A3IN unsupported\n");
		break;

	case 1: /* A3USER */
		printk("icside: A3USER unsupported\n");
		break;

	case 3:	/* ARCIN V6 */
		printk(KERN_DEBUG "icside: detected ARCIN V6 in slot %d\n", ec->slot_no);
		iftype = ics_if_arcin_v6;
		break;

	case 15:/* ARCIN V5 (no id) */
		printk(KERN_DEBUG "icside: detected ARCIN V5 in slot %d\n", ec->slot_no);
		iftype = ics_if_arcin_v5;
		break;

	default:/* we don't know - complain very loudly */
		printk("icside: ***********************************\n");
		printk("icside: *** UNKNOWN ICS INTERFACE id=%d ***\n", id);
		printk("icside: ***********************************\n");
		printk("icside: please report this to linux@arm.linux.org.uk\n");
		printk("icside: defaulting to ARCIN V5\n");
		iftype = ics_if_arcin_v5;
		break;
	}

	return iftype;
}

#ifdef CONFIG_BLK_DEV_IDEDMA_ICS
/*
 * SG-DMA support.
 *
 * Similar to the BM-DMA, but we use the RiscPCs IOMD DMA controllers.
 * There is only one DMA controller per card, which means that only
 * one drive can be accessed at one time.  NOTE! We do not enforce that
 * here, but we rely on the main IDE driver spotting that both
 * interfaces use the same IRQ, which should guarantee this.
 */
#define NR_ENTRIES 256
#define TABLE_SIZE (NR_ENTRIES * 8)

static int ide_build_sglist(struct ata_channel *hwif, struct request *rq)
{
	request_queue_t *q = &hwif->drives[DEVICE_NR(rq->rq_dev) & 1].queue;
	struct scatterlist *sg = hwif->sg_table;
	int nents = blk_rq_map_sg(q, rq, sg);

	if (rq->q && nents > rq->nr_phys_segments)
		printk("icside: received %d segments, build %d\n",
			rq->nr_phys_segments, nents);

	if (rq_data_dir(rq) == READ)
		hwif->sg_dma_direction = PCI_DMA_FROMDEVICE;
	else
		hwif->sg_dma_direction = PCI_DMA_TODEVICE;

	return pci_map_sg(NULL, sg, nents, hwif->sg_dma_direction);
}

static int
icside_udma_new_table(struct ata_channel *ch, struct request *rq)
{
	return ch->sg_nents = ide_build_sglist(ch, rq);
}

/* Teardown mappings after DMA has completed.  */
static void icside_destroy_dmatable(ide_drive_t *drive)
{
	struct scatterlist *sg = drive->channel->sg_table;
	int nents = drive->channel->sg_nents;

	pci_unmap_sg(NULL, sg, nents, drive->channel->sg_dma_direction);
}

/*
 * Configure the IOMD to give the appropriate timings for the transfer
 * mode being requested.  We take the advice of the ATA standards, and
 * calculate the cycle time based on the transfer mode, and the EIDE
 * MW DMA specs that the drive provides in the IDENTIFY command.
 *
 * We have the following IOMD DMA modes to choose from:
 *
 *	Type	Active		Recovery	Cycle
 *	A	250 (250)	312 (550)	562 (800)
 *	B	187		250		437
 *	C	125 (125)	125 (375)	250 (500)
 *	D	62		125		187
 *
 * (figures in brackets are actual measured timings)
 *
 * However, we also need to take care of the read/write active and
 * recovery timings:
 *
 *			Read	Write
 *  	Mode	Active	-- Recovery --	Cycle	IOMD type
 *	MW0	215	50	215	480	A
 *	MW1	80	50	50	150	C
 *	MW2	70	25	25	120	C
 */
static int
icside_config_if(ide_drive_t *drive, int xfer_mode)
{
	int func = ide_dma_off;
	int cycle_time = 0, use_dma_info = 0;

	switch (xfer_mode) {
	case XFER_MW_DMA_2: cycle_time = 250; use_dma_info = 1;	break;
	case XFER_MW_DMA_1: cycle_time = 250; use_dma_info = 1;	break;
	case XFER_MW_DMA_0: cycle_time = 480;			break;
	}

	/*
	 * If we're going to be doing MW_DMA_1 or MW_DMA_2, we should
	 * take care to note the values in the ID...
	 */
	if (use_dma_info && drive->id->eide_dma_time > cycle_time)
		cycle_time = drive->id->eide_dma_time;

	drive->drive_data = cycle_time;

	if (!drive->init_speed)
		drive->init_speed = xfer_mode;

	if (cycle_time && ide_config_drive_speed(drive, xfer_mode) == 0)
		func = ide_dma_on;
	else
		drive->drive_data = 480;

	printk("%s: %s selected (peak %dMB/s)\n", drive->name,
		ide_xfer_verbose(xfer_mode), 2000 / drive->drive_data);

	drive->current_speed = xfer_mode;

	return func;
}

static int
icside_set_speed(ide_drive_t *drive, byte speed)
{
	return icside_config_if(drive, speed);
}

/*
 * dma_intr() is the handler for disk read/write DMA interrupts
 */
static ide_startstop_t icside_dmaintr(struct ata_device *drive, struct request *rq)
{
	int i;
	byte stat, dma_stat;

	dma_stat = drive->channel->udma(ide_dma_end, drive, rq);
	stat = GET_STAT();			/* get drive status */
	if (OK_STAT(stat,DRIVE_READY,drive->bad_wstat|DRQ_STAT)) {
		if (!dma_stat) {
			for (i = rq->nr_sectors; i > 0;) {
				i -= rq->current_nr_sectors;
				ide_end_request(drive, 1);
			}
			return ide_stopped;
		}
		printk("%s: dma_intr: bad DMA status (dma_stat=%x)\n",
		       drive->name, dma_stat);
	}
	return ide_error(drive, "dma_intr", stat);
}


static int
icside_dma_check(struct ata_device *drive, struct request *rq)
{
	struct hd_driveid *id = drive->id;
	struct ata_channel *hwif = drive->channel;
	int autodma = hwif->autodma;
	int xfer_mode = XFER_PIO_2;
	int func = ide_dma_off_quietly;

	if (!id || !(id->capability & 1) || !autodma)
		goto out;

	/*
	 * Enable DMA on any drive that has multiword DMA
	 */
	if (id->field_valid & 2) {
		if (id->dma_mword & 4) {
			xfer_mode = XFER_MW_DMA_2;
			func = ide_dma_on;
		} else if (id->dma_mword & 2) {
			xfer_mode = XFER_MW_DMA_1;
			func = ide_dma_on;
		} else if (id->dma_mword & 1) {
			xfer_mode = XFER_MW_DMA_0;
			func = ide_dma_on;
		}
		goto out;
	}

out:
	func = icside_config_if(drive, xfer_mode);

	return hwif->udma(func, drive, rq);
}

static int
icside_dmaproc(ide_dma_action_t func, ide_drive_t *drive)
{
	struct ata_channel *hwif = drive->channel;
	int count, reading = 0;

	switch (func) {
	case ide_dma_off:
		printk("%s: DMA disabled\n", drive->name);
		/*FALLTHROUGH*/

	case ide_dma_off_quietly:
	case ide_dma_on:
		/*
		 * We don't need any bouncing.  Yes, this looks the
		 * wrong way around, but it is correct.
		 */
		blk_queue_bounce_limit(&drive->queue, BLK_BOUNCE_ANY);
		drive->using_dma = (func == ide_dma_on);
		return 0;

	case ide_dma_check:
		return icside_dma_check(drive, rq);

	case ide_dma_read:
		reading = 1;
	case ide_dma_write:
		count = icside_udma_new_table(hwif, rq);
		if (!count)
			return 1;
		disable_dma(hwif->hw.dma);

		/* Route the DMA signals to
		 * to the correct interface.
		 */
		outb(hwif->select_data, hwif->config_data);

		/* Select the correct timing
		 * for this drive
		 */
		set_dma_speed(hwif->hw.dma, drive->drive_data);

		set_dma_sg(hwif->hw.dma, drive->channel->sg_table, count);
		set_dma_mode(hwif->hw.dma, reading ? DMA_MODE_READ
			     : DMA_MODE_WRITE);

		drive->waiting_for_dma = 1;
		if (drive->type != ATA_DISK)
			return 0;

		ide_set_handler(drive, icside_dmaintr, WAIT_CMD, NULL);
		OUT_BYTE(reading ? WIN_READDMA : WIN_WRITEDMA,
			 IDE_COMMAND_REG);

	case ide_dma_begin:
		enable_dma(hwif->hw.dma);
		return 0;

	case ide_dma_end:
		drive->waiting_for_dma = 0;
		disable_dma(hwif->hw.dma);
		icside_destroy_dmatable(drive);
		return get_dma_residue(hwif->hw.dma) != 0;

	case ide_dma_test_irq:
		return inb((unsigned long)hwif->hw.priv) & 1;

	case ide_dma_timeout:
	default:
		printk("icside_dmaproc: unsupported function: %d\n", func);
	}
	return 1;
}

static int
icside_setup_dma(struct ata_channel *hwif, int autodma)
{
	printk("    %s: SG-DMA", hwif->name);

	hwif->sg_table = kmalloc(sizeof(struct scatterlist) * NR_ENTRIES,
				 GFP_KERNEL);
	if (!hwif->sg_table)
		goto failed;

	hwif->dmatable_cpu = NULL;
	hwif->dmatable_dma = 0;
	hwif->speedproc = icside_set_speed;
	hwif->dmaproc = icside_dmaproc;
	hwif->autodma = autodma;

	printk(" capable%s\n", autodma ?
		", auto-enable" : "");

	return 1;

failed:
	printk(" -- ERROR, unable to allocate DMA table\n");
	return 0;
}

void ide_release_dma(struct ata_channel *hwif)
{
	if (hwif->sg_table) {
		kfree(hwif->sg_table);
		hwif->sg_table = NULL;
	}
}
#endif

static struct ata_channel *
icside_find_hwif(unsigned long dataport)
{
	struct ata_channel *hwif;
	int index;

	for (index = 0; index < MAX_HWIFS; ++index) {
		hwif = &ide_hwifs[index];
		if (hwif->io_ports[IDE_DATA_OFFSET] == (ide_ioreg_t)dataport)
			goto found;
	}

	for (index = 0; index < MAX_HWIFS; ++index) {
		hwif = &ide_hwifs[index];
		if (!hwif->io_ports[IDE_DATA_OFFSET])
			goto found;
	}

	return NULL;
found:
	return hwif;
}

static struct ata_channel *
icside_setup(unsigned long base, struct cardinfo *info, int irq)
{
	unsigned long port = base + info->dataoffset;
	struct ata_channel *hwif;

	hwif = icside_find_hwif(base);
	if (hwif) {
		int i;

		memset(&hwif->hw, 0, sizeof(hw_regs_t));

		for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++) {
			hwif->hw.io_ports[i] = (ide_ioreg_t)port;
			hwif->io_ports[i] = (ide_ioreg_t)port;
			port += 1 << info->stepping;
		}
		hwif->hw.io_ports[IDE_CONTROL_OFFSET] = base + info->ctrloffset;
		hwif->io_ports[IDE_CONTROL_OFFSET] = base + info->ctrloffset;
		hwif->hw.irq  = irq;
		hwif->irq     = irq;
		hwif->hw.dma  = NO_DMA;
		hwif->noprobe = 0;
		hwif->chipset = ide_acorn;
	}

	return hwif;
}

static int __init icside_register_v5(struct expansion_card *ec, int autodma)
{
	unsigned long slot_port;
	struct ata_channel *hwif;

	slot_port = ecard_address(ec, ECARD_MEMC, 0);

	ec->irqaddr  = (unsigned char *)ioaddr(slot_port + ICS_ARCIN_V5_INTRSTAT);
	ec->irqmask  = 1;
	ec->irq_data = (void *)slot_port;
	ec->ops      = (expansioncard_ops_t *)&icside_ops_arcin_v5;

	/*
	 * Be on the safe side - disable interrupts
	 */
	inb(slot_port + ICS_ARCIN_V5_INTROFFSET);

	hwif = icside_setup(slot_port, &icside_cardinfo_v5, ec->irq);

	return hwif ? 0 : -1;
}

static int __init icside_register_v6(struct expansion_card *ec, int autodma)
{
	unsigned long slot_port, port;
	struct ata_channel *hwif;
	struct ata_channel *mate;
	int sel = 0;

	slot_port = ecard_address(ec, ECARD_IOC, ECARD_FAST);
	port      = ecard_address(ec, ECARD_EASI, ECARD_FAST);

	if (port == 0)
		port = slot_port;
	else
		sel = 1 << 5;

	outb(sel, slot_port);

	ec->irq_data = (void *)port;
	ec->ops      = (expansioncard_ops_t *)&icside_ops_arcin_v6;

	/*
	 * Be on the safe side - disable interrupts
	 */
	inb(port + ICS_ARCIN_V6_INTROFFSET_1);
	inb(port + ICS_ARCIN_V6_INTROFFSET_2);

	hwif = icside_setup(port, &icside_cardinfo_v6_1, ec->irq);
	mate = icside_setup(port, &icside_cardinfo_v6_2, ec->irq);

#ifdef CONFIG_BLK_DEV_IDEDMA_ICS
	if (ec->dma != NO_DMA) {
		if (request_dma(ec->dma, hwif->name))
			goto no_dma;

		if (hwif) {
			hwif->config_data = slot_port;
			hwif->select_data = sel;
			hwif->hw.dma  = ec->dma;
			hwif->hw.priv = (void *)
					(port + ICS_ARCIN_V6_INTRSTAT_1);
			hwif->unit = 0;
			icside_setup_dma(hwif, autodma);
		}
		if (mate) {
			mate->config_data = slot_port;
			mate->select_data = sel | 1;
			mate->hw.dma  = ec->dma;
			mate->hw.priv = (void *)
					(port + ICS_ARCIN_V6_INTRSTAT_2);
			mate->unit = 1;
			icside_setup_dma(mate, autodma);
		}
	}
no_dma:
#endif
	return hwif || mate ? 0 : -1;
}

int __init icside_init(void)
{
	int autodma = 0;

#ifdef CONFIG_IDEDMA_ICS_AUTO
	autodma = 1;
#endif

	ecard_startfind ();

	do {
		struct expansion_card *ec;
		int result;

		ec = ecard_find(0, icside_cids);
		if (ec == NULL)
			break;

		ecard_claim(ec);

		switch (icside_identifyif(ec)) {
		case ics_if_arcin_v5:
			result = icside_register_v5(ec, autodma);
			break;

		case ics_if_arcin_v6:
			result = icside_register_v6(ec, autodma);
			break;

		default:
			result = -1;
			break;
		}

		if (result)
			ecard_release(ec);
	} while (1);

	return 0;
}
