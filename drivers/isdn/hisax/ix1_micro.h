/* $Id: ix1_micro.h,v 1.1 1997/01/27 15:42:48 keil Exp $

 * ix1_micro.h  low level stuff for ITK ix1-micro Rev.2 isdn cards
 *
 *              derived from teles3.h from Karsten Keil
 *
 * Copyright (C) 1997 Klaus-Peter Nischke (ITK AG)   (for the modifications
 to the original file teles.c)
 *
 * $Log: ix1_micro.h,v $
 * Revision 1.1  1997/01/27 15:42:48  keil
 * first version
 *
 *
 */

/*
   For the modification done by the author the following terms and conditions
   apply (GNU PUBLIC LICENSE)


   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.


   You may contact Klaus-Peter Nischke by email: klaus@nischke.do.eunet.de
   or by conventional mail:

   Klaus-Peter Nischke
   Deusener Str. 287
   44369 Dortmund
   Germany
 */


extern void ix1micro_report(struct IsdnCardState *sp);
extern void release_io_ix1micro(struct IsdnCard *card);
extern int setup_ix1micro(struct IsdnCard *card);
extern int initix1micro(struct IsdnCardState *sp);
