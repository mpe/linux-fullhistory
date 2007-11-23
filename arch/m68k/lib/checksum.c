/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		IP/TCP/UDP checksumming routines
 *
 * Authors:	Jorge Cwik, <jorge@laser.satlink.net>
 *		Arnt Gulbrandsen, <agulbra@nvg.unit.no>
 *		Tom May, <ftom@netcom.com>
 *		Andreas Schwab, <schwab@issan.informatik.uni-dortmund.de>
 *		Lots of code moved from tcp.c and ip.c; see those files
 *		for more names.
 *
 * 03/02/96	Jes Sorensen, Andreas Schwab, Roman Hodek:
 *		Fixed some nasty bugs, causing some horrible crashes.
 *		A: At some points, the sum (%0) was used as
 *		length-counter instead of the length counter
 *		(%1). Thanks to Roman Hodek for pointing this out.
 *		B: GCC seems to mess up if one uses too many
 *		data-registers to hold input values and one tries to
 *		specify d0 and d1 as scratch registers. Letting gcc choose these
 *      registers itself solves the problem.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <net/checksum.h>

/*
 * computes a partial checksum, e.g. for TCP/UDP fragments
 */

unsigned int
csum_partial (const unsigned char *buff, int len, unsigned int sum)
{
	unsigned long tmp1, tmp2;
	  /*
	   * Experiments with ethernet and slip connections show that buff
	   * is aligned on either a 2-byte or 4-byte boundary.
	   */
	__asm__("movel %2,%3\n\t"
		"btst #1,%3\n\t"	/* Check alignment */
		"jeq 2f\n\t"
		"subql #2,%1\n\t"	/* buff%4==2: treat first word */
		"jgt 1f\n\t"
		"addql #2,%1\n\t"	/* len was == 2, treat only rest */
		"jra 4f\n"
	     "1:\t"
		"addw %2@+,%0\n\t"	/* add first word to sum */
		"clrl %3\n\t"
		"addxl %3,%0\n"		/* add X bit */
	     "2:\t"
		/* unrolled loop for the main part: do 8 longs at once */
		"movel %1,%3\n\t"	/* save len in tmp1 */
		"lsrl #5,%1\n\t"	/* len/32 */
		"jeq 2f\n\t"		/* not enough... */
		"subql #1,%1\n"
	     "1:\t"
		"movel %2@+,%4\n\t"
		"addxl %4,%0\n\t"
		"movel %2@+,%4\n\t"
		"addxl %4,%0\n\t"
		"movel %2@+,%4\n\t"
		"addxl %4,%0\n\t"
		"movel %2@+,%4\n\t"
		"addxl %4,%0\n\t"
		"movel %2@+,%4\n\t"
		"addxl %4,%0\n\t"
		"movel %2@+,%4\n\t"
		"addxl %4,%0\n\t"
		"movel %2@+,%4\n\t"
		"addxl %4,%0\n\t"
		"movel %2@+,%4\n\t"
		"addxl %4,%0\n\t"
		"dbra %1,1b\n\t"
		"clrl %4\n\t"
		"addxl %4,%0\n\t"	/* add X bit */
		"clrw %1\n\t"
		"subql #1,%1\n\t"
		"jcc 1b\n"
	     "2:\t"
		"movel %3,%1\n\t"	/* restore len from tmp1 */
		"andw #0x1c,%3\n\t"	/* number of rest longs */
		"jeq 4f\n\t"
		"lsrw #2,%3\n\t"
		"subqw #1,%3\n"
	     "3:\t"
		/* loop for rest longs */
		"movel %2@+,%4\n\t"
		"addxl %4,%0\n\t"
		"dbra %3,3b\n\t"
		"clrl %4\n\t"
		"addxl %4,%0\n"		/* add X bit */
	     "4:\t"
		/* now check for rest bytes that do not fit into longs */
		"andw #3,%1\n\t"
		"jeq 7f\n\t"
		"clrl %4\n\t"		/* clear tmp2 for rest bytes */
		"subqw #2,%1\n\t"
		"jlt 5f\n\t"
		"movew %2@+,%4\n\t"	/* have rest >= 2: get word */
		"swap %4\n\t"		/* into bits 16..31 */
		"tstw %1\n\t"		/* another byte? */
		"jeq 6f\n"
	     "5:\t"
		"moveb %2@,%4\n\t"	/* have odd rest: get byte */
		"lslw #8,%4\n\t"	/* into bits 8..15; 16..31 untouched */
	     "6:\t"
		"addl %4,%0\n\t"	/* now add rest long to sum */
		"clrl %4\n\t"
		"addxl %4,%0\n"		/* add X bit */
	     "7:\t"
		: "=d" (sum), "=d" (len), "=a" (buff),
		  "=&d" (tmp1), "=&d" (tmp2)
		: "0" (sum), "1" (len), "2" (buff)
	    );
	return(sum);
}



