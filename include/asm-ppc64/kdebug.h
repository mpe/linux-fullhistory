#ifndef _PPC64_KDEBUG_H
#define _PPC64_KDEBUG_H 1

/* nearly identical to x86_64/i386 code */

#include <linux/notifier.h>

struct pt_regs;

struct die_args {
	struct pt_regs *regs;
	const char *str;
	long err;
	int trapnr;
	int signr;
};

/*
   Note - you should never unregister because that can race with NMIs.
   If you really want to do it first unregister - then synchronize_kernel -
   then free.
 */
int register_die_notifier(struct notifier_block *nb);
extern struct notifier_block *ppc64_die_chain;

/* Grossly misnamed. */
enum die_val {
	DIE_OOPS = 1,
	DIE_IABR_MATCH,
	DIE_DABR_MATCH,
	DIE_BPT,
	DIE_SSTEP,
	DIE_GPF,
	DIE_PAGE_FAULT,
};

static inline int notify_die(enum die_val val,char *str,struct pt_regs *regs,long err,int trap, int sig)
{
	struct die_args args = { .regs=regs, .str=str, .err=err, .trapnr=trap,.signr=sig };
	return notifier_call_chain(&ppc64_die_chain, val, &args);
}

#endif
