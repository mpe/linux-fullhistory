/*
 * sound/dev_table.c
 *
 * Device call tables.
 *
 * Copyright by Hannu Savolainen 1993
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer. 2.
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#define _DEV_TABLE_C_
#include "sound_config.h"

#ifdef CONFIGURE_SOUNDCARD

int
snd_find_driver (int type)
{
  int             i, n = num_sound_drivers;

  for (i = 0; i < n; i++)
    if (sound_drivers[i].card_type == type)
      return i;

  return -1;			/*
				 * Not found
				 */
}

long
sndtable_init (long mem_start)
{
  int             i, n = num_sound_cards;
  int             drv;

  printk ("Sound initialization started\n");

/*
 * Check the number of cards actually defined in the table
 */

  for (i = 0; i < n && snd_installed_cards[i].card_type; i++)
    num_sound_cards = i + 1;

  for (i = 0; i < n && snd_installed_cards[i].card_type; i++)
    if (snd_installed_cards[i].enabled)
      {
	snd_installed_cards[i].for_driver_use = NULL;

	if ((drv = snd_find_driver (snd_installed_cards[i].card_type)) == -1)
	  snd_installed_cards[i].enabled = 0;	/*
						 * Mark as not detected
						 */
	else if (sound_drivers[drv].probe (&snd_installed_cards[i].config))
	  {
	    printk ("snd%d",
		    snd_installed_cards[i].card_type);

	    mem_start = sound_drivers[drv].attach (mem_start, &snd_installed_cards[i].config);
	    printk (" at 0x%x irq %d drq %d",
		    snd_installed_cards[i].config.io_base,
		    snd_installed_cards[i].config.irq,
		    snd_installed_cards[i].config.dma);
	    if (snd_installed_cards[i].config.dma2 != -1)
	      printk (",%d\n",
		      snd_installed_cards[i].config.dma2);
	    else
	      printk ("\n");
	  }
	else
	  snd_installed_cards[i].enabled = 0;	/*
						 * Mark as not detected
						 */
      }

  printk ("Sound initialization complete\n");
  return mem_start;
}

void
sound_unload_drivers (void)
{
  int             i, n = num_sound_cards;
  int             drv;

  printk ("Sound unload started\n");

  for (i = 0; i < n && snd_installed_cards[i].card_type; i++)
    if (snd_installed_cards[i].enabled)
      if ((drv = snd_find_driver (snd_installed_cards[i].card_type)) != -1)
	if (sound_drivers[drv].unload)
	  sound_drivers[drv].unload (&snd_installed_cards[i].config);

  printk ("Sound unload complete\n");
}

int
sndtable_probe (int unit, struct address_info *hw_config)
{
  int             i, sel = -1, n = num_sound_cards;

  if (!unit)
    return TRUE;

  for (i = 0; i < n && sel == -1 && snd_installed_cards[i].card_type; i++)
    if (snd_installed_cards[i].enabled)
      if (snd_installed_cards[i].card_type == unit)
	sel = i;

  if (sel == -1 && num_sound_cards < max_sound_cards)
    {
      int             i;

      i = sel = (num_sound_cards++);

      snd_installed_cards[sel].card_type = unit;
      snd_installed_cards[sel].enabled = 1;
    }

  if (sel != -1)
    {
      int             drv;

      snd_installed_cards[sel].for_driver_use = NULL;
      snd_installed_cards[sel].config.io_base = hw_config->io_base;
      snd_installed_cards[sel].config.irq = hw_config->irq;
      snd_installed_cards[sel].config.dma = hw_config->dma;
      snd_installed_cards[sel].config.dma2 = hw_config->dma2;
      if ((drv = snd_find_driver (snd_installed_cards[sel].card_type)) == -1)
	{
	  snd_installed_cards[sel].enabled = 0;
	}
      else if (sound_drivers[drv].probe (hw_config))
	return TRUE;

      snd_installed_cards[sel].enabled = 0;	/*
						 * Mark as not detected
						 */
      return FALSE;
    }

  return FALSE;
}

