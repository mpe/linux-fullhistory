/*
 * alpha/entry.S
 *
 * kernel entry-points
 */

#include <asm/system.h>

#define halt	.long PAL_halt
#define rti	.long PAL_rti

#define NR_SYSCALLS 320
#define osf_vfork sys_fork

/*
 * These offsets must match with "struct hae" in io.h:	
 */
#define HAE_CACHE	0
#define HAE_REG		8

/*
 * stack offsets
 */
#define SP_OFF		160

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
 */
#define SAVE_ALL			\
	subq	$30,160,$30;		\
	stq	$0,0($30);		\
	stq	$1,8($30);		\
	stq	$2,16($30);		\
	stq	$3,24($30);		\
	stq	$4,32($30);		\
	stq	$5,40($30);		\
	stq	$6,48($30);		\
	stq	$7,56($30);		\
	stq	$8,64($30);		\
	stq	$19,72($30);		\
	stq	$20,80($30);		\
	stq	$21,88($30);		\
	stq	$22,96($30);		\
	stq	$23,104($30);		\
	stq	$24,112($30);		\
	stq	$25,120($30);		\
	stq	$26,128($30);		\
	stq	$27,136($30);		\
	stq	$28,144($30);		\
	lda	$2,hae;			\
	ldq	$2,HAE_CACHE($2);	\
	stq	$2,152($30)

#define RESTORE_ALL			\
	lda	$8,hae;			\
	ldq	$7,HAE_CACHE($8);	\
	ldq	$6,152($30);		\
	subq	$7,$6,$5;		\
	beq	$5,99f;			\
	ldq	$7,HAE_REG($8);		\
	addq	$31,7,$16;		\
	call_pal PAL_swpipl;		\
	stq	$6,HAE_CACHE($8);	\
	stq	$6,0($7);		\
	mb;				\
	bis	$0,$0,$16;		\
	call_pal PAL_swpipl;		\
99:;					\
	ldq	$0,0($30);		\
	ldq	$1,8($30);		\
	ldq	$2,16($30);		\
	ldq	$3,24($30);		\
	ldq	$4,32($30);		\
	ldq	$5,40($30);		\
	ldq	$6,48($30);		\
	ldq	$7,56($30);		\
	ldq	$8,64($30);		\
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
	addq	$30,160,$30

.text
.set noat
#ifdef __linux__
  .set singlegp
#endif

.align 3
.globl	entInt
.ent	entInt
entInt:
	SAVE_ALL
/* start atomic operation with respect to software interrupts */
	lda	$0,intr_count
	ldq	$1,0($0)
	addq	$1,1,$1
	stq	$1,0($0)
/* set up the arguments to the C interrupt handler */
	lda	$27,do_entInt
	jsr	$26,($27),do_entInt
/* ok, check if we need to do software interrupts */
1:	lda	$0,intr_count
	ldq	$1,0($0)
	subq	$1,1,$1
	bne	$1,2f		/* interrupt within interrupt: return now */
	lda	$2,bh_active
	ldq	$3,0($2)
	lda	$2,bh_mask
	ldq	$2,0($2)
	and	$2,$3,$2
	bne	$2,3f
	stq	$1,0($0)
	br	$31,ret_from_sys_call
.align 3
2:	stq	$1,0($0)
	br	$31,restore_all
.align 3
3:	lda	$27,do_bottom_half
	jsr	$26,($27),do_bottom_half
	br	$31,1b
.end entInt

.align 3
.globl	entMM
.ent	entMM
entMM:
	SAVE_ALL
	lda	$27,do_page_fault
	lda	$26,ret_from_sys_call
	jsr	$31,($27),do_page_fault
.end entMM

.align 3
.globl	entArith
.ent	entArith
entArith:
	SAVE_ALL
	lda	$27,do_entArith
	lda	$26,ret_from_sys_call
	jsr	$31,($27),do_entArith
.end entArith

.align 3
.globl	entIF
.ent	entIF
entIF:
	SAVE_ALL
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
.globl	kernel_fork
.ent	kernel_fork
kernel_fork:
	subq $30,6*8,$30
	stq $31,0($30)
	stq $26,8($30)
	stq $29,16($30)
	stq $16,24($30)
	stq $17,32($30)
	stq $18,40($30)
	bis $31,2,$0	/* Register v0: syscall nr for fork() */
	SAVE_ALL
	lda $27,sys_fork
	jsr $26,($27),sys_fork
	stq $0,0($30)	
	br ret_from_sys_call
