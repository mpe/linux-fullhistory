#define _PAS2_CARD_C_
/*
 * sound/pas2_card.c
 *
 * Detection routine for the Pro Audio Spectrum cards.
 */

#include <linux/config.h>

#include "sound_config.h"

#if defined(CONFIG_PAS)

static unsigned char dma_bits[] =
{4, 1, 2, 3, 0, 5, 6, 7};
static unsigned char irq_bits[] =
{0, 0, 1, 2, 3, 4, 5, 6, 0, 1, 7, 8, 9, 0, 10, 11};
static unsigned char sb_irq_bits[] =
{0x00, 0x00, 0x08, 0x10, 0x00, 0x18, 0x00, 0x20, 0x00, 0x08, 0x28, 0x30, 0x38, 0, 0};
static unsigned char sb_dma_bits[] =
{0x00, 0x40, 0x80, 0xC0, 0, 0, 0, 0};

/*
 * The Address Translation code is used to convert I/O register addresses to
 * be relative to the given base -register
 */

int             translat_code;
static int      pas_intr_mask = 0;
static int      pas_irq = 0;
static int      pas_sb_base = 0;

int            *pas_osp;

char            pas_model;
static char    *pas_model_names[] =
{"", "Pro AudioSpectrum+", "CDPC", "Pro AudioSpectrum 16", "Pro AudioSpectrum 16D"};

/*
 * pas_read() and pas_write() are equivalents of inb and outb 
 * These routines perform the I/O address translation required
 * to support other than the default base address
 */
extern void     mix_write (unsigned char data, int ioaddr);

unsigned char
pas_read (int ioaddr)
{
  return inb (ioaddr ^ translat_code);
}

void
pas_write (unsigned char data, int ioaddr)
{
  outb (data, ioaddr ^ translat_code);
}

/******************* Begin of the Interrupt Handler ********************/

void
pasintr (int irq, void *dev_id, struct pt_regs *dummy)
{
  int             status;

  status = pas_read (0x0B89);
  pas_write (status, 0x0B89);	/* Clear interrupt */

  if (status & 0x08)
    {
#ifdef CONFIG_AUDIO
      pas_pcm_interrupt (status, 1);
#endif
      status &= ~0x08;
    }
  if (status & 0x10)
    {
#ifdef CONFIG_MIDI
      pas_midi_interrupt ();
#endif
      status &= ~0x10;
    }
}

int
pas_set_intr (int mask)
{
  if (!mask)
    return 0;

  pas_intr_mask |= mask;

  pas_write (pas_intr_mask, 0x0B8B);
  return 0;
}

int
pas_remove_intr (int mask)
{
  if (!mask)
    return 0;

  pas_intr_mask &= ~mask;
  pas_write (pas_intr_mask, 0x0B8B);

  return 0;
}

/******************* End of the Interrupt handler **********************/

/******************* Begin of the Initialization Code ******************/

