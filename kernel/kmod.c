/*
	kmod, the new module loader (replaces kerneld)
	Kirk Petersen

	Reorganized not to be a daemon by Adam Richter, with guidance
	from Greg Zornetzer.

	Modified to avoid chroot and file sharing problems.
	Mikael Pettersson
*/

#define __KERNEL_SYSCALLS__

#include <linux/sched.h>
#include <linux/unistd.h>
#include <linux/smp_lock.h>

#include <asm/uaccess.h>

/*
	modprobe_path is set via /proc/sys.
*/
char modprobe_path[256] = "/sbin/modprobe";

static inline void
use_init_fs_context(void)
{
	struct fs_struct *our_fs, *init_fs;

	/*
	 * Make modprobe's fs context be a copy of init's.
	 *
	 * We cannot use the user's fs context, because it
	 * may have a different root than init.
	 * Since init was created with CLONE_FS, we can grab
	 * its fs context from "init_task".
	 *
	 * The fs context has to be a copy. If it is shared
	 * with init, then any chdir() call in modprobe will
	 * also affect init and the other threads sharing
	 * init_task's fs context.
	 *
	 * We created the exec_modprobe thread without CLONE_FS,
	 * so we can update the fields in our fs context freely.
	 */
	lock_kernel();

	our_fs = current->fs;
	dput(our_fs->root);
	dput(our_fs->pwd);

	init_fs = init_task.fs;
	our_fs->umask = init_fs->umask;
	our_fs->root = dget(init_fs->root);
	our_fs->pwd = dget(init_fs->pwd);

	unlock_kernel();
}

static int exec_modprobe(void * module_name)
{
	static char * envp[] = { "HOME=/", "TERM=linux", "PATH=/sbin:/usr/sbin:/bin:/usr/bin", NULL };
	char *argv[] = { modprobe_path, "-s", "-k", (char*)module_name, NULL };
	int i;

	current->session = 1;
	current->pgrp = 1;

	use_init_fs_context();

	/* Prevent parent user process from sending signals to child.
	   Otherwise, if the modprobe program does not exist, it might
	   be possible to get a user defined signal handler to execute
	   as the super user right after the execve fails if you time
	   the signal just right.
	*/
	spin_lock_irq(&current->sigmask_lock);
	flush_signals(current);
	flush_signal_handlers(current);
	spin_unlock_irq(&current->sigmask_lock);

	for (i = 0; i < current->files->max_fds; i++ ) {
		if (current->files->fd[i]) close(i);
	}

	/* Drop the "current user" thing */
	free_uid(current);

	/* Give kmod all privileges.. */
	current->uid = current->euid = current->fsuid = 0;
	cap_set_full(current->cap_inheritable);
	cap_set_full(current->cap_effective);

	/* Allow execve args to be in kernel space. */
	set_fs(KERNEL_DS);

	/* Go, go, go... */
	if (execve(modprobe_path, argv, envp) < 0) {
		printk(KERN_ERR
		       "kmod: failed to exec %s -s -k %s, errno = %d\n",
		       modprobe_path, (char*) module_name, errno);
		return -errno;
	}
	return 0;
}

/*
	request_module: the function that everyone calls when they need
	a module.
*/
int request_module(const char * module_name)
{
	int pid;
	int waitpid_result;
	sigset_t tmpsig;

	/* Don't allow request_module() before the root fs is mounted!  */
	if ( ! current->fs->root ) {
		printk(KERN_ERR "request_module[%s]: Root fs not mounted\n",
			module_name);
		return -EPERM;
	}

	pid = kernel_thread(exec_modprobe, (void*) module_name, 0);
	if (pid < 0) {
		printk(KERN_ERR "request_module[%s]: fork failed, errno %d\n", module_name, -pid);
		return pid;
	}

	/* Block everything but SIGKILL/SIGSTOP */
	spin_lock_irq(&current->sigmask_lock);
	tmpsig = current->blocked;
	siginitsetinv(&current->blocked, sigmask(SIGKILL) | sigmask(SIGSTOP));
	recalc_sigpending(current);
	spin_unlock_irq(&current->sigmask_lock);

	waitpid_result = waitpid(pid, NULL, __WCLONE);

	/* Allow signals again.. */
	spin_lock_irq(&current->sigmask_lock);
	current->blocked = tmpsig;
	recalc_sigpending(current);
	spin_unlock_irq(&current->sigmask_lock);

	if (waitpid_result != pid) {
		printk(KERN_ERR "request_module[%s]: waitpid(%d,...) failed, errno %d\n",
		       module_name, pid, -waitpid_result);
	}
	return 0;
}
