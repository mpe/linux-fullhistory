/*
 * linux/arch/arm/drivers/block/ide-rapide.c
 *
 * Copyright (c) 1996-1998 Russell King.
 *
 * Changelog:
 *  08-06-1996	RMK	Created
 *  13-04-1998	RMK	Added manufacturer and product IDs
 */

#include <linux/module.h>
#include <linux/malloc.h>
#include <linux/blkdev.h>
#include <linux/errno.h>
#include <asm/ecard.h>
#include <asm/ide.h>

#include "../../block/ide.h"

static const card_ids rapide_cids[] = {
	{ MANU_YELLOWSTONE, PROD_YELLOWSTONE_RAPIDE32 },
	{ 0xffff, 0xffff }
};

static struct expansion_card *ec[MAX_ECARDS];
static int result[MAX_ECARDS];

static inline int rapide_register(struct expansion_card *ec)
{
	unsigned long port = ecard_address (ec, ECARD_MEMC, 0);
	ide_ioregspec_t spec;

	spec.base = port;
	spec.ctrl = port + 0x206;
	spec.offset = 1 << 4;
	spec.irq = ec->irq;

	return ide_register_port(&spec);
}

int rapide_init(void)
{
	int i;

	for (i = 0; i < MAX_ECARDS; i++)
		ec[i] = NULL;

	ecard_startfind();

	for (i = 0; ; i++) {
		if ((ec[i] = ecard_find(0, rapide_cids)) == NULL)
			break;

		ecard_claim(ec[i]);
		result[i] = rapide_register(ec[i]);
	}
	for (i = 0; i < MAX_ECARDS; i++)
		if (ec[i] && result[i] < 0) {
			ecard_release(ec[i]);
			ec[i] = NULL;
	}
	return 0;
}

#ifdef MODULE

int init_module (void)
{
	return rapide_init();
}

void cleanup_module (void)
{
	int i;

	for (i = 0; i < MAX_ECARDS; i++)
		if (ec[i]) {
			unsigned long port;
			port = ecard_address(ec[i], ECARD_MEMC, 0);

			ide_unregister_port(port, ec[i]->irq, 16);
			ecard_release(ec[i]);
			ec[i] = NULL;
		}
}
#endif

