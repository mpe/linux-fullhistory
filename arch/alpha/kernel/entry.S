/*
 * alpha/entry.S
 *
 * kernel entry-points
 */

#include <asm/system.h>

#define halt	.long PAL_halt
#define rti	.long PAL_rti
#define SIGCHLD	20

#define NR_SYSCALLS 350
#define osf_vfork sys_fork

/*
 * These offsets must match with "struct hae" in io.h:
 */
#define HAE_CACHE	0
#define HAE_REG		8

/*
 * stack offsets
 */
#define SP_OFF		184

#define SWITCH_STACK_SIZE 320

/*
 * task structure offsets
 */
#define TASK_STATE	0
#define TASK_COUNTER	8
#define TASK_PRIORITY	16
#define TASK_SIGNAL	24
#define TASK_BLOCKED	32
#define TASK_FLAGS	40

/*
 * task flags (must match include/linux/sched.h):
 */
#define	PF_PTRACED	0x00000010

/*
 * This defines the normal kernel pt-regs layout.
 *
 * regs 9-15 preserved by C code
 * regs 16-18 saved by PAL-code
 * regs 29-30 saved and set up by PAL-code
 * JRP - Save regs 16-18 in a special area of the stack, so that
 * the palcode-provided values are available to the signal handler.
 */
#define SAVE_ALL			\
	subq	$30,184,$30;		\
	stq	$0,0($30);		\
	stq	$1,8($30);		\
	stq	$2,16($30);		\
	stq	$3,24($30);		\
	stq	$4,32($30);		\
	lda	$2,hae;			\
	stq	$5,40($30);		\
	stq	$6,48($30);		\
	stq	$7,56($30);		\
	stq	$8,64($30);		\
	stq	$19,72($30);		\
	stq	$20,80($30);		\
	stq	$21,88($30);		\
	ldq	$2,HAE_CACHE($2);	\
	stq	$22,96($30);		\
	stq	$23,104($30);		\
	stq	$24,112($30);		\
	stq	$25,120($30);		\
	stq	$26,128($30);		\
	stq	$27,136($30);		\
	stq	$28,144($30);		\
	stq	$2,152($30);		\
	stq	$16,160($30);		\
	stq	$17,168($30);		\
	stq	$18,176($30)

#define RESTORE_ALL			\
	lda	$19,hae;		\
	ldq	$0,0($30);		\
	ldq	$1,8($30);		\
	ldq	$2,16($30);		\
	ldq	$3,24($30);		\
	ldq	$20,152($30);		\
	ldq	$21,HAE_CACHE($19);	\
	ldq	$4,32($30);		\
	ldq	$5,40($30);		\
	ldq	$6,48($30);		\
	ldq	$7,56($30);		\
	subq	$20,$21,$20;		\
	ldq	$8,64($30);		\
	beq	$20,99f;		\
	ldq	$20,HAE_REG($19);	\
	addq	$31,7,$16;		\
	call_pal PAL_swpipl;		\
	stq	$21,HAE_CACHE($19);	\
	stq	$21,0($20);		\
	bis	$0,$0,$16;		\
	call_pal PAL_swpipl;		\
	ldq	$0,0($30);		\
	ldq	$1,8($30);		\
99:;					\
	ldq	$19,72($30);		\
	ldq	$20,80($30);		\
	ldq	$21,88($30);		\
	ldq	$22,96($30);		\
	ldq	$23,104($30);		\
	ldq	$24,112($30);		\
	ldq	$25,120($30);		\
	ldq	$26,128($30);		\
	ldq	$27,136($30);		\
	ldq	$28,144($30);		\
	addq	$30,184,$30

.text
.set noat
#if defined(__linux__) && !defined(__ELF__)
  .set singlegp
#endif

.align 3
.globl	entInt
.ent	entInt
entInt:
	SAVE_ALL
	ldq	$8,current_set
	jsr	$26,do_entInt
	br	$31,ret_from_sys_call
.end entInt

.align 3
.globl	entMM
.ent	entMM
entMM:
	SAVE_ALL
