/*
 * sound/mad16.c
 *
 * Initialization code for OPTi MAD16 compatible audio chips. Including
 *
 *      OPTi 82C928     MAD16           (replaced by C929)
 *      OAK OTI-601D    Mozart
 *      OPTi 82C929     MAD16 Pro
 *
 * These audio interface chips don't prduce sound themselves. They just
 * connect some other components (OPL-[234] and a WSS compatible codec)
 * to the PC bus and perform I/O, DMA and IRQ address decoding. There is
 * also a UART for the MPU-401 mode (not 82C928/Mozart).
 * The Mozart chip appears to be compatible with the 82C928 (can anybody
 * confirm this?).
 *
 * NOTE! If you want to set CD-ROM address and/or joystick enable, define
 *       MAD16_CONF in local.h as combination of the following bits:
 *
 *      0x01    - joystick disabled
 *
 *      CD-ROM type selection (select just one):
 *      0x02    - Sony 31A
 *      0x04    - Mitsumi
 *      0x06    - Panasonic
 *      0x08    - Secondary IDE
 *      0x0a    - Primary IDE
 *      
 *      For example Mitsumi with joystick disabled = 0x04|0x01 = 0x05
 *      
 *      This defaults to CD I/O 0x340, no IRQ and DMA3 
 *      (DMA5 with Mitsumi or IDE). If you like to change these, define
 *      MAD16_CDSEL with the following bits:
 *
 *      CD-ROM port: 0x00=340, 0x40=330, 0x80=360 or 0xc0=320
 *      OPL4 select: 0x20=OPL4, 0x00=OPL3
 *      CD-ROM irq: 0x00=disabled, 0x04=IRQ5, 0x08=IRQ7, 0x0a=IRQ3, 0x10=IRQ9,
 *                  0x14=IRQ10 and 0x18=IRQ11.
 *
 *      CD-ROM DMA (Sony or Panasonic): 0x00=DMA3, 0x01=DMA2, 0x02=DMA1 or 0x03=disabled
 *   or
 *      CD-ROM DMA (Mitsumi or IDE):    0x00=DMA5, 0x01=DMA6, 0x02=DMA7 or 0x03=disabled
 *
 * Copyright by Hannu Savolainen 1995
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

#include "sound_config.h"

#if defined(CONFIGURE_SOUNDCARD) && !defined(EXCLUDE_MAD16)

static int      already_initialized = 0;

#define C928	1
#define MOZART	2
#define C929	3

/*
 *    Registers
 *
 *      The MAD16 occupies I/O ports 0xf8d to 0xf93 (fixed locations).
 *      All ports are inactive by default. They can be activated by
 *      writing 0xE2 or 0xE3 to the password register. The password is valid
 *      only until the next I/O read or write.
 */

#define MC1_PORT	0xf8d
#define MC2_PORT	0xf8e
#define MC3_PORT	0xf8f
#define PASSWD_REG	0xf8f
#define MC4_PORT	0xf90
#define MC5_PORT	0xf91
#define MC6_PORT	0xf92
#define MC7_PORT	0xf93

static int      board_type = C928;

#ifndef DDB
#define DDB(x)
#endif

static unsigned char
mad_read (int port)
{
  unsigned long   flags;
  unsigned char   tmp;

  DISABLE_INTR (flags);

  switch (board_type)		/* Output password */
    {
    case C928:
    case MOZART:
      OUTB (0xE2, PASSWD_REG);
      break;

    case C929:
      OUTB (0xE3, PASSWD_REG);
      break;
    }

  tmp = INB (port);
  RESTORE_INTR (flags);

  return tmp;
}

static void
mad_write (int port, int value)
{
  unsigned long   flags;

  DISABLE_INTR (flags);

  switch (board_type)		/* Output password */
    {
    case C928:
    case MOZART:
      OUTB (0xE2, PASSWD_REG);
      break;

    case C929:
      OUTB (0xE3, PASSWD_REG);
      break;
    }

  OUTB ((unsigned char) (value & 0xff), port);
  RESTORE_INTR (flags);
}

static int
detect_mad16 (void)
{
  unsigned char   tmp, tmp2;

/*
 * Check that reading a register doesn't return bus float (0xff)
 * when the card is accessed using password. This may fail in case
 * the card is in low power mode. Normally at least the power saving mode
 * bit should be 0.
 */
  if ((tmp = mad_read (MC1_PORT)) == 0xff)
    {
      DDB (printk ("MC1_PORT returned 0xff\n"));
      return 0;
    }
/*
 * Now check that the gate is closed on first I/O after writing
 * the password. (This is how a MAD16 compatible card works).
 */

  if ((tmp2 = INB (MC1_PORT)) == tmp)	/* It didn't close */
    {
      DDB (printk ("MC1_PORT didn't close after read (0x%02x)\n", tmp2));
      return 0;
    }

  mad_write (MC1_PORT, tmp ^ 0x80);	/* Togge a bit */

  if ((tmp2 = mad_read (MC1_PORT)) != (tmp ^ 0x80))	/* Compare the bit */
    {
      mad_write (MC1_PORT, tmp);	/* Restore */
      DDB (printk ("Bit revert test failed (0x%02x, 0x%02x)\n", tmp, tmp2));
      return 0;
    }

  mad_write (MC1_PORT, tmp);	/* Restore */
  return 1;			/* Bingo */

}

