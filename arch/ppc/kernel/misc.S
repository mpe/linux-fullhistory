/*
 * This module contains the PowerPC interrupt fielders
 * set of code at specific locations, based on function
 */

#include <linux/sys.h>
#include "ppc_asm.tmpl"

/* Keep track of low-level exceptions - rather crude, but informative */	
#define STATS

/*
 * Increment a [64 bit] statistic counter
 * Uses R2, R3
 */
#define BUMP(ctr) \
	lis	r2,ctr@h; \
	ori	r2,r2,ctr@l; \
	lwz	r3,4(r2); \
	addic	r3,r3,1; \
	stw	r3,4(r2); \
	lwz	r3,0(r2); \
	addze	r3,r3; \
	stw	r3,0(r2)

/* This instruction is not implemented on the PPC 603 */
#define tlbia \
	li	r4,32; \
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
	mfmsr	r0		/* Get current interrupt state */
	rlwinm	r3,r0,16+1,32-1,31	/* Extract old value of 'EE' */
	li	r4,0		/* Need [unsigned] value of MSR_EE */
	ori	r4,r4,MSR_EE	/* Set to turn off bit */
	andc	r0,r0,r4	/* Clears bit in (r4) */
	mtmsr	r0		/* Update machine state */
	blr			/* Done */

/*
 * Enable interrupts
 *	_enable_interrupts(int state)
 * turns on interrupts if state = 1.
 */
_GLOBAL(_enable_interrupts)
	mfmsr	r0		/* Get current state */
	rlwimi	r0,r3,16-1,32-16,32-16	/* Insert bit */
	mtmsr	r0		/* Update machine state */
	blr

/*
 * Get 'flags' (aka machine status register)
 *	__save_flags(long *ptr)
 */
_GLOBAL(__save_flags)
	mfmsr	r0		/* Get current state */
	stw	r0,0(r3)
	mr	r3,r0
	blr

/*
 * Restore 'flags'
 *	__restore_flags(long val)
 */
_GLOBAL(__restore_flags)
	mtmsr	r3
	isync
	blr

/*
 * Disable interrupts - like an 80x86
 *	cli()
 */
_GLOBAL(cli)
	mfmsr	r0		/* Get current interrupt state */
	rlwinm	r3,r0,16+1,32-1,31	/* Extract old value of 'EE' */
	li	r4,0		/* Need [unsigned] value of MSR_EE */
	ori	r4,r4,MSR_EE	/* Set to turn off bit */
	andc	r0,r0,r4	/* Clears bit in (r4) */
	mtmsr	r0		/* Update machine state */
	blr			/* Done */

/*
 * Enable interrupts - like an 80x86
 *	sti()
 */
_GLOBAL(sti)
	mfmsr	r0		/* Get current state */
	ori	r0,r0,MSR_EE	/* Turn on 'EE' bit */
	mtmsr	r0		/* Update machine state */
	blr

/*
 * Flush MMU TLB
 */
_GLOBAL(_tlbia)
	tlbia
	BUMP(__TLBIAs)
	blr	

/*
 * Flush MMU TLB for a particular address
 */
_GLOBAL(_tlbie)
	tlbie	r3
	BUMP(__TLBIEs)
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
 * Delay for a specific # of "loops"
 *	__delay(int loops)
 */
_GLOBAL(__delay)
	mtctr	r3
00:	addi	r3,r3,0		/* NOP */
	bdnz	00b
	blr

/*
 * Delay for a number of microseconds
 *	udelay(int usecs)
 */
_GLOBAL(udelay)
00:	li	r0,86	/* Instructions / microsecond? */
	mtctr	r0
10:	addi	r0,r0,0 /* NOP */
	bdnz	10b
	subic.	r3,r3,1
	bne	00b
	blr

/*
 * Atomically increment [intr_count]
 */
_GLOBAL(start_bh_atomic)
	lis	r3,intr_count@h
	ori	r3,r3,intr_count@l
10:	lwarx	r4,0,r3
	addi	r4,r4,1
	stwcx.	r4,0,r3
	bne-	10b
	blr

/*
 * Atomically decrement [intr_count]
 */
