/*
 *  linux/fs/exec.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * #!-checking implemented by tytso.
 */
/*
 * Demand-loading implemented 01.12.91 - no need to read anything but
 * the header into memory. The inode of the executable is put into
 * "current->executable", and page faults do the actual loading. Clean.
 *
 * Once more I can proudly say that linux stood up to being changed: it
 * was less than 2 hours work to get demand-loading completely implemented.
 *
 * Demand loading changed July 1993 by Eric Youngdale.   Use mmap instead,
 * current->executable is only used by the procfs.  This allows a dispatch
 * table to check for several different types  of binary formats.  We keep
 * trying until we recognize the file or we run out of supported binary
 * formats. 
 */

#include <linux/config.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/mman.h>
#include <linux/a.out.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/user.h>
#include <linux/smp_lock.h>
#include <linux/init.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/mmu_context.h>

#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif

/*
 * Here are the actual binaries that will be accepted:
 * add more with "register_binfmt()" if using modules...
 *
 * These are defined again for the 'real' modules if you are using a
 * module definition for these routines.
 */

static struct linux_binfmt *formats = (struct linux_binfmt *) NULL;

void __init binfmt_setup(void)
{
#ifdef CONFIG_BINFMT_MISC
	init_misc_binfmt();
#endif

#ifdef CONFIG_BINFMT_ELF
	init_elf_binfmt();
#endif

#ifdef CONFIG_BINFMT_ELF32
	init_elf32_binfmt();
#endif

#ifdef CONFIG_BINFMT_AOUT
	init_aout_binfmt();
#endif

#ifdef CONFIG_BINFMT_AOUT32
	init_aout32_binfmt();
#endif

#ifdef CONFIG_BINFMT_JAVA
	init_java_binfmt();
#endif

#ifdef CONFIG_BINFMT_EM86
	init_em86_binfmt();
#endif

	/* This cannot be configured out of the kernel */
	init_script_binfmt();
}

int register_binfmt(struct linux_binfmt * fmt)
{
	struct linux_binfmt ** tmp = &formats;

	if (!fmt)
		return -EINVAL;
	if (fmt->next)
		return -EBUSY;
	while (*tmp) {
		if (fmt == *tmp)
			return -EBUSY;
		tmp = &(*tmp)->next;
	}
	fmt->next = formats;
	formats = fmt;
	return 0;	
}

#ifdef CONFIG_MODULES
int unregister_binfmt(struct linux_binfmt * fmt)
{
	struct linux_binfmt ** tmp = &formats;

	while (*tmp) {
		if (fmt == *tmp) {
			*tmp = fmt->next;
			return 0;
		}
		tmp = &(*tmp)->next;
	}
	return -EINVAL;
}
#endif	/* CONFIG_MODULES */

/* N.B. Error returns must be < 0 */
int open_dentry(struct dentry * dentry, int mode)
{
	struct inode * inode = dentry->d_inode;
	struct file * f;
	int fd, error;

	error = -EINVAL;
	if (!inode->i_op || !inode->i_op->default_file_ops)
		goto out;
	fd = get_unused_fd();
	if (fd >= 0) {
		error = -ENFILE;
		f = get_empty_filp();
		if (!f)
			goto out_fd;
		f->f_flags = mode;
		f->f_mode = (mode+1) & O_ACCMODE;
		f->f_dentry = dentry;
		f->f_pos = 0;
		f->f_reada = 0;
		f->f_op = inode->i_op->default_file_ops;
		if (f->f_op->open) {
			error = f->f_op->open(inode,f);
			if (error)
				goto out_filp;
		}
		fd_install(fd, f);
		dget(dentry);
	}
	return fd;

out_filp:
	if (error > 0)
		error = -EIO;
	put_filp(f);
out_fd:
	put_unused_fd(fd);
out:
	return error;
}

/*
 * Note that a shared library must be both readable and executable due to
 * security reasons.
 *
 * Also note that we take the address to load from from the file itself.
 */