/* save $9 - $15 so the inline exception code can manipulate them.  */
	subq	$30,56,$30
	stq	$9,0($30)
	stq	$10,8($30)
	stq	$11,16($30)
	stq	$12,24($30)
	stq	$13,32($30)
	stq	$14,40($30)
	stq	$15,48($30)
	addq	$30,56,$19
/* handle the fault */
	ldq	$8,current_set
	jsr	$26,do_page_fault
/* reload the registers after the exception code played.  */
	ldq	$9,0($30)
	ldq	$10,8($30)
	ldq	$11,16($30)
	ldq	$12,24($30)
	ldq	$13,32($30)
	ldq	$14,40($30)
	ldq	$15,48($30)
	addq	$30,56,$30
/* finish up the syscall as normal.  */
	br	ret_from_sys_call
.end entMM

.align 3
.globl	entArith
.ent	entArith
entArith:
	SAVE_ALL
	ldq	$8,current_set
	/* How much of a win is this clockwise?  We are, after all, messing
	   up the call/return prefetch stack.  -- rth */
	lda	$27,do_entArith
	lda	$26,ret_from_sys_call
	jsr	$31,($27),do_entArith
.end entArith

.align 3
.globl	entIF
.ent	entIF
entIF:
	SAVE_ALL
	ldq	$8,current_set
	lda	$27,do_entIF
	lda	$26,ret_from_sys_call
	jsr	$31,($27),do_entIF
.end entIF

/*
 * Fork() is one of the special system calls: it needs to
 * save the callee-saved regs so that the regs can be found
 * for the new process.. We save them in the "context switch"
 * stack format (see arch/alpha/kernel/process.c).
 *
 * Also, for the kernel fork, we need to fake the system call
 * stack buildup, as we can't do system calls from kernel space.
 */
.align 3
.ent	kernel_clone
kernel_clone:
	.frame $30, 0, $26
	.prologue 0
	subq $30,6*8,$30
	stq $31,0($30)
	stq $26,8($30)
	stq $29,16($30)
	stq $16,24($30)
	stq $17,32($30)
	stq $18,40($30)
	bis $31,2,$0	/* Register v0: syscall nr for fork() */
	SAVE_ALL
	jsr $26,sys_clone
	stq $0,0($30)
	br $31,ret_from_sys_call
.end	kernel_clone

/*
 * __kernel_thread(clone_flags, fn, arg)
 */
.align 3
.globl	__kernel_thread
.ent	__kernel_thread
__kernel_thread:
	.frame $30, 4*8, $26
	subq $30,4*8,$30
	stq $10,16($30)
	stq $9,8($30)
	stq $26,0($30)
	.prologue 0
	bis $17,$17,$9		/* save fn */
	bis $18,$18,$10		/* save arg */
	bsr $26,kernel_clone
	bne $20,1f		/* $20 is non-zero in child */
	ldq $26,0($30)
	ldq $9,8($30)
	ldq $10,16($30)
	addq $30,4*8,$30
	ret $31,($26),1
/* this is in child: look out as we don't have any stack here.. */
1:	bis $9,$9,$27		/* get fn */
	br $29,2f
2:	ldgp $29,0($29)
	bis $10,$10,$16		/* get arg */
	ldq $8,current_set
	jsr $26,($27)
	bis $0,$0,$16
	jsr $26,sys_exit
	call_pal PAL_halt
.end	__kernel_thread