.end	kernel_fork

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
	stt $f28,288($30)
	stt $f29,296($30)
	stt $f30,304($30)
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
	ldt $f0,64($30)
	ldt $f1,72($30)
	ldt $f2,80($30)
	ldt $f3,88($30)
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
	lda $27,do_entUna
	jsr $26,($27),do_entUna
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
	lda $27,do_entUnaUser
	bis $31,$30,$19
	jsr $26,($27),do_entUnaUser
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

.align 3
.globl	sys_fork
.ent	sys_fork
sys_fork:
	br $1,do_switch_stack
	bis $30,$30,$16
	lda $27,alpha_fork
	jsr $26,($27),alpha_fork
	br $1,undo_switch_stack
	ret $31,($26),1
.end	sys_fork

.align 3
.globl  alpha_switch_to
.ent    alpha_switch_to
alpha_switch_to:
	br $1,do_switch_stack
	call_pal PAL_swpctx
	br $1,undo_switch_stack
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
	stq	$16,24($30)
	stq	$17,32($30)
	stq	$18,40($30)
	SAVE_ALL
	lda	$1,NR_SYSCALLS($31)
	lda	$2,sys_call_table
	lda	$27,do_entSys
	cmpult	$0,$1,$1
	s8addq	$0,$2,$2
	beq	$1,1f
	ldq	$27,0($2)
1:	jsr	$26,($27),do_entSys
	blt	$0,syscall_error	/* the call failed */
	stq	$0,0($30)
ret_success:
	stq	$31,72($30)		/* a3=0 => no error */
.align 3
ret_from_sys_call:
	ldq	$0,SP_OFF($30)
	cmovne	$26,0,$19		/* $19 = 0 => non-restartable */
	and	$0,8,$0
	beq	$0,restore_all
ret_from_reschedule:
	lda	$0,need_resched
	lda	$1,current
	ldl	$2,0($0)
	lda	$4,init_task
	ldq	$3,0($1)
	bne	$2,reschedule
	subq	$4,$3,$4
	beq	$4,restore_all
	ldq	$4,TASK_SIGNAL($3)
	ldq	$16,TASK_BLOCKED($3)
	bic	$4,$16,$4
	bne	$4,signal_return
restore_all:
	RESTORE_ALL
	rti

	.align 3
syscall_error:
	/*
	 * Some system calls (e.g., ptrace) can return arbitrary
	 * values which might normally be mistaken as error numbers.
	 * Those functions must setup $0 (v0) directly in the stack frame
	 * and return with value -255.
	 */
	addq	$0,255,$1
	beq	$1,ret_success

	bis	$31,$31,$26	/* tell "ret_from_sys_call" that we can restart */
	ldq	$19,0($30)	/* .. with this syscall nr */
	ldq	$20,72($30)	/* .. and this a3 */
	subq	$31,$0,$0	/* with error in v0 */
	addq	$31,1,$1	/* set a3 for errno return */
	stq	$0,0($30)
	stq	$1,72($30)	/* a3 for return */
	br	$31,ret_from_sys_call

.align 3
signal_return:
	bis	$30,$30,$17
	br	$1,do_switch_stack
	bis	$30,$30,$18
	lda	$27,do_signal
	jsr	$26,($27),do_signal
	lda	$30,SWITCH_STACK_SIZE($30)
	br	$31,restore_all
.end entSys

.align 3
.ent reschedule
reschedule:
	subq	$30,16,$30
	stq	$19,0($30)	/* save syscall nr */
	stq	$20,8($30)	/* and error indication (a3) */
	lda	$27,schedule
	jsr	$26,($27),schedule
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
	lda	$27,do_sigreturn
	jsr	$26,($27),do_sigreturn
	br	$1,undo_switch_stack
	br	$31,ret_from_sys_call
.end sys_sigreturn

.align 3
.ent sys_sigsuspend
sys_sigsuspend:
	bis	$30,$30,$17
	br	$1,do_switch_stack
	bis	$30,$30,$18
	lda	$27,do_sigsuspend
	jsr	$26,($27),do_sigsuspend
	lda	$30,SWITCH_STACK_SIZE($30)
	br	$31,ret_from_sys_call
.end sys_sigsuspend

	.align 3
	.globl sys_call_table
sys_call_table:
/*0*/	.quad do_entSys, sys_exit, sys_fork, sys_read, sys_write
	.quad do_entSys, sys_close, sys_wait4, do_entSys, sys_link
	.quad sys_unlink, do_entSys, sys_chdir, sys_fchdir, sys_mknod
	.quad sys_chmod, sys_chown, sys_brk, do_entSys, sys_lseek
	.quad sys_getxpid, osf_mount, osf_umount, sys_setuid, sys_getxuid
	.quad do_entSys, sys_ptrace, do_entSys, do_entSys, do_entSys
	.quad do_entSys, do_entSys, do_entSys, sys_access, do_entSys
	.quad do_entSys, sys_sync, sys_kill, do_entSys, sys_setpgid
	.quad do_entSys, sys_dup, sys_pipe, do_entSys, do_entSys
	.quad sys_open, do_entSys, sys_getxgid, osf_sigprocmask, do_entSys