asmlinkage int sys_uselib(const char * library)
{
	int fd, retval;
	struct file * file;
	struct linux_binfmt * fmt;

	lock_kernel();
	fd = sys_open(library, 0, 0);
	retval = fd;
	if (fd < 0)
		goto out;
	file = fget(fd);
	retval = -ENOEXEC;
	if (file && file->f_dentry && file->f_op && file->f_op->read) {
		for (fmt = formats ; fmt ; fmt = fmt->next) {
			int (*fn)(int) = fmt->load_shlib;
			if (!fn)
				continue;
			/* N.B. Should use file instead of fd */
			retval = fn(fd);
			if (retval != -ENOEXEC)
				break;
		}
	}
	fput(file);
	sys_close(fd);
out:
	unlock_kernel();
  	return retval;
}

/*
 * count() counts the number of arguments/envelopes
 */
static int count(char ** argv)
{
	int i = 0;

	if (argv != NULL) {
		for (;;) {
			char * p;
			int error;

			error = get_user(p,argv);
			if (error)
				return error;
			if (!p)
				break;
			argv++;
			i++;
		}
	}
	return i;
}

/*
 * 'copy_string()' copies argument/envelope strings from user
 * memory to free pages in kernel mem. These are in a format ready
 * to be put directly into the top of new user memory.
 *
 * Modified by TYT, 11/24/91 to add the from_kmem argument, which specifies
 * whether the string and the string array are from user or kernel segments:
 * 
 * from_kmem     argv *        argv **
 *    0          user space    user space
 *    1          kernel space  user space
 *    2          kernel space  kernel space
 * 
 * We do this by playing games with the fs segment register.  Since it
 * is expensive to load a segment register, we try to avoid calling
 * set_fs() unless we absolutely have to.
 */
unsigned long copy_strings(int argc,char ** argv,unsigned long *page,
		unsigned long p, int from_kmem)
{
	char *str;
	mm_segment_t old_fs;

	if (!p)
		return 0;	/* bullet-proofing */
	old_fs = get_fs();
	if (from_kmem==2)
		set_fs(KERNEL_DS);
	while (argc-- > 0) {
		int len;
		unsigned long pos;

		if (from_kmem == 1)
			set_fs(KERNEL_DS);
		get_user(str, argv+argc);
		if (!str)
			panic("VFS: argc is wrong");
		if (from_kmem == 1)
			set_fs(old_fs);
		len = strlen_user(str);	/* includes the '\0' */
		if (p < len) {	/* this shouldn't happen - 128kB */
			set_fs(old_fs);
			return 0;
		}
		p -= len;
		pos = p;
		while (len) {
			char *pag;
			int offset, bytes_to_copy;

			offset = pos % PAGE_SIZE;
			if (!(pag = (char *) page[pos/PAGE_SIZE]) &&
			    !(pag = (char *) page[pos/PAGE_SIZE] =
			      (unsigned long *) get_free_page(GFP_USER))) {
				if (from_kmem==2)
					set_fs(old_fs);
				return 0;
			}
			bytes_to_copy = PAGE_SIZE - offset;
			if (bytes_to_copy > len)
				bytes_to_copy = len;
			copy_from_user(pag + offset, str, bytes_to_copy);
			pos += bytes_to_copy;
			str += bytes_to_copy;
			len -= bytes_to_copy;
		}
	}
	if (from_kmem==2)
		set_fs(old_fs);
	return p;
}

unsigned long setup_arg_pages(unsigned long p, struct linux_binprm * bprm)
{
	unsigned long stack_base;
	struct vm_area_struct *mpnt;
	int i;

	stack_base = STACK_TOP - MAX_ARG_PAGES*PAGE_SIZE;

	p += stack_base;
	if (bprm->loader)
		bprm->loader += stack_base;
	bprm->exec += stack_base;

	mpnt = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
	if (mpnt) {
		mpnt->vm_mm = current->mm;
		mpnt->vm_start = PAGE_MASK & (unsigned long) p;
		mpnt->vm_end = STACK_TOP;
		mpnt->vm_page_prot = PAGE_COPY;
		mpnt->vm_flags = VM_STACK_FLAGS;
		mpnt->vm_ops = NULL;
		mpnt->vm_offset = 0;
		mpnt->vm_file = NULL;
		mpnt->vm_pte = 0;
		insert_vm_struct(current->mm, mpnt);
		current->mm->total_vm = (mpnt->vm_end - mpnt->vm_start) >> PAGE_SHIFT;
	}

	for (i = 0 ; i < MAX_ARG_PAGES ; i++) {
		if (bprm->page[i]) {
			current->mm->rss++;
			put_dirty_page(current,bprm->page[i],stack_base);
		}
		stack_base += PAGE_SIZE;
	}
	return p;
}

