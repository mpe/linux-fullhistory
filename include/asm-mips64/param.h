/* $Id: param.h,v 1.1 1999/08/18 23:37:51 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright 1994 - 1999 Ralf Baechle (ralf@gnu.org)
 */
#ifndef _ASM_PARAM_H
#define _ASM_PARAM_H

#ifndef HZ
#define HZ 100
#  define HZ 100
#ifdef __KERNEL__
#  define HZ_TO_STD(a) (a)
#endif
#endif

#define EXEC_PAGESIZE	4096

#ifndef NGROUPS
#define NGROUPS		32
#endif

#ifndef NOGROUP
#define NOGROUP		(-1)
#endif

#define MAXHOSTNAMELEN	64	/* max length of hostname */

#endif /* _ASM_PARAM_H */
