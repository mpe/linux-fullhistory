/* $Id: isdn_v110.h,v 1.1 1998/02/20 17:32:11 fritz Exp $

 * Linux ISDN subsystem, V.110 related functions (linklevel).
 *
 * Copyright by Thomas Pfeiffer (pfeiffer@pds.de)
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
 * $Log: isdn_v110.h,v $
 * Revision 1.1  1998/02/20 17:32:11  fritz
 * First checkin (not yet completely functionable).
 *
 */
#ifndef _isdn_v110_h_
#define _isdn_v110_h_

/* isdn_v110_encode erhält len Nettodaten in buf, kodiert diese und liefert
   das Ergebnis wieder in buf. Wieviele Bytes kodiert wurden wird als
   return zurück gegeben. Diese Daten können dann 1:1 auf die Leitung
   gegeben werden.
*/
extern struct sk_buff *isdn_v110_encode(isdn_v110_stream *, struct sk_buff *);

/* isdn_v110_decode erhält vom input stream V110 kodierte Daten, die zu den
   V110 frames zusammengepackt werden müssen. Die Daten können an diese
   Schnittstelle so übergeben werden, wie sie von der Leitung kommen, ohne
   darauf achten zu müssen, das frames usw. eingehalten werden.
 */
extern struct sk_buff *isdn_v110_decode(isdn_v110_stream *, struct sk_buff *);

extern int isdn_v110_stat_callback(int, isdn_ctrl *);

#endif
