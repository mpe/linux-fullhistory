#ifndef __LINUX_ACCT_H
#define __LINUX_ACCT_H

#define ACCT_COMM 16

struct acct
{
	char	ac_comm[ACCT_COMM];	/* Accounting command name */
	time_t	ac_utime;		/* Accounting user time */
	time_t	ac_stime;		/* Accounting system time */
	time_t	ac_etime;		/* Accounting elapsed time */
	time_t	ac_btime;		/* Beginning time */
	uid_t	ac_uid;			/* Accounting user ID */
	gid_t	ac_gid;			/* Accounting group ID */
	dev_t	ac_tty;			/* controlling tty */
	char	ac_flag;		/* Accounting flag */
	long	ac_minflt;		/* Accounting minor pagefaults */
	long	ac_majflt;		/* Accounting major pagefaults */
	long	ac_exitcode;		/* Accounting process exitcode */
};

#define AFORK	0001	/* has executed fork, but no exec */
#define ASU	0002	/* used super-user privileges */
#define ACORE	0004	/* dumped core */
#define AXSIG	0010	/* killed by a signal */

#define AHZ     100

#endif
