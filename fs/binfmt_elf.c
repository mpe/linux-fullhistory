/*
 * linux/fs/binfmt_elf.c
 *
 * These are the functions used to load ELF format executables as used
 * on SVr4 machines.  Information on the format may be found in the book
 * "UNIX SYSTEM V RELEASE 4 Programmers Guide: Ansi C and Programming Support
 * Tools".
 *
 * Copyright 1993, 1994: Eric Youngdale (ericy@cais.com).
 */

#include <linux/module.h>

#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/a.out.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/binfmts.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/shm.h>
#include <linux/personality.h>
#include <linux/elfcore.h>
#include <linux/init.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>

#include <linux/config.h>

#define DLINFO_ITEMS 12

#include <linux/elf.h>

static int load_elf_binary(struct linux_binprm * bprm, struct pt_regs * regs);
static int load_elf_library(int fd);
extern int dump_fpu (struct pt_regs *, elf_fpregset_t *);
extern void dump_thread(struct pt_regs *, struct user *);

#ifdef __sparc__
extern unsigned long get_unmapped_area(unsigned long addr, unsigned long len);
#endif

#ifndef elf_addr_t
#define elf_addr_t unsigned long
#define elf_caddr_t char *
#endif

/*
 * If we don't support core dumping, then supply a NULL so we
 * don't even try.
 */
#ifdef USE_ELF_CORE_DUMP
static int elf_core_dump(long signr, struct pt_regs * regs);
#else
#define elf_core_dump	NULL
#endif

#define ELF_PAGESTART(_v) ((_v) & ~(unsigned long)(ELF_EXEC_PAGESIZE-1))
#define ELF_PAGEOFFSET(_v) ((_v) & (ELF_EXEC_PAGESIZE-1))
#define ELF_PAGEALIGN(_v) (((_v) + ELF_EXEC_PAGESIZE - 1) & ~(ELF_EXEC_PAGESIZE - 1))

static struct linux_binfmt elf_format = {
#ifndef MODULE
	NULL, NULL, load_elf_binary, load_elf_library, elf_core_dump
#else
	NULL, &__this_module, load_elf_binary, load_elf_library, elf_core_dump
#endif
};

static void set_brk(unsigned long start, unsigned long end)
{
	start = ELF_PAGEALIGN(start);
	end = ELF_PAGEALIGN(end);
	if (end <= start)
		return;
	do_mmap(NULL, start, end - start,
		PROT_READ | PROT_WRITE | PROT_EXEC,
		MAP_FIXED | MAP_PRIVATE, 0);
}


/* We need to explicitly zero any fractional pages
   after the data section (i.e. bss).  This would
   contain the junk from the file that should not
   be in memory */


static void padzero(unsigned long elf_bss)
{
	unsigned long nbyte;

	nbyte = ELF_PAGEOFFSET(elf_bss);
	if (nbyte) {
		nbyte = ELF_EXEC_PAGESIZE - nbyte;
		clear_user((void *) elf_bss, nbyte);
	}
}

static elf_addr_t * 
create_elf_tables(char *p, int argc, int envc,
		  struct elfhdr * exec,
		  unsigned long load_addr,
		  unsigned long interp_load_addr, int ibcs)
{
	elf_caddr_t *argv;
	elf_caddr_t *envp;
	elf_addr_t *sp, *csp;

	/*
	 * Force 16 byte _final_ alignment here for generality.
	 */
	sp = (elf_addr_t *) (~15UL & (unsigned long) p);
	csp = sp;
	csp -= exec ? DLINFO_ITEMS*2 : 2;
	csp -= envc+1;
	csp -= argc+1;
	csp -= (!ibcs ? 3 : 1);	/* argc itself */
	if ((unsigned long)csp & 15UL) {
		sp -= (16UL - ((unsigned long)csp & 15UL)) / sizeof(*sp);
	}

	/*
	 * Put the ELF interpreter info on the stack
	 */
#define NEW_AUX_ENT(nr, id, val) \
	  __put_user ((id), sp+(nr*2)); \
	  __put_user ((val), sp+(nr*2+1)); \

	sp -= 2;
	NEW_AUX_ENT(0, AT_NULL, 0);

	if (exec) {
		sp -= 11*2;

		NEW_AUX_ENT (0, AT_PHDR, load_addr + exec->e_phoff);
		NEW_AUX_ENT (1, AT_PHENT, sizeof (struct elf_phdr));
		NEW_AUX_ENT (2, AT_PHNUM, exec->e_phnum);
		NEW_AUX_ENT (3, AT_PAGESZ, ELF_EXEC_PAGESIZE);
		NEW_AUX_ENT (4, AT_BASE, interp_load_addr);
		NEW_AUX_ENT (5, AT_FLAGS, 0);
		NEW_AUX_ENT (6, AT_ENTRY, (elf_addr_t) exec->e_entry);
		NEW_AUX_ENT (7, AT_UID, (elf_addr_t) current->uid);
		NEW_AUX_ENT (8, AT_EUID, (elf_addr_t) current->euid);
		NEW_AUX_ENT (9, AT_GID, (elf_addr_t) current->gid);
		NEW_AUX_ENT (10, AT_EGID, (elf_addr_t) current->egid);
	}
#undef NEW_AUX_ENT

	sp -= envc+1;
	envp = (elf_caddr_t *) sp;
	sp -= argc+1;
	argv = (elf_caddr_t *) sp;
	if (!ibcs) {
		__put_user((elf_addr_t)(unsigned long) envp,--sp);
		__put_user((elf_addr_t)(unsigned long) argv,--sp);
	}

	__put_user((elf_addr_t)argc,--sp);
	current->mm->arg_start = (unsigned long) p;
	while (argc-->0) {
		__put_user((elf_caddr_t)(unsigned long)p,argv++);
		p += strlen_user(p);
	}
	__put_user(NULL, argv);
	current->mm->arg_end = current->mm->env_start = (unsigned long) p;
	while (envc-->0) {
		__put_user((elf_caddr_t)(unsigned long)p,envp++);
		p += strlen_user(p);
	}
	__put_user(NULL, envp);
	current->mm->env_end = (unsigned long) p;
	return sp;
}


