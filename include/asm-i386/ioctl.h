/* $Id: ioctl.h,v 1.5 1993/07/19 21:53:50 root Exp root $
 *
 * linux/ioctl.h for Linux by H.H. Bergman.
 */

#ifndef _ASMI386_IOCTL_H
#define _ASMI386_IOCTL_H

#include <asm/page.h>		/* for PAGE_SIZE */

/* ioctl command encoding: 32 bits total, command in lower 16 bits,
 * size of the parameter structure in the lower 14 bits of the
 * upper 16 bits.
 * Encoding the size of the parameter structure in the ioctl request
 * is useful for catching programs compiled with old versions
 * and to avoid overwriting user space outside the user buffer area.
 * The highest 2 bits are reserved for indicating the ``access mode''.
 * NOTE: This limits the max parameter size to 16kB -1 !
 */

#define IOC_VOID	0x00000000	/* param in size field */
#define IOC_IN		0x40000000	/* user --> kernel */
#define IOC_OUT		0x80000000	/* kernel --> user */
#define IOC_INOUT	(IOC_IN | IOC_OUT)	/* both */
#define IOCSIZE_MASK	0x3fff0000	/* size (max 16k-1 bytes) */
#define IOCSIZE_SHIFT	16		/* how to get the size */
#define IOCSIZE_MAX	((PAGE_SIZE-1)&(IOCSIZE_MASK >> IOCSIZE_SHIFT))
#define IOCCMD_MASK	0x0000ffff	/* command code */
#define IOCCMD_SHIFT	0
#define IOCPARM_MASK IOCCMD_MASK
#define IOCPARM_SHIFT IOCCMD_SHIFT

#define IOC_SIZE(cmd)	(((cmd) & IOCSIZE_MASK) >> IOCSIZE_SHIFT)
#define IOCBASECMD(cmd)	((cmd) & ~IOCPARM_MASK)
#define IOCGROUP(cmd)	(((cmd) >> 8) & 0xFF)

/* _IO(magic, subcode); size field is zero and the 
 * subcode determines the command.
 */
#define _IO(c,d)	(IOC_VOID | ((c)<<8) | (d)) /* param encoded */

/* _IOXX(magic, subcode, arg_t); where arg_t is the type of the
 * (last) argument field in the ioctl call, if present.
 */
#define _IOW(c,d,t)	(IOC_IN | ((sizeof(t)<<16) & IOCSIZE_MASK) | \
				  ((c)<<8) | (d))
#define _IOR(c,d,t)	(IOC_OUT | ((sizeof(t)<<16) & IOCSIZE_MASK) | \
				   ((c)<<8) | (d))
/* WR rather than RW to avoid conflict with stdio.h */
#define _IOWR(c,d,t)	(IOC_INOUT | ((sizeof(t)<<16) & IOCSIZE_MASK) | \
				     ((c)<<8) | (d))

/*
 * The following is for compatibility across the various Linux
 * platforms.  The i386 ioctl numbering scheme doesn't really enforce
 * a type field.  De facto, however, the top 8 bits of the lower 16
 * bits are indeed used as a type field, so we might just as well make
 * this explicit here.  Please be sure to use the decoding macros
 * below from now on.
 */
#define _IOC_NRBITS	8
#define _IOC_TYPEBITS	8
#define _IOC_SIZEBITS	14
#define _IOC_DIRBITS	2

#define _IOC_NRMASK	((1 << _IOC_NRBITS)-1)
#define _IOC_TYPEMASK	((1 << _IOC_TYPEBITS)-1)
#define _IOC_SIZEMASK	((1 << _IOC_SIZEBITS)-1)
#define _IOC_DIRMASK	((1 << _IOC_DIRBITS)-1)

#define _IOC_NRSHIFT	0
#define _IOC_TYPESHIFT	(_IOC_NRSHIFT+_IOC_NRBITS)
#define _IOC_SIZESHIFT	(_IOC_TYPESHIFT+_IOC_TYPEBITS)
#define _IOC_DIRSHIFT	(_IOC_SIZESHIFT+_IOC_SIZEBITS)

/*
 * Direction bits.
 */
#define _IOC_NONE	0U
#define _IOC_READ	1U
#define _IOC_WRITE	2U

/* used to decode ioctl numbers.. */
#define _IOC_DIR(nr)		(((nr) >> _IOC_DIRSHIFT) & _IOC_DIRMASK)
#define _IOC_NR(nr)		(((nr) >> _IOC_NRSHIFT) & _IOC_NRMASK)
#define _IOC_SIZE(nr)		(((nr) >> _IOC_SIZESHIFT) & _IOC_SIZEMASK)

#endif /* _ASMI386_IOCTL_H */
