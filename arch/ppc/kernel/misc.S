/*
 * This file contains miscellaneous low-level functions.
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

#include <linux/config.h>
#include <linux/sys.h>
#include <asm/unistd.h>
#include <asm/errno.h>
#include <asm/processor.h>
#include "ppc_asm.tmpl"
#include "ppc_defs.h"

/* This instruction is not implemented on the PPC 601 or 603 */
#define tlbia \
	li	r4,128; \
	mtspr	CTR,r4; \
	li	r4,0; \
0:	tlbie	r4; \
	addi	r4,r4,0x1000; \
	bdnz	0b
	
_TEXT()

/*
 * Disable interrupts
 *	rc = _disable_interrupts()
 */
_GLOBAL(_disable_interrupts)
_GLOBAL(__cli)
_GLOBAL(_hard_cli)
	mfmsr	r0		/* Get current interrupt state */
	rlwinm	r3,r0,16+1,32-1,31	/* Extract old value of 'EE' */
	li	r4,0		/* Need [unsigned] value of MSR_EE */
	ori	r4,r4,MSR_EE	/* Set to turn off bit */
	andc	r0,r0,r4	/* Clears bit in (r4) */
	sync			/* Some chip revs have problems here... */
	mtmsr	r0		/* Update machine state */
	blr			/* Done */

/*
 * Enable interrupts
 *	_enable_interrupts(int state)
 * turns on interrupts if state = 1.
 */
_GLOBAL(_enable_interrupts)
	cmpi	0,r3,0		/* turning them on? */
	beqlr			/* nothing to do if state == 0 */
_GLOBAL(__sti)
_GLOBAL(_hard_sti)
	lis	r4,lost_interrupts@ha
	lwz	r4,lost_interrupts@l(r4)
	mfmsr	r3		/* Get current state */
	ori	r3,r3,MSR_EE	/* Turn on 'EE' bit */
	cmpi	0,r4,0		/* lost interrupts to process first? */
	bne-	do_lost_interrupts
	sync			/* Some chip revs have problems here... */
	mtmsr	r3		/* Update machine state */
	blr

#if 0
/*
 * Restore 'flags'
 *	__restore_flags(long val)
 */
_GLOBAL(__restore_flags)
	andi.	r0,r3,MSR_EE	/* enabling interrupts? */
	beq	2f
	lis	r4,lost_interrupts@ha
	lwz	r4,lost_interrupts@l(r4)
	cmpi	0,r4,0
	bne	do_lost_interrupts
2:	sync			/* Some chip revs have problems here... */
	mtmsr	r3
	isync
	blr
#endif
	
/*
 * We were about to enable interrupts but we have to simulate
 * some interrupts that were lost by enable_irq first.
 */
	.globl do_lost_interrupts
do_lost_interrupts:
	stwu	r1,-16(r1)
	mflr	r0
	stw	r0,20(r1)
	stw	r3,8(r1)
1:	bl	fake_interrupt
	lis	r4,lost_interrupts@ha
	lwz	r4,lost_interrupts@l(r4)
	cmpi	0,r4,0
	bne-	1b
	lwz	r3,8(r1)
	sync
	mtmsr	r3
	lwz	r0,20(r1)
	mtlr	r0
	addi	r1,r1,16
	blr

/*
 * Flush MMU TLB
 */
_GLOBAL(_tlbia)
	tlbia
	blr	

/*
 * Flush MMU TLB for a particular address
 */
_GLOBAL(_tlbie)
	tlbie	r3
	blr
/*
 * Atomic [test&set] exchange
 *
 *	void *xchg_u32(void *ptr, unsigned long val)
 * Changes the memory location '*ptr' to be val and returns
 * the previous value stored there.
 */
_GLOBAL(xchg_u32)
	mr	r5,r3		/* Save pointer */
10:	lwarx	r3,0,r5		/* Fetch old value & reserve */
	stwcx.	r4,0,r5		/* Update with new value */
	bne-	10b		/* Retry if "reservation" (i.e. lock) lost */
	blr

