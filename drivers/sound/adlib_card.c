/*
 * sound/adlib_card.c
 *
 * Detection routine for the AdLib card.
 *
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 */

#include <linux/module.h>
#include <linux/init.h>

#include "sound_config.h"
#include "soundmodule.h"

#include "opl3.h"

static void __init attach_adlib_card(struct address_info *hw_config)
{
	hw_config->slots[0] = opl3_init(hw_config->io_base, hw_config->osp);
	request_region(hw_config->io_base, 4, "OPL3/OPL2");
}

static int __init probe_adlib(struct address_info *hw_config)
{
	if (check_region(hw_config->io_base, 4)) {
		DDB(printk("opl3.c: I/O port %x already in use\n", hw_config->io_base));
		return 0;
	}
	return opl3_detect(hw_config->io_base, hw_config->osp);
}

static struct address_info cfg;

static int __initdata io = -1;

MODULE_PARM(io, "i");

static int __init init_adlib(void)
{
	cfg.io_base = io;

	if (cfg.io_base == -1) {
		printk(KERN_ERR "adlib: must specify I/O address.\n");
		return -EINVAL;
	}
	if (probe_adlib(&cfg) == 0)
		return -ENODEV;
	attach_adlib_card(&cfg);
	SOUND_LOCK;
	return 0;
}

static void __exit cleanup_adlib(void)
{
	release_region(cfg.io_base, 4);
	sound_unload_synthdev(cfg.slots[0]);
	
	SOUND_LOCK_END;
}

module_init(init_adlib);
module_exit(cleanup_adlib);

#ifndef MODULE
static int __init setup_adlib(char *str)
{
        /* io */
	int ints[2];
	str = get_options(str, ARRAY_SIZE(ints), ints);
	
	io = ints[1];

	return 1;
}
__setup("adlib=", setup_adlib);
#endif
