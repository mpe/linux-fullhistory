/*
 *  linux/arch/i386/kernel/i387.c
 *
 *  Copyright (C) 1994 Linus Torvalds
 *
 *  Pentium III FXSR, SSE support
 *  General FPU state handling cleanups
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <asm/processor.h>
#include <asm/i387.h>
#include <asm/math_emu.h>
#include <asm/sigcontext.h>
#include <asm/user.h>
#include <asm/ptrace.h>
#include <asm/uaccess.h>

#if defined(CONFIG_X86_FXSR)
#define HAVE_FXSR 1
#elif defined(CONFIG_X86_RUNTIME_FXSR)
#define HAVE_FXSR (cpu_has_fxsr)
#else
#define HAVE_FXSR 0
#endif

#ifdef CONFIG_MATH_EMULATION
#define HAVE_HWFP (boot_cpu_data.hard_math)
#else
#define HAVE_HWFP 1
#endif

/*
 * FPU lazy state save handling.
 */

void save_fpu( struct task_struct *tsk )
{
	if ( HAVE_FXSR ) {
		asm volatile( "fxsave %0 ; fwait"
			      : "=m" (tsk->thread.i387.fxsave) );
	} else {
		asm volatile( "fnsave %0 ; fwait"
			      : "=m" (tsk->thread.i387.fsave) );
	}
	tsk->flags &= ~PF_USEDFPU;
	stts();
}

void save_init_fpu( struct task_struct *tsk )
{
	if ( HAVE_FXSR ) {
		asm volatile( "fxsave %0 ; fnclex"
			      : "=m" (tsk->thread.i387.fxsave) );
	} else {
		asm volatile( "fnsave %0 ; fwait"
			      : "=m" (tsk->thread.i387.fsave) );
	}
	tsk->flags &= ~PF_USEDFPU;
	stts();
}

void restore_fpu( struct task_struct *tsk )
{
	if ( HAVE_FXSR ) {
		asm volatile( "fxrstor %0"
			      : : "m" (tsk->thread.i387.fxsave) );
	} else {
		asm volatile( "frstor %0"
			      : : "m" (tsk->thread.i387.fsave) );
	}
}

/*
 * FPU tag word conversions.
 */

static inline unsigned short twd_i387_to_fxsr( unsigned short twd )
{
	unsigned short ret = 0;
	int i;

	for ( i = 0 ; i < 8 ; i++ ) {
		if ( (twd & 0x3) != 0x3 ) {
			ret |= (1 << i);
		}
		twd = twd >> 2;
	}
	return ret;
}

static inline unsigned long twd_fxsr_to_i387( struct i387_fxsave_struct *fxsave )
{
	struct _fpxreg *st = NULL;
	unsigned long twd = (unsigned long) fxsave->twd;
	unsigned long tag;
	unsigned long ret = 0xffff0000;
	int i;

#define FPREG_ADDR(f, n)	((char *)&(f)->st_space + (n) * 16);

	for ( i = 0 ; i < 8 ; i++ ) {
		if ( twd & 0x1 ) {
			st = (struct _fpxreg *) FPREG_ADDR( fxsave, i );

			switch ( st->exponent ) {
			case 0xffff:
				tag = 2;		/* Special */
				break;
			case 0x0000:
				if ( !st->significand[0] &&
				     !st->significand[1] &&
				     !st->significand[2] &&
				     !st->significand[3] ) {
					tag = 1;	/* Zero */
				} else {
					tag = 2;	/* Special */
				}
				break;
			default:
				if ( st->significand[3] & 0x8000 ) {
					tag = 0;	/* Valid */
				} else {
					tag = 2;	/* Special */
				}
				break;
			}
		} else {
			tag = 3;			/* Empty */
		}
		ret |= (tag << (2 * i));
		twd = twd >> 1;
	}
	return ret;
}

/*
 * FPU state interaction.
 */

unsigned short get_fpu_cwd( struct task_struct *tsk )
{
	if ( HAVE_FXSR ) {
		return tsk->thread.i387.fxsave.cwd;
	} else {
		return (unsigned short)tsk->thread.i387.fsave.cwd;
	}
}

unsigned short get_fpu_swd( struct task_struct *tsk )
{
	if ( HAVE_FXSR ) {
		return tsk->thread.i387.fxsave.swd;
	} else {
		return (unsigned short)tsk->thread.i387.fsave.swd;
	}
}

unsigned short get_fpu_twd( struct task_struct *tsk )
{
	if ( HAVE_FXSR ) {
		return tsk->thread.i387.fxsave.twd;
	} else {
		return (unsigned short)tsk->thread.i387.fsave.twd;
	}
}

unsigned short get_fpu_mxcsr( struct task_struct *tsk )
{
	if ( HAVE_FXSR ) {
		return tsk->thread.i387.fxsave.mxcsr;
	} else {
		return 0x1f80;
	}
}

void set_fpu_cwd( struct task_struct *tsk, unsigned short cwd )
{
	if ( HAVE_FXSR ) {
		tsk->thread.i387.fxsave.cwd = cwd;
	} else {
		tsk->thread.i387.fsave.cwd = ((long)cwd | 0xffff0000);
	}
}

