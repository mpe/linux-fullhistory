/*
 *  linux/fs/binfmt_aout.c
 *
 *  Copyright (C) 1991, 1992, 1996  Linus Torvalds
 *
 *  Hacked a bit by DaveM to make it work with 32-bit SunOS
 *  binaries on the sparc64 port.
 */

#include <linux/module.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/a.out.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/malloc.h>
#include <linux/binfmts.h>
#include <linux/personality.h>
#include <linux/init.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/pgalloc.h>

static int load_aout32_binary(struct linux_binprm *, struct pt_regs * regs);
static int load_aout32_library(int fd);
static int aout32_core_dump(long signr, struct pt_regs * regs, struct file *file);

extern void dump_thread(struct pt_regs *, struct user *);

static struct linux_binfmt aout32_format = {
	NULL, THIS_MODULE, load_aout32_binary, load_aout32_library, aout32_core_dump,
	PAGE_SIZE
};

static void set_brk(unsigned long start, unsigned long end)
{
	start = PAGE_ALIGN(start);
	end = PAGE_ALIGN(end);
	if (end <= start)
		return;
	do_brk(start, end - start);
}

/*
 * These are the only things you should do on a core-file: use only these
 * macros to write out all the necessary info.
 */

static int dump_write(struct file *file, const void *addr, int nr)
{
	return file->f_op->write(file, addr, nr, &file->f_pos) == nr;
}

#define DUMP_WRITE(addr, nr)	\
	if (!dump_write(file, (void *)(addr), (nr))) \
		goto end_coredump;

#define DUMP_SEEK(offset) \
if (file->f_op->llseek) { \
	if (file->f_op->llseek(file,(offset),0) != (offset)) \
 		goto end_coredump; \
} else file->f_pos = (offset)

/*
 * Routine writes a core dump image in the current directory.
 * Currently only a stub-function.
 *
 * Note that setuid/setgid files won't make a core-dump if the uid/gid
 * changed due to the set[u|g]id. It's enforced by the "current->dumpable"
 * field, which also makes sure the core-dumps won't be recursive if the
 * dumping of the process results in another error..
 */

static int aout32_core_dump(long signr, struct pt_regs *regs, struct file *file)
{
	mm_segment_t fs;
	int has_dumped = 0;
	unsigned long dump_start, dump_size;
	struct user dump;
#       define START_DATA(u)    (u.u_tsize)
#       define START_STACK(u)   ((regs->u_regs[UREG_FP]) & ~(PAGE_SIZE - 1))

	fs = get_fs();
	set_fs(KERNEL_DS);
	has_dumped = 1;
	current->flags |= PF_DUMPCORE;
       	strncpy(dump.u_comm, current->comm, sizeof(current->comm));
	dump.signal = signr;
	dump_thread(regs, &dump);

/* If the size of the dump file exceeds the rlimit, then see what would happen
   if we wrote the stack, but not the data area.  */
	if ((dump.u_dsize+dump.u_ssize) >
	    current->rlim[RLIMIT_CORE].rlim_cur)
		dump.u_dsize = 0;

/* Make sure we have enough room to write the stack and data areas. */
	if ((dump.u_ssize) >
	    current->rlim[RLIMIT_CORE].rlim_cur)
		dump.u_ssize = 0;

/* make sure we actually have a data and stack area to dump */
	set_fs(USER_DS);
	if (verify_area(VERIFY_READ, (void *) START_DATA(dump), dump.u_dsize))
		dump.u_dsize = 0;
	if (verify_area(VERIFY_READ, (void *) START_STACK(dump), dump.u_ssize))
		dump.u_ssize = 0;

	set_fs(KERNEL_DS);
/* struct user */
	DUMP_WRITE(&dump,sizeof(dump));
/* now we start writing out the user space info */
	set_fs(USER_DS);
/* Dump the data area */
	if (dump.u_dsize != 0) {
		dump_start = START_DATA(dump);
		dump_size = dump.u_dsize;
		DUMP_WRITE(dump_start,dump_size);
	}
/* Now prepare to dump the stack area */
	if (dump.u_ssize != 0) {
		dump_start = START_STACK(dump);
		dump_size = dump.u_ssize;
		DUMP_WRITE(dump_start,dump_size);
	}
/* Finally dump the task struct.  Not be used by gdb, but could be useful */
	set_fs(KERNEL_DS);
	DUMP_WRITE(current,sizeof(*current));
end_coredump:
	set_fs(fs);
	return has_dumped;
}

/*
 * create_aout32_tables() parses the env- and arg-strings in new user
 * memory and creates the pointer tables from them, and puts their
 * addresses on the "stack", returning the new stack pointer value.
 */
#define A(__x) ((unsigned long)(__x))