/* This is much more generalized than the library routine read function,
   so we keep this separate.  Technically the library read function
   is only provided so that we can read a.out libraries that have
   an ELF header */

static unsigned long load_elf_interp(struct elfhdr * interp_elf_ex,
				     struct inode * interpreter_inode,
				     unsigned long *interp_load_addr)
{
	struct file * file;
	struct elf_phdr *elf_phdata  =  NULL;
	struct elf_phdr *eppnt;
	unsigned long load_addr;
	int load_addr_set = 0;
	int elf_exec_fileno;
	int retval;
	unsigned long last_bss, elf_bss;
	unsigned long error;
	int i;

	elf_bss = 0;
	last_bss = 0;
	error = load_addr = 0;

	/* First of all, some simple consistency checks */
	if ((interp_elf_ex->e_type != ET_EXEC &&
	    interp_elf_ex->e_type != ET_DYN) ||
	   !elf_check_arch(interp_elf_ex->e_machine) ||
	   (!interpreter_inode->i_op ||
	    !interpreter_inode->i_op->default_file_ops->mmap)){
		return ~0UL;
	}

	/* Now read in all of the header information */

	if (sizeof(struct elf_phdr) * interp_elf_ex->e_phnum > ELF_EXEC_PAGESIZE) {
	    return ~0UL;
	}

	elf_phdata =  (struct elf_phdr *)
		kmalloc(sizeof(struct elf_phdr) * interp_elf_ex->e_phnum,
			GFP_KERNEL);
	if (!elf_phdata) {
	  return ~0UL;
	}

	/*
	 * If the size of this structure has changed, then punt, since
	 * we will be doing the wrong thing.
	 */
	if (interp_elf_ex->e_phentsize != sizeof(struct elf_phdr))
	  {
	    kfree(elf_phdata);
	    return ~0UL;
	  }

	retval = read_exec(interpreter_inode, interp_elf_ex->e_phoff,
			   (char *) elf_phdata,
			   sizeof(struct elf_phdr) * interp_elf_ex->e_phnum, 1);

	if (retval < 0) {
		kfree (elf_phdata);
		return retval;
 	}

	elf_exec_fileno = open_inode(interpreter_inode, O_RDONLY);
	if (elf_exec_fileno < 0) {
	  kfree(elf_phdata);
	  return ~0UL;
	}

	file = current->files->fd[elf_exec_fileno];

	eppnt = elf_phdata;
	for(i=0; i<interp_elf_ex->e_phnum; i++, eppnt++)
	  if (eppnt->p_type == PT_LOAD) {
	    int elf_type = MAP_PRIVATE | MAP_DENYWRITE;
	    int elf_prot = 0;
	    unsigned long vaddr = 0;
	    unsigned long k;

	    if (eppnt->p_flags & PF_R) elf_prot =  PROT_READ;
	    if (eppnt->p_flags & PF_W) elf_prot |= PROT_WRITE;
	    if (eppnt->p_flags & PF_X) elf_prot |= PROT_EXEC;
	    if (interp_elf_ex->e_type == ET_EXEC || load_addr_set) {
	    	elf_type |= MAP_FIXED;
	    	vaddr = eppnt->p_vaddr;
#ifdef __sparc__
	    } else {
		load_addr = get_unmapped_area(0, eppnt->p_filesz +
					ELF_PAGEOFFSET(eppnt->p_vaddr));
#endif
	    }

	    error = do_mmap(file,
			    load_addr + ELF_PAGESTART(vaddr),
			    eppnt->p_filesz + ELF_PAGEOFFSET(eppnt->p_vaddr),
			    elf_prot,
			    elf_type,
			    eppnt->p_offset - ELF_PAGEOFFSET(eppnt->p_vaddr));

	    if (error > -1024UL) {
	      /* Real error */
	      sys_close(elf_exec_fileno);
	      kfree(elf_phdata);
	      return ~0UL;
	    }

	    if (!load_addr_set && interp_elf_ex->e_type == ET_DYN) {
	      load_addr = error;
	      load_addr_set = 1;
	    }

	    /*
	     * Find the end of the file  mapping for this phdr, and keep
	     * track of the largest address we see for this.
	     */
	    k = load_addr + eppnt->p_vaddr + eppnt->p_filesz;
	    if (k > elf_bss) elf_bss = k;

	    /*
	     * Do the same thing for the memory mapping - between
	     * elf_bss and last_bss is the bss section.
	     */
	    k = load_addr + eppnt->p_memsz + eppnt->p_vaddr;
	    if (k > last_bss) last_bss = k;
	  }

	/* Now use mmap to map the library into memory. */

	sys_close(elf_exec_fileno);

	/*
	 * Now fill out the bss section.  First pad the last page up
	 * to the page boundary, and then perform a mmap to make sure
	 * that there are zeromapped pages up to and including the last
	 * bss page.
	 */
	padzero(elf_bss);
	elf_bss = ELF_PAGESTART(elf_bss + ELF_EXEC_PAGESIZE - 1); /* What we have mapped so far */

	/* Map the last of the bss segment */
	if (last_bss > elf_bss)
	  do_mmap(NULL, elf_bss, last_bss-elf_bss,
		  PROT_READ|PROT_WRITE|PROT_EXEC,
		  MAP_FIXED|MAP_PRIVATE, 0);
	kfree(elf_phdata);

	*interp_load_addr = load_addr;
	return ((unsigned long) interp_elf_ex->e_entry) + load_addr;
}

static unsigned long load_aout_interp(struct exec * interp_ex,
			     struct inode * interpreter_inode)
{
  int retval;
  unsigned long elf_entry;

