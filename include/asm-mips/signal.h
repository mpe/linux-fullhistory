#ifndef __ASM_MIPS_SIGNAL_H
#define __ASM_MIPS_SIGNAL_H

/*
 * For now ...
 */
#include <linux/types.h>
typedef __u64	sigset_t;

#if 0
/*
 * This is what we should really use but the kernel can't handle
 * a non-scalar type yet.  Since we use 64 signals only anyway we
 * just use __u64 and pad another 64 bits in the kernel for now ...
 */
typedef struct {
	unsigned int	sigbits[4];
} sigset_t;
#endif

#define _NSIG		65
#define NSIG		_NSIG

/*
 * For 1.3.0 Linux/MIPS changed the signal numbers to be compatible the ABI.
 */
#define SIGHUP		 1	/* Hangup (POSIX).  */
#define SIGINT		 2	/* Interrupt (ANSI).  */
#define SIGQUIT		 3	/* Quit (POSIX).  */
#define SIGILL		 4	/* Illegal instruction (ANSI).  */
#define SIGTRAP		 5	/* Trace trap (POSIX).  */
#define SIGIOT		 6	/* IOT trap (4.2 BSD).  */
#define SIGABRT		 SIGIOT	/* Abort (ANSI).  */
#define SIGEMT		 7
#define SIGFPE		 8	/* Floating-point exception (ANSI).  */
#define SIGKILL		 9	/* Kill, unblockable (POSIX).  */
#define SIGBUS		10	/* BUS error (4.2 BSD).  */
#define SIGSEGV		11	/* Segmentation violation (ANSI).  */
#define SIGSYS		12
#define SIGPIPE		13	/* Broken pipe (POSIX).  */
#define SIGALRM		14	/* Alarm clock (POSIX).  */
#define SIGTERM		15	/* Termination (ANSI).  */
#define SIGUSR1		16	/* User-defined signal 1 (POSIX).  */
#define SIGUSR2		17	/* User-defined signal 2 (POSIX).  */
#define SIGCHLD		18	/* Child status has changed (POSIX).  */
#define SIGCLD		SIGCHLD	/* Same as SIGCHLD (System V).  */
#define SIGPWR		19	/* Power failure restart (System V).  */
#define SIGWINCH	20	/* Window size change (4.3 BSD, Sun).  */
#define SIGURG		21	/* Urgent condition on socket (4.2 BSD).  */
#define SIGIO		22	/* I/O now possible (4.2 BSD).  */
#define SIGPOLL		SIGIO	/* Pollable event occurred (System V).  */
#define SIGSTOP		23	/* Stop, unblockable (POSIX).  */
#define SIGTSTP		24	/* Keyboard stop (POSIX).  */
#define SIGCONT		25	/* Continue (POSIX).  */
#define SIGTTIN		26	/* Background read from tty (POSIX).  */
#define SIGTTOU		27	/* Background write to tty (POSIX).  */
#define SIGVTALRM	28	/* Virtual alarm clock (4.2 BSD).  */
#define SIGPROF		29	/* Profiling alarm clock (4.2 BSD).  */
#define SIGXCPU		30	/* CPU limit exceeded (4.2 BSD).  */
#define SIGXFSZ		31	/* File size limit exceeded (4.2 BSD).  */

/*
 * sa_flags values: SA_STACK is not currently supported, but will allow the
 * usage of signal stacks by using the (now obsolete) sa_restorer field in
 * the sigaction structure as a stack pointer. This is now possible due to
 * the changes in signal handling. LBT 010493.
 * SA_INTERRUPT is a no-op, but left due to historical reasons. Use the
 * SA_RESTART flag to get restarting signals (which were the default long ago)
 * SA_SHIRQ flag is for shared interrupt support on PCI and EISA.
 */
#define SA_STACK	0x1
#define SA_ONSTACK	SA_STACK
#define SA_RESTART	0x4
#define SA_NOCLDSTOP	0x20000
/* Non ABI signals */
#define SA_INTERRUPT	0x01000000
#define SA_NOMASK	0x02000000
#define SA_ONESHOT	0x04000000
#define SA_SHIRQ	0x08000000

#ifdef __KERNEL__
/*
 * These values of sa_flags are used only by the kernel as part of the
 * irq handling routines.
 *
 * SA_INTERRUPT is also used by the irq handling routines.
 */
#define SA_PROBE SA_ONESHOT
#define SA_SAMPLE_RANDOM SA_RESTART
#endif

#define SIG_BLOCK          1	/* for blocking signals */
#define SIG_UNBLOCK        2	/* for unblocking signals */
#define SIG_SETMASK        3	/* for setting the signal mask */

/* Type of a signal handler.  */
typedef void (*__sighandler_t)(int);

/* Fake signal functions */
#define SIG_DFL	((__sighandler_t)0)	/* default signal handling */
#define SIG_IGN	((__sighandler_t)1)	/* ignore signal */
#define SIG_ERR	((__sighandler_t)-1)	/* error return from signal */

struct sigaction {
	unsigned int	sa_flags;
	__sighandler_t	sa_handler;
	sigset_t	sa_mask;
	/*
	 * To keep the ABI structure size we have to fill a little gap ...
	 */
	unsigned int	sa_mask_pad[2];

	/* Abi says here follows reserved int[2] */
	void		(*sa_restorer)(void);
#if __mips < 3
	/*
	 * For 32 bit code we have to pad struct sigaction to get
	 * constant size for the ABI
	 */
	int		pad0[1];	/* reserved */
#endif
};

#ifdef __KERNEL__
#include <asm/sigcontext.h>
#endif

#endif /* __ASM_MIPS_SIGNAL_H */
