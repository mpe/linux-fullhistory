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
#include <linux/types.h>
#include <linux/unistd.h>
#include <asm/smp_lock.h>
#include <asm/uaccess.h>

/*
	modprobe_path is set via /proc/sys.
*/
char modprobe_path[256] = "/sbin/modprobe";
static char * envp[] = { "HOME=/", "TERM=linux", "PATH=/usr/bin:/bin", NULL };

/*
	exec_modprobe is spawned from a kernel-mode user process,
	then changes its state to behave _as_if_ it was spawned
	from the kernel's init process
	(ppid and {e,}gid are not adjusted, but that shouldn't
	be a problem since we trust modprobe)
*/
#define task_init task[smp_num_cpus]

static inline void
use_init_file_context(void) {
	lock_kernel();

	/* don't use the user's root, use init's root instead */
	exit_fs(current);	/* current->fs->count--; */
	current->fs = task_init->fs;
	current->fs->count++;

	/* don't use the user's files, use init's files instead */
	exit_files(current);	/* current->files->count--; */
	current->files = task_init->files;
	current->files->count++;

	unlock_kernel();
}

static int exec_modprobe(void * module_name)
{
	char *argv[] = { modprobe_path, "-s", "-k", (char*)module_name, NULL};

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

	set_fs(KERNEL_DS);	/* Allow execve args to be in kernel space. */
	current->uid = current->euid = 0;
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

	pid = kernel_thread(exec_modprobe, (void*) module_name,
			    CLONE_FS | CLONE_FILES | SIGCHLD);
	if (pid < 0) {
		printk(KERN_ERR "kmod: fork failed, errno %d\n", -pid);
		return pid;
	}
	waitpid_result = waitpid(pid, NULL, 0);
	if (waitpid_result != pid) {
		printk (KERN_ERR "kmod: waitpid(%d,NULL,0) failed, returning %d.\n",
			pid, waitpid_result);
	}
	return 0;
}