/*
 * Read in the complete executable. This is used for "-N" files
 * that aren't on a block boundary, and for files on filesystems
 * without bmap support.
 */
int read_exec(struct dentry *dentry, unsigned long offset,
	char * addr, unsigned long count, int to_kmem)
{
	struct file file;
	struct inode * inode = dentry->d_inode;
	int result = -ENOEXEC;

	if (!inode->i_op || !inode->i_op->default_file_ops)
		goto end_readexec;
	if (init_private_file(&file, dentry, 1))
		goto end_readexec;
	if (!file.f_op->read)
		goto close_readexec;
	if (file.f_op->llseek) {
		if (file.f_op->llseek(&file,offset,0) != offset)
 			goto close_readexec;
	} else
		file.f_pos = offset;
	if (to_kmem) {
		mm_segment_t old_fs = get_fs();
		set_fs(get_ds());
		result = file.f_op->read(&file, addr, count, &file.f_pos);
		set_fs(old_fs);
	} else {
		result = verify_area(VERIFY_WRITE, addr, count);
		if (result)
			goto close_readexec;
		result = file.f_op->read(&file, addr, count, &file.f_pos);
	}
close_readexec:
	if (file.f_op->release)
		file.f_op->release(inode,&file);
end_readexec:
	return result;
}

static int exec_mmap(void)
{
	struct mm_struct * mm, * old_mm;
	int retval, nr;

	if (atomic_read(&current->mm->count) == 1) {
		flush_cache_mm(current->mm);
		mm_release();
		release_segments(current->mm);
		exit_mmap(current->mm);
		flush_tlb_mm(current->mm);
		return 0;
	}

	retval = -ENOMEM;
	mm = mm_alloc();
	if (!mm)
		goto fail_nomem;

	mm->cpu_vm_mask = (1UL << smp_processor_id());
	mm->total_vm = 0;
	mm->rss = 0;
	/*
	 * Make sure we have a private ldt if needed ...
	 */
	nr = current->tarray_ptr - &task[0]; 
	copy_segments(nr, current, mm);

	old_mm = current->mm;
	current->mm = mm;
	retval = new_page_tables(current);
	if (retval)
		goto fail_restore;
	activate_context(current);
	up(&mm->mmap_sem);
	mm_release();
	mmput(old_mm);
	return 0;

	/*
	 * Failure ... restore the prior mm_struct.
	 */
fail_restore:
	/* The pgd belongs to the parent ... don't free it! */
	mm->pgd = NULL;
	current->mm = old_mm;
	/* restore the ldt for this task */
	copy_segments(nr, current, NULL);
	mmput(mm);

fail_nomem:
	return retval;
}

/*
 * This function makes sure the current process has its own signal table,
 * so that flush_signal_handlers can later reset the handlers without
 * disturbing other processes.  (Other processes might share the signal
 * table via the CLONE_SIGHAND option to clone().)
 */
 
static inline int make_private_signals(void)
{
	struct signal_struct * newsig;

	if (atomic_read(&current->sig->count) <= 1)
		return 0;
	newsig = kmalloc(sizeof(*newsig), GFP_KERNEL);
	if (newsig == NULL)
		return -ENOMEM;
	spin_lock_init(&newsig->siglock);
	atomic_set(&newsig->count, 1);
	memcpy(newsig->action, current->sig->action, sizeof(newsig->action));
	current->sig = newsig;
	return 0;
}
	
/*
 * If make_private_signals() made a copy of the signal table, decrement the
 * refcount of the original table, and free it if necessary.
 * We don't do that in make_private_signals() so that we can back off
 * in flush_old_exec() if an error occurs after calling make_private_signals().
 */

static inline void release_old_signals(struct signal_struct * oldsig)
{
	if (current->sig == oldsig)
		return;
	if (atomic_dec_and_test(&oldsig->count))
		kfree(oldsig);
}