  current->mm->brk = interp_ex->a_bss +
    (current->mm->end_data = interp_ex->a_data +
     (current->mm->end_code = interp_ex->a_text));
  elf_entry = interp_ex->a_entry;


  if (N_MAGIC(*interp_ex) == OMAGIC) {
    do_mmap(NULL, 0, interp_ex->a_text+interp_ex->a_data,
	    PROT_READ|PROT_WRITE|PROT_EXEC,
	    MAP_FIXED|MAP_PRIVATE, 0);
    retval = read_exec(interpreter_inode, 32, (char *) 0,
		       interp_ex->a_text+interp_ex->a_data, 0);
  } else if (N_MAGIC(*interp_ex) == ZMAGIC || N_MAGIC(*interp_ex) == QMAGIC) {
    do_mmap(NULL, 0, interp_ex->a_text+interp_ex->a_data,
	    PROT_READ|PROT_WRITE|PROT_EXEC,
	    MAP_FIXED|MAP_PRIVATE, 0);
    retval = read_exec(interpreter_inode,
		       N_TXTOFF(*interp_ex) ,
		       (char *) N_TXTADDR(*interp_ex),
		       interp_ex->a_text+interp_ex->a_data, 0);
  } else
    retval = -1;

  if (retval >= 0)
    do_mmap(NULL, ELF_PAGESTART(interp_ex->a_text + interp_ex->a_data + ELF_EXEC_PAGESIZE - 1),
	    interp_ex->a_bss,
	    PROT_READ|PROT_WRITE|PROT_EXEC,
	    MAP_FIXED|MAP_PRIVATE, 0);
  if (retval < 0) {
  	return ~0UL;
  }
  return elf_entry;
}

/*
 * These are the functions used to load ELF style executables and shared
 * libraries.  There is no binary dependent code anywhere else.
 */

#define INTERPRETER_NONE 0
#define INTERPRETER_AOUT 1
#define INTERPRETER_ELF 2


