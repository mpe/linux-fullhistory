/*
 * sound/sb_card.c
 *
 * Detection routine for the SoundBlaster cards.
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

#if defined(CONFIG_SB)

#include "sb_mixer.h"
#include "sb.h"

void
attach_sb_card (struct address_info *hw_config)
{
#if defined(CONFIG_AUDIO) || defined(CONFIG_MIDI)
  sb_dsp_init (hw_config);
#endif
}

int
probe_sb (struct address_info *hw_config)
{
  if (check_region (hw_config->io_base, 16))
    {
      printk ("\n\nsb_dsp.c: I/O port %x already in use\n\n",
	      hw_config->io_base);
      return 0;
    }

  return sb_dsp_detect (hw_config);
}

void
unload_sb (struct address_info *hw_config)
{
  sb_dsp_unload (hw_config);
}

#endif