_GLOBAL(end_bh_atomic)
	lis	r3,intr_count@h
	ori	r3,r3,intr_count@l
10:	lwarx	r4,0,r3
	subic	r4,r4,1
	stwcx.	r4,0,r3
	bne-	10b
	blr

/*
 *extern inline int find_first_zero_bit(void * vaddr, unsigned size)
 *{
 *	unsigned long res;
 *	unsigned long *p;
 *	unsigned long *addr = vaddr;
 *
 *	if (!size)
 *		return 0;
 *	__asm__ __volatile__ ("    moveq #-1,d0\n\t"
 *			      "1:"
 *			      "    cmpl  %1@+,d0\n\t"
 *			      "    bne   2f\n\t"
 *			      "    subql #1,%0\n\t"
 *			      "    bne   1b\n\t"
 *			      "    bra   5f\n\t"
 *			      "2:"
 *			      "    movel %1@-,d0\n\t"
 *			      "    notl  d0\n\t"
 *			      "    bfffo d0{#0,#0},%0\n\t"
 *			      "5:"
 *			      : "=d" (res), "=a" (p)
 *			      : "0" ((size + 31) >> 5), "1" (addr)
 *			      : "d0");
 *	return ((p - addr) << 5) + res;
 *}
 */
_GLOBAL(find_first_zero_bit)
	li	r5,0		/* bit # */
	subi	r3,r3,4		/* Adjust pointer for auto-increment */
00:	lwzu	r0,4(r3)	/* Get next word */
	not.	r7,r0		/* Complement to find ones */
	beq	10f		/* Jump if all ones */
02:	andi.	r7,r0,1		/* Check low-order bit */
	beq	20f		/* All done when zero found */
	srawi	r0,r0,1
	addi	r5,r5,1
	b	02b
10:	addi	r5,r5,32	/* Update bit # */
	subic.	r4,r4,32	/* Any more? */
	bgt	00b
20:	mr	r3,r5		/* Compute result */	
	blr
 
/*
 *static inline int find_next_zero_bit (void *vaddr, int size,
 *				      int offset)
 *{
 *	unsigned long *addr = vaddr;
 *	unsigned long *p = addr + (offset >> 5);
 *	int set = 0, bit = offset & 31, res;
 *
 *	if (bit) {
 *		// Look for zero in first longword 
 *		__asm__("bfffo %1{#0,#0},%0"
 *			: "=d" (set)
 *			: "d" (~*p << bit));
 *		if (set < (32 - bit))
 *			return set + offset;
 *                set = 32 - bit;
 *		p++;
 *	}
 *	// No zero yet, search remaining full bytes for a zero 
 *	res = find_first_zero_bit (p, size - 32 * (p - addr));
 *	return (offset + set + res);
 *}
 */
_GLOBAL(find_next_zero_bit)
	addi	r5,r5,1		/* bump offset to start */
	srawi	r6,r5,5		/* word offset */
	add	r6,r6,r6	/* byte offset */
	add	r6,r6,r6	/* byte offset */
	add	r3,r3,r6	/* compute byte position */
	sub	r4,r4,r5	/* adjust size by starting index */
	andi.	r0,r5,0x1F	/* offset in current word? */
	beq	10f		/* at start of word */
	lwz	r0,0(r3)	/* get word */
	sraw	r0,r0,r5	/* shift right */
	not.	r7,r0
	beq	07f		/* jump if only ones remain */
05:	andi.	r7,r0,1		/* found zero? */
	beq	90f		/* yes - all done */
	srawi	r0,r0,1
	addi	r5,r5,1
	b	05b
07:	andi.	r6,r5,0x1F
	subfic	r0,r6,32
	add	r5,r5,r0
	sub	r4,r4,r0
	b	20f
10:	subi	r3,r3,4		/* Adjust pointer for auto-increment */
20:	lwzu	r0,4(r3)	/* Get next word */
	not.	r7,r0		/* Complement to find ones */
	beq	40f		/* Jump if all ones */
30:	andi.	r7,r0,1		/* Check low-order bit */
	beq	90f		/* All done when zero found */
	srawi	r0,r0,1
	addi	r5,r5,1
	b	30b
