#ifndef _PPC_RESOURCE_H
#define _PPC_RESOURCE_H

#define RLIMIT_CPU	0		/* CPU time in ms */
#define RLIMIT_FSIZE	1		/* Maximum filesize */
#define RLIMIT_DATA	2		/* max data size */
#define RLIMIT_STACK	3		/* max stack size */
#define RLIMIT_CORE	4		/* max core file size */
#define RLIMIT_RSS	5		/* max resident set size */
#define RLIMIT_NPROC	6		/* max number of processes */
#define RLIMIT_NOFILE	7		/* max number of open files */
#define RLIMIT_MEMLOCK	8		/* max locked-in-memory address space */
#define RLIMIT_AS	9		/* address space limit(?) */

#define RLIM_NLIMITS	10

#ifdef __KERNEL__

#define INIT_RLIMITS							\
{									\
    {LONG_MAX, LONG_MAX},			/* RLIMIT_CPU */	\
    {LONG_MAX, LONG_MAX},			/* RLIMIT_FSIZE */	\
    {LONG_MAX, LONG_MAX},			/* RLIMIT_DATA */	\
    {_STK_LIM, LONG_MAX},			/* RLIMIT_STACK */	\
    {       0, LONG_MAX},			/* RLIMIT_CORE */	\
    {LONG_MAX, LONG_MAX},			/* RLIMIT_RSS */	\
    {MAX_TASKS_PER_USER, MAX_TASKS_PER_USER},	/* RLIMIT_NPROC */	\
    { NR_OPEN,  NR_OPEN},			/* RLIMIT_NOFILE */	\
    {LONG_MAX, LONG_MAX},			/* RLIMIT_MEMLOCK */	\
    {LONG_MAX, LONG_MAX},			/* RLIMIT_AS */		\
}

#endif /* __KERNEL__ */

#endif