/*
 * These functions flushes out all traces of the currently running executable
 * so that a new one can be started
 */

static inline void flush_old_files(struct files_struct * files)
{
	unsigned long j;

	j = 0;
	for (;;) {
		unsigned long set, i;

		i = j * __NFDBITS;
		if (i >= files->max_fds)
			break;
		set = files->close_on_exec.fds_bits[j];
		files->close_on_exec.fds_bits[j] = 0;
		j++;
		for ( ; set ; i++,set >>= 1) {
			if (set & 1)
				sys_close(i);
		}
	}
}

int flush_old_exec(struct linux_binprm * bprm)
{
	char * name;
	int i, ch, retval;
	struct signal_struct * oldsig;

	/*
	 * Make sure we have a private signal table
	 */
	oldsig = current->sig;
	retval = make_private_signals();
	if (retval) goto flush_failed;

	/* 
	 * Release all of the old mmap stuff
	 */
	retval = exec_mmap();
	if (retval) goto mmap_failed;

	/* This is the point of no return */
	release_old_signals(oldsig);

	if (current->euid == current->uid && current->egid == current->gid)
		current->dumpable = 1;
	name = bprm->filename;
	for (i=0; (ch = *(name++)) != '\0';) {
		if (ch == '/')
			i = 0;
		else
			if (i < 15)
				current->comm[i++] = ch;
	}
	current->comm[i] = '\0';

	flush_thread();

	if (bprm->e_uid != current->euid || bprm->e_gid != current->egid || 
	    permission(bprm->dentry->d_inode,MAY_READ))
		current->dumpable = 0;

	flush_signal_handlers(current);
	flush_old_files(current->files);

	return 0;

mmap_failed:
	if (current->sig != oldsig)
		kfree(current->sig);
flush_failed:
	current->sig = oldsig;
	return retval;
}

/*
 * We mustn't allow tracing of suid binaries, unless
 * the tracer has the capability to trace anything..
 */
static inline int must_not_trace_exec(struct task_struct * p)
{
	return (p->flags & PF_PTRACED) && !cap_raised(p->p_pptr->cap_effective, CAP_SYS_PTRACE);
}

/* 
 * Fill the binprm structure from the inode. 
 * Check permissions, then read the first 512 bytes
 */
