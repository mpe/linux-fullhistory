#ifndef _FTAPE_WRITE_H
#define _FTAPE_WRITE_H

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
 $Source: /home/bas/distr/ftape-2.03b/RCS/ftape-write.h,v $
 $Author: bas $
 *
 $Revision: 1.13 $
 $Date: 1995/04/22 07:30:15 $
 $State: Beta $
 *
 *      This file contains the definitions for the write functions
 *      for the QIC-117 floppy-tape driver for Linux.
 *
 */

/*      ftape-write.c defined global vars.
 */

/*      ftape-write.c defined global functions.
 */
extern int _ftape_write(const char *buff, int req_len);
extern int ftape_flush_buffers(void);
extern int ftape_write_header_segments(byte * buffer);
extern int ftape_update_header_segments(byte * buffer, int update);
extern int write_segment(unsigned segment, byte * address, int flushing);
extern int ftape_fix(void);
extern void prevent_flush(void);
extern void ftape_zap_write_buffers(void);

#endif				/* _FTAPE_WRITE_H */