.align 3
.ent	do_switch_stack
do_switch_stack:
	lda $30,-SWITCH_STACK_SIZE($30)
	stq  $9,0($30)
	stq $10,8($30)
	stq $11,16($30)
	stq $12,24($30)
	stq $13,32($30)
	stq $14,40($30)
	stq $15,48($30)
	stq $26,56($30)
	stt $f0,64($30)
	stt $f1,72($30)
	stt $f2,80($30)
	stt $f3,88($30)
	stt $f4,96($30)
	stt $f5,104($30)
	stt $f6,112($30)
	stt $f7,120($30)
	stt $f8,128($30)
	stt $f9,136($30)
	stt $f10,144($30)
	stt $f11,152($30)
	stt $f12,160($30)
	stt $f13,168($30)
	stt $f14,176($30)
	stt $f15,184($30)
	stt $f16,192($30)
	stt $f17,200($30)
	stt $f18,208($30)
	stt $f19,216($30)
	stt $f20,224($30)
	stt $f21,232($30)
	stt $f22,240($30)
	stt $f23,248($30)
	stt $f24,256($30)
	stt $f25,264($30)
	stt $f26,272($30)
	stt $f27,280($30)
	mf_fpcr $f0		# get fpcr
	stt $f28,288($30)
	stt $f29,296($30)
	stt $f30,304($30)
	stt $f0,312($30)	# save fpcr in slot of $f31
	ret $31,($1),1
.end do_switch_stack

.align 3
.ent	undo_switch_stack
undo_switch_stack:
	ldq  $9,0($30)
	ldq $10,8($30)
	ldq $11,16($30)
	ldq $12,24($30)
	ldq $13,32($30)
	ldq $14,40($30)
	ldq $15,48($30)
	ldq $26,56($30)
	ldt $f30,312($30)	# get saved fpcr
	ldt $f0,64($30)
	ldt $f1,72($30)
	ldt $f2,80($30)
	ldt $f3,88($30)
	mt_fpcr $f30		# install saved fpcr
	ldt $f4,96($30)
	ldt $f5,104($30)
	ldt $f6,112($30)
	ldt $f7,120($30)
	ldt $f8,128($30)
	ldt $f9,136($30)
	ldt $f10,144($30)
	ldt $f11,152($30)
	ldt $f12,160($30)
	ldt $f13,168($30)
	ldt $f14,176($30)
	ldt $f15,184($30)
	ldt $f16,192($30)
	ldt $f17,200($30)
	ldt $f18,208($30)
	ldt $f19,216($30)
	ldt $f20,224($30)
	ldt $f21,232($30)
	ldt $f22,240($30)
	ldt $f23,248($30)
	ldt $f24,256($30)
	ldt $f25,264($30)
	ldt $f26,272($30)
	ldt $f27,280($30)
	ldt $f28,288($30)
	ldt $f29,296($30)
	ldt $f30,304($30)
	lda $30,SWITCH_STACK_SIZE($30)
	ret $31,($1),1
.end undo_switch_stack

.align 3
.globl	entUna
.ent	entUna
entUna:
	lda $30,-256($30)
	stq $0,0($30)
	ldq $0,256($30)		/* get PS */
	stq $1,8($30)
	stq $2,16($30)
	stq $3,24($30)
	and $0,8,$0		/* user mode? */
	stq $4,32($30)
	bne $0,entUnaUser	/* yup -> do user-level unaligned fault */
	stq $5,40($30)
	stq $6,48($30)
	stq $7,56($30)
	stq $8,64($30)
	stq $9,72($30)
	stq $10,80($30)
	stq $11,88($30)
	stq $12,96($30)
	stq $13,104($30)
	stq $14,112($30)
	stq $15,120($30)
	/* 16-18 PAL-saved */
	stq $19,152($30)
	stq $20,160($30)
	stq $21,168($30)
	stq $22,176($30)
	stq $23,184($30)
	stq $24,192($30)
	stq $25,200($30)
	stq $26,208($30)
	stq $27,216($30)
	stq $28,224($30)
	stq $29,232($30)
	stq $30,240($30)
	stq $31,248($30)
	ldq $8,current_set
	jsr $26,do_entUna
	ldq $0,0($30)
	ldq $1,8($30)
	ldq $2,16($30)
	ldq $3,24($30)
	ldq $4,32($30)
	ldq $5,40($30)
	ldq $6,48($30)
	ldq $7,56($30)
	ldq $8,64($30)
	ldq $9,72($30)
	ldq $10,80($30)
	ldq $11,88($30)
	ldq $12,96($30)
	ldq $13,104($30)
	ldq $14,112($30)
	ldq $15,120($30)
	/* 16-18 PAL-saved */
	ldq $19,152($30)
	ldq $20,160($30)
	ldq $21,168($30)
	ldq $22,176($30)
	ldq $23,184($30)
	ldq $24,192($30)
	ldq $25,200($30)
	ldq $26,208($30)
	ldq $27,216($30)
	ldq $28,224($30)
	ldq $29,232($30)
	ldq $30,240($30)
	lda $30,256($30)
	rti
