#ifndef _ALPHA_RESOURCE_H
#define _ALPHA_RESOURCE_H

/*
 * Resource limits
 */

#define RLIMIT_CPU	0		/* CPU time in ms */
#define RLIMIT_FSIZE	1		/* Maximum filesize */
#define RLIMIT_DATA	2		/* max data size */
#define RLIMIT_STACK	3		/* max stack size */
#define RLIMIT_CORE	4		/* max core file size */
#define RLIMIT_RSS	5		/* max resident set size */
#define RLIMIT_NOFILE	6		/* max number of open files */
#define RLIMIT_AS	7		/* address space limit(?) */
#define RLIMIT_NPROC	8		/* max number of processes */
#define RLIMIT_MEMLOCK	9		/* max locked-in-memory address space */

#define RLIM_NLIMITS	10

#ifdef __KERNEL__

#define INIT_RLIMITS							\
{									\
    {LONG_MAX, LONG_MAX},			/* RLIMIT_CPU */	\
    {LONG_MAX, LONG_MAX},			/* RLIMIT_FSIZE */	\
    {LONG_MAX, LONG_MAX},			/* RLIMIT_DATA */	\
    {_STK_LIM, _STK_LIM},			/* RLIMIT_STACK */	\
    {       0, LONG_MAX},			/* RLIMIT_CORE */	\
    {LONG_MAX, LONG_MAX},			/* RLIMIT_RSS */	\
    { NR_OPEN,  NR_OPEN},			/* RLIMIT_NOFILE */	\
    {LONG_MAX, LONG_MAX},			/* RLIMIT_AS */		\
    {MAX_TASKS_PER_USER, MAX_TASKS_PER_USER},	/* RLIMIT_NPROC */	\
    {LONG_MAX, LONG_MAX},			/* RLIMIT_MEMLOCK */	\
}

#endif /* __KERNEL__ */

#endif /* _ALPHA_RESOURCE_H */
