#ifndef _FTAPE_H
#define _FTAPE_H

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
 $Source: /home/bas/distr/ftape-2.03b/RCS/ftape.h,v $
 $Author: bas $
 *
 $Revision: 1.18 $
 $Date: 1995/05/06 16:11:53 $
 $State: Beta $
 *
 *      This file contains global definitions, typedefs and macro's
 *      for the QIC-40/80 floppy-tape driver for Linux.
 */

#include <linux/sched.h>
#include <linux/mm.h>

#define SECTOR(x)       (x+1)         /* sector offset into real sector */
#define SECTOR_SIZE     (1024)
#define SECTORS_PER_SEGMENT (32)
#define BUFF_SIZE       (SECTORS_PER_SEGMENT * SECTOR_SIZE)
#define FTAPE_UNIT      (ftape_unit & 3)
#define RQM_DELAY       (12)
#define MILLISECOND     (1)
#define SECOND          (1000)
#define FOREVER         (-1)
#ifndef HZ
# error "HZ undefined."
#endif
#define MSPT            (SECOND / HZ) /* milliseconds per tick */

/* This defines the number of retries that the driver will allow
 * before giving up (and letting a higher level handle the error).
 */
#ifdef TESTING
# define SOFT_RETRIES 1          /* number of low level retries */
# define RETRIES_ON_ECC_ERROR 3  /* ecc error when correcting segment */
#else
# define SOFT_RETRIES 6          /* number of low level retries (triple) */
# define RETRIES_ON_ECC_ERROR 3  /* ecc error when correcting segment */
#endif
/*      some useful macro's
 */
#define ABS(a)          ((a) < 0 ? -(a) : (a))
#define NR_ITEMS(x)     (sizeof(x)/ sizeof(*x))

typedef unsigned char byte;

extern int ftape_init(void);

#endif

