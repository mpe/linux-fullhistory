#ifndef _FTAPE_READ_H
#define _FTAPE_READ_H

/*
 * Copyright (C) 1994-1995 Bas Laarhoven.

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2, or (at your option)
 any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; see the file COPYING.  If not, write to
 the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.

 *
 $Source: /home/bas/distr/ftape-2.03b/RCS/ftape-read.h,v $
 $Author: bas $
 *
 $Revision: 1.13 $
 $Date: 1995/05/10 16:09:36 $
 $State: Beta $
 *
 *      This file contains the definitions for the read functions
 *      for the QIC-117 floppy-tape driver for Linux.
 *
 */

/*      ftape-read.c defined global vars.
 */

/*      ftape-read.c defined global functions.
 */
extern int _ftape_read(char *buff, int req_len);
extern int read_header_segment(byte * address);
extern int read_segment(unsigned segment, byte * address, int *eof_mark,
			int read_ahead);
extern void ftape_zap_read_buffers(void);

#endif				/* _FTAPE_READ_H */
