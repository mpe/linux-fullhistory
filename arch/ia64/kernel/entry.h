/*
 * Preserved registers that are shared between code in ivt.S and entry.S.  Be
 * careful not to step on these!
 */
#define pEOI		p1	/* should leave_kernel write EOI? */
#define pKern		p2	/* will leave_kernel return to kernel-mode? */
#define pSys		p4	/* are we processing a (synchronous) system call? */
#define pNonSys		p5	/* complement of pSys */
