/*
 * Copyright 2003 PathScale, Inc.
 *
 * Licensed under the GPL
 */

#ifndef __SYSDEP_X86_64_PTRACE_USER_H__
#define __SYSDEP_X86_64_PTRACE_USER_H__

#define __FRAME_OFFSETS
#include <asm/ptrace.h>
#undef __FRAME_OFFSETS

#define PT_INDEX(off) ((off) / sizeof(unsigned long))

#define PT_SYSCALL_NR(regs) ((regs)[PT_INDEX(ORIG_RAX)])
#define PT_SYSCALL_NR_OFFSET (ORIG_RAX)

#define PT_SYSCALL_ARG1(regs) (((unsigned long *) (regs))[PT_INDEX(RDI)])
#define PT_SYSCALL_ARG1_OFFSET (RDI)

#define PT_SYSCALL_ARG2(regs) (((unsigned long *) (regs))[PT_INDEX(RSI)])
#define PT_SYSCALL_ARG2_OFFSET (RSI)

#define PT_SYSCALL_ARG3(regs) (((unsigned long *) (regs))[PT_INDEX(RDX)])
#define PT_SYSCALL_ARG3_OFFSET (RDX)

#define PT_SYSCALL_ARG4(regs) (((unsigned long *) (regs))[PT_INDEX(RCX)])
#define PT_SYSCALL_ARG4_OFFSET (RCX)

#define PT_SYSCALL_ARG5(regs) (((unsigned long *) (regs))[PT_INDEX(R8)])
#define PT_SYSCALL_ARG5_OFFSET (R8)

#define PT_SYSCALL_ARG6(regs) (((unsigned long *) (regs))[PT_INDEX(R9)])
#define PT_SYSCALL_ARG6_OFFSET (R9)

#define PT_SYSCALL_RET_OFFSET (RAX)

#define PT_IP_OFFSET (RIP)
#define PT_IP(regs) ((regs)[PT_INDEX(RIP)])

#define PT_SP_OFFSET (RSP)
#define PT_SP(regs) ((regs)[PT_INDEX(RSP)])

#define PT_ORIG_RAX_OFFSET (ORIG_RAX)
#define PT_ORIG_RAX(regs) ((regs)[PT_INDEX(ORIG_RAX)])

#define MAX_REG_OFFSET (FRAME_SIZE)
#define MAX_REG_NR ((MAX_REG_OFFSET) / sizeof(unsigned long))

/* x86_64 FC3 doesn't define this in /usr/include/linux/ptrace.h even though
 * it's defined in the kernel's include/linux/ptrace.h. Additionally, use the
 * 2.4 name and value for 2.4 host compatibility.
 */
#ifndef PTRACE_OLDSETOPTIONS
#define PTRACE_OLDSETOPTIONS 21
#endif

#endif

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
