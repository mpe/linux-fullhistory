/*
 * linux/fs/binfmt_elf.c
 */
#include <linux/fs.h>
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

#include <asm/segment.h>

asmlinkage int sys_exit(int exit_code);
asmlinkage int sys_close(unsigned fd);
asmlinkage int sys_open(const char *, int, int);
asmlinkage int sys_brk(unsigned long);

#include <linux/elf.h>

/* We need to explicitly zero any fractional pages
   after the data section (i.e. bss).  This would
   contain the junk from the file that should not
   be in memory */

static void padzero(int elf_bss){
  unsigned int fpnt, nbyte;
  
  if(elf_bss & 0xfff) {
    
    nbyte = (PAGE_SIZE - (elf_bss & 0xfff)) & 0xfff;
    if(nbyte){
      verify_area(VERIFY_WRITE, (void *) elf_bss, nbyte);
      
      fpnt = elf_bss;
      while(fpnt & 0xfff) put_fs_byte(0, fpnt++);
    };
  };
}

/*
 * These are the functions used to load ELF style executables and shared
 * libraries.  There is no binary dependent code anywhere else.
 */

int load_elf_binary(struct linux_binprm * bprm, struct pt_regs * regs)
{
	struct elfhdr elf_ex;
	struct file * file;
  	struct exec ex;
	struct inode *interpreter_inode;
	int i;
	int old_fs;
	int error;
	struct elf_phdr * elf_ppnt, *elf_phdata;
	int elf_exec_fileno;
	unsigned int elf_bss, k, elf_brk;
	int retval;
	char * elf_interpreter;
	unsigned int elf_entry;
	int status;
	unsigned int start_code, end_code, end_data;
	unsigned int elf_stack;
	char passed_fileno[6];
	
	status = 0;
	elf_ex = *((struct elfhdr *) bprm->buf);	  /* exec-header */
	
	if (elf_ex.e_ident[0] != 0x7f ||
	    strncmp(&elf_ex.e_ident[1], "ELF",3) != 0)
		return  -ENOEXEC;
	
	
	/* First of all, some simple consistency checks */
	if(elf_ex.e_type != ET_EXEC || 
	   (elf_ex.e_machine != EM_386 && elf_ex.e_machine != EM_486) ||
	   (!bprm->inode->i_op || !bprm->inode->i_op->default_file_ops ||
	    !bprm->inode->i_op->default_file_ops->mmap)){
		return -ENOEXEC;
	};
	
	/* Now read in all of the header information */
	
	elf_phdata = (struct elf_phdr *) kmalloc(elf_ex.e_phentsize * 
						 elf_ex.e_phnum, GFP_KERNEL);
	
	old_fs = get_fs();
	set_fs(get_ds());
	retval = read_exec(bprm->inode, elf_ex.e_phoff, (char *) elf_phdata,
			   elf_ex.e_phentsize * elf_ex.e_phnum);
	set_fs(old_fs);
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
	
	file = current->filp[elf_exec_fileno];
	
	elf_stack = 0xffffffff;
	elf_interpreter = NULL;
	start_code = 0;
	end_code = 0;
	end_data = 0;
	
	old_fs = get_fs();
	set_fs(get_ds());
	
	for(i=0;i < elf_ex.e_phnum; i++){
		if(elf_ppnt->p_type == PT_INTERP) {
			/* This is the program interpreter used for shared libraries - 
			   for now assume that this is an a.out format binary */
			
			elf_interpreter = (char *) kmalloc(elf_ppnt->p_filesz, 
							   GFP_KERNEL);
			
			retval = read_exec(bprm->inode,elf_ppnt->p_offset,elf_interpreter,
					   elf_ppnt->p_filesz);
#if 0
			printk("Using ELF interpreter %s\n", elf_interpreter);
#endif
			if(retval >= 0)
				retval = namei(elf_interpreter, &interpreter_inode);
			if(retval >= 0)
				retval = read_exec(interpreter_inode,0,bprm->buf,128);
			
			if(retval >= 0){
				ex = *((struct exec *) bprm->buf);		/* exec-header */
				
#if 0
				printk("Interpreter: %x %x %x\n",N_MAGIC(ex), ex.a_text,ex.a_data);
#endif
			};
		};
		elf_ppnt++;
	};
	
	set_fs(old_fs);
	
	/* Some simple consistency checks for the interpreter */
	if(elf_interpreter){
		if(retval < 0) {
			kfree(elf_interpreter);
			kfree(elf_phdata);
			return -ELIBACC;
		};
		if((N_MAGIC(ex) != OMAGIC) && (N_MAGIC(ex) != ZMAGIC)) {
			kfree(elf_interpreter);
			kfree(elf_phdata);
			return -ELIBBAD;
		};
	}
	
	/* OK, we are done with that, now set up the arg stuff,
	   and then start this sucker up */
	
	if (!bprm->sh_bang) {
		char * passed_p;
		
		sprintf(passed_fileno, "%d", elf_exec_fileno);
		passed_p = passed_fileno;
		
		if(elf_interpreter) {
			bprm->p = copy_strings(1,&passed_p,bprm->page,bprm->p,2);
			bprm->argc++;
		};
		if (!bprm->p) {
		        if(elf_interpreter) {
			      kfree(elf_interpreter);
			}
		        kfree (elf_phdata);
			return -E2BIG;
		}
	}
	
	/* OK, This is the point of no return */
	flush_old_exec(bprm);

	current->end_data = 0;
	current->end_code = 0;
	current->start_mmap = ELF_START_MMAP;
	current->mmap = NULL;
	elf_entry = (unsigned int) elf_ex.e_entry;
	
	/* Do this so that we can load the interpreter, if need be.  We will
	   change some of these later */
	current->rss = 0;
	bprm->p += change_ldt(0, bprm->page);
	current->start_stack = bprm->p;
	
	/* Now we do a little grungy work by mmaping the ELF image into
	   the correct location in memory.  At this point, we assume that
	   the image should be loaded at fixed address, not at a variable
	   address. */
	
	old_fs = get_fs();
	set_fs(get_ds());
	
	elf_ppnt = elf_phdata;
	for(i=0;i < elf_ex.e_phnum; i++){
		
		if(elf_ppnt->p_type == PT_INTERP) {
			/* Set these up so that we are able to load the interpreter */
			current->brk = ex.a_bss +
				(current->end_data = ex.a_data +
				 (current->end_code = ex.a_text));
			elf_entry = ex.a_entry;
			
			/* Now load the interpreter into user address space */
			set_fs(old_fs);
			
			if (N_MAGIC(ex) == OMAGIC) {
			  do_mmap(NULL, 0, ex.a_text+ex.a_data,
				  PROT_READ|PROT_WRITE|PROT_EXEC,
				  MAP_FIXED|MAP_PRIVATE, 0);
			  retval = read_exec(interpreter_inode, 32, (char *) 0, 
					     ex.a_text+ex.a_data);
				iput(interpreter_inode);
			} else if (N_MAGIC(ex) == ZMAGIC || N_MAGIC(ex) == QMAGIC) {
			  do_mmap(NULL, 0, ex.a_text+ex.a_data,
				  PROT_READ|PROT_WRITE|PROT_EXEC,
				  MAP_FIXED|MAP_PRIVATE, 0);
			  retval = read_exec(interpreter_inode,
					     N_TXTOFF(ex) ,
					     (char *) N_TXTADDR(ex),
					     ex.a_text+ex.a_data);
			  iput(interpreter_inode);
			} else
				retval = -1;
			
			old_fs = get_fs();
			set_fs(get_ds());
			
			if(retval >= 0)
			  do_mmap(NULL, (ex.a_text + ex.a_data + 0xfff) & 
				  0xfffff000, ex.a_bss,
				  PROT_READ|PROT_WRITE|PROT_EXEC,
				  MAP_FIXED|MAP_PRIVATE, 0);

			kfree(elf_interpreter);
			
			if(retval < 0) { 
				kfree(elf_phdata);
				send_sig(SIGSEGV, current, 0);
				return 0;
			};
		};
		
		
		if(elf_ppnt->p_type == PT_LOAD) {
			error = do_mmap(file,
					elf_ppnt->p_vaddr & 0xfffff000,
					elf_ppnt->p_filesz + (elf_ppnt->p_vaddr & 0xfff),
					PROT_READ | PROT_WRITE | PROT_EXEC,
					MAP_FIXED | MAP_PRIVATE,
					elf_ppnt->p_offset & 0xfffff000);
			
#ifdef LOW_ELF_STACK
			if(elf_ppnt->p_vaddr & 0xfffff000 < elf_stack) 
				elf_stack = elf_ppnt->p_vaddr & 0xfffff000;
#endif
			
			k = elf_ppnt->p_vaddr;
			if(k > start_code) start_code = k;
			k = elf_ppnt->p_vaddr + elf_ppnt->p_filesz;
			if(k > elf_bss) elf_bss = k;
			if((elf_ppnt->p_flags | PROT_WRITE) && end_code <  k)
				end_code = k; 
			if(end_data < k) end_data = k; 
			k = elf_ppnt->p_vaddr + elf_ppnt->p_memsz;
			if(k > elf_brk) elf_brk = k;		     
			
			if(status == 0xffffffff) {
				set_fs(old_fs);
				kfree(elf_phdata);
				send_sig(SIGSEGV, current, 0);
				return 0;
			};
		};
		elf_ppnt++;
	};
	set_fs(old_fs);
	
	kfree(elf_phdata);
	
	if(!elf_interpreter) sys_close(elf_exec_fileno);
	current->elf_executable = 1;
	current->executable = bprm->inode;
	bprm->inode->i_count++;
#ifdef LOW_ELF_STACK
	current->start_stack = p = elf_stack - 4;
#endif
	bprm->p -= MAX_ARG_PAGES*PAGE_SIZE;
	bprm->p = (unsigned long) create_tables((char *)bprm->p,bprm->argc,bprm->envc,0);
	if(elf_interpreter) current->arg_start += strlen(passed_fileno) + 1;
	current->start_brk = current->brk = elf_brk;
	current->end_code = end_code;
	current->start_code = start_code;
	current->end_data = end_data;
	current->start_stack = bprm->p;
	current->suid = current->euid = bprm->e_uid;
	current->sgid = current->egid = bprm->e_gid;

	/* Calling sys_brk effectively mmaps the pages that we need for the bss and break
	   sections */
	current->brk = (elf_bss + 0xfff) & 0xfffff000;
	sys_brk((elf_brk + 0xfff) & 0xfffff000);

	padzero(elf_bss);

	regs->eip = elf_entry;		/* eip, magic happens :-) */
	regs->esp = bprm->p;			/* stack pointer */
	if (current->flags & PF_PTRACED)
		send_sig(SIGTRAP, current, 0);
	return 0;
}

