/*
 * sound/adlib_card.c
 *
 * Detection routine for the AdLib card.
 */

/*
 * Copyright (C) by Hannu Savolainen 1993-1996
 *
 * USS/Lite for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 */
#include <linux/config.h>


#include "sound_config.h"

#if defined(CONFIG_YM3812)

void
attach_adlib_card (struct address_info *hw_config)
{

  opl3_init (hw_config->io_base, hw_config->osp);
  request_region (hw_config->io_base, 4, "OPL3/OPL2");
}

int
probe_adlib (struct address_info *hw_config)
{

  if (check_region (hw_config->io_base, 4))
    {
      printk ("\n\nopl3.c: I/O port %x already in use\n\n", hw_config->io_base);
      return 0;
    }

  return opl3_detect (hw_config->io_base, hw_config->osp);
}

void
unload_adlib (struct address_info *hw_config)
{
  release_region (hw_config->io_base, 4);
}


#endif