/*
 * Atomic add/sub/inc/dec operations
 *
 * void atomic_add(int c, int *v)
 * void atomic_sub(int c, int *v)
 * void atomic_inc(int *v)
 * void atomic_dec(int *v)
 * int atomic_dec_and_test(int *v)
 * int atomic_inc_return(int *v)
 * int atomic_dec_return(int *v)
 * void atomic_clear_mask(atomic_t mask, atomic_t *addr)
 * void atomic_set_mask(atomic_t mask, atomic_t *addr);
 */
_GLOBAL(atomic_add)
10:	lwarx	r5,0,r4		/* Fetch old value & reserve */
	add	r5,r5,r3	/* Perform 'add' operation */
	stwcx.	r5,0,r4		/* Update with new value */
	bne-	10b		/* Retry if "reservation" (i.e. lock) lost */
	blr
_GLOBAL(atomic_sub)
10:	lwarx	r5,0,r4		/* Fetch old value & reserve */
	sub	r5,r5,r3	/* Perform 'add' operation */
	stwcx.	r5,0,r4		/* Update with new value */
	bne-	10b		/* Retry if "reservation" (i.e. lock) lost */
	blr
_GLOBAL(atomic_inc)
10:	lwarx	r5,0,r3		/* Fetch old value & reserve */
	addi	r5,r5,1		/* Perform 'add' operation */
	stwcx.	r5,0,r3		/* Update with new value */
	bne-	10b		/* Retry if "reservation" (i.e. lock) lost */
	blr
_GLOBAL(atomic_inc_return)
10:	lwarx	r5,0,r3		/* Fetch old value & reserve */
	addi	r5,r5,1		/* Perform 'add' operation */
	stwcx.	r5,0,r3		/* Update with new value */
	bne-	10b		/* Retry if "reservation" (i.e. lock) lost */
	mr	r3,r5		/* Return new value */
	blr
_GLOBAL(atomic_dec)
10:	lwarx	r5,0,r3		/* Fetch old value & reserve */
	subi	r5,r5,1		/* Perform 'add' operation */
	stwcx.	r5,0,r3		/* Update with new value */
	bne-	10b		/* Retry if "reservation" (i.e. lock) lost */
	blr
_GLOBAL(atomic_dec_return)
10:	lwarx	r5,0,r3		/* Fetch old value & reserve */
	subi	r5,r5,1		/* Perform 'add' operation */
	stwcx.	r5,0,r3		/* Update with new value */
	bne-	10b		/* Retry if "reservation" (i.e. lock) lost */
	mr	r3,r5		/* Return new value */
	blr
_GLOBAL(atomic_dec_and_test)
10:	lwarx	r5,0,r3		/* Fetch old value & reserve */
	subi	r5,r5,1		/* Perform 'add' operation */
	stwcx.	r5,0,r3		/* Update with new value */
	bne-	10b		/* Retry if "reservation" (i.e. lock) lost */
	cmpi	0,r5,0		/* Return 'true' IFF 0 */
	li	r3,1
	beqlr
	li	r3,0
	blr
_GLOBAL(atomic_clear_mask)
10:	lwarx	r5,0,r4
	andc	r5,r5,r3
	stwcx.	r5,0,r4
	bne-	10b
	blr
_GLOBAL(atomic_set_mask)
10:	lwarx	r5,0,r4
	or	r5,r5,r3
	stwcx.	r5,0,r4
	bne-	10b
	blr

/*
 * I/O string operations
 *
 * insw(port, buf, len)
 * outsw(port, buf, len)
 * insl(port, buf, len)
 * outsl(port, buf, len)
 */
_GLOBAL(_insw)
	mtctr	r5
	subi	r4,r4,2
00:	lhbrx	r5,0,r3
	sthu	r5,2(r4)
	bdnz	00b
	blr

_GLOBAL(_outsw)
	mtctr	r5
	subi	r4,r4,2
00:	lhzu	r5,2(r4)
	sthbrx	r5,0,r3	
	bdnz	00b
	blr	

_GLOBAL(_insl)
	mtctr	r5
	subi	r4,r4,4
00:	lwbrx	r5,0,r3
	stwu	r5,4(r4)
	bdnz	00b
	blr

_GLOBAL(_outsl)
	mtctr	r5
	subi	r4,r4,4
00:	lwzu	r5,4(r4)
	stwbrx	r5,0,r3	
	bdnz	00b
	blr	