int
probe_mad16 (struct address_info *hw_config)
{
  int             i;
  static int      valid_ports[] =
  {0x530, 0xe80, 0xf40, 0x604};
  unsigned char   tmp;

  if (already_initialized)
    return 0;

/*
 *    Check that all ports return 0xff (bus float) when no password
 *      is written to the password register.
 */

  DDB (printk ("--- Detecting MAD16 / Mozart ---\n"));

#if 0
  for (i = 0xf8d; i <= 0xf93; i++)
    if (INB (i) != 0xff)
      {
	DDB (printk ("port 0x%03x != 0xff (0x%02x)\n", i, INB (i)));
	return 0;
      }
#endif

/*
 *    Then try to detect with the old password
 */
  board_type = C928;

  DDB (printk ("Detect using password = 0xE2\n"));

  if (!detect_mad16 ())		/* No luck. Try different model */
    {
      board_type = C929;

      DDB (printk ("Detect using password = 0xE3\n"));

      if (!detect_mad16 ())
	return 0;

      printk ("mad16.c: 82C929 detected???\n");
    }
  else
    {
      unsigned char model;

      if (((model=mad_read (MC3_PORT)) & 0x03) == 0x03)
	{
	  printk ("mad16.c: Mozart detected???\n");
	  board_type = MOZART;
	}
      else
	{
	  printk ("mad16.c: 82C928 detected???\n");
	  board_type = C928;
	}
    }

  for (i = 0xf8d; i <= 0xf93; i++)
    DDB (printk ("port %03x = %03x\n", i, mad_read (i)));

/*
 * Set the WSS address
 */

  tmp = 0x80;			/* Enable WSS, Disable SB */

  for (i = 0; i < 5; i++)
    {
      if (i > 3)		/* Not a valid port */
	{
	  printk ("MAD16/Mozart: Bad WSS base address 0x%x\n", hw_config->io_base);
	  return 0;
	}

      if (valid_ports[i] == hw_config->io_base)
	{
	  tmp |= i << 4;	/* WSS port select bits */
	  break;
	}
    }

/*
 * Set optional CD-ROM and joystick settings.
 */

#ifdef MAD16_CONF
  tmp |= ((MAD16_CONF) & 0x0f);	/* CD-ROM and joystick bits */
#endif
  mad_write (MC1_PORT, tmp);

#if defined(MAD16_CONF) && defined(MAD16_CDSEL)
  tmp = MAD16_CDSEL;
#else
  tmp = 0x03;
#endif

#ifdef MAD16_OPL4
  tmp |= 0x20;			/* Enable OPL4 access */
#endif

  mad_write (MC2_PORT, tmp);
  mad_write (MC3_PORT, 0xf0);	/* Disable SB */

  if (board_type == C929)
    {
      mad_write (MC4_PORT, 0xa2);
      mad_write (MC5_PORT, 0x95);	/* AD184x mode (0x9f for CS42xx) */
      mad_write (MC6_PORT, 0x03);	/* Disable MPU401 */
    }
  else
    {
      mad_write (MC4_PORT, 0x02);
      mad_write (MC5_PORT, 0x10);	/* AD184x mode (0x12 for CS42xx) */
    }

  for (i = 0xf8d; i <= 0xf93; i++)
    DDB (printk ("port %03x after init = %03x\n", i, mad_read (i)));

  return probe_ms_sound (hw_config);
}

long
attach_mad16 (long mem_start, struct address_info *hw_config)
{

  already_initialized = 1;

  return attach_ms_sound (mem_start, hw_config);
}

long
attach_mad16_mpu (long mem_start, struct address_info *hw_config)
{

#ifdef EXCLUDE_MIDI
  return mem_start;
#else
  if (!already_initialized)
    return mem_start;

  return attach_mpu401 (mem_start, hw_config);
#endif
}

int
probe_mad16_mpu (struct address_info *hw_config)
{
#ifdef EXCLUDE_MIDI
  return 0;
#else
  static int      mpu_attached = 0;
  static int      valid_ports[] =
  {0x330, 0x320, 0x310, 0x300};
  static short    valid_irqs[] =
  {9, 10, 5, 7};
  unsigned char   tmp;

  int             i;		/* A variable with secret power */

  if (!already_initialized)	/* The MSS port must be initialized first */
    return 0;

  if (mpu_attached)		/* Don't let them call this twice */
    return 0;
  mpu_attached = 1;

  if (board_type < C929)	/* Early chip. No MPU support */
    {
      printk ("Mozart and OPTi 82C928 based cards don't support MPU401. Sorry\n");
      return 0;
    }

  tmp = 0x80;			/* MPU-401 enable */

/*
 * Set the MPU base bits
 */

  for (i = 0; i < 5; i++)
    {
      if (i > 3)		/* Out of array bounds */
	{
	  printk ("MAD16 / Mozart: Invalid MIDI port 0x%x\n", hw_config->io_base);
	  return 0;
	}

      if (valid_ports[i] == hw_config->io_base)
	{
	  tmp |= i << 5;
	  break;
	}
    }

/*
 * Set the MPU IRQ bits
 */

  for (i = 0; i < 5; i++)
    {
      if (i > 3)		/* Out of array bounds */
	{
	  printk ("MAD16 / Mozart: Invalid MIDI IRQ %d\n", hw_config->irq);
	  return 0;
	}

      if (valid_irqs[i] == hw_config->irq)
	{
	  tmp |= i << 3;
	  break;
	}

      tmp |= 0x03;		/* ???????? */
      mad_write (MC6_PORT, tmp);	/* Write MPU401 config */
    }

  return probe_mpu401 (hw_config);
#endif
}

/* That's all folks */
#endif