static u32 *create_aout32_tables(char * p, struct linux_binprm * bprm)
{
	u32 *argv, *envp;
	u32 *sp;
	int argc = bprm->argc;
	int envc = bprm->envc;

	sp = (u32 *) ((-(unsigned long)sizeof(char *)) & (unsigned long) p);

	/* This imposes the proper stack alignment for a new process. */
	sp = (u32 *) (((unsigned long) sp) & ~7);
	if ((envc+argc+3)&1)
		--sp;

	sp -= envc+1;
	envp = (u32 *) sp;
	sp -= argc+1;
	argv = (u32 *) sp;
	put_user(argc,--sp);
	current->mm->arg_start = (unsigned long) p;
	while (argc-->0) {
		char c;
		put_user(((u32)A(p)),argv++);
		do {
			get_user(c,p++);
		} while (c);
	}
	put_user(NULL,argv);
	current->mm->arg_end = current->mm->env_start = (unsigned long) p;
	while (envc-->0) {
		char c;
		put_user(((u32)A(p)),envp++);
		do {
			get_user(c,p++);
		} while (c);
	}
	put_user(NULL,envp);
	current->mm->env_end = (unsigned long) p;
	return sp;
}

/*
 * These are the functions used to load a.out style executables and shared
 * libraries.  There is no binary dependent code anywhere else.
 */

static int load_aout32_binary(struct linux_binprm * bprm, struct pt_regs * regs)
{
	struct exec ex;
	struct file * file;
	int fd;
	unsigned long error;
	unsigned long fd_offset;
	unsigned long rlim;
	int retval;

	ex = *((struct exec *) bprm->buf);		/* exec-header */
	if ((N_MAGIC(ex) != ZMAGIC && N_MAGIC(ex) != OMAGIC &&
	     N_MAGIC(ex) != QMAGIC && N_MAGIC(ex) != NMAGIC) ||
	    N_TRSIZE(ex) || N_DRSIZE(ex) ||
	    bprm->dentry->d_inode->i_size < ex.a_text+ex.a_data+N_SYMSIZE(ex)+N_TXTOFF(ex)) {
		return -ENOEXEC;
	}

	fd_offset = N_TXTOFF(ex);

	/* Check initial limits. This avoids letting people circumvent
	 * size limits imposed on them by creating programs with large
	 * arrays in the data or bss.
	 */
	rlim = current->rlim[RLIMIT_DATA].rlim_cur;
	if (rlim >= RLIM_INFINITY)
		rlim = ~0;
	if (ex.a_data + ex.a_bss > rlim)
		return -ENOMEM;

	/* Flush all traces of the currently running executable */
	retval = flush_old_exec(bprm);
	if (retval)
		return retval;

	/* OK, This is the point of no return */
	current->personality = PER_LINUX;

	current->mm->end_code = ex.a_text +
		(current->mm->start_code = N_TXTADDR(ex));
	current->mm->end_data = ex.a_data +
		(current->mm->start_data = N_DATADDR(ex));
	current->mm->brk = ex.a_bss +
		(current->mm->start_brk = N_BSSADDR(ex));

	current->mm->rss = 0;
	current->mm->mmap = NULL;
	compute_creds(bprm);
 	current->flags &= ~PF_FORKNOEXEC;
	if (N_MAGIC(ex) == NMAGIC) {
		/* Fuck me plenty... */
		error = do_brk(N_TXTADDR(ex), ex.a_text);
		read_exec(bprm->dentry, fd_offset, (char *) N_TXTADDR(ex),
			  ex.a_text, 0);
		error = do_brk(N_DATADDR(ex), ex.a_data);
		read_exec(bprm->dentry, fd_offset + ex.a_text, (char *) N_DATADDR(ex),
			  ex.a_data, 0);
		goto beyond_if;
	}

	if (N_MAGIC(ex) == OMAGIC) {
		do_brk(N_TXTADDR(ex) & PAGE_MASK,
			ex.a_text+ex.a_data + PAGE_SIZE - 1);
		read_exec(bprm->dentry, fd_offset, (char *) N_TXTADDR(ex),
			  ex.a_text+ex.a_data, 0);
	} else {
		static unsigned long error_time;
		if ((ex.a_text & 0xfff || ex.a_data & 0xfff) &&
		    (N_MAGIC(ex) != NMAGIC) && (jiffies-error_time) > 5*HZ)
		{
			printk(KERN_NOTICE "executable not page aligned\n");
			error_time = jiffies;
		}

		fd = open_dentry(bprm->dentry, O_RDONLY);
		if (fd < 0)
			return fd;
		file = fget(fd);

		if (!file->f_op || !file->f_op->mmap) {
			fput(file);
			sys_close(fd);
			do_brk(0, ex.a_text+ex.a_data);
			read_exec(bprm->dentry, fd_offset,
				  (char *) N_TXTADDR(ex), ex.a_text+ex.a_data, 0);
			goto beyond_if;
		}

		error = do_mmap(file, N_TXTADDR(ex), ex.a_text,
			PROT_READ | PROT_EXEC,
			MAP_FIXED | MAP_PRIVATE | MAP_DENYWRITE | MAP_EXECUTABLE,
			fd_offset);

		if (error != N_TXTADDR(ex)) {
			fput(file);
			sys_close(fd);
			send_sig(SIGKILL, current, 0);
			return error;
		}

 		error = do_mmap(file, N_DATADDR(ex), ex.a_data,
				PROT_READ | PROT_WRITE | PROT_EXEC,
				MAP_FIXED | MAP_PRIVATE | MAP_DENYWRITE | MAP_EXECUTABLE,
				fd_offset + ex.a_text);
		fput(file);
		sys_close(fd);
		if (error != N_DATADDR(ex)) {
			send_sig(SIGKILL, current, 0);
			return error;
		}
	}
beyond_if:
	put_exec_domain(current->exec_domain);
	if (current->binfmt && current->binfmt->module)
		__MOD_DEC_USE_COUNT(current->binfmt->module);
	current->exec_domain = lookup_exec_domain(current->personality);
	current->binfmt = &aout32_format;
	if (current->binfmt && current->binfmt->module)
		__MOD_INC_USE_COUNT(current->binfmt->module);

	set_brk(current->mm->start_brk, current->mm->brk);

	retval = setup_arg_pages(bprm);
	if (retval < 0) { 
		/* Someone check-me: is this error path enough? */ 
		send_sig(SIGKILL, current, 0); 
		return retval;
	}

	current->mm->start_stack =
		(unsigned long) create_aout32_tables((char *)bprm->p, bprm);
	if (!(current->thread.flags & SPARC_FLAG_32BIT)) {
		unsigned long pgd_cache;

		pgd_cache = ((unsigned long)current->mm->pgd[0])<<11UL;
		__asm__ __volatile__("stxa\t%0, [%1] %2"
				     : /* no outputs */
				     : "r" (pgd_cache),
				       "r" (TSB_REG), "i" (ASI_DMMU));
		current->thread.flags |= SPARC_FLAG_32BIT;
	}
	start_thread32(regs, ex.a_entry, current->mm->start_stack);
	if (current->flags & PF_PTRACED)
		send_sig(SIGTRAP, current, 0);
	return 0;
}