_GLOBAL(ide_insw)
	mtctr	r5
	subi	r4,r4,2
00:	lhzx	r5,0,r3
	sthu	r5,2(r4)
	bdnz	00b
	blr

_GLOBAL(ide_outsw)
	mtctr	r5
	subi	r4,r4,2
00:	lhzu	r5,2(r4)
	sthx	r5,0,r3	
	bdnz	00b
	blr	

/*
 * Extended precision shifts
 *
 * R3/R4 has 64 bit value
 * R5    has shift count
 * result in R3/R4
 *
 *  ashrdi3:     XXXYYY/ZZZAAA -> SSSXXX/YYYZZZ
 *  ashldi3:     XXXYYY/ZZZAAA -> YYYZZZ/AAA000
 */
_GLOBAL(__ashrdi3)
	li	r6,32
	sub	r6,r6,r5
	slw	r7,r3,r6	/* isolate YYY */
	srw	r4,r4,r5	/* isolate ZZZ */
	or	r4,r4,r7	/* YYYZZZ */
	sraw	r3,r3,r5	/* SSSXXX */
	blr
	
_GLOBAL(__ashldi3)
	li	r6,32
	sub	r6,r6,r5
	srw	r7,r4,r6	/* isolate ZZZ */
	slw	r4,r4,r5	/* AAA000 */
	slw	r3,r3,r5	/* YYY--- */
	or	r3,r3,r7	/* YYYZZZ */
	blr

_GLOBAL(abs)
	cmpi	0,r3,0
	bge	10f
	neg	r3,r3
10:	blr

_GLOBAL(_get_SP)
	mr	r3,r1		/* Close enough */
	blr

_GLOBAL(_get_PVR)
	mfspr	r3,PVR
	blr

_GLOBAL(cvt_fd)
cvt_fd:
	lfs	0,0(r3)
	stfd	0,0(r4)
	blr

/*
 * Fetch the current SR register
 *   get_SR(int index)
 */
_GLOBAL(get_SR)
	mfsrin	r4,r3
	mr	r3,r4
	blr

_GLOBAL(cvt_df)
cvt_df:
	lfd	0,0(r3)
	stfs	0,0(r4)
	blr

/*
 * Create a kernel thread
 *   __kernel_thread(flags, fn, arg)
 */
_GLOBAL(__kernel_thread)
	li	r0,__NR_clone
	sc
	cmpi	0,r3,0		/* parent or child? */
	bnelr			/* return if parent */
	mtlr	r4              /* fn addr in lr */
	mr	r3,r5	        /* load arg and call fn */
	blrl
	li	r0,__NR_exit	/* exit after child exits */
        li	r3,0
	sc

#define SYSCALL(name) \
_GLOBAL(name) \
	li	r0,__NR_##name; \
	sc; \
	bnslr; \
	lis	r4,errno@ha; \
	stw	r3,errno@l(r4); \
	li	r3,-1; \
	blr

#define __NR__exit __NR_exit

SYSCALL(idle)
SYSCALL(setup)
SYSCALL(sync)
SYSCALL(setsid)
SYSCALL(write)
SYSCALL(dup)
SYSCALL(execve)
SYSCALL(open)
SYSCALL(close)
SYSCALL(waitpid)


/* Why isn't this a) automatic, b) written in 'C'? */	
	.data
	.align 4
	.globl	sys_call_table
