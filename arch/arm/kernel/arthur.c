/*
 * Arthur personality
 * Copyright (C) 1998 Philip Blundell
 */

#include <linux/personality.h>
#include <linux/module.h>
#include <linux/stddef.h>
#include <linux/signal.h>
#include <linux/sched.h>

#include <asm/ptrace.h>

/* RISC OS doesn't have many signals, and a lot of those that it does
   have don't map easily to any Linux equivalent.  Never mind.  */

#define RISCOS_SIGABRT		1
#define RISCOS_SIGFPE		2
#define RISCOS_SIGILL		3
#define RISCOS_SIGINT		4
#define RISCOS_SIGSEGV		5
#define RISCOS_SIGTERM		6
#define RISCOS_SIGSTAK		7
#define RISCOS_SIGUSR1		8
#define RISCOS_SIGUSR2		9
#define RISCOS_SIGOSERROR	10

static unsigned long riscos_to_linux_signals[32] = {
	0,	1,	2,	3,	4,	5,	6,	7,
	8,	9,	10,	11,	12,	13,	14,	15,
	16,	17,	18,	19,	20,	21,	22,	23,
	24,	25,	26,	27,	28,	29,	30,	31
};

static unsigned long linux_to_riscos_signals[32] = {
	0,		-1,		RISCOS_SIGINT,	-1,
       	RISCOS_SIGILL,	5,		RISCOS_SIGABRT,	7,
	RISCOS_SIGFPE,	9,		RISCOS_SIGUSR1,	RISCOS_SIGSEGV,	
	RISCOS_SIGUSR2,	13,		14,		RISCOS_SIGTERM,
	16,		17,		18,		19,
	20,		21,		22,		23,
	24,		25,		26,		27,
	28,		29,		30,		31
};

static void arthur_lcall7(int nr, struct pt_regs *regs)
{
	struct siginfo info;
	info.si_signo = SIGSWI;
	info.si_code = nr;
	/* Bounce it to the emulator */
	send_sig_info(SIGSWI, &info, current);
}

static struct exec_domain riscos_exec_domain = {
	"Arthur",	/* name */
	(lcall7_func)arthur_lcall7,
	PER_RISCOS, PER_RISCOS,
	riscos_to_linux_signals,
	linux_to_riscos_signals,
#ifdef MODULE
	&__this_module,	/* No usage counter. */
#else
	NULL,
#endif
	NULL		/* Nothing after this in the list. */
};

/*
 * We could do with some locking to stop Arthur being removed while
 * processes are using it.
 */

#ifdef MODULE
int init_module(void)
#else
int initialise_arthur(void)
#endif
{
	return register_exec_domain(&riscos_exec_domain);
}

#ifdef MODULE
void cleanup_module(void)
{
	unregister_exec_domain(&riscos_exec_domain);
}
#endif
