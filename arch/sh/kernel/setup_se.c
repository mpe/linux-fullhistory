/* $Id: setup_se.c,v 1.6 2000/05/14 08:41:25 gniibe Exp $
 *
 * linux/arch/sh/kernel/setup_se.c
 *
 * Copyright (C) 2000  Kazumoto Kojima
 *
 * Hitachi SolutionEngine Support.
 *
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/irq.h>

#include <linux/hdreg.h>
#include <linux/ide.h>
#include <asm/io.h>
#include <asm/hitachi_se.h>
#include <asm/smc37c93x.h>

/*
 * Configure the Super I/O chip
 */
static void __init smsc_config(int index, int data)
{
	outb_p(index, INDEX_PORT);
	outb_p(data, DATA_PORT);
}

static void __init init_smsc(void)
{
	outb_p(CONFIG_ENTER, CONFIG_PORT);
	outb_p(CONFIG_ENTER, CONFIG_PORT);

	/* FDC */
	smsc_config(CURRENT_LDN_INDEX, LDN_FDC);
	smsc_config(ACTIVATE_INDEX, 0x01);
	smsc_config(IRQ_SELECT_INDEX, 6); /* IRQ6 */

	/* IDE1 */
	smsc_config(CURRENT_LDN_INDEX, LDN_IDE1);
	smsc_config(ACTIVATE_INDEX, 0x01);
	smsc_config(IRQ_SELECT_INDEX, 14); /* IRQ14 */

	/* AUXIO (GPIO): to use IDE1 */
	smsc_config(CURRENT_LDN_INDEX, LDN_AUXIO);
	smsc_config(GPIO46_INDEX, 0x00); /* nIOROP */
	smsc_config(GPIO47_INDEX, 0x00); /* nIOWOP */

	/* COM1 */
	smsc_config(CURRENT_LDN_INDEX, LDN_COM1);
	smsc_config(ACTIVATE_INDEX, 0x01);
	smsc_config(IO_BASE_HI_INDEX, 0x03);
	smsc_config(IO_BASE_LO_INDEX, 0xf8);
	smsc_config(IRQ_SELECT_INDEX, 3); /* IRQ3 */

	/* RTC */
	smsc_config(CURRENT_LDN_INDEX, LDN_RTC);
	smsc_config(ACTIVATE_INDEX, 0x01);
	smsc_config(IRQ_SELECT_INDEX, 8); /* IRQ8 */

	/* XXX: COM2, PARPORT, KBD, and MOUSE will come here... */
	outb_p(CONFIG_EXIT, CONFIG_PORT);
}

/*
 * Initialize IRQ setting
 */
static void __init init_se_IRQ(void)
{
	int i;

	/*
	 * Super I/O (Just mimic PC):
	 *  1: keyboard
	 *  3: serial 0
	 *  4: serial 1
	 *  5: printer
	 *  6: floppy
	 *  8: rtc
	 * 12: mouse
	 * 14: ide0
	 */
	set_ipr_data(14, BCR_ILCRA, 2, 0x0f-14);
	set_ipr_data(12, BCR_ILCRA, 1, 0x0f-12); 
	set_ipr_data( 8, BCR_ILCRB, 1, 0x0f- 8); 
	set_ipr_data( 6, BCR_ILCRC, 3, 0x0f- 6);
	set_ipr_data( 5, BCR_ILCRC, 2, 0x0f- 5);
	set_ipr_data( 4, BCR_ILCRC, 1, 0x0f- 4);
	set_ipr_data( 3, BCR_ILCRC, 0, 0x0f- 3);
	set_ipr_data( 1, BCR_ILCRD, 3, 0x0f- 1);

	set_ipr_data(10, BCR_ILCRD, 1, 0x0f-10); /* LAN */

	set_ipr_data( 0, BCR_ILCRE, 3, 0x0f- 0); /* PCIRQ3 */
	set_ipr_data(11, BCR_ILCRE, 2, 0x0f-11); /* PCIRQ2 */
	set_ipr_data( 9, BCR_ILCRE, 1, 0x0f- 9); /* PCIRQ1 */
	set_ipr_data( 7, BCR_ILCRE, 0, 0x0f- 7); /* PCIRQ0 */

	/* #2, #13 are allocated for SLOT IRQ #1 and #2 (for now) */
	/* NOTE: #2 and #13 are not used on PC */
	set_ipr_data(13, BCR_ILCRG, 1, 0x0f-13); /* SLOTIRQ2 */
	set_ipr_data( 2, BCR_ILCRG, 0, 0x0f- 2); /* SLOTIRQ1 */

	for (i = 0; i < 15; i++) {
		make_ipr_irq(i);
	}
}

/*
 * Initialize the board
 */
int __init setup_se(void)
{
	init_se_IRQ();
	init_smsc();
	/* XXX: RTC setting comes here */

	printk(KERN_INFO "Hitach SolutionEngine Setup...done\n");
	return 0;
}

module_init(setup_se);