40:	addi	r5,r5,32	/* Update bit # */
	subic.	r4,r4,32	/* Any more? */
	bgt	20b
90:	mr	r3,r5		/* Compute result */	
	blr
 
/*
 *
 * ffz = Find First Zero in word. Undefined if no zero exists,
 * so code should check against ~0UL first..
 *
 *extern inline unsigned long ffz(unsigned long word)
 *{
 *	__asm__ __volatile__ ("bfffo %1{#0,#0},%0"
 *			      : "=d" (word)
 *			      : "d" (~(word)));
 *	return word;
 *}
 */
_GLOBAL(ffz)
	mr	r4,r3
	li	r3,0
10:	andi.	r0,r4,1		/* Find the zero we know is there */
	srawi	r4,r4,1
	beq	90f
	addi	r3,r3,1
	b	10b
90:	blr

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
	
_GLOBAL(abort)
	.long	0

_GLOBAL(bzero)
#define bufp r3
#define len  r4
#define pat  r5
/* R3 has buffer */
/* R4 has length */
/* R5 has pattern */
	cmpi	0,len,0		/* Exit if len <= 0 */
	ble	99f
	andi.	r0,bufp,3	/* Must be on longword boundary */
	bne	10f		/* Use byte loop if not aligned */
	andi.	r0,len,3	/* Check for overrage */
	subi	bufp,bufp,4	/* Adjust pointer */
	srawi	len,len,2	/* Divide by 4 */
	blt	99f		/* If negative - bug out! */
	mtspr	CTR,len		/* Set up counter */
	li	pat,0
00:	stwu	pat,4(bufp)	/* Store value */
	bdnz	00b		/* Loop [based on counter] */
	mr	len,r0		/* Get remainder (bytes) */
10:	cmpi	0,len,0		/* Any bytes left */
	ble	99f		/* No - all done */
	mtspr	CTR,len		/* Set up counter */
	subi	bufp,bufp,1	/* Adjust pointer */
	li	pat,0
20:	stbu	pat,1(bufp)	/* Store value */
	bdnz	20b		/* Loop [based on counter] */
99:	blr

_GLOBAL(abs)
	cmpi	0,r3,0
	bge	10f
	neg	r3,r3
10:	blr

/*
 * Compute IP checksums
 *   _ip_fast_csum(buf, len) -- Optimized for IP header
 *   _ip_compute_csum(buf, len)
 */

_GLOBAL(_ip_fast_csum)
	li	r0,0
	addic	r0,r0,0		/* Clear initial carry */
	lwz	r4,0(r3)
	lwz	r5,4(r3)
	adde	r0,r0,r4
	lwz	r4,8(r3)
	adde	r0,r0,r5
	lwz	r5,12(r3)
	adde	r0,r0,r4
	lwz	r4,16(r3)
	adde	r0,r0,r5
	adde	r0,r0,r4
	mr	r3,r0
	andi.	r3,r3,0xFFFF
	srwi	r0,r0,16
	adde	r3,r3,r0
	andis.	r0,r3,1
	beq	10f
	addi	r3,r3,1
10:	not	r3,r3
	andi.	r3,r3,0xFFFF
	blr

_GLOBAL(_ip_compute_csum)
	li	r0,0
	addic	r0,r0,0
finish_ip_csum:	
	subi	r3,r3,4
	andi.	r5,r3,2		/* Align buffer to longword boundary */
	beq	10f
	lhz	r5,4(r3)
	adde	r0,r0,r5
	addi	r3,r3,2
	subi	r4,r4,2
10:	cmpi	0,r4,16		/* unrolled loop - 16 bytes at a time */
	blt	20f
	lwz	r5,4(r3)
	lwz	r6,8(r3)
	adde	r0,r0,r5
	lwz	r5,12(r3)
	adde	r0,r0,r6
	lwzu	r6,16(r3)
	adde	r0,r0,r5
	adde	r0,r0,r6
	subi	r4,r4,16
	b	10b
