#ifndef __ASM_MIPS_UCONTEXT_H
#define __ASM_MIPS_UCONTEXT_H

struct ucontext {
	unsigned long	  uc_flags;
	struct ucontext  *uc_link;
	stack_t		  uc_stack;
	struct sigcontext uc_mcontext;
	sigset_t	  uc_sigmask;	/* mask last for extensibility */
};

#endif /* __ASM_MIPS_UCONTEXT_H */
