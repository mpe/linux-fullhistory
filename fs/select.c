/*
 * This file contains the procedures for the handling of select
 *
 * Created for Linux based loosely upon Mathius Lattner's minix
 * patches by Peter MacDonald. Heavily edited by Linus.
 *
 *  4 February 1994
 *     COFF/ELF binary emulation. If the process has the STICKY_TIMEOUTS
 *     flag set in its personality we do *not* modify the given timeout
 *     parameter to reflect time remaining.
 */

#include <linux/types.h>
#include <linux/time.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/personality.h>
#include <linux/mm.h>

#include <asm/segment.h>
#include <asm/system.h>

#define ROUND_UP(x,y) (((x)+(y)-1)/(y))

/*
 * Ok, Peter made a complicated, but straightforward multiple_wait() function.
 * I have rewritten this, taking some shortcuts: This code may not be easy to
 * follow, but it should be free of race-conditions, and it's practical. If you
 * understand what I'm doing here, then you understand how the linux
 * sleep/wakeup mechanism works.
 *
 * Two very simple procedures, select_wait() and free_wait() make all the work.
 * select_wait() is a inline-function defined in <linux/sched.h>, as all select
 * functions have to call it to add an entry to the select table.
 */

/*
 * I rewrote this again to make the select_table size variable, take some
 * more shortcuts, improve responsiveness, and remove another race that
 * Linus noticed.  -- jrs
 */

static void free_wait(select_table * p)
{
	struct select_table_entry * entry = p->entry + p->nr;

	while (p->nr > 0) {
		p->nr--;
		entry--;
		remove_wait_queue(entry->wait_address,&entry->wait);
	}
}

/*
 * The check function checks the ready status of a file using the vfs layer.
 *
 * If the file was not ready we were added to its wait queue.  But in
 * case it became ready just after the check and just before it called
 * select_wait, we call it again, knowing we are already on its
 * wait queue this time.  The second call is not necessary if the
 * select_table is NULL indicating an earlier file check was ready
 * and we aren't going to sleep on the select_table.  -- jrs
 */

static int check(int flag, select_table * wait, struct file * file)
{
	struct inode * inode;
	struct file_operations *fops;
	int (*select) (struct inode *, struct file *, int, select_table *);

	inode = file->f_inode;
	if ((fops = file->f_op) && (select = fops->select))
		return select(inode, file, flag, wait)
		    || (wait && select(inode, file, flag, NULL));
	if (flag != SEL_EX)
		return 1;
	return 0;
}

static int do_select(int n, fd_set *in, fd_set *out, fd_set *ex,
	fd_set *res_in, fd_set *res_out, fd_set *res_ex)
{
	int count;
	select_table wait_table, *wait;
	struct select_table_entry *entry;
	unsigned long set;
	int i,j;
	int max = -1;

	for (j = 0 ; j < __FDSET_INTS ; j++) {
		i = j * __NFDBITS;
		if (i >= n)
			break;
		set = in->fds_bits[j] | out->fds_bits[j] | ex->fds_bits[j];
		for ( ; set ; i++,set >>= 1) {
			if (i >= n)
				goto end_check;
			if (!(set & 1))
				continue;
			if (!current->files->fd[i])
				return -EBADF;
			if (!current->files->fd[i]->f_inode)
				return -EBADF;
			max = i;
		}
	}
end_check:
	n = max + 1;
	if(!(entry = (struct select_table_entry*) __get_free_page(GFP_KERNEL)))
		return -ENOMEM;
	FD_ZERO(res_in);
	FD_ZERO(res_out);
	FD_ZERO(res_ex);
	count = 0;
	wait_table.nr = 0;
	wait_table.entry = entry;
	wait = &wait_table;
repeat:
	current->state = TASK_INTERRUPTIBLE;
	for (i = 0 ; i < n ; i++) {
		if (FD_ISSET(i,in) && check(SEL_IN,wait,current->files->fd[i])) {
			FD_SET(i, res_in);
			count++;
			wait = NULL;
		}
		if (FD_ISSET(i,out) && check(SEL_OUT,wait,current->files->fd[i])) {
			FD_SET(i, res_out);
			count++;
			wait = NULL;
		}
		if (FD_ISSET(i,ex) && check(SEL_EX,wait,current->files->fd[i])) {
			FD_SET(i, res_ex);
			count++;
			wait = NULL;
		}
	}
	wait = NULL;
	if (!count && current->timeout && !(current->signal & ~current->blocked)) {
		schedule();
		goto repeat;
	}
	free_wait(&wait_table);
	free_page((unsigned long) entry);
	current->state = TASK_RUNNING;
	return count;
}