.end entUna

.align 3
.ent	entUnaUser
entUnaUser:
	ldq $0,0($30)			/* restore original $0 */
	lda $30,256($30)		/* pop entUna's stack frame */
	SAVE_ALL			/* setup normal kernel stack */
	lda $30,-56($30)
	stq $9,0($30)
	stq $10,8($30)
	stq $11,16($30)
	stq $12,24($30)
	stq $13,32($30)
	stq $14,40($30)
	stq $15,48($30)
	bis $31,$30,$19
	ldq $8,current_set
	jsr $26,do_entUnaUser
	ldq $9,0($30)
	ldq $10,8($30)
	ldq $11,16($30)
	ldq $12,24($30)
	ldq $13,32($30)
	ldq $14,40($30)
	ldq $15,48($30)
	lda $30,56($30)
	br $31,ret_from_sys_call

.end	entUnaUser

/*
 * A fork is the same as clone(SIGCHLD, 0);
 */
.align 3
.globl	sys_fork
.ent	sys_fork
sys_fork:
	bsr $1,do_switch_stack
	bis $31,SIGCHLD,$16
	bis $31,$31,$17
	bis $30,$30,$18
	jsr $26,alpha_clone
	bsr $1,undo_switch_stack
	ret $31,($26),1
.end	sys_fork

.align 3
.globl	sys_clone
.ent	sys_clone
sys_clone:
	bsr $1,do_switch_stack
	/* arg1 and arg2 come from the user */
	bis $30,$30,$18
	jsr $26,alpha_clone
	bsr $1,undo_switch_stack
	ret $31,($26),1
.end	sys_clone

.align 3
.globl  alpha_switch_to
.ent    alpha_switch_to
alpha_switch_to:
	bsr $1,do_switch_stack
	call_pal PAL_swpctx
	bsr $1,undo_switch_stack
	ret $31,($26),1
.end alpha_switch_to

/*
 * Oh, well.. Disassembling OSF/1 binaries to find out how the
 * system calls work isn't much fun.
 *
 * entSys is special in that the PAL-code doesn't save a0-a2, so
 * we start off by doing that by hand.
 */
.align 3
.globl	entSys
.globl	ret_from_sys_call
.ent	entSys
entSys:
	SAVE_ALL
	ldq	$8,current_set
	lda	$4,NR_SYSCALLS($31)
	stq	$16,SP_OFF+24($30)
	lda	$5,sys_call_table
	lda	$27,do_entSys
	cmpult	$0,$4,$4
	ldq	$3,TASK_FLAGS($8)
	stq	$17,SP_OFF+32($30)
	s8addq	$0,$5,$5
	and     $3,PF_PTRACED,$3
	stq	$18,SP_OFF+40($30)
	bne     $3,strace
	beq	$4,1f
	ldq	$27,0($5)
1:	jsr	$26,($27),do_entSys
	blt	$0,syscall_error	/* the call failed */
	stq	$0,0($30)
	stq	$31,72($30)		/* a3=0 => no error */

.align 3
ret_from_sys_call:
	cmovne	$26,0,$19		/* $19 = 0 => non-restartable */
	/* check bottom half interrupts */
	bne	$1,ret_from_handle_bh
	ldq	$3,bh_active
	ldq	$4,bh_mask
	and	$3,$4,$2
	bne	$2,handle_bottom_half
ret_from_handle_bh:
	ldq	$0,SP_OFF($30)
	and	$0,8,$0
	beq	$0,restore_all
