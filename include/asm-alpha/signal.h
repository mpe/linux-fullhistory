#ifndef _ASMAXP_SIGNAL_H
#define _ASMAXP_SIGNAL_H

typedef unsigned long sigset_t;		/* at least 32 bits */

#define _NSIG             32
#define NSIG		_NSIG

/*
 * Linux/AXP has different signal numbers that Linux/i386: I'm trying
 * to make it OSF/1 binary compatible, at least for normal binaries.
 */
#define SIGHUP		 1
#define SIGINT		 2
#define SIGQUIT		 3
#define SIGILL		 4
#define SIGTRAP		 5
#define SIGABRT		 6
#define SIGEMT		 7
#define SIGFPE		 8
#define SIGKILL		 9
#define SIGBUS		10
#define SIGSEGV		11
#define SIGSYS		12
#define SIGPIPE		13
#define SIGALRM		14
#define SIGTERM		15
#define SIGURG		16
#define SIGSTOP		17
#define SIGTSTP		18
#define SIGCONT		19
#define SIGCHLD		20
#define SIGTTIN		21
#define SIGTTOU		22
#define SIGIO		23
#define SIGXCPU		24
#define SIGXFSZ		25
#define SIGVTALRM	26
#define SIGPROF		27
#define SIGWINCH	28
#define SIGINFO		29
#define SIGUSR1		30
#define SIGUSR2		31

#define SIGPOLL	SIGIO

/*
 * sa_flags values: SA_STACK is not currently supported, but will allow the
 * usage of signal stacks by using the (now obsolete) sa_restorer field in
 * the sigaction structure as a stack pointer. This is now possible due to
 * the changes in signal handling. LBT 010493.
 * SA_INTERRUPT is a no-op, but left due to historical reasons. Use the
 * SA_RESTART flag to get restarting signals (which were the default long ago)
 */
#define SA_NOCLDSTOP	0x00000004

#define SA_STACK	0x00000001
#define SA_RESTART	0x00000002
#define SA_INTERRUPT	0x20000000
#define SA_NOMASK	0x00000008
#define SA_ONESHOT	0x00000010

#define SIG_BLOCK          1	/* for blocking signals */
#define SIG_UNBLOCK        2	/* for unblocking signals */
#define SIG_SETMASK        3	/* for setting the signal mask */

/* Type of a signal handler.  */
typedef void (*__sighandler_t)(int);

#define SIG_DFL	((__sighandler_t)0)	/* default signal handling */
#define SIG_IGN	((__sighandler_t)1)	/* ignore signal */
#define SIG_ERR	((__sighandler_t)-1)	/* error return from signal */

struct sigaction {
	__sighandler_t	sa_handler;
	sigset_t	sa_mask;
	unsigned int	sa_flags;
};

#ifdef __KERNEL__

struct sigcontext_struct {
	/*
	 * what should we have here? I'd probably better use the same
	 * stack layout as OSF/1, just in case we ever want to try
	 * running their binaries.. 
	 *
	 * This is the basic layout, but I don't know if we'll ever
	 * actually fill in all the values..
	 */
	 long		sc_onstack;
	 long		sc_mask;
	 long		sc_pc;
	 long		sc_ps;
	 long		sc_regs[32];
	 long		sc_ownedfp;
	 long		sc_fpregs[32];
	 unsigned long	sc_fpcr;
	 unsigned long	sc_fp_control;
	 unsigned long	sc_reserved1, sc_reserved2;
	 unsigned long	sc_ssize;
	 char *		sc_sbase;
	 unsigned long	sc_traparg_a0;
	 unsigned long	sc_traparg_a1;
	 unsigned long	sc_traparg_a2;
	 unsigned long	sc_fp_trap_pc;
	 unsigned long	sc_fp_trigger_sum;
	 unsigned long	sc_fp_trigger_inst;
	 unsigned long	sc_retcode[2];
};

#endif

#endif