void set_fpu_swd( struct task_struct *tsk, unsigned short swd )
{
	if ( HAVE_FXSR ) {
		tsk->thread.i387.fxsave.swd = swd;
	} else {
		tsk->thread.i387.fsave.swd = ((long)swd | 0xffff0000);
	}
}

void set_fpu_twd( struct task_struct *tsk, unsigned short twd )
{
	if ( HAVE_FXSR ) {
		tsk->thread.i387.fxsave.twd = twd_i387_to_fxsr(twd);
	} else {
		tsk->thread.i387.fsave.twd = ((long)twd | 0xffff0000);
	}
}

void set_fpu_mxcsr( struct task_struct *tsk, unsigned short mxcsr )
{
	if ( HAVE_FXSR ) {
		tsk->thread.i387.fxsave.mxcsr = mxcsr;
	}
}

/*
 * FXSR floating point environment conversions.
 */

static inline int convert_fxsr_to_user( struct _fpstate *buf,
					struct i387_fxsave_struct *fxsave )
{
	unsigned long env[7];
	struct _fpreg *to;
	struct _fpxreg *from;
	int i;

	env[0] = (unsigned long)fxsave->cwd | 0xffff0000;
	env[1] = (unsigned long)fxsave->swd | 0xffff0000;
	env[2] = twd_fxsr_to_i387(fxsave);
	env[3] = fxsave->fip;
	env[4] = fxsave->fcs | ((unsigned long)fxsave->fop << 16);
	env[5] = fxsave->foo;
	env[6] = fxsave->fos;

	if ( __copy_to_user( buf, env, 7 * sizeof(unsigned long) ) )
		return 1;

	to = &buf->_st[0];
	from = (struct _fpxreg *) &fxsave->st_space[0];
	for ( i = 0 ; i < 8 ; i++, to++, from++ ) {
		if ( __copy_to_user( to, from, sizeof(*to) ) )
			return 1;
	}
	return 0;
}

static inline int convert_fxsr_from_user( struct i387_fxsave_struct *fxsave,
					  struct _fpstate *buf )
{
	unsigned long env[7];
	struct _fpxreg *to;
	struct _fpreg *from;
	int i;

	if ( __copy_from_user( env, buf, 7 * sizeof(long) ) )
		return 1;

	fxsave->cwd = (unsigned short)(env[0] & 0xffff);
	fxsave->swd = (unsigned short)(env[1] & 0xffff);
	fxsave->twd = twd_i387_to_fxsr((unsigned short)(env[2] & 0xffff));
	fxsave->fip = env[3];
	fxsave->fop = (unsigned short)((env[4] & 0xffff0000) >> 16);
	fxsave->fcs = (env[4] & 0xffff);
	fxsave->foo = env[5];
	fxsave->fos = env[6];

	to = (struct _fpxreg *) &fxsave->st_space[0];
	from = &buf->_st[0];
	for ( i = 0 ; i < 8 ; i++, to++, from++ ) {
		if ( __copy_from_user( to, from, sizeof(*from) ) )
			return 1;
	}
	return 0;
}

/*
 * Signal frame handlers.
 */

static inline int save_i387_fsave( struct _fpstate *buf )
{
	struct task_struct *tsk = current;

	tsk->thread.i387.fsave.status = tsk->thread.i387.fsave.swd;
	if ( __copy_to_user( buf, &tsk->thread.i387.fsave,
			     sizeof(struct i387_fsave_struct) ) )
		return -1;
	return 1;
}

static inline int save_i387_fxsave( struct _fpstate *buf )
{
	struct task_struct *tsk = current;
	int err = 0;

	if ( convert_fxsr_to_user( buf, &tsk->thread.i387.fxsave ) )
		return -1;

	err |= __put_user( tsk->thread.i387.fxsave.swd, &buf->status );
	err |= __put_user( X86_FXSR_MAGIC, &buf->magic );
	if ( err )
		return -1;

	if ( __copy_to_user( &buf->_fxsr_env[0], &tsk->thread.i387.fxsave,
			     sizeof(struct i387_fxsave_struct) ) )
		return -1;
	return 1;
}

int save_i387( struct _fpstate *buf )
{
	if ( !current->used_math )
		return 0;

	/* This will cause a "finit" to be triggered by the next
	 * attempted FPU operation by the 'current' process.
	 */
	current->used_math = 0;

	if ( HAVE_HWFP ) {
		if ( HAVE_FXSR ) {
			return save_i387_fxsave( buf );
		} else {
			return save_i387_fsave( buf );
		}
	} else {
		return save_i387_soft( &current->thread.i387.soft, buf );
	}
}

static inline int restore_i387_fsave( struct _fpstate *buf )
{
	struct task_struct *tsk = current;
	clear_fpu( tsk );
	return __copy_from_user( &tsk->thread.i387.fsave, buf,
				 sizeof(struct i387_fsave_struct) );
}

