

#ifndef _FTAPE_EOF_H
#define _FTAPE_EOF_H

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
 $Source: /home/bas/distr/ftape-2.03b/RCS/ftape-eof.h,v $
 $Author: bas $
 *
 $Revision: 1.12 $
 $Date: 1995/04/22 07:30:15 $
 $State: Beta $
 *
 *      Definitions and declarations for the end of file markers
 *      for the QIC-40/80 floppy-tape driver for Linux.
 */

/*      ftape-eof.c defined global vars.
 */
extern int failed_sector_log_changed;
extern int eof_mark;

/*      ftape-eof.c defined global functions.
 */
extern void clear_eof_mark_if_set(unsigned segment, unsigned byte_count);
extern void reset_eof_list(void);
extern int check_for_eof(unsigned segment);
extern int ftape_weof(unsigned count, unsigned segment, unsigned sector);
extern int ftape_erase(void);
extern void put_file_mark_in_map(unsigned segment, unsigned sector);
extern void extract_file_marks(byte * address);
extern int update_failed_sector_log(byte * buffer);
extern int ftape_seek_eom(void);
extern int ftape_seek_eof(unsigned count);
extern int ftape_file_no(daddr_t * file, daddr_t * block);
extern int ftape_validate_label(char *label);

#endif