static inline int
do_load_elf_binary(struct linux_binprm * bprm, struct pt_regs * regs)
{
	struct elfhdr elf_ex;
	struct elfhdr interp_elf_ex;
	struct file * file;
  	struct exec interp_ex;
	struct inode *interpreter_inode;
	unsigned long load_addr;
	int load_addr_set = 0;
	unsigned int interpreter_type = INTERPRETER_NONE;
	unsigned char ibcs2_interpreter;
	int i;
	int old_fs;
	int error;
	struct elf_phdr * elf_ppnt, *elf_phdata;
	int elf_exec_fileno;
	unsigned long elf_bss, k, elf_brk;
	int retval;
	char * elf_interpreter;
	unsigned long elf_entry, interp_load_addr = 0;
	int status;
	unsigned long start_code, end_code, end_data;
	unsigned long elf_stack;
	char passed_fileno[6];

	ibcs2_interpreter = 0;
	status = 0;
	load_addr = 0;
	elf_ex = *((struct elfhdr *) bprm->buf);	  /* exec-header */

	if (elf_ex.e_ident[0] != 0x7f ||
	    strncmp(&elf_ex.e_ident[1], "ELF",3) != 0) {
		return  -ENOEXEC;
	}


	/* First of all, some simple consistency checks */
	if ((elf_ex.e_type != ET_EXEC &&
	    elf_ex.e_type != ET_DYN) ||
	   (! elf_check_arch(elf_ex.e_machine)) ||
	   (!bprm->inode->i_op || !bprm->inode->i_op->default_file_ops ||
	    !bprm->inode->i_op->default_file_ops->mmap)){
		return -ENOEXEC;
	}

	/* Now read in all of the header information */

	elf_phdata = (struct elf_phdr *) kmalloc(elf_ex.e_phentsize *
						 elf_ex.e_phnum, GFP_KERNEL);
	if (elf_phdata == NULL) {
		return -ENOMEM;
	}

	retval = read_exec(bprm->inode, elf_ex.e_phoff, (char *) elf_phdata,
			   elf_ex.e_phentsize * elf_ex.e_phnum, 1);
	if (retval < 0) {
		kfree (elf_phdata);
		return retval;
	}

	elf_ppnt = elf_phdata;

	elf_bss = 0;
	elf_brk = 0;

	elf_exec_fileno = open_inode(bprm->inode, O_RDONLY);

	if (elf_exec_fileno < 0) {
		kfree (elf_phdata);
		return elf_exec_fileno;
	}

	file = current->files->fd[elf_exec_fileno];

	elf_stack = ~0UL;
	elf_interpreter = NULL;
	start_code = ~0UL;
	end_code = 0;
	end_data = 0;

	for(i=0;i < elf_ex.e_phnum; i++){
		if (elf_ppnt->p_type == PT_INTERP) {
		  	if ( elf_interpreter != NULL )
			{
				kfree (elf_phdata);
				kfree(elf_interpreter);
				sys_close(elf_exec_fileno);
				return -EINVAL;
			}

			/* This is the program interpreter used for
			 * shared libraries - for now assume that this
			 * is an a.out format binary
			 */

			elf_interpreter = (char *) kmalloc(elf_ppnt->p_filesz,
							   GFP_KERNEL);
			if (elf_interpreter == NULL) {
				kfree (elf_phdata);
				sys_close(elf_exec_fileno);
				return -ENOMEM;
			}

			retval = read_exec(bprm->inode,elf_ppnt->p_offset,
					   elf_interpreter,
					   elf_ppnt->p_filesz, 1);
			/* If the program interpreter is one of these two,
			   then assume an iBCS2 image. Otherwise assume
			   a native linux image. */
			if (strcmp(elf_interpreter,"/usr/lib/libc.so.1") == 0 ||
			    strcmp(elf_interpreter,"/usr/lib/ld.so.1") == 0)
			  ibcs2_interpreter = 1;
#if 0
			printk("Using ELF interpreter %s\n", elf_interpreter);
#endif
			if (retval >= 0) {
				old_fs = get_fs(); /* This could probably be optimized */
				set_fs(get_ds());
				retval = open_namei(elf_interpreter, 0, 0,
						    &interpreter_inode, NULL);
				set_fs(old_fs);
			}

			if (retval >= 0)
				retval = read_exec(interpreter_inode,0,bprm->buf,128, 1);

			if (retval >= 0) {
				interp_ex = *((struct exec *) bprm->buf);		/* exec-header */
				interp_elf_ex = *((struct elfhdr *) bprm->buf);	  /* exec-header */

			}
			if (retval < 0) {
				kfree (elf_phdata);
				kfree(elf_interpreter);
				sys_close(elf_exec_fileno);
				return retval;
			}
		}
		elf_ppnt++;
	}

	/* Some simple consistency checks for the interpreter */
	if (elf_interpreter){
		interpreter_type = INTERPRETER_ELF | INTERPRETER_AOUT;

		/* Now figure out which format our binary is */
		if ((N_MAGIC(interp_ex) != OMAGIC) &&
		    (N_MAGIC(interp_ex) != ZMAGIC) &&
		    (N_MAGIC(interp_ex) != QMAGIC))
		  interpreter_type = INTERPRETER_ELF;

		if (interp_elf_ex.e_ident[0] != 0x7f ||
		    strncmp(&interp_elf_ex.e_ident[1], "ELF",3) != 0)
		  interpreter_type &= ~INTERPRETER_ELF;

		if (!interpreter_type)
		  {
		    kfree(elf_interpreter);
		    kfree(elf_phdata);
		    sys_close(elf_exec_fileno);
		    return -ELIBBAD;
		  }
	}

	/* OK, we are done with that, now set up the arg stuff,
	   and then start this sucker up */

	if (!bprm->sh_bang) {
		char * passed_p;

		if (interpreter_type == INTERPRETER_AOUT) {
		  sprintf(passed_fileno, "%d", elf_exec_fileno);
		  passed_p = passed_fileno;

		  if (elf_interpreter) {
		    bprm->p = copy_strings(1,&passed_p,bprm->page,bprm->p,2);
		    bprm->argc++;
		  }
		}
		if (!bprm->p) {
			if (elf_interpreter) {
			      kfree(elf_interpreter);
			}
			kfree (elf_phdata);
			sys_close(elf_exec_fileno);
			return -E2BIG;
		}
	}

	/* OK, This is the point of no return */
	flush_old_exec(bprm);

	current->mm->end_data = 0;
	current->mm->end_code = 0;
	current->mm->start_mmap = ELF_START_MMAP;
	current->mm->mmap = NULL;
	elf_entry = (unsigned long) elf_ex.e_entry;

	/* Do this so that we can load the interpreter, if need be.  We will
	   change some of these later */
	current->mm->rss = 0;
#ifdef ELF_FLAGS_INIT
	ELF_FLAGS_INIT;
#endif
	bprm->p = setup_arg_pages(bprm->p, bprm);
	current->mm->start_stack = bprm->p;

	/* Now we do a little grungy work by mmaping the ELF image into
	   the correct location in memory.  At this point, we assume that
	   the image should be loaded at fixed address, not at a variable
	   address. */

	old_fs = get_fs();
	set_fs(get_ds());
	for(i = 0, elf_ppnt = elf_phdata; i < elf_ex.e_phnum; i++, elf_ppnt++) {
		if (elf_ppnt->p_type == PT_LOAD) {
			int elf_prot = 0;
			if (elf_ppnt->p_flags & PF_R) elf_prot |= PROT_READ;
			if (elf_ppnt->p_flags & PF_W) elf_prot |= PROT_WRITE;
			if (elf_ppnt->p_flags & PF_X) elf_prot |= PROT_EXEC;

			error = do_mmap(file,
					ELF_PAGESTART(elf_ppnt->p_vaddr),
					(elf_ppnt->p_filesz +
					 ELF_PAGEOFFSET(elf_ppnt->p_vaddr)),
					elf_prot,
					(MAP_FIXED | MAP_PRIVATE |
					 MAP_DENYWRITE | MAP_EXECUTABLE),
					(elf_ppnt->p_offset -
					 ELF_PAGEOFFSET(elf_ppnt->p_vaddr)));

#ifdef LOW_ELF_STACK
			if (ELF_PAGESTART(elf_ppnt->p_vaddr) < elf_stack)
				elf_stack = ELF_PAGESTART(elf_ppnt->p_vaddr);
#endif

			if (!load_addr_set) {
			  load_addr = elf_ppnt->p_vaddr - elf_ppnt->p_offset;
			  load_addr_set = 1;
			}
			k = elf_ppnt->p_vaddr;
			if (k < start_code) start_code = k;
			k = elf_ppnt->p_vaddr + elf_ppnt->p_filesz;
			if (k > elf_bss) elf_bss = k;
#if 1
			if ((elf_ppnt->p_flags & PF_X) && end_code <  k)
#else
			if ( !(elf_ppnt->p_flags & PF_W) && end_code <  k)
#endif
				end_code = k;
			if (end_data < k) end_data = k;
			k = elf_ppnt->p_vaddr + elf_ppnt->p_memsz;
			if (k > elf_brk) elf_brk = k;
		}
	}
	set_fs(old_fs);

	if (elf_interpreter) {
		if (interpreter_type & 1)
			elf_entry = load_aout_interp(&interp_ex,
						     interpreter_inode);
		else if (interpreter_type & 2)
			elf_entry = load_elf_interp(&interp_elf_ex,
						    interpreter_inode,
						    &interp_load_addr);

		iput(interpreter_inode);
		kfree(elf_interpreter);

		if (elf_entry == ~0UL) {
			printk("Unable to load interpreter\n");
			kfree(elf_phdata);
			send_sig(SIGSEGV, current, 0);
			return 0;
		}
	}

	kfree(elf_phdata);

	if (interpreter_type != INTERPRETER_AOUT) sys_close(elf_exec_fileno);
	current->personality = (ibcs2_interpreter ? PER_SVR4 : PER_LINUX);

	if (current->exec_domain && current->exec_domain->module)
		__MOD_DEC_USE_COUNT(current->exec_domain->module);
	if (current->binfmt && current->binfmt->module)
		__MOD_DEC_USE_COUNT(current->binfmt->module);
	current->exec_domain = lookup_exec_domain(current->personality);
	current->binfmt = &elf_format;
	if (current->exec_domain && current->exec_domain->module)
		__MOD_INC_USE_COUNT(current->exec_domain->module);
	if (current->binfmt && current->binfmt->module)
		__MOD_INC_USE_COUNT(current->binfmt->module);

#ifndef VM_STACK_FLAGS
	current->executable = bprm->inode;
	atomic_inc(&bprm->inode->i_count);
#endif
#ifdef LOW_ELF_STACK
	current->start_stack = bprm->p = elf_stack - 4;
#endif
	current->suid = current->euid = current->fsuid = bprm->e_uid;
	current->sgid = current->egid = current->fsgid = bprm->e_gid;
	current->flags &= ~PF_FORKNOEXEC;
	bprm->p = (unsigned long)
	  create_elf_tables((char *)bprm->p,
			bprm->argc,
			bprm->envc,
			(interpreter_type == INTERPRETER_ELF ? &elf_ex : NULL),
			load_addr,
			interp_load_addr,
			(interpreter_type == INTERPRETER_AOUT ? 0 : 1));
	if (interpreter_type == INTERPRETER_AOUT)
	  current->mm->arg_start += strlen(passed_fileno) + 1;
	current->mm->start_brk = current->mm->brk = elf_brk;
	current->mm->end_code = end_code;
	current->mm->start_code = start_code;
	current->mm->end_data = end_data;
	current->mm->start_stack = bprm->p;

	/* Calling set_brk effectively mmaps the pages that we need for the bss and break
	   sections */
	set_brk(elf_bss, elf_brk);

	padzero(elf_bss);

#if 0
	printk("(start_brk) %x\n" , current->mm->start_brk);
	printk("(end_code) %x\n" , current->mm->end_code);
	printk("(start_code) %x\n" , current->mm->start_code);
	printk("(end_data) %x\n" , current->mm->end_data);
	printk("(start_stack) %x\n" , current->mm->start_stack);
	printk("(brk) %x\n" , current->mm->brk);
#endif

	if ( current->personality == PER_SVR4 )
	{
		/* Why this, you ask???  Well SVr4 maps page 0 as read-only,
		   and some applications "depend" upon this behavior.
		   Since we do not have the power to recompile these, we
		   emulate the SVr4 behavior.  Sigh.  */
		error = do_mmap(NULL, 0, 4096, PROT_READ | PROT_EXEC,
				MAP_FIXED | MAP_PRIVATE, 0);
	}

#ifdef ELF_PLAT_INIT
	/*
	 * The ABI may specify that certain registers be set up in special
	 * ways (on i386 %edx is the address of a DT_FINI function, for
	 * example.  This macro performs whatever initialization to
	 * the regs structure is required.
	 */
	ELF_PLAT_INIT(regs);
#endif


	start_thread(regs, elf_entry, bprm->p);
	if (current->flags & PF_PTRACED)
		send_sig(SIGTRAP, current, 0);
	return 0;
}