/*50*/	.quad do_entSys, sys_acct, do_entSys, do_entSys, sys_ioctl
	.quad do_entSys, do_entSys, sys_symlink, sys_readlink, sys_execve
	.quad sys_umask, do_entSys, do_entSys, sys_getpgrp, sys_getpagesize
	.quad do_entSys, osf_vfork, sys_newstat, sys_newlstat, do_entSys
	.quad do_entSys, osf_mmap, do_entSys, sys_munmap, sys_mprotect
	.quad sys_madvise, sys_vhangup, do_entSys, do_entSys, sys_getgroups
	/* map BSD's setpgrp to sys_setpgid for binary compatibility: */
	.quad sys_setgroups, do_entSys, sys_setpgid, sys_setitimer, do_entSys
	.quad do_entSys, sys_getitimer, sys_gethostname, sys_sethostname, sys_getdtablesize
	.quad sys_dup2, sys_newfstat, sys_fcntl, sys_select, do_entSys
	.quad sys_fsync, sys_setpriority, sys_socket, sys_connect, sys_accept
/*100*/	.quad sys_getpriority, sys_send, sys_recv, sys_sigreturn, sys_bind
	.quad sys_setsockopt, sys_listen, do_entSys, do_entSys, do_entSys
	.quad do_entSys, sys_sigsuspend, do_entSys, do_entSys, do_entSys
	.quad do_entSys, sys_gettimeofday, sys_getrusage, sys_getsockopt, do_entSys
	.quad do_entSys, do_entSys, sys_settimeofday, sys_fchown, sys_fchmod
	.quad sys_recvfrom, sys_setreuid, sys_setregid, sys_rename, sys_truncate
	.quad sys_ftruncate, do_entSys, sys_setgid, sys_sendto, sys_shutdown
	.quad sys_socketpair, sys_mkdir, sys_rmdir, sys_utimes, do_entSys
	.quad do_entSys, sys_getpeername, do_entSys, do_entSys, sys_getrlimit
	.quad sys_setrlimit, do_entSys, sys_setsid, do_entSys, do_entSys
/*150*/	.quad sys_getsockname, do_entSys, do_entSys, do_entSys, do_entSys
	.quad do_entSys, sys_sigaction, do_entSys, do_entSys, osf_getdirentries
	.quad osf_statfs, osf_fstatfs, do_entSys, do_entSys, do_entSys
	.quad osf_getdomainname, do_entSys, do_entSys, do_entSys, do_entSys
	.quad do_entSys, do_entSys, do_entSys, do_entSys, do_entSys
	.quad do_entSys, do_entSys, do_entSys, do_entSys, do_entSys
	.quad do_entSys, do_entSys, do_entSys, do_entSys, do_entSys
	.quad do_entSys, do_entSys, do_entSys, do_entSys, do_entSys
	.quad do_entSys, do_entSys, do_entSys, do_entSys, do_entSys
	.quad do_entSys, do_entSys, do_entSys, do_entSys, osf_swapon
/*200*/	.quad sys_msgctl, sys_msgget, sys_msgrcv, sys_msgsnd, sys_semctl
	.quad sys_semget, sys_semop, osf_utsname, do_entSys, osf_shmat
	.quad sys_shmctl, sys_shmdt, sys_shmget, do_entSys, do_entSys
	.quad do_entSys, do_entSys, do_entSys, do_entSys, do_entSys
	.quad do_entSys, do_entSys, do_entSys, do_entSys, do_entSys
	.quad do_entSys, do_entSys, do_entSys, do_entSys, do_entSys
	.quad do_entSys, do_entSys, do_entSys, do_entSys, do_entSys
	.quad do_entSys, do_entSys, do_entSys, do_entSys, do_entSys
	.quad do_entSys, do_entSys, do_entSys, do_entSys, osf_proplist_syscall
	.quad do_entSys, do_entSys, do_entSys, do_entSys, do_entSys
/*250*/	.quad do_entSys, osf_usleep_thread, do_entSys, do_entSys, do_entSys
	.quad do_entSys, do_entSys, do_entSys, do_entSys, do_entSys
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
	.quad sys_getdents, sys_create_module, sys_init_module, sys_delete_module, sys_get_kernel_syms
	.quad sys_syslog, sys_reboot, do_entSys, do_entSys, do_entSys
	.quad do_entSys, do_entSys, do_entSys, do_entSys, do_entSys

