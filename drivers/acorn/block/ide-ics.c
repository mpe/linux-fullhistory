/*
 * linux/arch/arm/drivers/block/ide-ics.c
 *
 * Copyright (c) 1996,1997 Russell King.
 *
 * Changelog:
 *  08-06-1996	RMK	Created
 *  12-09-1997	RMK	Added interrupt enable/disable
 */

#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/malloc.h>
#include <linux/blkdev.h>
#include <linux/errno.h>
#include <linux/hdreg.h>

#include <asm/ecard.h>
#include <asm/io.h>

#include "../../block/ide.h"

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

static const card_ids icside_cids[] = {
	{ MANU_ICS, PROD_ICS_IDE },
	{ 0xffff, 0xffff }
};

typedef enum {
	ics_if_unknown,
	ics_if_arcin_v5,
	ics_if_arcin_v6
} iftype_t;

static struct expansion_card *ec[MAX_ECARDS];
static int result[MAX_ECARDS][MAX_IFS];


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

static const expansioncard_ops_t icside_ops_arcin_v6 = {
	icside_irqenable_arcin_v6,
	icside_irqdisable_arcin_v6,
	NULL,
	NULL
};



/* Prototype: icside_identifyif (struct expansion_card *ec)
 * Purpose  : identify IDE interface type
 * Notes    : checks the description string
 */
static iftype_t icside_identifyif (struct expansion_card *ec)
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
		printk ("icside: A3IN unsupported\n");
		break;

	case 1: /* A3USER */
		printk ("icside: A3USER unsupported\n");
		break;

	case 3:	/* ARCIN V6 */
		printk ("icside: detected ARCIN V6 in slot %d\n", ec->slot_no);
		iftype = ics_if_arcin_v6;
		break;

	case 15:/* ARCIN V5 (no id) */
		printk ("icside: detected ARCIN V5 in slot %d\n", ec->slot_no);
		iftype = ics_if_arcin_v5;
		break;

	default:/* we don't know - complain very loudly */
		printk ("icside: ***********************************\n");
		printk ("icside: *** UNKNOWN ICS INTERFACE id=%d ***\n", id);
		printk ("icside: ***********************************\n");
		printk ("icside: please report this to: linux@arm.uk.linux.org\n");
		printk ("icside: defaulting to ARCIN V5\n");
		iftype = ics_if_arcin_v5;
		break;
	}

	return iftype;
}

static int icside_register_port(unsigned long dataport, unsigned long ctrlport, int stepping, int irq)
{
	hw_regs_t hw;
	int i;

	memset(&hw, 0, sizeof(hw));

	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++) {
		hw.io_ports[i] = (ide_ioreg_t)dataport;
		dataport += 1 << stepping;
	}
	hw.io_ports[IDE_CONTROL_OFFSET] = ctrlport;
	hw.irq = irq;

	return ide_register_hw(&hw, NULL);
}

/* Prototype: icside_register (struct expansion_card *ec)
 * Purpose  : register an ICS IDE card with the IDE driver
 * Notes    : we make sure that interrupts are disabled from the card
 */
static inline void icside_register (struct expansion_card *ec, int index)
{
	unsigned long port;

	result[index][0] = -1;
	result[index][1] = -1;

	switch (icside_identifyif (ec)) {
	case ics_if_unknown:
	default:
		printk ("** Warning: ICS IDE Interface unrecognised! **\n");
		break;

	case ics_if_arcin_v5:
		port = ecard_address (ec, ECARD_MEMC, 0);
		ec->irqaddr = ioaddr(port + ICS_ARCIN_V5_INTRSTAT);
		ec->irqmask = 1;
		ec->irq_data = (void *)port;
		ec->ops = (expansioncard_ops_t *)&icside_ops_arcin_v5;

		/*
		 * Be on the safe side - disable interrupts
		 */
		inb (port + ICS_ARCIN_V5_INTROFFSET);
		result[index][0] = icside_register_port(port + ICS_ARCIN_V5_IDEOFFSET,
							port + ICS_ARCIN_V5_IDEALTOFFSET,
							ICS_ARCIN_V5_IDESTEPPING,
							ec->irq);
		result[index][1] = -1;
		break;

	case ics_if_arcin_v6:
		port = ecard_address (ec, ECARD_IOC, ECARD_FAST);
		ec->irqaddr = ioaddr(port + ICS_ARCIN_V6_INTRSTAT_1);
		ec->irqmask = 1;
		ec->irq_data = (void *)port;
		ec->ops = (expansioncard_ops_t *)&icside_ops_arcin_v6;

		/*
		 * Be on the safe side - disable interrupts
		 */
		inb (port + ICS_ARCIN_V6_INTROFFSET_1);
		inb (port + ICS_ARCIN_V6_INTROFFSET_2);

		result[index][0] = icside_register_port(port + ICS_ARCIN_V6_IDEOFFSET_1,
							port + ICS_ARCIN_V6_IDEALTOFFSET_1,
							ICS_ARCIN_V6_IDESTEPPING,
							ec->irq);
		result[index][1] = icside_register_port(port + ICS_ARCIN_V6_IDEOFFSET_2,
							port + ICS_ARCIN_V6_IDEALTOFFSET_2,
							ICS_ARCIN_V6_IDESTEPPING,
							ec->irq);
		break;
	}		
}

int icside_init (void)
{
	int i;

	for (i = 0; i < MAX_ECARDS; i++)
		ec[i] = NULL;

	ecard_startfind ();

	for (i = 0; ; i++) {
		if ((ec[i] = ecard_find (0, icside_cids)) == NULL)
			break;

		ecard_claim (ec[i]);
		icside_register (ec[i], i);
	}

	for (i = 0; i < MAX_ECARDS; i++)
		if (ec[i] && result[i][0] < 0 && result[i][1] < 0) {
			ecard_release (ec[i]);
			ec[i] = NULL;
		}
	return 0;
}

#ifdef MODULE
int init_module (void)
{
	return icside_init();
}

void cleanup_module (void)
{
	int i;

	for (i = 0; i < MAX_ECARDS; i++)
		if (ec[i]) {
			if (result[i][0] >= 0)
				ide_unregister (result[i][0]);

			if (result[i][1] >= 0)
				ide_unregister (result[i][1]);
				
			ecard_release (ec[i]);
			ec[i] = NULL;
		}
}
#endif

