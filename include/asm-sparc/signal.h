/* $Id: signal.h,v 1.21 1996/04/25 06:13:28 davem Exp $ */
#ifndef _ASMSPARC_SIGNAL_H
#define _ASMSPARC_SIGNAL_H

#include <asm/sigcontext.h>

/* On the Sparc the signal handlers get passed a 'sub-signal' code
 * for certain signal types, which we document here.
 */
#define _NSIG             32
#define NSIG		_NSIG

#define SIGHUP		 1
#define SIGINT		 2
#define SIGQUIT		 3
#define SIGILL		 4
#define    SUBSIG_STACK       0
#define    SUBSIG_ILLINST     2
#define    SUBSIG_PRIVINST    3
#define    SUBSIG_BADTRAP(t)  (0x80 + (t))

#define SIGTRAP		 5
#define SIGABRT		 6
#define SIGIOT		 6

#define SIGEMT           7
#define    SUBSIG_TAG    10

#define SIGFPE		 8
#define    SUBSIG_FPDISABLED     0x400
#define    SUBSIG_FPERROR        0x404
#define    SUBSIG_FPINTOVFL      0x001
#define    SUBSIG_FPSTSIG        0x002
#define    SUBSIG_IDIVZERO       0x014
#define    SUBSIG_FPINEXACT      0x0c4
#define    SUBSIG_FPDIVZERO      0x0c8
#define    SUBSIG_FPUNFLOW       0x0cc
#define    SUBSIG_FPOPERROR      0x0d0
#define    SUBSIG_FPOVFLOW       0x0d4

#define SIGKILL		 9
#define SIGBUS          10
#define    SUBSIG_BUSTIMEOUT    1
#define    SUBSIG_ALIGNMENT     2
#define    SUBSIG_MISCERROR     5

#define SIGSEGV		11
#define    SUBSIG_NOMAPPING     3
#define    SUBSIG_PROTECTION    4
#define    SUBSIG_SEGERROR      5

#define SIGSYS          12
#define SIGPIPE		13
#define SIGALRM		14
#define SIGTERM		15
#define SIGURG          16

/* SunOS values which deviate from the Linux/i386 ones */
#define SIGSTOP		17
#define SIGTSTP		18
#define SIGCONT		19
#define SIGCHLD		20
#define SIGTTIN		21
#define SIGTTOU		22
#define SIGIO		23
#define SIGPOLL		SIGIO   /* SysV name for SIGIO */
#define SIGXCPU		24
#define SIGXFSZ		25
#define SIGVTALRM	26
#define SIGPROF		27
#define SIGWINCH	28
#define SIGLOST		29
#define SIGUSR1		30
#define SIGUSR2		31

#ifndef __ASSEMBLY__

typedef unsigned long sigset_t;

#ifdef __KERNEL__
#include <asm/sigcontext.h>
#endif

/* A SunOS sigstack */
struct sigstack {
	char *the_stack;
	int   cur_status;
};

/* Sigvec flags */
#define SV_SSTACK    1     /* This signal handler should use sig-stack */
#define SV_INTR      2     /* Sig return should not restart system call */
#define SV_RESET     4     /* Set handler to SIG_DFL upon taken signal */
#define SV_IGNCHILD  8     /* Do not send SIGCHLD */

/*
 * sa_flags values: SA_STACK is not currently supported, but will allow the
 * usage of signal stacks by using the (now obsolete) sa_restorer field in
 * the sigaction structure as a stack pointer. This is now possible due to
 * the changes in signal handling. LBT 010493.
 * SA_INTERRUPT is a no-op, but left due to historical reasons. Use the
 * SA_RESTART flag to get restarting signals (which were the default long ago)
 * SA_SHIRQ flag is for shared interrupt support on PCI and EISA.
 */
#define SA_NOCLDSTOP	SV_IGNCHILD
#define SA_STACK	SV_SSTACK
#define SA_RESTART	SV_INTR
#define SA_ONESHOT	SV_RESET
#define SA_INTERRUPT	0x10
#define SA_NOMASK	0x20
#define SA_SHIRQ	0x40

#define SIG_BLOCK          0x01	/* for blocking signals */
#define SIG_UNBLOCK        0x02	/* for unblocking signals */
#define SIG_SETMASK        0x04	/* for setting the signal mask */

#ifdef __KERNEL__
/*
 * These values of sa_flags are used only by the kernel as part of the
 * irq handling routines.
 *
 * SA_INTERRUPT is also used by the irq handling routines.
 *
 * DJHR
 * SA_STATIC_ALLOC is used for the SPARC system to indicate that this
 * interrupt handler's irq structure should be statically allocated
 * by the request_irq routine.
 * The alternative is that arch/sparc/kernel/irq.c has carnal knowledge
 * of interrupt usage and that sucks. Also without a flag like this
 * it may be possible for the free_irq routine to attempt to free
 * statically allocated data.. which is NOT GOOD.
 *
 */
#define SA_PROBE SA_ONESHOT
#define SA_SAMPLE_RANDOM SA_RESTART
#define SA_STATIC_ALLOC		0x80
#endif

/* Type of a signal handler.  */
#ifdef __KERNEL__
typedef void (*__sighandler_t)(int, int, struct sigcontext_struct *, char *);
#else
typedef void (*__sighandler_t)(int);
#endif

#define SIG_DFL	((__sighandler_t)0)	/* default signal handling */
#define SIG_IGN	((__sighandler_t)1)	/* ignore signal */
#define SIG_ERR	((__sighandler_t)-1)	/* error return from signal */

struct sigaction {
	__sighandler_t  sa_handler;
	sigset_t        sa_mask;
	unsigned long   sa_flags;
};

#endif /* !(__ASSEMBLY__) */

#endif /* !(_ASMSPARC_SIGNAL_H) */
