
/*
 * sound/softoss_rs.c
 *
 * Software based MIDI synthsesizer driver, the actual mixing loop.
 * Keep the loop as simple as possible to make it easier to rewrite this 
 * routine in assembly.
 *
 *
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 */
#include <linux/config.h>


#include "sound_config.h"

#ifdef CONFIG_SOFTOSS
#include "softoss.h"

void softsynth_resample_loop(short *buf, int loops)
{
	int iloop, voice;
	volatile voice_info *v;

#ifdef OSS_BIG_ENDIAN
	unsigned char  *cbuf = (unsigned char *) buf;

#endif

	for (iloop = 0; iloop < loops; iloop++)
	{			/* Mix one sample */
		int accum, left = 0, right = 0;
		int ix, position;

		for (voice = 0; voice < devc->maxvoice; voice++)
		{
			if (voice_active[voice])
			{	/* Compute voice */
				v = &softoss_voices[voice];
#ifdef SOFTOSS_TEST
				ix = iloop << 3;
				position = v->ptr;
#else
				ix = (position = v->ptr) >> 9;
#endif
				/* Interpolation (resolution of 512 steps) */
				{
					int fract = v->ptr & 0x1ff;	/* 9 bits */

					/* This method works with less arithmetic operations */
					register int v1 = v->wave[ix];
					accum = v1 + ((((v->wave[ix + 1] - v1)) * (fract)) >> 9);
				}

				left += (accum * v->leftvol);
				right += (accum * v->rightvol);

				/* Update sample pointer */
				position += v->step;
				if (position <= v->endloop)
					v->ptr = position;
				else if (v->mode & WAVE_LOOPING)
				{
					if (v->mode & WAVE_BIDIR_LOOP)
					{							v->mode ^= WAVE_LOOP_BACK;	/* Turn around */
						v->step *= -1;
					}
					else
					{
						position -= v->looplen;
						v->ptr = position;
					}
				}
				/*  else leave the voice looping the current sample */

				if (v->mode & WAVE_LOOP_BACK && position < v->startloop)
				{
					if (v->mode & WAVE_BIDIR_LOOP)
					{							v->mode ^= WAVE_LOOP_BACK;	/* Turn around */
						v->step *= -1;
					}
					else
					{
						position += v->looplen;
						v->ptr = position;
					}
				}
			}	/* Compute voice */
		}
#if 1				/* Delay */
		left += left_delay[delayp];
		right += right_delay[delayp];

		left_delay[delayp] = right >> 2;
		right_delay[delayp] = left >> 2;
		delayp = (delayp + 1) % devc->delay_size;
#endif

#define AFTERSCALE devc->afterscale;

		left >>= AFTERSCALE;
		right >>= AFTERSCALE;

		if (left > 32767)
			left = 32767;
		if (left < -32768)
			left = -32768;
		if (right > 32767)
			right = 32767;
		if (right < -32768)
			right = -32768;

#ifdef OSS_BIG_ENDIAN
		*cbuf++ = left & 0xff;
		*cbuf++ = (left >> 8) & 0xff;
		*cbuf++ = right & 0xff;
		*cbuf++ = (right >> 8) & 0xff;
#else
		*buf++ = left;
		*buf++ = right;
#endif
		if (devc->control_counter++ >= devc->control_rate)
		{
			devc->control_counter = 0;
			softsyn_control_loop();
		}
	}			/* Mix one sample */
}
#endif
