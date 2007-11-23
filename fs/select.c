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

	j = 0;
	for (;;) {
		i = j * __NFDBITS;
		if (i >= n)
			break;
		set = in->fds_bits[j] | out->fds_bits[j] | ex->fds_bits[j];
		j++;
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
 *
 * Use "int" accesses to let user-mode fd_set's be int-aligned.
 */
static int __get_fd_set(unsigned long nr, int * fs_pointer, int * fdset)
{
	/* round up nr to nearest "int" */
	nr = (nr + 8*sizeof(int)-1) / (8*sizeof(int));
	if (fs_pointer) {
		int error = verify_area(VERIFY_WRITE,fs_pointer,nr*sizeof(int));
		if (!error) {
			while (nr) {
				*fdset = get_user(fs_pointer);
				nr--;
				fs_pointer++;
				fdset++;
			}
		}
		return error;
	}
	while (nr) {
		*fdset = 0;
		nr--;
		fdset++;
	}
	return 0;
}

static void __set_fd_set(long nr, int * fs_pointer, int * fdset)
{
	if (!fs_pointer)
		return;
	while (nr >= 0) {
		put_user(*fdset, fs_pointer);
		nr -= 8 * sizeof(int);
		fdset++;
		fs_pointer++;
	}
}

/* We can do long accesses here, kernel fdsets are always long-aligned */
static inline void __zero_fd_set(long nr, unsigned long * fdset)
{
	while (nr >= 0) {
		*fdset = 0;
		nr -= 8 * sizeof(unsigned long);
		fdset++;
	}
}		

/*
 * Due to kernel stack usage, we use a _limited_ fd_set type here, and once
 * we really start supporting >256 file descriptors we'll probably have to
 * allocate the kernel fd_set copies dynamically.. (The kernel select routines
 * are careful to touch only the defined low bits of any fd_set pointer, this
 * is important for performance too).
 *
 * Note a few subtleties: we use "long" for the dummy, not int, and we do a
 * subtract by 1 on the nr of file descriptors. The former is better for
 * machines with long > int, and the latter allows us to test the bit count
 * against "zero or positive", which can mostly be just a sign bit test..
 */
typedef struct {
	unsigned long dummy[NR_OPEN/(8*(sizeof(unsigned long)))];
} limited_fd_set;

#define get_fd_set(nr,fsp,fdp) \
__get_fd_set(nr, (int *) (fsp), (int *) (fdp))

#define set_fd_set(nr,fsp,fdp) \
__set_fd_set((nr)-1, (int *) (fsp), (int *) (fdp))

#define zero_fd_set(nr,fdp) \
__zero_fd_set((nr)-1, (unsigned long *) (fdp))

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
	int error;
	limited_fd_set res_in, in;
	limited_fd_set res_out, out;
	limited_fd_set res_ex, ex;
	unsigned long timeout;

	error = -EINVAL;
	if (n < 0)
		goto out;
	if (n > NR_OPEN)
		n = NR_OPEN;
	if ((error = get_fd_set(n, inp, &in)) ||
	    (error = get_fd_set(n, outp, &out)) ||
	    (error = get_fd_set(n, exp, &ex))) goto out;
	timeout = ~0UL;
	if (tvp) {
		error = verify_area(VERIFY_WRITE, tvp, sizeof(*tvp));
		if (error)
			goto out;
		timeout = ROUND_UP(get_user(&tvp->tv_usec),(1000000/HZ));
		timeout += get_user(&tvp->tv_sec) * (unsigned long) HZ;
		if (timeout)
			timeout += jiffies + 1;
	}
	zero_fd_set(n, &res_in);
	zero_fd_set(n, &res_out);
	zero_fd_set(n, &res_ex);
	current->timeout = timeout;
	error = do_select(n,
		(fd_set *) &in,
		(fd_set *) &out,
		(fd_set *) &ex,
		(fd_set *) &res_in,
		(fd_set *) &res_out,
		(fd_set *) &res_ex);
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
	if (error < 0)
		goto out;
	if (!error) {
		error = -ERESTARTNOHAND;
		if (current->signal & ~current->blocked)
			goto out;
		error = 0;
	}
	set_fd_set(n, inp, &res_in);
	set_fd_set(n, outp, &res_out);
	set_fd_set(n, exp, &res_ex);
out:
	return error;
}