static inline int restore_i387_fxsave( struct _fpstate *buf )
{
	struct task_struct *tsk = current;
	clear_fpu( tsk );
	if ( __copy_from_user( &tsk->thread.i387.fxsave, &buf->_fxsr_env[0],
			       sizeof(struct i387_fxsave_struct) ) )
		return 1;
	return convert_fxsr_from_user( &tsk->thread.i387.fxsave, buf );
}

int restore_i387( struct _fpstate *buf )
{
	int err;

	if ( HAVE_HWFP ) {
		if ( HAVE_FXSR ) {
			err =  restore_i387_fxsave( buf );
		} else {
			err = restore_i387_fsave( buf );
		}
	} else {
		err = restore_i387_soft( &current->thread.i387.soft, buf );
	}
	current->used_math = 1;
	return err;
}

/*
 * ptrace request handlers.
 */

static inline int get_fpregs_fsave( struct user_i387_struct *buf,
				    struct task_struct *tsk )
{
	return __copy_to_user( buf, &tsk->thread.i387.fsave,
			       sizeof(struct user_i387_struct) );
}

static inline int get_fpregs_fxsave( struct user_i387_struct *buf,
				     struct task_struct *tsk )
{
	return convert_fxsr_to_user( (struct _fpstate *)buf,
				     &tsk->thread.i387.fxsave );
}

int get_fpregs( struct user_i387_struct *buf, struct task_struct *tsk )
{
	if ( HAVE_HWFP ) {
		if ( HAVE_FXSR ) {
			return get_fpregs_fxsave( buf, tsk );
		} else {
			return get_fpregs_fsave( buf, tsk );
		}
	} else {
		return save_i387_soft( &tsk->thread.i387.soft,
				       (struct _fpstate *)buf );
	}
}

static inline int set_fpregs_fsave( struct task_struct *tsk,
				    struct user_i387_struct *buf )
{
	return __copy_from_user( &tsk->thread.i387.fsave, buf,
				 sizeof(struct user_i387_struct) );
}

static inline int set_fpregs_fxsave( struct task_struct *tsk,
				     struct user_i387_struct *buf )
{
	return convert_fxsr_from_user( &tsk->thread.i387.fxsave,
				       (struct _fpstate *)buf );
}

int set_fpregs( struct task_struct *tsk, struct user_i387_struct *buf )
{
	if ( HAVE_HWFP ) {
		if ( HAVE_FXSR ) {
			return set_fpregs_fxsave( tsk, buf );
		} else {
			return set_fpregs_fsave( tsk, buf );
		}
	} else {
		return restore_i387_soft( &tsk->thread.i387.soft,
					  (struct _fpstate *)buf );
	}
}

int get_fpxregs( struct user_fxsr_struct *buf, struct task_struct *tsk )
{
	if ( HAVE_FXSR ) {
		__copy_to_user( (void *)buf, &tsk->thread.i387.fxsave,
				sizeof(struct user_fxsr_struct) );
		return 0;
	} else {
		return -EIO;
	}
}

int set_fpxregs( struct task_struct *tsk, struct user_fxsr_struct *buf )
{
	if ( HAVE_FXSR ) {
		__copy_from_user( &tsk->thread.i387.fxsave, (void *)buf,
				  sizeof(struct user_fxsr_struct) );
		return 0;
	} else {
		return -EIO;
	}
}

/*
 * FPU state for core dumps.
 */

static inline void copy_fpu_fsave( struct task_struct *tsk,
				   struct user_i387_struct *fpu )
{
	memcpy( fpu, &tsk->thread.i387.fsave,
		sizeof(struct user_i387_struct) );
}

static inline void copy_fpu_fxsave( struct task_struct *tsk,
				   struct user_i387_struct *fpu )
{
	unsigned short *to;
	unsigned short *from;
	int i;

	memcpy( fpu, &tsk->thread.i387.fxsave, 7 * sizeof(long) );

	to = (unsigned short *)&fpu->st_space[0];
	from = (unsigned short *)&tsk->thread.i387.fxsave.st_space[0];
	for ( i = 0 ; i < 8 ; i++, to += 5, from += 8 ) {
		memcpy( to, from, 5 * sizeof(unsigned short) );
	}
}

int dump_fpu( struct pt_regs *regs, struct user_i387_struct *fpu )
{
	int fpvalid;
	struct task_struct *tsk = current;

	fpvalid = tsk->used_math;
	if ( fpvalid ) {
		unlazy_fpu( tsk );
		if ( HAVE_FXSR ) {
			copy_fpu_fxsave( tsk, fpu );
		} else {
			copy_fpu_fsave( tsk, fpu );
		}
	}

	return fpvalid;
}

int dump_extended_fpu( struct pt_regs *regs, struct user_fxsr_struct *fpu )
{
	int fpvalid;
	struct task_struct *tsk = current;

	fpvalid = tsk->used_math && HAVE_FXSR;
	if ( fpvalid ) {
		unlazy_fpu( tsk );
		memcpy( fpu, &tsk->thread.i387.fxsave,
			sizeof(struct user_fxsr_struct) );
	}

	return fpvalid;
}
