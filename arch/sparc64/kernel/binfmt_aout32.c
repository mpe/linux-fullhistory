/*
 *  linux/fs/binfmt_aout.c
 *
 *  Copyright (C) 1991, 1992, 1996  Linus Torvalds
 *
 *  Hacked a bit by DaveM to make it work with 32-bit SunOS
 *  binaries on the sparc64 port.
 */

#include <linux/module.h>

#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/a.out.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/file.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/malloc.h>
#include <linux/binfmts.h>
#include <linux/personality.h>
#include <linux/init.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>

static int load_aout32_binary(struct linux_binprm *, struct pt_regs * regs);
static int load_aout32_library(int fd);
static int aout32_core_dump(long signr, struct pt_regs * regs);

extern void dump_thread(struct pt_regs *, struct user *);

static struct linux_binfmt aout32_format = {
	NULL, NULL, load_aout32_binary, load_aout32_library, aout32_core_dump
};

static void set_brk(unsigned long start, unsigned long end)
{
	start = PAGE_ALIGN(start);
	end = PAGE_ALIGN(end);
	if (end <= start)
		return;
	do_mmap(NULL, start, end - start,
		PROT_READ | PROT_WRITE | PROT_EXEC,
		MAP_FIXED | MAP_PRIVATE, 0);
}

/*
 * These are the only things you should do on a core-file: use only these
 * macros to write out all the necessary info.
 */
#define DUMP_WRITE(addr,nr) \
while (file.f_op->write(&file,(char *)(addr),(nr),&file.f_pos) != (nr)) \
	goto close_coredump

#define DUMP_SEEK(offset) \
if (file.f_op->llseek) { \
	if (file.f_op->llseek(&file,(offset),0) != (offset)) \
 		goto close_coredump; \
} else file.f_pos = (offset)

/*
 * Routine writes a core dump image in the current directory.
 * Currently only a stub-function.
 *
 * Note that setuid/setgid files won't make a core-dump if the uid/gid
 * changed due to the set[u|g]id. It's enforced by the "current->dumpable"
 * field, which also makes sure the core-dumps won't be recursive if the
 * dumping of the process results in another error..
 */

static inline int
do_aout32_core_dump(long signr, struct pt_regs * regs)
{
	struct dentry * dentry = NULL;
	struct inode * inode = NULL;
	struct file file;
	mm_segment_t fs;
	int has_dumped = 0;
	char corefile[6+sizeof(current->comm)];
	unsigned long dump_start, dump_size;
	struct user dump;
#       define START_DATA(u)    (u.u_tsize)
#       define START_STACK(u)   ((regs->u_regs[UREG_FP]) & ~(PAGE_SIZE - 1))

	if (!current->dumpable || atomic_read(&current->mm->count) != 1)
		return 0;
	current->dumpable = 0;

/* See if we have enough room to write the upage.  */
	if (current->rlim[RLIMIT_CORE].rlim_cur < PAGE_SIZE)
		return 0;
	fs = get_fs();
	set_fs(KERNEL_DS);
	memcpy(corefile,"core.",5);
#if 0
	memcpy(corefile+5,current->comm,sizeof(current->comm));
#else
	corefile[4] = '\0';
#endif
	dentry = open_namei(corefile,O_CREAT | 2 | O_TRUNC | O_NOFOLLOW, 0600);
	if (IS_ERR(dentry)) {
		dentry = NULL;
		goto end_coredump;
	}
	inode = dentry->d_inode;
	if (!S_ISREG(inode->i_mode))
		goto end_coredump;
	if (!inode->i_op || !inode->i_op->default_file_ops)
		goto end_coredump;
	if (get_write_access(inode))
		goto end_coredump;
	file.f_mode = 3;
	file.f_flags = 0;
	file.f_count = 1;
	file.f_dentry = dentry;
	file.f_pos = 0;
	file.f_reada = 0;
	file.f_op = inode->i_op->default_file_ops;
	if (file.f_op->open)
		if (file.f_op->open(inode,&file))
			goto done_coredump;
	if (!file.f_op->write)
		goto close_coredump;
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
close_coredump:
	if (file.f_op->release)
		file.f_op->release(inode,&file);
done_coredump:
	put_write_access(inode);
end_coredump:
	set_fs(fs);
	dput(dentry);
	return has_dumped;
}

