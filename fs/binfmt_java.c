/*
 *  linux/fs/binfmt_java.c
 *
 *  Copyright (C) 1996  Brian A. Lantz
 *  derived from binfmt_script.c
 */

#include <linux/module.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/malloc.h>
#include <linux/binfmts.h>
#include <paths.h>

#define _PATH_JAVA	"/usr/bin/java"
#define _PATH_APPLET	"/usr/bin/appletviewer"
#define _PATH_SH	"/bin/bash"

char binfmt_java_interpreter[65] = _PATH_JAVA;
char binfmt_java_appletviewer[65] = _PATH_APPLET;

static int do_load_script(struct linux_binprm *bprm,struct pt_regs *regs)
{
	char *cp, *interp, *i_name;
	int retval;
	unsigned char *ucp = (unsigned char *) bprm->buf;
	if ((ucp[0] != 0xca) || (ucp[1] != 0xfe) || (ucp[2] != 0xba) || (ucp[3] != 0xbe)) 
		return -ENOEXEC;

	iput(bprm->inode);
	bprm->dont_iput=1;

	/*
	 * OK, we've set the interpreter name
	 * Splice in (1) the interpreter's name for argv[0] (_PATH_SH)
	 *           (2) the name of the java wrapper for argv[1] (_PATH_JAVA)
	 *           (3) filename of Java class (replace argv[0])
	 *               without leading path or trailing '.class'
	 *
	 * This is done in reverse order, because of how the
	 * user environment and arguments are stored.
	 */
	remove_arg_zero(bprm);
	if ((cp = strstr (bprm->filename, ".class")) != NULL)
		*cp = 0;
	if ((i_name = strrchr (bprm->filename, '/')) != NULL)
		i_name++;
	else
		i_name = bprm->filename;
	bprm->p = copy_strings(1, &i_name, bprm->page, bprm->p, 2);
	bprm->argc++;

	strcpy (bprm->buf, binfmt_java_interpreter);
	cp = bprm->buf;
	bprm->p = copy_strings(1, &cp, bprm->page, bprm->p, 2);
	bprm->argc++;

	strcpy (bprm->buf, _PATH_SH);
	interp = bprm->buf;
	if ((i_name = strrchr (bprm->buf, '/')) != NULL)
		i_name++;
	else
		i_name = bprm->buf;
	bprm->p = copy_strings(1, &i_name, bprm->page, bprm->p, 2);
	bprm->argc++;
	if (!bprm->p) 
		return -E2BIG;
	/*
	 * OK, now restart the process with the interpreter's inode.
	 * Note that we use open_namei() as the name is now in kernel
	 * space, and we don't need to copy it.
	 */
	retval = open_namei(interp, 0, 0, &bprm->inode, NULL);
	if (retval)
		return retval;
	bprm->dont_iput=0;
	retval=prepare_binprm(bprm);
	if(retval<0)
		return retval;

	return search_binary_handler(bprm,regs);
}

static int do_load_applet(struct linux_binprm *bprm,struct pt_regs *regs)
{
	char *cp, *interp, *i_name;
	int retval;
	if (strncmp (bprm->buf, "<!--applet", 10))
		return -ENOEXEC;

	iput(bprm->inode);
	bprm->dont_iput=1;

	/*
	 * OK, we've set the interpreter name
	 * Splice in (1) the interpreter's name for argv[0] (_PATH_BSHELL)
	 *           (2) the name of the appletviewer wrapper for argv[1] (_PATH_APPLET)
	 *           (3) filename of html file (replace argv[0])
	 *
	 * This is done in reverse order, because of how the
	 * user environment and arguments are stored.
	 */
	remove_arg_zero(bprm);
	i_name = bprm->filename;
	bprm->p = copy_strings(1, &i_name, bprm->page, bprm->p, 2);
	bprm->argc++;

	strcpy (bprm->buf, binfmt_java_appletviewer);
	cp = bprm->buf;
	bprm->p = copy_strings(1, &cp, bprm->page, bprm->p, 2);
	bprm->argc++;

	strcpy (bprm->buf, _PATH_SH);
	interp = bprm->buf;
	if ((i_name = strrchr (bprm->buf, '/')) != NULL)
		i_name++;
	else
		i_name = bprm->buf;
	bprm->p = copy_strings(1, &i_name, bprm->page, bprm->p, 2);
	bprm->argc++;
	if (!bprm->p) 
		return -E2BIG;
	/*
	 * OK, now restart the process with the interpreter's inode.
	 * Note that we use open_namei() as the name is now in kernel
	 * space, and we don't need to copy it.
	 */
	retval = open_namei(interp, 0, 0, &bprm->inode, NULL);
	if (retval)
		return retval;
	bprm->dont_iput=0;
	retval=prepare_binprm(bprm);
	if(retval<0)
		return retval;

	return search_binary_handler(bprm,regs);
}

static int load_script(struct linux_binprm *bprm,struct pt_regs *regs)
{
	int retval;
	MOD_INC_USE_COUNT;
	retval = do_load_script(bprm,regs);
	MOD_DEC_USE_COUNT;
	return retval;
}

struct linux_binfmt java_format = {
#ifndef MODULE
	NULL, 0, load_script, NULL, NULL
#else
	NULL, &mod_use_count_, load_script, NULL, NULL
#endif
};

static int load_applet(struct linux_binprm *bprm,struct pt_regs *regs)
{
	int retval;
	MOD_INC_USE_COUNT;
	retval = do_load_applet(bprm,regs);
	MOD_DEC_USE_COUNT;
	return retval;
}

struct linux_binfmt applet_format = {
#ifndef MODULE
	NULL, 0, load_applet, NULL, NULL
#else
	NULL, &mod_use_count_, load_applet, NULL, NULL
#endif
};

int init_java_binfmt(void) {
	printk(KERN_INFO "JAVA Binary support v1.01 for Linux 1.3.98 (C)1996 Brian A. Lantz\n");
	register_binfmt(&java_format);
	return register_binfmt(&applet_format);
}

#ifdef MODULE
int init_module(void)
{
	return init_java_binfmt();
}

void cleanup_module( void) {
	printk(KERN_INFO "Removing JAVA Binary support...\n");
	unregister_binfmt(&java_format);
	unregister_binfmt(&applet_format);
}
#endif
