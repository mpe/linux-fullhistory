/*
 * sound/gus_card.c
 *
 * Detection routine for the Gravis Ultrasound.
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

#if defined(CONFIG_GUS)

#include "gus_hw.h"

void            gusintr (int irq, void *dev_id, struct pt_regs *dummy);

int             gus_base, gus_irq, gus_dma;
extern int      gus_wave_volume;
extern int      gus_pcm_volume;
extern int      have_gus_max;
int             gus_pnp_flag = 0;

int            *gus_osp;

void
attach_gus_card (struct address_info *hw_config)
{
  int             io_addr;

  gus_osp = hw_config->osp;
  snd_set_irq_handler (hw_config->irq, gusintr, "Gravis Ultrasound", hw_config->osp);

  if (gus_wave_detect (hw_config->io_base))	/*
						 * Try first the default
						 */
    {
      gus_wave_init (hw_config);

      request_region (hw_config->io_base, 16, "GUS");
      request_region (hw_config->io_base + 0x100, 12, "GUS");	/* 0x10c-> is MAX */

      if (sound_alloc_dma (hw_config->dma, "GUS"))
	printk ("gus_card.c: Can't allocate DMA channel\n");
      if (hw_config->dma2 != -1 && hw_config->dma2 != hw_config->dma)
	if (sound_alloc_dma (hw_config->dma2, "GUS(2)"))
	  printk ("gus_card.c: Can't allocate DMA channel2\n");
#ifdef CONFIG_MIDI
      gus_midi_init ();
#endif
      return;
    }

#ifndef EXCLUDE_GUS_IODETECT

  /*
   * Look at the possible base addresses (0x2X0, X=1, 2, 3, 4, 5, 6)
   */

  for (io_addr = 0x210; io_addr <= 0x260; io_addr += 0x10)
    if (io_addr != hw_config->io_base)	/*
					 * Already tested
					 */
      if (gus_wave_detect (io_addr))
	{
	  hw_config->io_base = io_addr;

	  printk (" WARNING! GUS found at %x, config was %x ", io_addr, hw_config->io_base);
	  gus_wave_init (hw_config);
	  request_region (io_addr, 16, "GUS");
	  request_region (io_addr + 0x100, 12, "GUS");	/* 0x10c-> is MAX */
	  if (sound_alloc_dma (hw_config->dma, "GUS"))
	    printk ("gus_card.c: Can't allocate DMA channel\n");
	  if (hw_config->dma2 != -1 && hw_config->dma2 != hw_config->dma)
	    if (sound_alloc_dma (hw_config->dma2, "GUS"))
	      printk ("gus_card.c: Can't allocate DMA channel2\n");
#ifdef CONFIG_MIDI
	  gus_midi_init ();
#endif
	  return;
	}

#endif

}

int
probe_gus (struct address_info *hw_config)
{
  int             io_addr, irq;

  gus_osp = hw_config->osp;

  if (hw_config->card_subtype == 1)
    gus_pnp_flag = 1;

  irq = hw_config->irq;

  if (hw_config->card_subtype == 0)	/* GUS/MAX/ACE */
    if (irq != 3 && irq != 5 && irq != 7 && irq != 9 &&
	irq != 11 && irq != 12 && irq != 15)
      {
	printk ("GUS: Unsupported IRQ %d\n", irq);
	return 0;
      }

  if (check_region (hw_config->io_base, 16))
    printk ("GUS: I/O range conflict (1)\n");
  else if (check_region (hw_config->io_base + 0x100, 16))
    printk ("GUS: I/O range conflict (2)\n");
  else if (gus_wave_detect (hw_config->io_base))
    return 1;

#ifndef EXCLUDE_GUS_IODETECT

  /*
   * Look at the possible base addresses (0x2X0, X=1, 2, 3, 4, 5, 6)
   */

  for (io_addr = 0x210; io_addr <= 0x260; io_addr += 0x10)
    if (io_addr != hw_config->io_base)	/*
					 * Already tested
					 */
      if (!check_region (io_addr, 16))
	if (!check_region (io_addr + 0x100, 16))
	  if (gus_wave_detect (io_addr))
	    {
	      hw_config->io_base = io_addr;
	      return 1;
	    }

#endif

  return 0;
}

void
unload_gus (struct address_info *hw_config)
{
  DDB (printk ("unload_gus(%x)\n", hw_config->io_base));

  gus_wave_unload ();

  release_region (hw_config->io_base, 16);
  release_region (hw_config->io_base + 0x100, 12);	/* 0x10c-> is MAX */
  snd_release_irq (hw_config->irq);

  sound_free_dma (hw_config->dma);

  if (hw_config->dma2 != -1 && hw_config->dma2 != hw_config->dma)
    sound_free_dma (hw_config->dma2);
}

void
gusintr (int irq, void *dev_id, struct pt_regs *dummy)
{
  unsigned char   src;
  extern int      gus_timer_enabled;

  sti ();

#ifdef CONFIG_GUSMAX
  if (have_gus_max)
    ad1848_interrupt (irq, NULL, NULL);
#endif

  while (1)
    {
      if (!(src = inb (u_IrqStatus)))
	return;

      if (src & DMA_TC_IRQ)
	{
	  guswave_dma_irq ();
	}

      if (src & (MIDI_TX_IRQ | MIDI_RX_IRQ))
	{
#ifdef CONFIG_MIDI
	  gus_midi_interrupt (0);
#endif
	}

      if (src & (GF1_TIMER1_IRQ | GF1_TIMER2_IRQ))
	{
#ifdef CONFIG_SEQUENCER
	  if (gus_timer_enabled)
	    {
	      sound_timer_interrupt ();
	    }

	  gus_write8 (0x45, 0);	/* Ack IRQ */
	  gus_timer_command (4, 0x80);	/* Reset IRQ flags */

#else
	  gus_write8 (0x45, 0);	/* Stop timers */
#endif
	}

      if (src & (WAVETABLE_IRQ | ENVELOPE_IRQ))
	{
	  gus_voice_irq ();
	}
    }
}

#endif

/*
 * Some extra code for the 16 bit sampling option
 */
#if defined(CONFIG_GUS16)

int
probe_gus_db16 (struct address_info *hw_config)
{
  return ad1848_detect (hw_config->io_base, NULL, hw_config->osp);
}

void
attach_gus_db16 (struct address_info *hw_config)
{
#ifdef CONFIG_GUS
  gus_pcm_volume = 100;
  gus_wave_volume = 90;
#endif

  ad1848_init ("GUS 16 bit sampling", hw_config->io_base,
	       hw_config->irq,
	       hw_config->dma,
	       hw_config->dma, 0,
	       hw_config->osp);
}

void
unload_gus_db16 (struct address_info *hw_config)
{

  ad1848_unload (hw_config->io_base,
		 hw_config->irq,
		 hw_config->dma,
		 hw_config->dma, 0);
}
#endif