/*
 * copy from fs while checksumming, otherwise like csum_partial
 */

unsigned int
csum_partial_copy_fromuser(const char *src, char *dst, int len, int sum)
{
	unsigned long tmp1, tmp2;
	__asm__("movel %2,%4\n\t"
		"btst #1,%4\n\t"	/* Check alignment */
		"jeq 2f\n\t"
		"subql #2,%1\n\t"	/* buff%4==2: treat first word */
		"jgt 1f\n\t"
		"addql #2,%1\n\t"	/* len was == 2, treat only rest */
		"jra 4f\n"
	     "1:\t"
		"movesw %2@+,%4\n\t"	/* add first word to sum */
		"addw %4,%0\n\t"
		"movew %4,%3@+\n\t"
		"clrl %4\n\t"
		"addxl %4,%0\n"		/* add X bit */
	     "2:\t"
		/* unrolled loop for the main part: do 8 longs at once */
		"movel %1,%4\n\t"	/* save len in tmp1 */
		"lsrl #5,%1\n\t"	/* len/32 */
		"jeq 2f\n\t"		/* not enough... */
		"subql #1,%1\n"
	     "1:\t"
		"movesl %2@+,%5\n\t"
		"addxl %5,%0\n\t"
		"movel %5,%3@+\n\t"
		"movesl %2@+,%5\n\t"
		"addxl %5,%0\n\t"
		"movel %5,%3@+\n\t"
		"movesl %2@+,%5\n\t"
		"addxl %5,%0\n\t"
		"movel %5,%3@+\n\t"
		"movesl %2@+,%5\n\t"
		"addxl %5,%0\n\t"
		"movel %5,%3@+\n\t"
		"movesl %2@+,%5\n\t"
		"addxl %5,%0\n\t"
		"movel %5,%3@+\n\t"
		"movesl %2@+,%5\n\t"
		"addxl %5,%0\n\t"
		"movel %5,%3@+\n\t"
		"movesl %2@+,%5\n\t"
		"addxl %5,%0\n\t"
		"movel %5,%3@+\n\t"
		"movesl %2@+,%5\n\t"
		"addxl %5,%0\n\t"
		"movel %5,%3@+\n\t"
		"dbra %1,1b\n\t"
		"clrl %5\n\t"
		"addxl %5,%0\n\t"	/* add X bit */
		"clrw %1\n\t"
		"subql #1,%1\n\t"
		"jcc 1b\n"
	     "2:\t"
		"movel %4,%1\n\t"	/* restore len from tmp1 */
		"andw #0x1c,%4\n\t"	/* number of rest longs */
		"jeq 4f\n\t"
		"lsrw #2,%4\n\t"
		"subqw #1,%4\n"
	     "3:\t"
		/* loop for rest longs */
		"movesl %2@+,%5\n\t"
		"addxl %5,%0\n\t"
		"movel %5,%3@+\n\t"
		"dbra %4,3b\n\t"
		"clrl %5\n\t"
		"addxl %5,%0\n"		/* add X bit */
	     "4:\t"
		/* now check for rest bytes that do not fit into longs */
		"andw #3,%1\n\t"
		"jeq 7f\n\t"
		"clrl %5\n\t"		/* clear tmp2 for rest bytes */
		"subqw #2,%1\n\t"
		"jlt 5f\n\t"
		"movesw %2@+,%5\n\t"	/* have rest >= 2: get word */
		"movew %5,%3@+\n\t"
		"swap %5\n\t"		/* into bits 16..31 */
		"tstw %1\n\t"		/* another byte? */
		"jeq 6f\n"
	     "5:\t"
		"movesb %2@,%5\n\t"	/* have odd rest: get byte */
		"moveb %5,%3@+\n\t"
		"lslw #8,%5\n\t"	/* into bits 8..15; 16..31 untouched */
	     "6:\t"
		"addl %5,%0\n\t"	/* now add rest long to sum */
		"clrl %5\n\t"
		"addxl %5,%0\n"		/* add X bit */
	     "7:\t"
		: "=d" (sum), "=d" (len), "=a" (src), "=a" (dst),
		  "=&d" (tmp1), "=&d" (tmp2)
		: "0" (sum), "1" (len), "2" (src), "3" (dst)
	    );
	return(sum);
}
/*
 * copy from ds while checksumming, otherwise like csum_partial
 */