/* This is really simpleminded and specialized - we are loading an a.out library that is given
   an ELF header */

int load_elf_library(int fd){
        struct file * file;
	struct elfhdr elf_ex;
	struct elf_phdr *elf_phdata  =  NULL;
	struct  inode * inode;
	unsigned int len;
	int elf_bss;
	int old_fs, retval;
	unsigned int bss;
	int error;
	int i,j, k;
	
	len = 0;
	file = current->filp[fd];
	inode = file->f_inode;
	elf_bss = 0;
	
	set_fs(KERNEL_DS);
	if (file->f_op->read(inode, file, (char *) &elf_ex, sizeof(elf_ex)) != sizeof(elf_ex)) {
		sys_close(fd);
		return -EACCES;
	}
	set_fs(USER_DS);
	
	if (elf_ex.e_ident[0] != 0x7f ||
	    strncmp(&elf_ex.e_ident[1], "ELF",3) != 0)
		return -ENOEXEC;
	
	/* First of all, some simple consistency checks */
	if(elf_ex.e_type != ET_EXEC || elf_ex.e_phnum > 2 ||
	   (elf_ex.e_machine != EM_386 && elf_ex.e_machine != EM_486) ||
	   (!inode->i_op || !inode->i_op->bmap || 
	    !inode->i_op->default_file_ops->mmap)){
		return -ENOEXEC;
	};
	
	/* Now read in all of the header information */
	
	if(sizeof(struct elf_phdr) * elf_ex.e_phnum > PAGE_SIZE) 
		return -ENOEXEC;
	
	elf_phdata =  (struct elf_phdr *) 
		kmalloc(sizeof(struct elf_phdr) * elf_ex.e_phnum, GFP_KERNEL);
	
	old_fs = get_fs();
	set_fs(get_ds());
	retval = read_exec(inode, elf_ex.e_phoff, (char *) elf_phdata,
			   sizeof(struct elf_phdr) * elf_ex.e_phnum);
	set_fs(old_fs);
	
	j = 0;
	for(i=0; i<elf_ex.e_phnum; i++)
		if((elf_phdata + i)->p_type == PT_LOAD) j++;
	
	if(j != 1)  {
		kfree(elf_phdata);
		return -ENOEXEC;
	};
	
	while(elf_phdata->p_type != PT_LOAD) elf_phdata++;
	
	/* Now use mmap to map the library into memory. */
	error = do_mmap(file,
			elf_phdata->p_vaddr & 0xfffff000,
			elf_phdata->p_filesz + (elf_phdata->p_vaddr & 0xfff),
			PROT_READ | PROT_WRITE | PROT_EXEC,
			MAP_FIXED | MAP_PRIVATE,
			elf_phdata->p_offset & 0xfffff000);

	k = elf_phdata->p_vaddr + elf_phdata->p_filesz;
	if(k > elf_bss) elf_bss = k;
	
	sys_close(fd);
	if (error != elf_phdata->p_vaddr & 0xfffff000) {
	        kfree(elf_phdata);
		return error;
	}

	padzero(elf_bss);

	len = (elf_phdata->p_filesz + elf_phdata->p_vaddr+ 0xfff) & 0xfffff000;
	bss = elf_phdata->p_memsz + elf_phdata->p_vaddr;
	if (bss > len)
	  do_mmap(NULL, len, bss-len,
		  PROT_READ|PROT_WRITE|PROT_EXEC,
		  MAP_FIXED|MAP_PRIVATE, 0);
	kfree(elf_phdata);
	return 0;
}