int prepare_binprm(struct linux_binprm *bprm)
{
	int mode;
	int retval,id_change,cap_raised;
	struct inode * inode = bprm->dentry->d_inode;

	mode = inode->i_mode;
	if (!S_ISREG(mode))			/* must be regular file */
		return -EACCES;
	if (!(mode & 0111))			/* with at least _one_ execute bit set */
		return -EACCES;
	if (IS_NOEXEC(inode))			/* FS mustn't be mounted noexec */
		return -EACCES;
	if (!inode->i_sb)
		return -EACCES;
	if ((retval = permission(inode, MAY_EXEC)) != 0)
		return retval;
	/* better not execute files which are being written to */
	if (inode->i_writecount > 0)
		return -ETXTBSY;

	bprm->e_uid = current->euid;
	bprm->e_gid = current->egid;
	id_change = cap_raised = 0;

	/* Set-uid? */
	if (mode & S_ISUID) {
		bprm->e_uid = inode->i_uid;
		if (bprm->e_uid != current->euid)
			id_change = 1;
	}

	/* Set-gid? */
	/*
	 * If setgid is set but no group execute bit then this
	 * is a candidate for mandatory locking, not a setgid
	 * executable.
	 */
	if ((mode & (S_ISGID | S_IXGRP)) == (S_ISGID | S_IXGRP)) {
		bprm->e_gid = inode->i_gid;
		if (!in_group_p(bprm->e_gid))
			id_change = 1;
	}

	/* We don't have VFS support for capabilities yet */
	cap_clear(bprm->cap_inheritable);
	cap_clear(bprm->cap_permitted);
	cap_clear(bprm->cap_effective);

	/*  To support inheritance of root-permissions and suid-root
         *  executables under compatibility mode, we raise the
         *  effective and inherited bitmasks of the executable file
         *  (translation: we set the executable "capability dumb" and
         *  set the allowed set to maximum). We don't set any forced
         *  bits.
         *
         *  If only the real uid is 0, we only raise the inheritable
         *  bitmask of the executable file (translation: we set the
         *  allowed set to maximum and the application to "capability
         *  smart"). 
         */

	if (!issecure(SECURE_NOROOT)) {
		if (bprm->e_uid == 0 || current->uid == 0)
			cap_set_full(bprm->cap_inheritable);
		if (bprm->e_uid == 0) 
			cap_set_full(bprm->cap_effective);
	}

        /* Only if pP' is _not_ a subset of pP, do we consider there
         * has been a capability related "change of capability".  In
         * such cases, we need to check that the elevation of
         * privilege does not go against other system constraints.
         * The new Permitted set is defined below -- see (***). */
	{
		kernel_cap_t working =
			cap_combine(bprm->cap_permitted,
				    cap_intersect(bprm->cap_inheritable,
						  current->cap_inheritable));
		if (!cap_issubset(working, current->cap_permitted)) {
			cap_raised = 1;
		}
	}

	if (id_change || cap_raised) {
		/* We can't suid-execute if we're sharing parts of the executable */
		/* or if we're being traced (or if suid execs are not allowed)    */
		/* (current->mm->count > 1 is ok, as we'll get a new mm anyway)   */
		if (IS_NOSUID(inode)
		    || must_not_trace_exec(current)
		    || (atomic_read(&current->fs->count) > 1)
		    || (atomic_read(&current->sig->count) > 1)
		    || (atomic_read(&current->files->count) > 1)) {
 			if (id_change && !capable(CAP_SETUID))
 				return -EPERM;
 			if (cap_raised && !capable(CAP_SETPCAP))
  				return -EPERM;
		}
	}

	memset(bprm->buf,0,sizeof(bprm->buf));
	return read_exec(bprm->dentry,0,bprm->buf,128,1);
}

/*
 * This function is used to produce the new IDs and capabilities
 * from the old ones and the file's capabilities.
 *
 * The formula used for evolving capabilities is:
 *
 *       pI' = pI
 * (***) pP' = fP | (fI & pI)
 *       pE' = pP' & fE          [NB. fE is 0 or ~0]
 *
 * I=Inheritable, P=Permitted, E=Effective // p=process, f=file
 * ' indicates post-exec().
 */

void compute_creds(struct linux_binprm *bprm) 
{
	int new_permitted = cap_t(bprm->cap_permitted) |
		(cap_t(bprm->cap_inheritable) & 
		 cap_t(current->cap_inheritable));

	/* For init, we want to retain the capabilities set
         * in the init_task struct. Thus we skip the usual
         * capability rules */
	if (current->pid != 1) {
		cap_t(current->cap_permitted) = new_permitted;
		cap_t(current->cap_effective) = new_permitted & 
						cap_t(bprm->cap_effective);
	}
	
        /* AUD: Audit candidate if current->cap_effective is set */

        current->suid = current->euid = current->fsuid = bprm->e_uid;
        current->sgid = current->egid = current->fsgid = bprm->e_gid;
        if (current->euid != current->uid || current->egid != current->gid ||
	    !cap_issubset(new_permitted, current->cap_permitted))
                current->dumpable = 0;
}


void remove_arg_zero(struct linux_binprm *bprm)
{
	if (bprm->argc) {
		unsigned long offset;
		char * page;
		offset = bprm->p % PAGE_SIZE;
		page = (char*)bprm->page[bprm->p/PAGE_SIZE];
		while(bprm->p++,*(page+offset++))
			if(offset==PAGE_SIZE){
				offset=0;
				page = (char*)bprm->page[bprm->p/PAGE_SIZE];
			}
		bprm->argc--;
	}
}

/*
 * cycle the list of binary formats handler, until one recognizes the image
 */
