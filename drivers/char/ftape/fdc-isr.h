#ifndef _FDC_ISR_H
#define _FDC_ISR_H

/*
 * Copyright (C) 1993-1995 Bas Laarhoven.

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
 $Source: /home/bas/distr/ftape-2.03b/RCS/fdc-isr.h,v $
 $Author: bas $
 *
 $Revision: 1.8 $
 $Date: 1995/04/22 07:30:15 $
 $State: Beta $
 *
 *      This file contains the low level functions
 *      that communicate with the floppy disk controller,
 *      for the QIC-40/80 floppy-tape driver for Linux.
 */

/*
 *      fdc-isr.c defined public variables
 */
extern volatile int expected_stray_interrupts;	/* masks stray interrupts */
extern volatile int seek_completed;	/* flag set by isr */
extern volatile int interrupt_seen;	/* flag set by isr */
extern volatile int expect_stray_interrupt;

/*
 *      fdc-io.c defined public functions
 */
extern void fdc_isr(void);

/*
 *      A kernel hook that steals one interrupt from the floppy
 *      driver (Should be fixed when the new fdc driver gets ready)
 *      See the linux kernel source files:
 *          drivers/block/floppy.c & drivers/block/blk.h
 *      for the details.
 */
extern void (*do_floppy) (void);

#endif