sys_call_table:
	.long sys_setup		/* 0 */
	.long sys_exit
	.long sys_fork
	.long sys_read
	.long sys_write
	.long sys_open		/* 5 */
	.long sys_close
	.long sys_waitpid
	.long sys_creat
	.long sys_link
	.long sys_unlink	/* 10 */
	.long sys_execve
	.long sys_chdir
	.long sys_time
	.long sys_mknod
	.long sys_chmod		/* 15 */
	.long sys_chown
	.long sys_break
	.long sys_stat
	.long sys_lseek
	.long sys_getpid	/* 20 */
	.long sys_mount
	.long sys_umount
	.long sys_setuid
	.long sys_getuid
	.long sys_stime		/* 25 */
	.long sys_ptrace
	.long sys_alarm
	.long sys_fstat
	.long sys_pause
	.long sys_utime		/* 30 */
	.long sys_stty
	.long sys_gtty
	.long sys_access
	.long sys_nice
	.long sys_ftime		/* 35 */
	.long sys_sync
	.long sys_kill
	.long sys_rename
	.long sys_mkdir
	.long sys_rmdir		/* 40 */
	.long sys_dup
	.long sys_pipe
	.long sys_times
	.long sys_prof
	.long sys_brk		/* 45 */
	.long sys_setgid
	.long sys_getgid
	.long sys_signal
	.long sys_geteuid
	.long sys_getegid	/* 50 */
	.long sys_acct
	.long sys_phys
	.long sys_lock
	.long sys_ioctl
	.long sys_fcntl		/* 55 */
	.long sys_mpx
	.long sys_setpgid
	.long sys_ulimit
	.long sys_olduname
	.long sys_umask		/* 60 */
	.long sys_chroot
	.long sys_ustat
	.long sys_dup2
	.long sys_getppid
	.long sys_getpgrp	/* 65 */
	.long sys_setsid
	.long sys_sigaction
	.long sys_sgetmask
	.long sys_ssetmask
	.long sys_setreuid	/* 70 */
	.long sys_setregid
	.long sys_sigsuspend
	.long sys_sigpending
	.long sys_sethostname
	.long sys_setrlimit	/* 75 */
	.long sys_getrlimit
	.long sys_getrusage
	.long sys_gettimeofday
	.long sys_settimeofday
	.long sys_getgroups	/* 80 */
	.long sys_setgroups
	.long ppc_select
	.long sys_symlink
	.long sys_lstat
	.long sys_readlink	/* 85 */
	.long sys_uselib
	.long sys_swapon
	.long sys_reboot
	.long old_readdir		/* was sys_readdir */
	.long sys_mmap		/* 90 */
	.long sys_munmap
	.long sys_truncate
	.long sys_ftruncate
	.long sys_fchmod
	.long sys_fchown	/* 95 */
	.long sys_getpriority
	.long sys_setpriority
	.long sys_profil
	.long sys_statfs
	.long sys_fstatfs	/* 100 */
	.long sys_ioperm
	.long sys_socketcall
	.long sys_syslog
	.long sys_setitimer
	.long sys_getitimer	/* 105 */
	.long sys_newstat
	.long sys_newlstat
	.long sys_newfstat
	.long sys_uname
	.long sys_iopl		/* 110 */
	.long sys_vhangup
	.long sys_idle
	.long sys_vm86
	.long sys_wait4
	.long sys_swapoff	/* 115 */
	.long sys_sysinfo
	.long sys_ipc
	.long sys_fsync
	.long sys_sigreturn
	.long sys_clone		/* 120 */
	.long sys_setdomainname
	.long sys_newuname
	.long sys_modify_ldt
	.long sys_adjtimex
	.long sys_mprotect	/* 125 */
	.long sys_sigprocmask
	.long sys_create_module
	.long sys_init_module
	.long sys_delete_module
	.long sys_get_kernel_syms	/* 130 */
	.long sys_quotactl
	.long sys_getpgid
	.long sys_fchdir
	.long sys_bdflush
	.long sys_sysfs		/* 135 */
	.long sys_personality
	.long 0				/* for afs_syscall */
	.long sys_setfsuid
	.long sys_setfsgid
	.long sys_llseek	/* 140 */
        .long sys_getdents
	.long ppc_select
	.long sys_flock
	.long sys_msync
	.long sys_readv		/* 145 */
	.long sys_writev
	.long sys_getsid
	.long sys_fdatasync
	.long sys_sysctl
	.long sys_mlock		/* 150 */
	.long sys_munlock
	.long sys_mlockall
	.long sys_munlockall
	.long sys_sched_setparam
	.long sys_sched_getparam	/* 155 */
	.long sys_sched_setscheduler
	.long sys_sched_getscheduler
	.long sys_sched_yield
	.long sys_sched_get_priority_max
	.long sys_sched_get_priority_min  /* 160 */
	.long sys_sched_rr_get_interval
	.long sys_nanosleep
	.long sys_mremap
	.long sys_setresuid
	.long sys_getresuid		/* 165 */
	.long sys_query_module
	.long sys_poll
	.long sys_nfsservctl
	.long sys_debug
	.space (NR_syscalls-170)*4

