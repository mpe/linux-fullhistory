#ifndef _ASM_PARISC_RT_SIGFRAME_H
#define _ASM_PARISC_RT_SIGFRAME_H

struct rt_sigframe {
	unsigned int tramp[4];
	struct siginfo info;
	struct ucontext uc;
};

/*
 * The 32-bit ABI wants at least 48 bytes for a function call frame:
 * 16 bytes for arg0-arg3, and 32 bytes for magic (the only part of
 * which Linux/parisc uses is sp-20 for the saved return pointer...)
 * Then, the stack pointer must be rounded to a cache line (64 bytes).
 */
#define SIGFRAME32		64
#define FUNCTIONCALLFRAME32	48
#define PARISC_RT_SIGFRAME_SIZE32					\
	(((sizeof(struct rt_sigframe) + FUNCTIONCALLFRAME32) + SIGFRAME32) & -SIGFRAME32)

#ifdef __LP64__
#define	SIGFRAME		128
#define FUNCTIONCALLFRAME	96
#define PARISC_RT_SIGFRAME_SIZE					\
	(((sizeof(struct rt_sigframe) + FUNCTIONCALLFRAME) + SIGFRAME) & -SIGFRAME)
#else
#define	SIGFRAME		SIGFRAME32
#define FUNCTIONCALLFRAME	FUNCTIONCALLFRAME32
#define PARISC_RT_SIGFRAME_SIZE	PARISC_RT_SIGFRAME_SIZE32
#endif

#endif