static int
load_elf_binary(struct linux_binprm * bprm, struct pt_regs * regs)
{
	int retval;

	MOD_INC_USE_COUNT;
	retval = do_load_elf_binary(bprm, regs);
	MOD_DEC_USE_COUNT;
	return retval;
}

/* This is really simpleminded and specialized - we are loading an
   a.out library that is given an ELF header. */

static inline int
do_load_elf_library(int fd){
	struct file * file;
	struct elfhdr elf_ex;
	struct elf_phdr *elf_phdata  =  NULL;
	struct  inode * inode;
	unsigned long len;
	int elf_bss;
	int retval;
	unsigned long bss;
	int error;
	int i,j, k;

	len = 0;
	file = current->files->fd[fd];
	inode = file->f_inode;
	elf_bss = 0;

	if (!file || !file->f_op)
		return -EACCES;

	/* seek to the beginning of the file */
	if (file->f_op->llseek) {
		if ((error = file->f_op->llseek(inode, file, 0, 0)) != 0)
			return -ENOEXEC;
	} else
		file->f_pos = 0;

	set_fs(KERNEL_DS);
	error = file->f_op->read(inode, file, (char *) &elf_ex, sizeof(elf_ex));
	set_fs(USER_DS);
	if (error != sizeof(elf_ex))
		return -ENOEXEC;

	if (elf_ex.e_ident[0] != 0x7f ||
	    strncmp(&elf_ex.e_ident[1], "ELF",3) != 0)
		return -ENOEXEC;

	/* First of all, some simple consistency checks */
	if (elf_ex.e_type != ET_EXEC || elf_ex.e_phnum > 2 ||
	   !elf_check_arch(elf_ex.e_machine) ||
	   (!inode->i_op || !inode->i_op->default_file_ops->mmap))
		return -ENOEXEC;

	/* Now read in all of the header information */

	if (sizeof(struct elf_phdr) * elf_ex.e_phnum > ELF_EXEC_PAGESIZE)
		return -ENOEXEC;

	elf_phdata =  (struct elf_phdr *)
		kmalloc(sizeof(struct elf_phdr) * elf_ex.e_phnum, GFP_KERNEL);
	if (elf_phdata == NULL)
		return -ENOMEM;

	retval = read_exec(inode, elf_ex.e_phoff, (char *) elf_phdata,
			   sizeof(struct elf_phdr) * elf_ex.e_phnum, 1);

	j = 0;
	for(i=0; i<elf_ex.e_phnum; i++)
		if ((elf_phdata + i)->p_type == PT_LOAD) j++;

	if (j != 1)  {
		kfree(elf_phdata);
		return -ENOEXEC;
	}

	while(elf_phdata->p_type != PT_LOAD) elf_phdata++;

	/* Now use mmap to map the library into memory. */
	error = do_mmap(file,
			ELF_PAGESTART(elf_phdata->p_vaddr),
			(elf_phdata->p_filesz +
			 ELF_PAGEOFFSET(elf_phdata->p_vaddr)),
			PROT_READ | PROT_WRITE | PROT_EXEC,
			MAP_FIXED | MAP_PRIVATE | MAP_DENYWRITE,
			(elf_phdata->p_offset -
			 ELF_PAGEOFFSET(elf_phdata->p_vaddr)));

	k = elf_phdata->p_vaddr + elf_phdata->p_filesz;
	if (k > elf_bss) elf_bss = k;

	if (error != ELF_PAGESTART(elf_phdata->p_vaddr)) {
		kfree(elf_phdata);
		return error;
	}

	padzero(elf_bss);

	len = ELF_PAGESTART(elf_phdata->p_filesz + elf_phdata->p_vaddr+ ELF_EXEC_PAGESIZE - 1);
	bss = elf_phdata->p_memsz + elf_phdata->p_vaddr;
	if (bss > len)
	  do_mmap(NULL, len, bss-len,
		  PROT_READ|PROT_WRITE|PROT_EXEC,
		  MAP_FIXED|MAP_PRIVATE, 0);
	kfree(elf_phdata);
	return 0;
}

