/* $Id: isdn_audio.h,v 1.5 1997/02/03 22:45:21 fritz Exp $

 * Linux ISDN subsystem, audio conversion and compression (linklevel).
 *
 * Copyright 1994,95,96 by Fritz Elfert (fritz@wuemaus.franken.de)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * $Log: isdn_audio.h,v $
 * Revision 1.5  1997/02/03 22:45:21  fritz
 * Reformatted according CodingStyle
 *
 * Revision 1.4  1996/06/06 14:43:32  fritz
 * Changed to support DTMF decoding on audio playback also.
 *
 * Revision 1.3  1996/06/05 02:24:09  fritz
 * Added DTMF decoder for audio mode.
 *
 * Revision 1.2  1996/05/10 08:48:32  fritz
 * Corrected adpcm bugs.
 *
 * Revision 1.1  1996/04/30 09:29:06  fritz
 * Taken under CVS control.
 *
 */

#define DTMF_NPOINTS 205        /* Number of samples for DTMF recognition */
typedef struct adpcm_state {
	int a;
	int d;
	int word;
	int nleft;
	int nbits;
} adpcm_state;

typedef struct dtmf_state {
	char last;
	int idx;
	int buf[DTMF_NPOINTS];
} dtmf_state;

extern void isdn_audio_ulaw2alaw(unsigned char *, unsigned long);
extern void isdn_audio_alaw2ulaw(unsigned char *, unsigned long);
extern adpcm_state *isdn_audio_adpcm_init(adpcm_state *, int);
extern int isdn_audio_adpcm2xlaw(adpcm_state *, int, unsigned char *, unsigned char *, int);
extern int isdn_audio_xlaw2adpcm(adpcm_state *, int, unsigned char *, unsigned char *, int);
extern int isdn_audio_2adpcm_flush(adpcm_state * s, unsigned char *out);
extern void isdn_audio_calc_dtmf(modem_info *, unsigned char *, int, int);
extern void isdn_audio_eval_dtmf(modem_info *);
dtmf_state *isdn_audio_dtmf_init(dtmf_state *);