int
sndtable_init_card (int unit, struct address_info *hw_config)
{
  int             i, n = num_sound_cards;

  DDB (printk ("sndtable_init_card(%d) entered\n", unit));

  if (!unit)
    {
      if (sndtable_init (0) != 0)
	panic ("sound: Invalid memory allocation\n");
      return TRUE;
    }

  for (i = 0; i < n && snd_installed_cards[i].card_type; i++)
    if (snd_installed_cards[i].card_type == unit)
      {
	int             drv;

	snd_installed_cards[i].config.io_base = hw_config->io_base;
	snd_installed_cards[i].config.irq = hw_config->irq;
	snd_installed_cards[i].config.dma = hw_config->dma;
	snd_installed_cards[i].config.dma2 = hw_config->dma2;

	if ((drv = snd_find_driver (snd_installed_cards[i].card_type)) == -1)
	  snd_installed_cards[i].enabled = 0;	/*
						 * Mark as not detected
						 */
	else
	  {

	    DDB (printk ("Located card - calling attach routine\n"));
	    printk ("snd%d", unit);
	    if (sound_drivers[drv].attach (0, hw_config) != 0)
	      panic ("sound: Invalid memory allocation\n");

	    DDB (printk ("attach routine finished\n"));
	    printk (" at 0x%x irq %d drq %d",
		    snd_installed_cards[i].config.io_base,
		    snd_installed_cards[i].config.irq,
		    snd_installed_cards[i].config.dma);
	    if (snd_installed_cards[i].config.dma2 != -1)
	      printk (",%d\n",
		      snd_installed_cards[i].config.dma2);
	    else
	      printk ("\n");
	  }
	return TRUE;
      }

  printk ("sndtable_init_card: No card defined with type=%d, num cards: %d\n",
	  unit, num_sound_cards);
  return FALSE;
}

int
sndtable_get_cardcount (void)
{
  return num_audiodevs + num_mixers + num_synths + num_midis;
}

int
sndtable_identify_card (char *name)
{
  int             i, n = num_sound_drivers;

  if (name == NULL)
    return 0;

  for (i = 0; i < n; i++)
    if (sound_drivers[i].driver_id != NULL)
      {
	char           *id = sound_drivers[i].driver_id;
	int             j;

	for (j = 0; j < 80 && name[j] == id[j]; j++)
	  if (id[j] == 0 && name[j] == 0)	/* Match */
	    return sound_drivers[i].card_type;
      }

  return 0;
}

void
sound_setup (char *str, int *ints)
{
  int             i, n = num_sound_cards;

  /*
     * First disable all drivers
   */

  for (i = 0; i < n && snd_installed_cards[i].card_type; i++)
    snd_installed_cards[i].enabled = 0;

  if (ints[0] == 0 || ints[1] == 0)
    return;
  /*
     * Then enable them one by time
   */

  for (i = 1; i <= ints[0]; i++)
    {
      int             card_type, ioaddr, irq, dma, ptr, j;
      unsigned int    val;

      val = (unsigned int) ints[i];

      card_type = (val & 0x0ff00000) >> 20;

      if (card_type > 127)
	{
	  /*
	   * Add any future extensions here
	   */
	  return;
	}

      ioaddr = (val & 0x000fff00) >> 8;
      irq = (val & 0x000000f0) >> 4;
      dma = (val & 0x0000000f);

      ptr = -1;
      for (j = 0; j < n && ptr == -1; j++)
	if (snd_installed_cards[j].card_type == card_type &&
	    !snd_installed_cards[j].enabled)	/*
						 * Not already found
						 */
	  ptr = j;

      if (ptr == -1)
	printk ("Sound: Invalid setup parameter 0x%08x\n", val);
      else
	{
	  snd_installed_cards[ptr].enabled = 1;
	  snd_installed_cards[ptr].config.io_base = ioaddr;
	  snd_installed_cards[ptr].config.irq = irq;
	  snd_installed_cards[ptr].config.dma = dma;
	  snd_installed_cards[ptr].config.dma2 = -1;
	}
    }
}


struct address_info *
sound_getconf (int card_type)
{
  int             j, ptr;
  int             n = num_sound_cards;

  ptr = -1;
  for (j = 0; j < n && ptr == -1 && snd_installed_cards[j].card_type; j++)
    if (snd_installed_cards[j].card_type == card_type)
      ptr = j;

  if (ptr == -1)
    return (struct address_info *) NULL;

  return &snd_installed_cards[ptr].config;
}

#else

void
sound_setup (char *str, int *ints)
{
}

#endif
