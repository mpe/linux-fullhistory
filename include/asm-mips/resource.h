/*
 * Process resource limits
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 1996 by Ralf Baechle
 */
#ifndef __ASM_MIPS_RESOURCE_H
#define __ASM_MIPS_RESOURCE_H

/*
 * Resource limits
 */
#define RLIMIT_CPU 0			/* CPU time in ms */
#define RLIMIT_FSIZE 1			/* Maximum filesize */
#define RLIMIT_DATA 2			/* max data size */
#define RLIMIT_STACK 3			/* max stack size */
#define RLIMIT_CORE 4			/* max core file size */
#define RLIMIT_NOFILE 5			/* max number of open files */
#define RLIMIT_AS 6			/* mapped memory */
#define RLIMIT_RSS 7			/* max resident set size */
#define RLIMIT_NPROC 8			/* max number of processes */
#define RLIMIT_MEMLOCK 9		/* max locked-in-memory address space */

#define RLIM_NLIMITS 10			/* Number of limit flavors.  */

#ifdef __KERNEL__

#define INIT_RLIMITS					\
{							\
	{LONG_MAX, LONG_MAX},				\
	{LONG_MAX, LONG_MAX},				\
	{LONG_MAX, LONG_MAX},				\
	{_STK_LIM, _STK_LIM},				\
	{       0, LONG_MAX},				\
	{NR_OPEN, NR_OPEN},				\
	{LONG_MAX, LONG_MAX},				\
	{LONG_MAX, LONG_MAX},				\
	{MAX_TASKS_PER_USER, MAX_TASKS_PER_USER},	\
	{ LONG_MAX, LONG_MAX },				\
}

#endif /* __KERNEL__ */

#endif /* __ASM_MIPS_RESOURCE_H */