ret_from_reschedule:
	lda	$0,need_resched
	ldl	$2,0($0)
	lda	$4,init_task
	bne	$2,reschedule
	xor	$4,$8,$4
	beq	$4,restore_all
	ldq	$4,TASK_SIGNAL($8)
	ldq	$16,TASK_BLOCKED($8)
	bic	$4,$16,$4
	bne	$4,signal_return
restore_all:
	RESTORE_ALL
	rti


/* PTRACE syscall handler */
.align 3
strace:
	/* set up signal stack, call syscall_trace */
	bsr	$1,do_switch_stack
	jsr     $26,syscall_trace
	bsr	$1,undo_switch_stack

	/* get the system call number and the arguments back.. */
	ldq     $0,0($30)
	ldq     $16,SP_OFF+24($30)
	ldq     $17,SP_OFF+32($30)
	ldq     $18,SP_OFF+40($30)
	ldq     $19,72($30)
	ldq     $20,80($30)
	ldq     $21,88($30)

	/* get the system call pointer.. */
	lda	$1,NR_SYSCALLS($31)
	lda	$2,sys_call_table
	lda	$27,do_entSys
	cmpult	$0,$1,$1
	s8addq	$0,$2,$2
	beq	$1,1f
	ldq	$27,0($2)
1:	jsr	$26,($27),do_entSys

	/* check return.. */
	blt	$0,strace_error	/* the call failed */
	stq	$31,72($30)		/* a3=0 => no error */
strace_success:
	stq	$0,0($30)		/* save return value */

	bsr	$1,do_switch_stack
	jsr     $26,syscall_trace
	bsr	$1,undo_switch_stack
	br	$31,ret_from_sys_call

	.align  3
strace_error:
	ldq	$19,0($30)	/* old syscall nr (zero if success) */
	beq	$19,strace_success
	ldq	$20,72($30)	/* .. and this a3 */

	subq	$31,$0,$0	/* with error in v0 */
	addq	$31,1,$1	/* set a3 for errno return */
	stq	$0,0($30)
	stq	$1,72($30)	/* a3 for return */

	bsr	$1,do_switch_stack
	bis	$19,$19,$9	/* save old syscall number */
	bis	$20,$20,$10	/* save old a3 */
	jsr     $26,syscall_trace
	bis	$9,$9,$19
	bis	$10,$10,$20
	bsr	$1,undo_switch_stack

	bis	$31,$31,$26	/* tell "ret_from_sys_call" that we can restart */
	br	$31,ret_from_sys_call

	.align 3
handle_bottom_half:
	subq	$30,16,$30
	stq	$19,0($30)	/* save syscall nr */
	stq	$20,8($30)	/* and error indication (a3) */
	jsr	$26,do_bottom_half
	ldq	$19,0($30)
	ldq	$20,8($30)
	addq	$30,16,$30
	br	$31,ret_from_handle_bh

	.align 3
syscall_error:
	/*
	 * Some system calls (e.g., ptrace) can return arbitrary
	 * values which might normally be mistaken as error numbers.
	 * Those functions must zero $0 (v0) directly in the stack
	 * frame to indicate that a negative return value wasn't an
	 * error number..
	 */
	ldq	$19,0($30)	/* old syscall nr (zero if success) */
	beq	$19,ret_success

	ldq	$20,72($30)	/* .. and this a3 */
	subq	$31,$0,$0	/* with error in v0 */
	addq	$31,1,$1	/* set a3 for errno return */
	stq	$0,0($30)
	bis	$31,$31,$26	/* tell "ret_from_sys_call" we can restart */
	stq	$1,72($30)	/* a3 for return */
	br	$31,ret_from_sys_call

ret_success:
	stq	$0,0($30)
	stq	$31,72($30)	/* a3=0 => no error */
	br	$31,ret_from_sys_call

.align 3
signal_return:
	bis	$30,$30,$17
	br	$1,do_switch_stack
	bis	$30,$30,$18
	jsr	$26,do_signal
	lda	$30,SWITCH_STACK_SIZE($30)
	br	$31,restore_all
.end entSys

