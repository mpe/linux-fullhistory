/*
 * gus_vol.c - Compute volume for GUS.
 */
/*
 * Copyright by Hannu Savolainen 1993-1996
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
 */
#include <linux/config.h>

#include "sound_config.h"
#ifdef CONFIG_GUS
#include "gus_linearvol.h"

#define GUS_VOLUME	gus_wave_volume


extern int      gus_wave_volume;

/*
 * Calculate gus volume from note velocity, main volume, expression, and
 * intrinsic patch volume given in patch library.  Expression is multiplied
 * in, so it emphasizes differences in note velocity, while main volume is
 * added in -- I don't know whether this is right, but it seems reasonable to
 * me.  (In the previous stage, main volume controller messages were changed
 * to expression controller messages, if they were found to be used for
 * dynamic volume adjustments, so here, main volume can be assumed to be
 * constant throughout a song.)
 *
 * Intrinsic patch volume is added in, but if over 64 is also multiplied in, so
 * we can give a big boost to very weak voices like nylon guitar and the
 * basses.  The normal value is 64.  Strings are assigned lower values.
 */
unsigned short
gus_adagio_vol (int vel, int mainv, int xpn, int voicev)
{
  int             i, m, n, x;


  /*
   * A voice volume of 64 is considered neutral, so adjust the main volume if
   * something other than this neutral value was assigned in the patch
   * library.
   */
  x = 256 + 6 * (voicev - 64);

  /*
   * Boost expression by voice volume above neutral.
   */
  if (voicev > 65)
    xpn += voicev - 64;
  xpn += (voicev - 64) / 2;

  /*
   * Combine multiplicative and level components.
   */
  x = vel * xpn * 6 + (voicev / 4) * x;

#ifdef GUS_VOLUME
  /*
   * Further adjustment by installation-specific master volume control
   * (default 60).
   */
  x = (x * GUS_VOLUME * GUS_VOLUME) / 10000;
#endif

#ifdef GUS_USE_CHN_MAIN_VOLUME
  /*
   * Experimental support for the channel main volume
   */

  mainv = (mainv / 2) + 64;	/* Scale to 64 to 127 */
  x = (x * mainv * mainv) / 16384;
#endif

  if (x < 2)
    return (0);
  else if (x >= 65535)
    return ((15 << 8) | 255);

  /*
   * Convert to gus's logarithmic form with 4 bit exponent i and 8 bit
   * mantissa m.
   */
  n = x;
  i = 7;
  if (n < 128)
    {
      while (i > 0 && n < (1 << i))
	i--;
    }
  else
    while (n > 255)
      {
	n >>= 1;
	i++;
      }
  /*
   * Mantissa is part of linear volume not expressed in exponent.  (This is
   * not quite like real logs -- I wonder if it's right.)
   */
  m = x - (1 << i);

  /*
   * Adjust mantissa to 8 bits.
   */
  if (m > 0)
    {
      if (i > 8)
	m >>= i - 8;
      else if (i < 8)
	m <<= 8 - i;
    }

  return ((i << 8) + m);
}

/*
 * Volume-values are interpreted as linear values. Volume is based on the
 * value supplied with SEQ_START_NOTE(), channel main volume (if compiled in)
 * and the volume set by the mixer-device (default 60%).
 */

unsigned short
gus_linear_vol (int vol, int mainvol)
{
  int             mixer_mainvol;

  if (vol <= 0)
    vol = 0;
  else if (vol >= 127)
    vol = 127;

#ifdef GUS_VOLUME
  mixer_mainvol = GUS_VOLUME;
#else
  mixer_mainvol = 100;
#endif

#ifdef GUS_USE_CHN_MAIN_VOLUME
  if (mainvol <= 0)
    mainvol = 0;
  else if (mainvol >= 127)
    mainvol = 127;
#else
  mainvol = 127;
#endif

  return gus_linearvol[(((vol * mainvol) / 127) * mixer_mainvol) / 100];
}

#endif