/* N.B. Move to .h file and use code in fs/binfmt_aout.c? */
static int load_aout32_library(int fd)
{
        struct file * file;
	struct inode * inode;
	unsigned long bss, start_addr, len;
	unsigned long error;
	int retval;
	loff_t offset = 0;
	struct exec ex;

	retval = -EACCES;
	file = fget(fd);
	if (!file)
		goto out;
	if (!file->f_op)
		goto out_putf;
	inode = file->f_dentry->d_inode;

	retval = -ENOEXEC;
	/* N.B. Save current fs? */
	set_fs(KERNEL_DS);
	error = file->f_op->read(file, (char *) &ex, sizeof(ex), &offset);
	set_fs(USER_DS);
	if (error != sizeof(ex))
		goto out_putf;

	/* We come in here for the regular a.out style of shared libraries */
	if ((N_MAGIC(ex) != ZMAGIC && N_MAGIC(ex) != QMAGIC) || N_TRSIZE(ex) ||
	    N_DRSIZE(ex) || ((ex.a_entry & 0xfff) && N_MAGIC(ex) == ZMAGIC) ||
	    inode->i_size < ex.a_text+ex.a_data+N_SYMSIZE(ex)+N_TXTOFF(ex)) {
		goto out_putf;
	}

	if (N_MAGIC(ex) == ZMAGIC && N_TXTOFF(ex) &&
	    (N_TXTOFF(ex) < inode->i_sb->s_blocksize)) {
		printk("N_TXTOFF < BLOCK_SIZE. Please convert library\n");
		goto out_putf;
	}

	if (N_FLAGS(ex))
		goto out_putf;

	/* For  QMAGIC, the starting address is 0x20 into the page.  We mask
	   this off to get the starting address for the page */

	start_addr =  ex.a_entry & 0xfffff000;

	/* Now use mmap to map the library into memory. */
	error = do_mmap(file, start_addr, ex.a_text + ex.a_data,
			PROT_READ | PROT_WRITE | PROT_EXEC,
			MAP_FIXED | MAP_PRIVATE | MAP_DENYWRITE,
			N_TXTOFF(ex));
	retval = error;
	if (error != start_addr)
		goto out_putf;

	len = PAGE_ALIGN(ex.a_text + ex.a_data);
	bss = ex.a_text + ex.a_data + ex.a_bss;
	if (bss > len) {
		error = do_brk(start_addr + len, bss - len);
		retval = error;
		if (error != start_addr + len)
			goto out_putf;
	}
	retval = 0;

out_putf:
	fput(file);
out:
	return retval;
}

static int __init init_aout32_binfmt(void)
{
	return register_binfmt(&aout32_format);
}

static void __exit exit_aout32_binfmt(void)
{
	unregister_binfmt(&aout32_format);
}

EXPORT_NO_SYMBOLS;

module_init(init_aout32_binfmt);
module_exit(exit_aout32_binfmt);
