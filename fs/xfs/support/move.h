/*
 * Copyright (c) 2000-2002 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */

#ifndef __XFS_SUPPORT_MOVE_H__
#define __XFS_SUPPORT_MOVE_H__

#include <linux/uio.h>
#include <asm/uaccess.h>

typedef struct iovec iovec_t;

typedef struct uio {
	iovec_t         *uio_iov;       /* pointer to array of iovecs */
	int             uio_iovcnt;     /* number of iovecs */
	int             uio_fmode;      /* file mode flags */
	xfs_off_t       uio_offset;     /* file offset */
	short           uio_segflg;     /* address space (kernel or user) */
	ssize_t         uio_resid;      /* residual count */
} uio_t;

/*
 * I/O direction.
 */
typedef enum uio_rw { UIO_READ, UIO_WRITE } uio_rw_t;

/*
 * Segment flag values.
 */
typedef enum uio_seg {
	UIO_USERSPACE,          /* uio_iov describes user space */
	UIO_SYSSPACE,           /* uio_iov describes system space */
} uio_seg_t;


extern int	uiomove (void *, size_t, uio_rw_t, uio_t *);

#endif  /* __XFS_SUPPORT_MOVE_H__ */
