/*
 *      Copyright (C) 1993 Ning and David Mosberger.
 *      Original:
 *      Copyright (C) 1993 Bas Laarhoven.
 *      Copyright (C) 1992 David L. Brown, Jr.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139,
 * USA.
 *
 *
 * $Source: /home/bas/distr/ftape-2.03b/RCS/ecc.h,v $
 * $Author: bas $
 *
 * $Revision: 1.20 $
 * $Date: 1995/01/08 14:16:21 $
 * $State: Beta $
 *
 *      This file contains the definitions for the
 *      Reed-Solomon error correction code 
 *      for the QIC-40/80 tape streamer device driver.
 */
#ifndef _ecc_h_
#define _ecc_h_

typedef unsigned long BAD_SECTOR;
#define BAD_CLEAR(entry) ((entry)=0)
#define BAD_SET(entry,sector) ((entry)|=(1<<(sector)))
#define BAD_CHECK(entry,sector) ((entry)&(1<<(sector)))

/*
 * Return values for ecc_correct_data:
 */
enum {
	ECC_OK,			/* Data was correct. */
	ECC_CORRECTED,		/* Correctable error in data. */
	ECC_FAILED,		/* Could not correct data. */
};

/*
 * Representation of an in memory segment.  MARKED_BAD lists the
 * sectors that were marked bad during formatting.  If the N-th sector
 * in a segment is marked bad, bit 1<<N will be set in MARKED_BAD.
 * The sectors should be read in from the disk and packed, as if the
 * bad sectors were not there, and the segment just contained fewer
 * sectors.  READ_SECTORS is a bitmap of errors encountered while
 * reading the data.  These offsets are relative to the packed data.
 * BLOCKS is a count of the sectors not marked bad.  This is just to
 * prevent having to count the zero bits in MARKED_BAD each time this
 * is needed.  DATA is the actual sector packed data from (or to) the
 * tape.
 */
struct memory_segment {
	BAD_SECTOR marked_bad;
	BAD_SECTOR read_bad;
	int blocks;
	unsigned char *data;
	BAD_SECTOR corrected;
};

/*
 * ecc.c defined global variables:
 */
#ifdef TEST
extern int ftape_ecc_tracing;
#endif

/*
 * ecc.c defined global functions:
 */
extern int ecc_correct_data(struct memory_segment *data);
extern int ecc_set_segment_parity(struct memory_segment *data);

#endif	/* _ecc_h_ */