unsigned int
csum_partial_copy(const char *src, char *dst, int len, int sum)
{
	unsigned long tmp1, tmp2;
	__asm__("movel %2,%4\n\t"
		"btst #1,%4\n\t"	/* Check alignment */
		"jeq 2f\n\t"
		"subql #2,%1\n\t"	/* buff%4==2: treat first word */
		"jgt 1f\n\t"
		"addql #2,%1\n\t"	/* len was == 2, treat only rest */
		"jra 4f\n"
	     "1:\t"
		"movew %2@+,%4\n\t"	/* add first word to sum */
		"addw %4,%0\n\t"
		"movew %4,%3@+\n\t"
		"clrl %4\n\t"
		"addxl %4,%0\n"		/* add X bit */
	     "2:\t"
		/* unrolled loop for the main part: do 8 longs at once */
		"movel %1,%4\n\t"	/* save len in tmp1 */
		"lsrl #5,%1\n\t"	/* len/32 */
		"jeq 2f\n\t"		/* not enough... */
		"subql #1,%1\n"
	     "1:\t"
		"movel %2@+,%5\n\t"
		"addxl %5,%0\n\t"
		"movel %5,%3@+\n\t"
		"movel %2@+,%5\n\t"
		"addxl %5,%0\n\t"
		"movel %5,%3@+\n\t"
		"movel %2@+,%5\n\t"
		"addxl %5,%0\n\t"
		"movel %5,%3@+\n\t"
		"movel %2@+,%5\n\t"
		"addxl %5,%0\n\t"
		"movel %5,%3@+\n\t"
		"movel %2@+,%5\n\t"
		"addxl %5,%0\n\t"
		"movel %5,%3@+\n\t"
		"movel %2@+,%5\n\t"
		"addxl %5,%0\n\t"
		"movel %5,%3@+\n\t"
		"movel %2@+,%5\n\t"
		"addxl %5,%0\n\t"
		"movel %5,%3@+\n\t"
		"movel %2@+,%5\n\t"
		"addxl %5,%0\n\t"
		"movel %5,%3@+\n\t"
		"dbra %1,1b\n\t"
		"clrl %5\n\t"
		"addxl %5,%0\n\t"	/* add X bit */
		"clrw %1\n\t"
		"subql #1,%1\n\t"
		"jcc 1b\n"
	     "2:\t"
		"movel %4,%1\n\t"	/* restore len from tmp1 */
		"andw #0x1c,%4\n\t"	/* number of rest longs */
		"jeq 4f\n\t"
		"lsrw #2,%4\n\t"
		"subqw #1,%4\n"
	     "3:\t"
		/* loop for rest longs */
		"movel %2@+,%5\n\t"
		"addxl %5,%0\n\t"
		"movel %5,%3@+\n\t"
		"dbra %4,3b\n\t"
		"clrl %5\n\t"
		"addxl %5,%0\n"		/* add X bit */
	     "4:\t"
		/* now check for rest bytes that do not fit into longs */
		"andw #3,%1\n\t"
		"jeq 7f\n\t"
		"clrl %5\n\t"		/* clear tmp2 for rest bytes */
		"subqw #2,%1\n\t"
		"jlt 5f\n\t"
		"movew %2@+,%5\n\t"	/* have rest >= 2: get word */
		"movew %5,%3@+\n\t"
		"swap %5\n\t"		/* into bits 16..31 */
		"tstw %1\n\t"		/* another byte? */
		"jeq 6f\n"
	     "5:\t"
		"moveb %2@,%5\n\t"	/* have odd rest: get byte */
		"moveb %5,%3@+\n\t"
		"lslw #8,%5\n"		/* into bits 8..15; 16..31 untouched */
	     "6:\t"
		"addl %5,%0\n\t"	/* now add rest long to sum */
		"clrl %5\n\t"
		"addxl %5,%0\n"		/* add X bit */
	     "7:\t"
		: "=d" (sum), "=d" (len), "=a" (src), "=a" (dst),
		  "=&d" (tmp1), "=&d" (tmp2)
		: "0" (sum), "1" (len), "2" (src), "3" (dst)
	    );
    return(sum);
}
