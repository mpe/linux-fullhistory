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
use_init_file_context(void)
{
	struct fs_struct * fs;

	lock_kernel();

	/*
	 * Don't use the user's root, use init's root instead.
	 * Note that we can use "init_task" (which is not actually
	 * the same as the user-level "init" process) because we
	 * started "init" with a CLONE_FS
	 */
	exit_fs(current);	/* current->fs->count--; */
	fs = init_task.fs;
	current->fs = fs;
	atomic_inc(&fs->count);

	unlock_kernel();
}

static int exec_modprobe(void * module_name)
{
	static char * envp[] = { "HOME=/", "TERM=linux", "PATH=/sbin:/usr/sbin:/bin:/usr/bin", NULL };
	char *argv[] = { modprobe_path, "-s", "-k", (char*)module_name, NULL };
	int i;

	use_init_file_context();

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

	pid = kernel_thread(exec_modprobe, (void*) module_name, CLONE_FS);
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
		printk (KERN_ERR "kmod: waitpid(%d,NULL,0) failed, returning %d.\n",
			pid, waitpid_result);
	}
	return 0;
}
