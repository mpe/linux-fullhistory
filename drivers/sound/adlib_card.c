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


#include <linux/config.h>
#include <linux/module.h>
#include "sound_config.h"
#include "soundmodule.h"

#ifdef CONFIG_YM3812

void attach_adlib_card(struct address_info *hw_config)
{
	hw_config->slots[0] = opl3_init(hw_config->io_base, hw_config->osp);
	request_region(hw_config->io_base, 4, "OPL3/OPL2");
}

int probe_adlib(struct address_info *hw_config)
{
	if (check_region(hw_config->io_base, 4)) 
	{
		DDB(printk("opl3.c: I/O port %x already in use\n", hw_config->io_base));
		return 0;
	}
	return opl3_detect(hw_config->io_base, hw_config->osp);
}

void unload_adlib(struct address_info *hw_config)
{
	release_region(hw_config->io_base, 4);
	sound_unload_synthdev(hw_config->slots[0]);
}

#ifdef MODULE

int io = -1;
MODULE_PARM(io, "i");

EXPORT_NO_SYMBOLS;

struct address_info cfg;

int init_module(void)
{
	if (io == -1) {
		printk(KERN_ERR "adlib: must specify I/O address.\n");
		return -EINVAL;
	}
	cfg.io_base = io;
	if (probe_adlib(&cfg) == 0)
		return -ENODEV;
	attach_adlib_card(&cfg);
	SOUND_LOCK;
	return 0;
}

void cleanup_module(void)
{
	unload_adlib(&cfg);
	SOUND_LOCK_END;
}

#endif
#endif
