/*
 *  linux/fs/binfmt_em86.c
 *
 *  Based on linux/fs/binfmt_script.c
 *  Copyright (C) 1996  Martin von L�wis
 *  original #!-checking implemented by tytso.
 *
 *  em86 changes Copyright (C) 1997  Jim Paradis
 */

#include <linux/module.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/malloc.h>
#include <linux/binfmts.h>
#include <linux/elf.h>
#include <linux/init.h>


#define EM86_INTERP	"/usr/bin/em86"
#define EM86_I_NAME	"em86"

static int do_load_em86(struct linux_binprm *bprm,struct pt_regs *regs)
{
	char *interp, *i_name, *i_arg;
	struct dentry * dentry;
	int retval;
	struct elfhdr	elf_ex;

	/* Make sure this is a Linux/Intel ELF executable... */
	elf_ex = *((struct elfhdr *)bprm->buf);

	if (memcmp(elf_ex.e_ident, ELFMAG, SELFMAG) != 0)
		return  -ENOEXEC;

	/* First of all, some simple consistency checks */
	if ((elf_ex.e_type != ET_EXEC && elf_ex.e_type != ET_DYN) ||
		(!((elf_ex.e_machine == EM_386) || (elf_ex.e_machine == EM_486))) ||
		(!bprm->dentry->d_inode->i_fop || 
		!bprm->dentry->d_inode->i_fop->mmap)) {
			return -ENOEXEC;
	}

	bprm->sh_bang++;	/* Well, the bang-shell is implicit... */
	lock_kernel();
	dput(bprm->dentry);
	unlock_kernel();
	bprm->dentry = NULL;

	/* Unlike in the script case, we don't have to do any hairy
	 * parsing to find our interpreter... it's hardcoded!
	 */
	interp = EM86_INTERP;
	i_name = EM86_I_NAME;
	i_arg = NULL;		/* We reserve the right to add an arg later */

	/*
	 * Splice in (1) the interpreter's name for argv[0]
	 *           (2) (optional) argument to interpreter
	 *           (3) filename of emulated file (replace argv[0])
	 *
	 * This is done in reverse order, because of how the
	 * user environment and arguments are stored.
	 */
	remove_arg_zero(bprm);
	retval = copy_strings_kernel(1, &bprm->filename, bprm);
	if (retval < 0) return retval; 
	bprm->argc++;
	if (i_arg) {
		retval = copy_strings_kernel(1, &i_arg, bprm);
		if (retval < 0) return retval; 
		bprm->argc++;
	}
	retval = copy_strings_kernel(1, &i_name, bprm);
	if (retval < 0)	return retval;
	bprm->argc++;

	/*
	 * OK, now restart the process with the interpreter's inode.
	 * Note that we use open_namei() as the name is now in kernel
	 * space, and we don't need to copy it.
	 */
	lock_kernel();
	dentry = open_namei(interp, 0, 0);
	unlock_kernel();
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);

	bprm->dentry = dentry;

	retval = prepare_binprm(bprm);
	if (retval < 0)
		return retval;

	return search_binary_handler(bprm, regs);
}

static int load_em86(struct linux_binprm *bprm,struct pt_regs *regs)
{
	int retval;
	MOD_INC_USE_COUNT;
	retval = do_load_em86(bprm,regs);
	MOD_DEC_USE_COUNT;
	return retval;
}

struct linux_binfmt em86_format = {
	NULL, THIS_MODULE, load_em86, NULL, NULL, 0
};

static int __init init_em86_binfmt(void)
{
	return register_binfmt(&em86_format);
}

static void __exit exit_em86_binfmt(void)
{
	unregister_binfmt(&em86_format);
}

module_init(init_em86_binfmt)
module_exit(exit_em86_binfmt)