.align 3
.ent reschedule
reschedule:
	subq	$30,16,$30
	stq	$19,0($30)	/* save syscall nr */
	stq	$20,8($30)	/* and error indication (a3) */
	jsr	$26,schedule
	ldq	$19,0($30)
	ldq	$20,8($30)
	addq	$30,16,$30
	br	$31,ret_from_reschedule
.end reschedule

.align 3
.ent sys_sigreturn
sys_sigreturn:
	bis	$30,$30,$17
	lda	$30,-SWITCH_STACK_SIZE($30)
	bis	$30,$30,$18
	jsr	$26,do_sigreturn
	br	$1,undo_switch_stack
	br	$31,ret_from_sys_call
.end sys_sigreturn

.align 3
.ent sys_sigsuspend
sys_sigsuspend:
	bis	$30,$30,$17
	br	$1,do_switch_stack
	bis	$30,$30,$18
	jsr	$26,do_sigsuspend
	lda	$30,SWITCH_STACK_SIZE($30)
	br	$31,ret_from_sys_call
.end sys_sigsuspend

	.align 3
	.globl sys_call_table
sys_call_table:
/*0*/	.quad do_entSys, sys_exit, sys_fork, sys_read, sys_write
	.quad do_entSys, sys_close, sys_wait4, do_entSys, sys_link
	.quad sys_unlink, do_entSys, sys_chdir, sys_fchdir, sys_mknod
	.quad sys_chmod, sys_chown, osf_brk, do_entSys, sys_lseek
	.quad sys_getxpid, osf_mount, osf_umount, sys_setuid, sys_getxuid
	.quad do_entSys, sys_ptrace, do_entSys, do_entSys, do_entSys
	.quad do_entSys, do_entSys, do_entSys, sys_access, do_entSys
	.quad do_entSys, sys_sync, sys_kill, do_entSys, sys_setpgid
	.quad do_entSys, sys_dup, sys_pipe, osf_set_program_attributes, do_entSys
	.quad sys_open, do_entSys, sys_getxgid, osf_sigprocmask, do_entSys
/*50*/	.quad do_entSys, sys_acct, sys_sigpending, do_entSys, sys_ioctl
	.quad do_entSys, do_entSys, sys_symlink, sys_readlink, sys_execve
	.quad sys_umask, sys_chroot, do_entSys, sys_getpgrp, sys_getpagesize
	.quad do_entSys, osf_vfork, sys_newstat, sys_newlstat, do_entSys
	.quad do_entSys, osf_mmap, do_entSys, sys_munmap, sys_mprotect
	.quad sys_madvise, sys_vhangup, do_entSys, do_entSys, sys_getgroups
	/* map BSD's setpgrp to sys_setpgid for binary compatibility: */
	.quad sys_setgroups, do_entSys, sys_setpgid, sys_setitimer, do_entSys
	.quad do_entSys, sys_getitimer, sys_gethostname, sys_sethostname, sys_getdtablesize
	.quad sys_dup2, sys_newfstat, sys_fcntl, sys_select, sys_poll
	.quad sys_fsync, sys_setpriority, sys_socket, sys_connect, sys_accept
/*100*/	.quad osf_getpriority, sys_send, sys_recv, sys_sigreturn, sys_bind
	.quad sys_setsockopt, sys_listen, do_entSys, do_entSys, do_entSys
	.quad do_entSys, sys_sigsuspend, do_entSys, sys_recvmsg, sys_sendmsg
	.quad do_entSys, sys_gettimeofday, sys_getrusage, sys_getsockopt, do_entSys
	.quad sys_readv, sys_writev, sys_settimeofday, sys_fchown, sys_fchmod
	.quad sys_recvfrom, sys_setreuid, sys_setregid, sys_rename, sys_truncate
	.quad sys_ftruncate, sys_flock, sys_setgid, sys_sendto, sys_shutdown
	.quad sys_socketpair, sys_mkdir, sys_rmdir, sys_utimes, do_entSys
	.quad do_entSys, sys_getpeername, do_entSys, do_entSys, sys_getrlimit
	.quad sys_setrlimit, do_entSys, sys_setsid, sys_quotactl, do_entSys
