/* fdomain.h -- Header for Future Domain TMC-16x0 driver
 * Created: Sun May  3 18:47:33 1992 by faith@cs.unc.edu
 * Revised: Sat Jan 14 20:56:52 1995 by faith@cs.unc.edu
 * Author: Rickard E. Faith, faith@cs.unc.edu
 * Copyright 1992, 1993, 1994, 1995 Rickard E. Faith
 *
 * $Id: fdomain.h,v 5.10 1995/01/15 01:56:56 root Exp $

 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.

 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.

 */

#ifndef _FDOMAIN_H
#define _FDOMAIN_H

int        fdomain_16x0_detect( Scsi_Host_Template * );
int        fdomain_16x0_command( Scsi_Cmnd * );
int        fdomain_16x0_abort( Scsi_Cmnd * );
const char *fdomain_16x0_info( struct Scsi_Host * );
int        fdomain_16x0_reset( Scsi_Cmnd * ); 
int        fdomain_16x0_queue( Scsi_Cmnd *, void (*done)(Scsi_Cmnd *) );
int        fdomain_16x0_biosparam( Disk *, int, int * );

#define FDOMAIN_16X0 { NULL,                             \
		       NULL,                             \
		       NULL,			         \
		       fdomain_16x0_detect,              \
		       NULL,				 \
		       fdomain_16x0_info,                \
		       fdomain_16x0_command,             \
		       fdomain_16x0_queue,               \
		       fdomain_16x0_abort,               \
		       fdomain_16x0_reset,               \
		       NULL,                             \
		       fdomain_16x0_biosparam,           \
		       1, 				 \
		       6, 				 \
		       64, 		                 \
		       1, 				 \
		       0, 				 \
		       0, 				 \
		       DISABLE_CLUSTERING }
#endif