20:	cmpi	0,r4,4
	blt	30f
	lwzu	r5,4(r3)
	adde	r0,r0,r5
	subi	r4,r4,4
	b	20b
30:	cmpi	0,r4,2
	blt	40f
	lhz	r5,4(r3)
	addi	r3,r3,2
	adde	r0,r0,r5
	subi	r4,r4,2
40:	cmpi	0,r4,1
	bne	50f
	lbz	r5,4(r3)
	slwi	r5,r5,8		/* Upper byte of word */
	adde	r0,r0,r5
50:	mr	r3,r0
	andi.	r3,r3,0xFFFF
	srwi	r0,r0,16
	adde	r3,r3,r0
	andis.	r0,r3,1
	beq	60f
	addi	r3,r3,1
60:	not	r3,r3
	andi.	r3,r3,0xFFFF
	blr

_GLOBAL(_udp_check)
	addc	r0,r5,r6	/* Add in header fields */
	adde	r0,r0,r7
	b	finish_ip_csum	
#if 0
_GLOBAL(_tcp_check)
	addc	r0,r5,r6	/* Add in header fields */
	adde	r0,r0,r7
	b	finish_ip_csum	
#endif
_GLOBAL(_get_SP)
	mr	r3,r1		/* Close enough */
	blr	

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
	.long sys_open			/* 5 */
	.long sys_close
	.long sys_waitpid
	.long sys_creat
	.long sys_link
	.long sys_unlink		/* 10 */
	.long sys_execve
	.long sys_chdir
	.long sys_time
	.long sys_mknod
	.long sys_chmod		/* 15 */
	.long sys_chown
	.long sys_break
	.long sys_stat
	.long sys_lseek
	.long sys_getpid		/* 20 */
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
	.long sys_brk			/* 45 */
	.long sys_setgid
	.long sys_getgid
	.long sys_signal
	.long sys_geteuid
	.long sys_getegid		/* 50 */
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
	.long sys_getpgrp		/* 65 */
	.long sys_setsid
	.long sys_sigaction
	.long sys_sgetmask
	.long sys_ssetmask
	.long sys_setreuid		/* 70 */
	.long sys_setregid
	.long sys_sigsuspend
	.long sys_sigpending
	.long sys_sethostname
	.long sys_setrlimit		/* 75 */
	.long sys_getrlimit
	.long sys_getrusage
	.long sys_gettimeofday
	.long sys_settimeofday
	.long sys_getgroups		/* 80 */
	.long sys_setgroups
	.long sys_select
	.long sys_symlink
	.long sys_lstat
	.long sys_readlink		/* 85 */
	.long sys_uselib
	.long sys_swapon
	.long sys_reboot
	.long sys_readdir
	.long sys_mmap			/* 90 */
	.long sys_munmap
	.long sys_truncate
	.long sys_ftruncate
	.long sys_fchmod
	.long sys_fchown		/* 95 */
	.long sys_getpriority
	.long sys_setpriority
	.long sys_profil
	.long sys_statfs
	.long sys_fstatfs		/* 100 */
	.long sys_ioperm
	.long sys_socketcall
	.long sys_syslog
	.long sys_setitimer
	.long sys_getitimer		/* 105 */
	.long sys_newstat
	.long sys_newlstat
	.long sys_newfstat
	.long sys_uname
	.long sys_iopl			/* 110 */
	.long sys_vhangup
	.long sys_idle
	.long sys_vm86
	.long sys_wait4
	.long sys_swapoff		/* 115 */
	.long sys_sysinfo
	.long sys_ipc
	.long sys_fsync
	.long sys_sigreturn
	.long sys_clone		/* 120 */
	.long sys_setdomainname
	.long sys_newuname
	.long sys_modify_ldt
	.long sys_adjtimex
	.long sys_mprotect		/* 125 */
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
	.long sys_llseek		/* 140 */
        .long sys_getdents
	.long sys_newselect
	.long sys_flock
	.long sys_msync
        .space (NR_syscalls-144)*4

	.data
#if 0
	.globl	floppy_track_buffer
floppy_track_buffer:
	.space	512*2*38		/* Space for one entire cylinder! */	
#endif