static int
aout32_core_dump(long signr, struct pt_regs * regs)
{
	int retval;

	MOD_INC_USE_COUNT;
	retval = do_aout32_core_dump(signr, regs);
	MOD_DEC_USE_COUNT;
	return retval;
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

static inline int do_load_aout32_binary(struct linux_binprm * bprm,
					struct pt_regs * regs)
{
	struct exec ex;
	struct file * file;
	int fd;
	unsigned long error;
	unsigned long p = bprm->p;
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

	current->personality = PER_LINUX;
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
	current->mm->end_code = ex.a_text +
		(current->mm->start_code = N_TXTADDR(ex));
	current->mm->end_data = ex.a_data +
		(current->mm->start_data = N_DATADDR(ex));
	current->mm->brk = ex.a_bss +
		(current->mm->start_brk = N_BSSADDR(ex));

	current->mm->rss = 0;
	current->mm->mmap = NULL;
	current->suid = current->euid = current->fsuid = bprm->e_uid;
	current->sgid = current->egid = current->fsgid = bprm->e_gid;
 	current->flags &= ~PF_FORKNOEXEC;
	if (N_MAGIC(ex) == NMAGIC) {
		/* Fuck me plenty... */
		error = do_mmap(NULL, N_TXTADDR(ex), ex.a_text,
				PROT_READ|PROT_WRITE|PROT_EXEC,
				MAP_FIXED|MAP_PRIVATE, 0);
		read_exec(bprm->dentry, fd_offset, (char *) N_TXTADDR(ex),
			  ex.a_text, 0);
		error = do_mmap(NULL, N_DATADDR(ex), ex.a_data,
				PROT_READ|PROT_WRITE|PROT_EXEC,
				MAP_FIXED|MAP_PRIVATE, 0);
		read_exec(bprm->dentry, fd_offset + ex.a_text, (char *) N_DATADDR(ex),
			  ex.a_data, 0);
		goto beyond_if;
	}

	if (N_MAGIC(ex) == OMAGIC) {
		do_mmap(NULL, N_TXTADDR(ex) & PAGE_MASK,
			ex.a_text+ex.a_data + PAGE_SIZE - 1,
			PROT_READ|PROT_WRITE|PROT_EXEC,
			MAP_FIXED|MAP_PRIVATE, 0);
		read_exec(bprm->dentry, fd_offset, (char *) N_TXTADDR(ex),
			  ex.a_text+ex.a_data, 0);
	} else {
		if ((ex.a_text & 0xfff || ex.a_data & 0xfff) &&
		    (N_MAGIC(ex) != NMAGIC))
			printk(KERN_NOTICE "executable not page aligned\n");

		fd = open_dentry(bprm->dentry, O_RDONLY);
		if (fd < 0)
			return fd;
		file = fcheck(fd);

		if (!file->f_op || !file->f_op->mmap) {
			sys_close(fd);
			do_mmap(NULL, 0, ex.a_text+ex.a_data,
				PROT_READ|PROT_WRITE|PROT_EXEC,
				MAP_FIXED|MAP_PRIVATE, 0);
			read_exec(bprm->dentry, fd_offset,
				  (char *) N_TXTADDR(ex), ex.a_text+ex.a_data, 0);
			goto beyond_if;
		}

		error = do_mmap(file, N_TXTADDR(ex), ex.a_text,
			PROT_READ | PROT_EXEC,
			MAP_FIXED | MAP_PRIVATE | MAP_DENYWRITE | MAP_EXECUTABLE,
			fd_offset);

		if (error != N_TXTADDR(ex)) {
			sys_close(fd);
			send_sig(SIGKILL, current, 0);
			return error;
		}

 		error = do_mmap(file, N_DATADDR(ex), ex.a_data,
				PROT_READ | PROT_WRITE | PROT_EXEC,
				MAP_FIXED | MAP_PRIVATE | MAP_DENYWRITE | MAP_EXECUTABLE,
				fd_offset + ex.a_text);
		sys_close(fd);
		if (error != N_DATADDR(ex)) {
			send_sig(SIGKILL, current, 0);
			return error;
		}
	}
beyond_if:
	if (current->exec_domain && current->exec_domain->module)
		__MOD_DEC_USE_COUNT(current->exec_domain->module);
	if (current->binfmt && current->binfmt->module)
		__MOD_DEC_USE_COUNT(current->binfmt->module);
	current->exec_domain = lookup_exec_domain(current->personality);
	current->binfmt = &aout32_format;
	if (current->exec_domain && current->exec_domain->module)
		__MOD_INC_USE_COUNT(current->exec_domain->module);
	if (current->binfmt && current->binfmt->module)
		__MOD_INC_USE_COUNT(current->binfmt->module);

	set_brk(current->mm->start_brk, current->mm->brk);

	p = setup_arg_pages(p, bprm);

	p = (unsigned long) create_aout32_tables((char *)p, bprm);
	current->mm->start_stack = p;
	start_thread32(regs, ex.a_entry, p);
	if (current->flags & PF_PTRACED)
		send_sig(SIGTRAP, current, 0);
	return 0;
}


static int
load_aout32_binary(struct linux_binprm * bprm, struct pt_regs * regs)
{
	int retval;

	MOD_INC_USE_COUNT;
	retval = do_load_aout32_binary(bprm, regs);
	MOD_DEC_USE_COUNT;
	return retval;
}

/* N.B. Move to .h file and use code in fs/binfmt_aout.c? */
static inline int
do_load_aout32_library(int fd)
{
        struct file * file;
	struct exec ex;
	struct dentry * dentry;
	struct inode * inode;
	unsigned int len;
	unsigned int bss;
	unsigned int start_addr;
	unsigned long error;

	file = fcheck(fd);

	if (!file || !file->f_op)
		return -EACCES;

	dentry = file->f_dentry;
	inode = dentry->d_inode;

	/* Seek into the file */
	if (file->f_op->llseek) {
		if ((error = file->f_op->llseek(file, 0, 0)) != 0)
			return -ENOEXEC;
	} else
		file->f_pos = 0;

	set_fs(KERNEL_DS);
	error = file->f_op->read(file, (char *) &ex, sizeof(ex), &file->f_pos);
	set_fs(USER_DS);
	if (error != sizeof(ex))
		return -ENOEXEC;

	/* We come in here for the regular a.out style of shared libraries */
	if ((N_MAGIC(ex) != ZMAGIC && N_MAGIC(ex) != QMAGIC) || N_TRSIZE(ex) ||
	    N_DRSIZE(ex) || ((ex.a_entry & 0xfff) && N_MAGIC(ex) == ZMAGIC) ||
	    inode->i_size < ex.a_text+ex.a_data+N_SYMSIZE(ex)+N_TXTOFF(ex)) {
		return -ENOEXEC;
	}
	if (N_MAGIC(ex) == ZMAGIC && N_TXTOFF(ex) &&
	    (N_TXTOFF(ex) < inode->i_sb->s_blocksize)) {
		printk("N_TXTOFF < BLOCK_SIZE. Please convert library\n");
		return -ENOEXEC;
	}

	if (N_FLAGS(ex)) return -ENOEXEC;

	/* For  QMAGIC, the starting address is 0x20 into the page.  We mask
	   this off to get the starting address for the page */

	start_addr =  ex.a_entry & 0xfffff000;

	/* Now use mmap to map the library into memory. */
	error = do_mmap(file, start_addr, ex.a_text + ex.a_data,
			PROT_READ | PROT_WRITE | PROT_EXEC,
			MAP_FIXED | MAP_PRIVATE | MAP_DENYWRITE,
			N_TXTOFF(ex));
	if (error != start_addr)
		return error;
	len = PAGE_ALIGN(ex.a_text + ex.a_data);
	bss = ex.a_text + ex.a_data + ex.a_bss;
	if (bss > len) {
		error = do_mmap(NULL, start_addr + len, bss-len,
				PROT_READ|PROT_WRITE|PROT_EXEC,
				MAP_PRIVATE|MAP_FIXED, 0);
		if (error != start_addr + len)
			return error;
	}
	return 0;
}

static int
load_aout32_library(int fd)
{
	int retval;

	MOD_INC_USE_COUNT;
	retval = do_load_aout32_library(fd);
	MOD_DEC_USE_COUNT;
	return retval;
}


__initfunc(int init_aout32_binfmt(void))
{
	return register_binfmt(&aout32_format);
}