int search_binary_handler(struct linux_binprm *bprm,struct pt_regs *regs)
{
	int try,retval=0;
	struct linux_binfmt *fmt;
#ifdef __alpha__
	/* handle /sbin/loader.. */
	{
	    struct exec * eh = (struct exec *) bprm->buf;
	    struct linux_binprm bprm_loader;

	    if (!bprm->loader && eh->fh.f_magic == 0x183 &&
		(eh->fh.f_flags & 0x3000) == 0x3000)
	    {
		int i;
		char * dynloader[] = { "/sbin/loader" };
		struct dentry * dentry;

		dput(bprm->dentry);
		bprm->dentry = NULL;

	        bprm_loader.p = PAGE_SIZE*MAX_ARG_PAGES-sizeof(void *);
	        for (i=0 ; i<MAX_ARG_PAGES ; i++)       /* clear page-table */
                    bprm_loader.page[i] = 0;

		dentry = open_namei(dynloader[0], 0, 0);
		retval = PTR_ERR(dentry);
		if (IS_ERR(dentry))
			return retval;
		bprm->dentry = dentry;
		bprm->loader = bprm_loader.p;
		retval = prepare_binprm(bprm);
		if (retval<0)
			return retval;
		/* should call search_binary_handler recursively here,
		   but it does not matter */
	    }
	}
#endif
	for (try=0; try<2; try++) {
		for (fmt = formats ; fmt ; fmt = fmt->next) {
			int (*fn)(struct linux_binprm *, struct pt_regs *) = fmt->load_binary;
			if (!fn)
				continue;
			retval = fn(bprm, regs);
			if (retval >= 0) {
				if (bprm->dentry)
					dput(bprm->dentry);
				bprm->dentry = NULL;
				current->did_exec = 1;
				return retval;
			}
			if (retval != -ENOEXEC)
				break;
			if (!bprm->dentry) /* We don't have the dentry anymore */
				return retval;
		}
		if (retval != -ENOEXEC) {
			break;
#ifdef CONFIG_KMOD
		}else{
#define printable(c) (((c)=='\t') || ((c)=='\n') || (0x20<=(c) && (c)<=0x7e))
			char modname[20];
			if (printable(bprm->buf[0]) &&
			    printable(bprm->buf[1]) &&
			    printable(bprm->buf[2]) &&
			    printable(bprm->buf[3]))
				break; /* -ENOEXEC */
			sprintf(modname, "binfmt-%04x", *(unsigned short *)(&bprm->buf[2]));
			request_module(modname);
#endif
		}
	}
	return retval;
}


/*
 * sys_execve() executes a new program.
 */
int do_execve(char * filename, char ** argv, char ** envp, struct pt_regs * regs)
{
	struct linux_binprm bprm;
	struct dentry * dentry;
	int retval;
	int i;

	bprm.p = PAGE_SIZE*MAX_ARG_PAGES-sizeof(void *);
	for (i=0 ; i<MAX_ARG_PAGES ; i++)	/* clear page-table */
		bprm.page[i] = 0;

	dentry = open_namei(filename, 0, 0);
	retval = PTR_ERR(dentry);
	if (IS_ERR(dentry))
		return retval;

	bprm.dentry = dentry;
	bprm.filename = filename;
	bprm.sh_bang = 0;
	bprm.java = 0;
	bprm.loader = 0;
	bprm.exec = 0;
	if ((bprm.argc = count(argv)) < 0) {
		dput(dentry);
		return bprm.argc;
	}

	if ((bprm.envc = count(envp)) < 0) {
		dput(dentry);
		return bprm.envc;
	}

	retval = prepare_binprm(&bprm);
	
	if (retval >= 0) {
		bprm.p = copy_strings(1, &bprm.filename, bprm.page, bprm.p, 2);
		bprm.exec = bprm.p;
		bprm.p = copy_strings(bprm.envc,envp,bprm.page,bprm.p,0);
		bprm.p = copy_strings(bprm.argc,argv,bprm.page,bprm.p,0);
		if (!bprm.p)
			retval = -E2BIG;
	}

	if (retval >= 0)
		retval = search_binary_handler(&bprm,regs);
	if (retval >= 0)
		/* execve success */
		return retval;

	/* Something went wrong, return the inode and free the argument pages*/
	if (bprm.dentry)
		dput(bprm.dentry);

	for (i=0 ; i<MAX_ARG_PAGES ; i++)
		free_page(bprm.page[i]);

	return retval;
}