static int load_elf_library(int fd)
{
	int retval;

	MOD_INC_USE_COUNT;
	retval = do_load_elf_library(fd);
	MOD_DEC_USE_COUNT;
	return retval;
}

/*
 * Note that some platforms still use traditional core dumps and not
 * the ELF core dump.  Each platform can select it as appropriate.
 */
#ifdef USE_ELF_CORE_DUMP

/*
 * ELF core dumper
 *
 * Modelled on fs/exec.c:aout_core_dump()
 * Jeremy Fitzhardinge <jeremy@sw.oz.au>
 */
/*
 * These are the only things you should do on a core-file: use only these
 * functions to write out all the necessary info.
 */
static int dump_write(struct file *file, const void *addr, int nr)
{
	return file->f_op->write(file->f_inode, file, addr, nr) == nr;
}

static int dump_seek(struct file *file, off_t off)
{
	if (file->f_op->llseek) {
		if (file->f_op->llseek(file->f_inode, file, off, 0) != off)
			return 0;
	} else
		file->f_pos = off;
	return 1;
}

/*
 * Decide whether a segment is worth dumping; default is yes to be
 * sure (missing info is worse than too much; etc).
 * Personally I'd include everything, and use the coredump limit...
 *
 * I think we should skip something. But I am not sure how. H.J.
 */
static inline int maydump(struct vm_area_struct *vma)
{
	if (!(vma->vm_flags & (VM_READ|VM_WRITE|VM_EXEC)))
		return 0;

	/* Do not dump I/O mapped devices! -DaveM */
	if(vma->vm_flags & VM_IO)
		return 0;
#if 1
	if (vma->vm_flags & (VM_WRITE|VM_GROWSUP|VM_GROWSDOWN))
		return 1;
	if (vma->vm_flags & (VM_READ|VM_EXEC|VM_EXECUTABLE|VM_SHARED))
		return 0;
#endif
	return 1;
}

#define roundup(x, y)  ((((x)+((y)-1))/(y))*(y))

/* An ELF note in memory */
struct memelfnote
{
	const char *name;
	int type;
	unsigned int datasz;
	void *data;
};

static int notesize(struct memelfnote *en)
{
	int sz;

	sz = sizeof(struct elf_note);
	sz += roundup(strlen(en->name), 4);
	sz += roundup(en->datasz, 4);

	return sz;
}

/* #define DEBUG */

#ifdef DEBUG
static void dump_regs(const char *str, elf_greg_t *r)
{
	int i;
	static const char *regs[] = { "ebx", "ecx", "edx", "esi", "edi", "ebp",
					      "eax", "ds", "es", "fs", "gs",
					      "orig_eax", "eip", "cs",
					      "efl", "uesp", "ss"};
	printk("Registers: %s\n", str);

	for(i = 0; i < ELF_NGREG; i++)
	{
		unsigned long val = r[i];
		printk("   %-2d %-5s=%08lx %lu\n", i, regs[i], val, val);
	}
}
#endif

#define DUMP_WRITE(addr, nr)	\
	do { if (!dump_write(file, (addr), (nr))) return 0; } while(0)
#define DUMP_SEEK(off)	\
	do { if (!dump_seek(file, (off))) return 0; } while(0)

static int writenote(struct memelfnote *men, struct file *file)
{
	struct elf_note en;

	en.n_namesz = strlen(men->name);
	en.n_descsz = men->datasz;
	en.n_type = men->type;

	DUMP_WRITE(&en, sizeof(en));
	DUMP_WRITE(men->name, en.n_namesz);
	/* XXX - cast from long long to long to avoid need for libgcc.a */
	DUMP_SEEK(roundup((unsigned long)file->f_pos, 4));	/* XXX */
	DUMP_WRITE(men->data, men->datasz);
	DUMP_SEEK(roundup((unsigned long)file->f_pos, 4));	/* XXX */

	return 1;
}
#undef DUMP_WRITE
#undef DUMP_SEEK

#define DUMP_WRITE(addr, nr)	\
	if (!dump_write(&file, (addr), (nr))) \
		goto close_coredump;