/*150*/	.quad sys_getsockname, do_entSys, do_entSys, do_entSys, do_entSys
	.quad do_entSys, sys_sigaction, do_entSys, do_entSys, osf_getdirentries
	.quad osf_statfs, osf_fstatfs, do_entSys, do_entSys, do_entSys
	.quad osf_getdomainname, sys_setdomainname, do_entSys, do_entSys, do_entSys
	.quad do_entSys, do_entSys, do_entSys, do_entSys, do_entSys
	.quad do_entSys, do_entSys, do_entSys, do_entSys, do_entSys
	.quad do_entSys, do_entSys, do_entSys, do_entSys, do_entSys
	.quad do_entSys, do_entSys, do_entSys, do_entSys, do_entSys
	.quad do_entSys, do_entSys, do_entSys, do_entSys, do_entSys
	.quad do_entSys, do_entSys, do_entSys, do_entSys, osf_swapon
/*200*/	.quad sys_msgctl, sys_msgget, sys_msgrcv, sys_msgsnd, sys_semctl
	.quad sys_semget, sys_semop, osf_utsname, do_entSys, osf_shmat
	.quad sys_shmctl, sys_shmdt, sys_shmget, do_entSys, do_entSys
	.quad do_entSys, do_entSys, sys_msync, do_entSys, do_entSys
	.quad do_entSys, do_entSys, do_entSys, do_entSys, do_entSys
	.quad do_entSys, do_entSys, do_entSys, do_entSys, do_entSys
	.quad do_entSys, do_entSys, do_entSys, sys_getpgid, sys_getsid
	.quad do_entSys, do_entSys, do_entSys, do_entSys, do_entSys
	.quad do_entSys, osf_sysinfo, do_entSys, do_entSys, osf_proplist_syscall
	.quad do_entSys, do_entSys, do_entSys, do_entSys, do_entSys
/*250*/	.quad do_entSys, osf_usleep_thread, do_entSys, do_entSys, sys_sysfs
	.quad do_entSys, osf_getsysinfo, osf_setsysinfo, do_entSys, do_entSys
	.quad do_entSys, do_entSys, do_entSys, do_entSys, do_entSys
	.quad do_entSys, do_entSys, do_entSys, do_entSys, do_entSys
	.quad do_entSys, do_entSys, do_entSys, do_entSys, do_entSys
	.quad do_entSys, do_entSys, do_entSys, do_entSys, do_entSys
	.quad do_entSys, do_entSys, do_entSys, do_entSys, do_entSys
	.quad do_entSys, do_entSys, do_entSys, do_entSys, do_entSys
	.quad do_entSys, do_entSys, do_entSys, do_entSys, do_entSys
	.quad do_entSys, do_entSys, do_entSys, do_entSys, do_entSys
/* linux-specific system calls start at 300 */
/*300*/	.quad sys_bdflush, sys_sethae, sys_mount, sys_adjtimex, sys_swapoff
	.quad sys_getdents, alpha_create_module, sys_init_module, sys_delete_module, sys_get_kernel_syms
	.quad sys_syslog, sys_reboot, sys_clone, sys_uselib, sys_mlock
	.quad sys_munlock, sys_mlockall, sys_munlockall, sys_sysinfo, sys_sysctl
	.quad sys_idle, sys_umount, sys_swapon, sys_times, sys_personality
	.quad sys_setfsuid, sys_setfsgid, sys_ustat, sys_statfs, sys_fstatfs
	.quad sys_sched_setparam, sys_sched_getparam, sys_sched_setscheduler, sys_sched_getscheduler, sys_sched_yield
	.quad sys_sched_get_priority_max, sys_sched_get_priority_min, sys_sched_rr_get_interval, do_entSys /* sys_afs_syscall */, sys_newuname
	.quad sys_nanosleep, sys_mremap, do_entSys, sys_setresuid, sys_getresuid
	.quad sys_pciconfig_read, sys_pciconfig_write, sys_query_module
	.quad do_entSys, do_entSys
