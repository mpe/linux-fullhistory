#ifndef _PPC_RESOURCE_H
#define _PPC_RESOURCE_H

/*
 * These were swiped from asm-i386 so they don't fit well with the
 * powerstack very well at all.  Anyone want to go through them and
 * correct them?
 *                              -- Cort
 */

/*
 * Resource limits
 */

#define RLIMIT_CPU	0		/* CPU time in ms */
#define RLIMIT_FSIZE	1		/* Maximum filesize */
#define RLIMIT_DATA	2		/* max data size */
#define RLIMIT_STACK	3		/* max stack size */
#define RLIMIT_CORE	4		/* max core file size */
#define RLIMIT_RSS	5		/* max resident set size */
#define RLIMIT_NPROC	6		/* max number of processes */
#define RLIMIT_NOFILE	7		/* max number of open files */

#ifdef notdef
#define RLIMIT_MEMLOCK	8		/* max locked-in-memory address space*/
#endif

#define RLIM_NLIMITS	8

#endif