int
config_pas_hw (struct address_info *hw_config)
{
  char            ok = 1;
  unsigned        int_ptrs;	/* scsi/sound interrupt pointers */

  pas_irq = hw_config->irq;

  pas_write (0x00, 0x0B8B);

  pas_write (0x36, 0x138B);	/*
				   * Local timer control *
				   * register
				 */

  pas_write (0x36, 0x1388);	/*
				   * Sample rate timer (16 bit)
				 */
  pas_write (0, 0x1388);

  pas_write (0x74, 0x138B);	/*
				   * Local timer control *
				   * register
				 */

  pas_write (0x74, 0x1389);	/*
				   * Sample count register (16
				   * * bit)
				 */
  pas_write (0, 0x1389);

  pas_write (0x80 | 0x40 | 0x20 | 1, 0x0B8A);
  pas_write (0x80 | 0x20 | 0x10 | 0x08 | 0x01, 0xF8A);
  pas_write (0x01 | 0x02 | 0x04 | 0x10	/*
					   * |
					   * 0x80
					 */ , 0xB88);

  pas_write (0x80
#ifdef PAS_JOYSTICK_ENABLE
	     | 0x40
#endif
	     ,0xF388);

  if (pas_irq < 0 || pas_irq > 15)
    {
      printk ("PAS2: Invalid IRQ %d", pas_irq);
      ok = 0;
    }
  else
    {
      int_ptrs = pas_read (0xF38A);
      int_ptrs |= irq_bits[pas_irq] & 0xf;
      pas_write (int_ptrs, 0xF38A);
      if (!irq_bits[pas_irq])
	{
	  printk ("PAS2: Invalid IRQ %d", pas_irq);
	  ok = 0;
	}
      else
	{
	  if (snd_set_irq_handler (pas_irq, pasintr, "PAS16", hw_config->osp) < 0)
	    ok = 0;
	}
    }

  if (hw_config->dma < 0 || hw_config->dma > 7)
    {
      printk ("PAS2: Invalid DMA selection %d", hw_config->dma);
      ok = 0;
    }
  else
    {
      pas_write (dma_bits[hw_config->dma], 0xF389);
      if (!dma_bits[hw_config->dma])
	{
	  printk ("PAS2: Invalid DMA selection %d", hw_config->dma);
	  ok = 0;
	}
      else
	{
	  if (sound_alloc_dma (hw_config->dma, "PAS16"))
	    {
	      printk ("pas2_card.c: Can't allocate DMA channel\n");
	      ok = 0;
	    }
	}
    }

  /*
     * This fixes the timing problems of the PAS due to the Symphony chipset
     * as per Media Vision.  Only define this if your PAS doesn't work correctly.
   */
#ifdef SYMPHONY_PAS
  outb (0x05, 0xa8);
  outb (0x60, 0xa9);
#endif

#ifdef BROKEN_BUS_CLOCK
  pas_write (0x01 | 0x10 | 0x20 | 0x04, 0x8388);
#else
  /*
   * pas_write(0x01, 0x8388);
   */
  pas_write (0x01 | 0x10 | 0x20, 0x8388);
#endif
  pas_write (0x18, 0x838A);	/* ??? */
  pas_write (0x20 | 0x01, 0x0B8A);	/* Mute off, filter = 17.897 kHz */
  pas_write (8, 0xBF8A);

  mix_write (0x80 | 5, 0x078B);
  mix_write (5, 0x078B);

#if !defined(DISABLE_SB_EMULATION) && defined(CONFIG_SB)

  {
    struct address_info *sb_config;

    if ((sb_config = sound_getconf (SNDCARD_SB)))
      {
	unsigned char   irq_dma;

	/*
	 * Turn on Sound Blaster compatibility
	 * bit 1 = SB emulation
	 * bit 0 = MPU401 emulation (CDPC only :-( )
	 */
	pas_write (0x02, 0xF788);

	/*
	 * "Emulation address"
	 */
	pas_write ((sb_config->io_base >> 4) & 0x0f, 0xF789);
	pas_sb_base = sb_config->io_base;

	if (!sb_dma_bits[sb_config->dma])
	  printk ("\n\nPAS16 Warning: Invalid SB DMA %d\n\n",
		  sb_config->dma);

	if (!sb_irq_bits[sb_config->irq])
	  printk ("\n\nPAS16 Warning: Invalid SB IRQ %d\n\n",
		  sb_config->irq);

	irq_dma = sb_dma_bits[sb_config->dma] |
	  sb_irq_bits[sb_config->irq];

	pas_write (irq_dma, 0xFB8A);
      }
    else
      pas_write (0x00, 0xF788);
  }
#else
  pas_write (0x00, 0xF788);
#endif

  if (!ok)
    printk ("PAS16: Driver not enabled\n");

  return ok;
}

int
detect_pas_hw (struct address_info *hw_config)
{
  unsigned char   board_id, foo;

  /*
   * WARNING: Setting an option like W:1 or so that disables warm boot reset
   * of the card will screw up this detect code something fierce. Adding code
   * to handle this means possibly interfering with other cards on the bus if
   * you have something on base port 0x388. SO be forewarned.
   */

  outb (0xBC, 0x9A01);		/* Activate first board */
  outb (hw_config->io_base >> 2, 0x9A01);	/* Set base address */
  translat_code = 0x388 ^ hw_config->io_base;
  pas_write (1, 0xBF88);	/* Select one wait states */

  board_id = pas_read (0x0B8B);

  if (board_id == 0xff)
    return 0;

  /*
   * We probably have a PAS-series board, now check for a PAS2-series board
   * by trying to change the board revision bits. PAS2-series hardware won't
   * let you do this - the bits are read-only.
   */

  foo = board_id ^ 0xe0;

  pas_write (foo, 0x0B8B);
  foo = inb (0x0B8B);
  pas_write (board_id, 0x0B8B);

  if (board_id != foo)		/*
				 * Not a PAS2
				 */
    return 0;

  pas_model = pas_read (0xFF88);

  return pas_model;
}

void
attach_pas_card (struct address_info *hw_config)
{
  pas_irq = hw_config->irq;
  pas_osp = hw_config->osp;

  if (detect_pas_hw (hw_config))
    {

      if ((pas_model = pas_read (0xFF88)))
	{
	  char            temp[100];

	  sprintf (temp,
		   "%s rev %d", pas_model_names[(int) pas_model],
		   pas_read (0x2789));
	  conf_printf (temp, hw_config);
	}

      if (config_pas_hw (hw_config))
	{

#ifdef CONFIG_AUDIO
	  pas_pcm_init (hw_config);
#endif

#if !defined(DISABLE_SB_EMULATION) && defined(CONFIG_SB)

	  sb_dsp_disable_midi (pas_sb_base);	/* No MIDI capability */
#endif


#ifdef CONFIG_MIDI
	  pas_midi_init ();
#endif

	  pas_init_mixer ();
	}
    }

}

int
probe_pas (struct address_info *hw_config)
{
  pas_osp = hw_config->osp;
  return detect_pas_hw (hw_config);
}

void
unload_pas (struct address_info *hw_config)
{
  sound_free_dma (hw_config->dma);
  snd_release_irq (hw_config->irq);
}

#endif