#define DUMP_SEEK(off)	\
	if (!dump_seek(&file, (off))) \
		goto close_coredump;
/*
 * Actual dumper
 *
 * This is a two-pass process; first we find the offsets of the bits,
 * and then they are actually written out.  If we run out of core limit
 * we just truncate.
 */
static int elf_core_dump(long signr, struct pt_regs * regs)
{
	int has_dumped = 0;
	struct file file;
	struct inode *inode;
	unsigned short fs;
	char corefile[6+sizeof(current->comm)];
	int segs;
	int i;
	size_t size;
	struct vm_area_struct *vma;
	struct elfhdr elf;
	off_t offset = 0, dataoff;
	int limit = current->rlim[RLIMIT_CORE].rlim_cur;
	int numnote = 4;
	struct memelfnote notes[4];
	struct elf_prstatus prstatus;	/* NT_PRSTATUS */
	elf_fpregset_t fpu;		/* NT_PRFPREG */
	struct elf_prpsinfo psinfo;	/* NT_PRPSINFO */

	if (!current->dumpable || limit < ELF_EXEC_PAGESIZE || current->mm->count != 1)
		return 0;
	current->dumpable = 0;

#ifndef CONFIG_BINFMT_ELF
	MOD_INC_USE_COUNT;
#endif

	/* Count what's needed to dump, up to the limit of coredump size */
	segs = 0;
	size = 0;
	for(vma = current->mm->mmap; vma != NULL; vma = vma->vm_next) {
		if (maydump(vma))
		{
			int sz = vma->vm_end-vma->vm_start;

			if (size+sz >= limit)
				break;
			else
				size += sz;
		}

		segs++;
	}
#ifdef DEBUG
	printk("elf_core_dump: %d segs taking %d bytes\n", segs, size);
#endif

	/* Set up header */
	memcpy(elf.e_ident, ELFMAG, SELFMAG);
	elf.e_ident[EI_CLASS] = ELF_CLASS;
	elf.e_ident[EI_DATA] = ELF_DATA;
	elf.e_ident[EI_VERSION] = EV_CURRENT;
	memset(elf.e_ident+EI_PAD, 0, EI_NIDENT-EI_PAD);

	elf.e_type = ET_CORE;
	elf.e_machine = ELF_ARCH;
	elf.e_version = EV_CURRENT;
	elf.e_entry = 0;
	elf.e_phoff = sizeof(elf);
	elf.e_shoff = 0;
	elf.e_flags = 0;
	elf.e_ehsize = sizeof(elf);
	elf.e_phentsize = sizeof(struct elf_phdr);
	elf.e_phnum = segs+1;		/* Include notes */
	elf.e_shentsize = 0;
	elf.e_shnum = 0;
	elf.e_shstrndx = 0;

	fs = get_fs();
	set_fs(KERNEL_DS);
	memcpy(corefile,"core.",5);
#if 0
	memcpy(corefile+5,current->comm,sizeof(current->comm));
#else
	corefile[4] = '\0';
#endif
	if (open_namei(corefile,O_CREAT | 2 | O_TRUNC,0600,&inode,NULL)) {
		inode = NULL;
		goto end_coredump;
	}
	if (!S_ISREG(inode->i_mode))
		goto end_coredump;
	if (!inode->i_op || !inode->i_op->default_file_ops)
		goto end_coredump;
	file.f_mode = 3;
	file.f_flags = 0;
	file.f_count = 1;
	file.f_inode = inode;
	file.f_pos = 0;
	file.f_reada = 0;
	file.f_op = inode->i_op->default_file_ops;
	if (file.f_op->open)
		if (file.f_op->open(inode,&file))
			goto end_coredump;
	if (!file.f_op->write)
		goto close_coredump;
	has_dumped = 1;
	current->flags |= PF_DUMPCORE;

	DUMP_WRITE(&elf, sizeof(elf));
	offset += sizeof(elf);				/* Elf header */
	offset += (segs+1) * sizeof(struct elf_phdr);	/* Program headers */

	/*
	 * Set up the notes in similar form to SVR4 core dumps made
	 * with info from their /proc.
	 */
	memset(&psinfo, 0, sizeof(psinfo));
	memset(&prstatus, 0, sizeof(prstatus));

	notes[0].name = "CORE";
	notes[0].type = NT_PRSTATUS;
	notes[0].datasz = sizeof(prstatus);
	notes[0].data = &prstatus;
	prstatus.pr_info.si_signo = prstatus.pr_cursig = signr;
	prstatus.pr_sigpend = current->signal;
	prstatus.pr_sighold = current->blocked;
	psinfo.pr_pid = prstatus.pr_pid = current->pid;
	psinfo.pr_ppid = prstatus.pr_ppid = current->p_pptr->pid;
	psinfo.pr_pgrp = prstatus.pr_pgrp = current->pgrp;
	psinfo.pr_sid = prstatus.pr_sid = current->session;
	prstatus.pr_utime.tv_sec = CT_TO_SECS(current->times.tms_utime);
	prstatus.pr_utime.tv_usec = CT_TO_USECS(current->times.tms_utime);
	prstatus.pr_stime.tv_sec = CT_TO_SECS(current->times.tms_stime);
	prstatus.pr_stime.tv_usec = CT_TO_USECS(current->times.tms_stime);
	prstatus.pr_cutime.tv_sec = CT_TO_SECS(current->times.tms_cutime);
	prstatus.pr_cutime.tv_usec = CT_TO_USECS(current->times.tms_cutime);
	prstatus.pr_cstime.tv_sec = CT_TO_SECS(current->times.tms_cstime);
	prstatus.pr_cstime.tv_usec = CT_TO_USECS(current->times.tms_cstime);

	/*
	 * This transfers the registers from regs into the standard
	 * coredump arrangement, whatever that is.
	 */
#ifdef ELF_CORE_COPY_REGS
	ELF_CORE_COPY_REGS(prstatus.pr_reg, regs)
#else
	if (sizeof(elf_gregset_t) != sizeof(struct pt_regs))
	{
		printk("sizeof(elf_gregset_t) (%ld) != sizeof(struct pt_regs) (%ld)\n",
			(long)sizeof(elf_gregset_t), (long)sizeof(struct pt_regs));
	}
	else
		*(struct pt_regs *)&prstatus.pr_reg = *regs;
#endif

#ifdef DEBUG
	dump_regs("Passed in regs", (elf_greg_t *)regs);
	dump_regs("prstatus regs", (elf_greg_t *)&prstatus.pr_reg);
#endif

	notes[1].name = "CORE";
	notes[1].type = NT_PRPSINFO;
	notes[1].datasz = sizeof(psinfo);
	notes[1].data = &psinfo;
	psinfo.pr_state = current->state;
	psinfo.pr_sname = (current->state < 0 || current->state > 5) ? '.' : "RSDZTD"[current->state];
	psinfo.pr_zomb = psinfo.pr_sname == 'Z';
	psinfo.pr_nice = current->priority-15;
	psinfo.pr_flag = current->flags;
	psinfo.pr_uid = current->uid;
	psinfo.pr_gid = current->gid;
	{
		int i, len;

		set_fs(fs);

		len = current->mm->arg_end - current->mm->arg_start;
		if (len >= ELF_PRARGSZ)
			len = ELF_PRARGSZ-1;
		copy_from_user(&psinfo.pr_psargs,
			      (const char *)current->mm->arg_start, len);
		for(i = 0; i < len; i++)
			if (psinfo.pr_psargs[i] == 0)
				psinfo.pr_psargs[i] = ' ';
		psinfo.pr_psargs[len] = 0;

		set_fs(KERNEL_DS);
	}
	strncpy(psinfo.pr_fname, current->comm, sizeof(psinfo.pr_fname));

	notes[2].name = "CORE";
	notes[2].type = NT_TASKSTRUCT;
	notes[2].datasz = sizeof(*current);
	notes[2].data = current;

	/* Try to dump the fpu. */
	prstatus.pr_fpvalid = dump_fpu (regs, &fpu);
	if (!prstatus.pr_fpvalid)
	{
		numnote--;
	}
	else
	{
		notes[3].name = "CORE";
		notes[3].type = NT_PRFPREG;
		notes[3].datasz = sizeof(fpu);
		notes[3].data = &fpu;
	}
	
	/* Write notes phdr entry */
	{
		struct elf_phdr phdr;
		int sz = 0;

		for(i = 0; i < numnote; i++)
			sz += notesize(&notes[i]);

		phdr.p_type = PT_NOTE;
		phdr.p_offset = offset;
		phdr.p_vaddr = 0;
		phdr.p_paddr = 0;
		phdr.p_filesz = sz;
		phdr.p_memsz = 0;
		phdr.p_flags = 0;
		phdr.p_align = 0;

		offset += phdr.p_filesz;
		DUMP_WRITE(&phdr, sizeof(phdr));
	}

	/* Page-align dumped data */
	dataoff = offset = roundup(offset, ELF_EXEC_PAGESIZE);

	/* Write program headers for segments dump */
	for(vma = current->mm->mmap, i = 0;
		i < segs && vma != NULL; vma = vma->vm_next) {
		struct elf_phdr phdr;
		size_t sz;

		i++;

		sz = vma->vm_end - vma->vm_start;

		phdr.p_type = PT_LOAD;
		phdr.p_offset = offset;
		phdr.p_vaddr = vma->vm_start;
		phdr.p_paddr = 0;
		phdr.p_filesz = maydump(vma) ? sz : 0;
		phdr.p_memsz = sz;
		offset += phdr.p_filesz;
		phdr.p_flags = vma->vm_flags & VM_READ ? PF_R : 0;
		if (vma->vm_flags & VM_WRITE) phdr.p_flags |= PF_W;
		if (vma->vm_flags & VM_EXEC) phdr.p_flags |= PF_X;
		phdr.p_align = ELF_EXEC_PAGESIZE;

		DUMP_WRITE(&phdr, sizeof(phdr));
	}

	for(i = 0; i < numnote; i++)
		if (!writenote(&notes[i], &file))
			goto close_coredump;

	set_fs(fs);

	DUMP_SEEK(dataoff);

	for(i = 0, vma = current->mm->mmap;
	    i < segs && vma != NULL;
	    vma = vma->vm_next) {
		unsigned long addr = vma->vm_start;
		unsigned long len = vma->vm_end - vma->vm_start;

		i++;
		if (!maydump(vma))
			continue;
#ifdef DEBUG
		printk("elf_core_dump: writing %08lx %lx\n", addr, len);
#endif
		DUMP_WRITE((void *)addr, len);
	}

	if ((off_t) file.f_pos != offset) {
		/* Sanity check */
		printk("elf_core_dump: file.f_pos (%ld) != offset (%ld)\n",
		       (off_t) file.f_pos, offset);
	}

 close_coredump:
	if (file.f_op->release)
		file.f_op->release(inode,&file);

 end_coredump:
	set_fs(fs);
	iput(inode);
#ifndef CONFIG_BINFMT_ELF
	MOD_DEC_USE_COUNT;
#endif
	return has_dumped;
}
#endif		/* USE_ELF_CORE_DUMP */

__initfunc(int init_elf_binfmt(void))
{
	return register_binfmt(&elf_format);
}

#ifdef MODULE

int init_module(void)
{
	/* Install the COFF, ELF and XOUT loaders.
	 * N.B. We *rely* on the table being the right size with the
	 * right number of free slots...
	 */
	return init_elf_binfmt();
}


void cleanup_module( void)
{
	/* Remove the COFF and ELF loaders. */
	unregister_binfmt(&elf_format);
}
#endif
