/*
 *  linux/fs/binfmt_java.c
 *
 *  Copyright (C) 1996  Brian A. Lantz
 *  derived from binfmt_script.c
 *
 *  Simplified and modified to support binary java interpreters
 *  by Tom May <ftom@netcom.com>.
 */

#include <linux/module.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/malloc.h>
#include <linux/binfmts.h>
#include <linux/init.h>

#define _PATH_JAVA	"/usr/bin/java"
#define _PATH_APPLET	"/usr/bin/appletviewer"

/*  These paths can be modified with sysctl().  */

char binfmt_java_interpreter[65] = _PATH_JAVA;
char binfmt_java_appletviewer[65] = _PATH_APPLET;

static int do_load_java(struct linux_binprm *bprm,struct pt_regs *regs)
{
	char *i_name;
	int len;
	int retval;
	struct dentry * dentry;
	unsigned char *ucp = (unsigned char *) bprm->buf;

	if ((ucp[0] != 0xca) || (ucp[1] != 0xfe) || (ucp[2] != 0xba) || (ucp[3] != 0xbe)) 
		return -ENOEXEC;

	/*
	 * Fail if we're called recursively, e.g., the Java interpreter
	 * is a java binary.
	 */

	if (bprm->java)
		return -ENOEXEC;

	bprm->java = 1;

	dput(bprm->dentry);
	bprm->dentry = NULL;

	/*
	 * Set args: [0] the name of the java interpreter
	 *           [1] name of java class to execute, which is the
	 *		 filename without the path and without trailing
	 *		 ".class".  Note that the interpreter will use
	 *		 its own way to found the class file (typically using
	 *		 environment variable CLASSPATH), and may in fact
	 *		 execute a different file from the one we want.
	 *
	 * This is done in reverse order, because of how the
	 * user environment and arguments are stored.
	 */
	remove_arg_zero(bprm);
	len = strlen (bprm->filename);
	if (len >= 6 && !strcmp (bprm->filename + len - 6, ".class"))
		bprm->filename[len - 6] = 0;
	if ((i_name = strrchr (bprm->filename, '/')) != NULL)
		i_name++;
	else
		i_name = bprm->filename;
	bprm->p = copy_strings(1, &i_name, bprm->page, bprm->p, 2);
	bprm->argc++;

	i_name = binfmt_java_interpreter;
	bprm->p = copy_strings(1, &i_name, bprm->page, bprm->p, 2);
	bprm->argc++;

	if (!bprm->p) 
		return -E2BIG;
	/*
	 * OK, now restart the process with the interpreter's dentry.
	 */
	bprm->filename = binfmt_java_interpreter;
	dentry = open_namei(binfmt_java_interpreter, 0, 0);
	retval = PTR_ERR(dentry);
	if (IS_ERR(dentry))
		return retval;

	bprm->dentry = dentry;
	retval = prepare_binprm(bprm);
	if (retval < 0)
		return retval;

	return search_binary_handler(bprm,regs);
}

static int do_load_applet(struct linux_binprm *bprm,struct pt_regs *regs)
{
	char *i_name;
	struct dentry * dentry;
	int retval;

	if (strncmp (bprm->buf, "<!--applet", 10))
		return -ENOEXEC;

	dput(bprm->dentry);
	bprm->dentry = NULL;

	/*
	 * Set args: [0] the name of the appletviewer
	 *           [1] filename of html file
	 *
	 * This is done in reverse order, because of how the
	 * user environment and arguments are stored.
	 */
	remove_arg_zero(bprm);
	i_name = bprm->filename;
	bprm->p = copy_strings(1, &i_name, bprm->page, bprm->p, 2);
	bprm->argc++;

	i_name = binfmt_java_appletviewer;
	bprm->p = copy_strings(1, &i_name, bprm->page, bprm->p, 2);
	bprm->argc++;

	if (!bprm->p) 
		return -E2BIG;
	/*
	 * OK, now restart the process with the interpreter's dentry.
	 */
	bprm->filename = binfmt_java_appletviewer;
	dentry = open_namei(binfmt_java_appletviewer, 0, 0);
	retval = PTR_ERR(dentry);
	if (IS_ERR(dentry))
		return retval;

	bprm->dentry = dentry;
	retval = prepare_binprm(bprm);
	if (retval < 0)
		return retval;

	return search_binary_handler(bprm,regs);
}

static int load_java(struct linux_binprm *bprm,struct pt_regs *regs)
{
	int retval;
	MOD_INC_USE_COUNT;
	retval = do_load_java(bprm,regs);
	MOD_DEC_USE_COUNT;
	return retval;
}

static struct linux_binfmt java_format = {
#ifndef MODULE
	NULL, 0, load_java, NULL, NULL
#else
	NULL, &__this_module, load_java, NULL, NULL
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

static struct linux_binfmt applet_format = {
#ifndef MODULE
	NULL, 0, load_applet, NULL, NULL
#else
	NULL, &__this_module, load_applet, NULL, NULL
#endif
};

int __init init_java_binfmt(void)
{
	register_binfmt(&java_format);
	return register_binfmt(&applet_format);
}

#ifdef MODULE
int init_module(void)
{
	return init_java_binfmt();
}

void cleanup_module( void) {
	unregister_binfmt(&java_format);
	unregister_binfmt(&applet_format);
}
#endif
