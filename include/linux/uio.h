#ifndef __LINUX_UIO_H
#define __LINUX_UIO_H

/*
 *	Berkeley style UIO structures	-	Alan Cox 1994.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */


/* A word of warning: Our uio structure will clash with the C library one (which is now obsolete). Remove the C
   library one from sys/uio.h if you have a very old library set */

struct iovec
{
	void *iov_base;		/* BSD uses caddr_t (same thing in effect) */
	int iov_len;
};

#define UIO_MAXIOV	16	/* Maximum iovec's in one operation 
				   16 matches BSD */

#endif