/*
 * We do a VERIFY_WRITE here even though we are only reading this time:
 * we'll write to it eventually..
 */
static int __get_fd_set(int nr, unsigned int * fs_pointer, fd_set * fdset)
{
	int error, i;
	unsigned int * tmp;

	FD_ZERO(fdset);
	if (!fs_pointer)
		return 0;
	error = verify_area(VERIFY_WRITE,fs_pointer,sizeof(fd_set));
	if (error)
		return error;
	tmp = fdset->fds_bits;
	for (i = __FDSET_INTS; i > 0; i--) {
		if (nr <= 0)
			break;
		*tmp = get_user(fs_pointer);
		tmp++;
		fs_pointer++;
		nr -= 8 * sizeof(unsigned int);
	}
	return 0;
}

static void __set_fd_set(int nr, unsigned int * fs_pointer, unsigned int * fdset)
{
	int i;

	if (!fs_pointer)
		return;
	for (i = __FDSET_INTS; i > 0; i--) {
		if (nr <= 0)
			break;
		put_user(*fdset, fs_pointer);
		fdset++;
		fs_pointer++;
		nr -= 8 * sizeof(unsigned int);
	}
}

#define get_fd_set(nr,fsp,fdp) \
__get_fd_set(nr, (unsigned int *) (fsp), fdp)

#define set_fd_set(nr,fsp,fdp) \
__set_fd_set(nr, (unsigned int *) (fsp), (unsigned int *) (fdp))

/*
 * We can actually return ERESTARTSYS instead of EINTR, but I'd
 * like to be certain this leads to no problems. So I return
 * EINTR just for safety.
 *
 * Update: ERESTARTSYS breaks at least the xview clock binary, so
 * I'm trying ERESTARTNOHAND which restart only when you want to.
 */
asmlinkage int sys_select(int n, fd_set *inp, fd_set *outp, fd_set *exp, struct timeval *tvp)
{
	int i;
	fd_set res_in, in;
	fd_set res_out, out;
	fd_set res_ex, ex;
	unsigned long timeout;

	if (n < 0)
		return -EINVAL;
	if (n > NR_OPEN)
		n = NR_OPEN;
	if ((i = get_fd_set(n, inp, &in)) ||
	    (i = get_fd_set(n, outp, &out)) ||
	    (i = get_fd_set(n, exp, &ex))) return i;
	timeout = ~0UL;
	if (tvp) {
		i = verify_area(VERIFY_WRITE, tvp, sizeof(*tvp));
		if (i)
			return i;
		timeout = ROUND_UP(get_user(&tvp->tv_usec),(1000000/HZ));
		timeout += get_user(&tvp->tv_sec) * (unsigned long) HZ;
		if (timeout)
			timeout += jiffies + 1;
	}
	current->timeout = timeout;
	i = do_select(n, &in, &out, &ex, &res_in, &res_out, &res_ex);
	timeout = current->timeout - jiffies - 1;
	current->timeout = 0;
	if ((long) timeout < 0)
		timeout = 0;
	if (tvp && !(current->personality & STICKY_TIMEOUTS)) {
		put_user(timeout/HZ, &tvp->tv_sec);
		timeout %= HZ;
		timeout *= (1000000/HZ);
		put_user(timeout, &tvp->tv_usec);
	}
	if (i < 0)
		return i;
	if (!i && (current->signal & ~current->blocked))
		return -ERESTARTNOHAND;
	set_fd_set(n, inp, &res_in);
	set_fd_set(n, outp, &res_out);
	set_fd_set(n, exp, &res_ex);
	return i;
}
