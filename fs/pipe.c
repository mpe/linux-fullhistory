/*
 *  linux/fs/pipe.c
 *
 *  Copyright (C) 1991, 1992, 1999  Linus Torvalds
 */

#include <linux/mm.h>
#include <linux/file.h>
#include <linux/poll.h>
#include <linux/malloc.h>
#include <linux/smp_lock.h>

#include <asm/uaccess.h>

/*
 * Define this if you want SunOS compatibility wrt braindead
 * select behaviour on FIFO's.
 */
#ifdef __sparc__
#define FIFO_SUNOS_BRAINDAMAGE
#else
#undef FIFO_SUNOS_BRAINDAMAGE
#endif

/*
 * We use a start+len construction, which provides full use of the 
 * allocated memory.
 * -- Florian Coosmann (FGC)
 * 
 * Reads with count = 0 should always return 0.
 * -- Julian Bradfield 1999-06-07.
 */

/* Drop the inode semaphore and wait for a pipe event, atomically */
static void pipe_wait(struct inode * inode)
{
	DECLARE_WAITQUEUE(wait, current);
	current->state = TASK_INTERRUPTIBLE;
	add_wait_queue(PIPE_WAIT(*inode), &wait);
	up(PIPE_SEM(*inode));
	schedule();
	remove_wait_queue(PIPE_WAIT(*inode), &wait);
	current->state = TASK_RUNNING;
	down(PIPE_SEM(*inode));
}

static ssize_t
pipe_read(struct file *filp, char *buf, size_t count, loff_t *ppos)
{
	struct inode *inode = filp->f_dentry->d_inode;
	ssize_t size, read, ret;

	/* Seeks are not allowed on pipes.  */
	ret = -ESPIPE;
	read = 0;
	if (ppos != &filp->f_pos)
		goto out_nolock;

	/* Always return 0 on null read.  */
	ret = 0;
	if (count == 0)
		goto out_nolock;

	/* Get the pipe semaphore */
	ret = -ERESTARTSYS;
	if (down_interruptible(PIPE_SEM(*inode)))
		goto out_nolock;

	if (PIPE_EMPTY(*inode)) {
do_more_read:
		ret = 0;
		if (!PIPE_WRITERS(*inode))
			goto out;

		ret = -EAGAIN;
		if (filp->f_flags & O_NONBLOCK)
			goto out;

		for (;;) {
			PIPE_WAITING_READERS(*inode)++;
			pipe_wait(inode);
			PIPE_WAITING_READERS(*inode)--;
			ret = -ERESTARTSYS;
			if (signal_pending(current))
				goto out;
			ret = 0;
			if (!PIPE_EMPTY(*inode))
				break;
			if (!PIPE_WRITERS(*inode))
				goto out;
		}
	}

	/* Read what data is available.  */
	ret = -EFAULT;
	while (count > 0 && (size = PIPE_LEN(*inode))) {
		char *pipebuf = PIPE_BASE(*inode) + PIPE_START(*inode);
		ssize_t chars = PIPE_MAX_RCHUNK(*inode);

		if (chars > count)
			chars = count;
		if (chars > size)
			chars = size;

		if (copy_to_user(buf, pipebuf, chars))
			goto out;

		read += chars;
		PIPE_START(*inode) += chars;
		PIPE_START(*inode) &= (PIPE_SIZE - 1);
		PIPE_LEN(*inode) -= chars;
		count -= chars;
		buf += chars;
	}

	/* Cache behaviour optimization */
	if (!PIPE_LEN(*inode))
		PIPE_START(*inode) = 0;

	if (count && PIPE_WAITING_WRITERS(*inode) && !(filp->f_flags & O_NONBLOCK)) {
		/*
		 * We know that we are going to sleep: signal
		 * writers synchronously that there is more
		 * room.
		 */
		wake_up_interruptible_sync(PIPE_WAIT(*inode));
		if (!PIPE_EMPTY(*inode))
			BUG();
		goto do_more_read;
	}
	/* Signal writers asynchronously that there is more room.  */
	wake_up_interruptible(PIPE_WAIT(*inode));

	ret = read;
out:
	up(PIPE_SEM(*inode));
out_nolock:
	if (read)
		ret = read;
	return ret;
}

static ssize_t
pipe_write(struct file *filp, const char *buf, size_t count, loff_t *ppos)
{
	struct inode *inode = filp->f_dentry->d_inode;
	ssize_t free, written, ret;

	/* Seeks are not allowed on pipes.  */
	ret = -ESPIPE;
	written = 0;
	if (ppos != &filp->f_pos)
		goto out_nolock;

	/* Null write succeeds.  */
	ret = 0;
	if (count == 0)
		goto out_nolock;

	ret = -ERESTARTSYS;
	if (down_interruptible(PIPE_SEM(*inode)))
		goto out_nolock;

do_more_write:
	/* No readers yields SIGPIPE.  */
	if (!PIPE_READERS(*inode))
		goto sigpipe;

	/* If count <= PIPE_BUF, we have to make it atomic.  */
	free = (count <= PIPE_BUF ? count : 1);

	/* Wait, or check for, available space.  */
	if (filp->f_flags & O_NONBLOCK) {
		ret = -EAGAIN;
		if (PIPE_FREE(*inode) < free)
			goto out;
	} else {
		while (PIPE_FREE(*inode) < free) {
			PIPE_WAITING_WRITERS(*inode)++;
			pipe_wait(inode);
			PIPE_WAITING_WRITERS(*inode)--;
			ret = -ERESTARTSYS;
			if (signal_pending(current))
				goto out;

			if (!PIPE_READERS(*inode))
				goto sigpipe;
		}
	}

	/* Copy into available space.  */
	ret = -EFAULT;
	while (count > 0) {
		int space;
		char *pipebuf = PIPE_BASE(*inode) + PIPE_END(*inode);
		ssize_t chars = PIPE_MAX_WCHUNK(*inode);

		if ((space = PIPE_FREE(*inode)) != 0) {
			if (chars > count)
				chars = count;
			if (chars > space)
				chars = space;

			if (copy_from_user(pipebuf, buf, chars))
				goto out;

			written += chars;
			PIPE_LEN(*inode) += chars;
			count -= chars;
			buf += chars;
			space = PIPE_FREE(*inode);
			continue;
		}

		ret = written;
		if (filp->f_flags & O_NONBLOCK)
			break;

		do {
			/*
			 * Synchronous wake-up: it knows that this process
			 * is going to give up this CPU, so it doesnt have
			 * to do idle reschedules.
			 */
			wake_up_interruptible_sync(PIPE_WAIT(*inode));
			PIPE_WAITING_WRITERS(*inode)++;
			pipe_wait(inode);
			PIPE_WAITING_WRITERS(*inode)--;
			if (signal_pending(current))
				goto out;
			if (!PIPE_READERS(*inode))
				goto sigpipe;
		} while (!PIPE_FREE(*inode));
		ret = -EFAULT;
	}

	/* Signal readers asynchronously that there is more data.  */
	wake_up_interruptible(PIPE_WAIT(*inode));

	inode->i_ctime = inode->i_mtime = CURRENT_TIME;
	mark_inode_dirty(inode);

out:
	up(PIPE_SEM(*inode));
out_nolock:
	if (written)
		ret = written;
	return ret;

sigpipe:
	if (written)
		goto out;
	up(PIPE_SEM(*inode));
	send_sig(SIGPIPE, current, 0);
	return -EPIPE;
}

static loff_t
pipe_lseek(struct file *file, loff_t offset, int orig)
{
	return -ESPIPE;
}

static ssize_t
bad_pipe_r(struct file *filp, char *buf, size_t count, loff_t *ppos)
{
	return -EBADF;
}

static ssize_t
bad_pipe_w(struct file *filp, const char *buf, size_t count, loff_t *ppos)
{
	return -EBADF;
}

static int
pipe_ioctl(struct inode *pino, struct file *filp,
	   unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
		case FIONREAD:
			return put_user(PIPE_LEN(*pino), (int *)arg);
		default:
			return -EINVAL;
	}
}

static unsigned int
pipe_poll(struct file *filp, poll_table *wait)
{
	unsigned int mask;
	struct inode *inode = filp->f_dentry->d_inode;

	poll_wait(filp, PIPE_WAIT(*inode), wait);

	/* Reading only -- no need for aquiring the semaphore.  */
	mask = POLLIN | POLLRDNORM;
	if (PIPE_EMPTY(*inode))
		mask = POLLOUT | POLLWRNORM;
	if (!PIPE_WRITERS(*inode))
		mask |= POLLHUP;
	if (!PIPE_READERS(*inode))
		mask |= POLLERR;

	return mask;
}

#ifdef FIFO_SUNOS_BRAINDAMAGE
/*
 * Argh!  Why does SunOS have to have different select() behaviour
 * for pipes and FIFOs?  Hate, hate, hate!  SunOS lacks POLLHUP.
 */
static unsigned int
fifo_poll(struct file *filp, poll_table *wait)
{
	unsigned int mask;
	struct inode *inode = filp->f_dentry->d_inode;

	poll_wait(filp, PIPE_WAIT(*inode), wait);

	/* Reading only -- no need for aquiring the semaphore.  */
	mask = POLLIN | POLLRDNORM;
	if (PIPE_EMPTY(*inode))
		mask = POLLOUT | POLLWRNORM;
	if (!PIPE_READERS(*inode))
		mask |= POLLERR;

	return mask;
}
#else

#define fifo_poll pipe_poll

#endif /* FIFO_SUNOS_BRAINDAMAGE */

/*
 * The 'connect_xxx()' functions are needed for named pipes when
 * the open() code hasn't guaranteed a connection (O_NONBLOCK),
 * and we need to act differently until we do get a writer..
 */
static ssize_t
connect_read(struct file *filp, char *buf, size_t count, loff_t *ppos)
{
	struct inode *inode = filp->f_dentry->d_inode;

	/* Reading only -- no need for aquiring the semaphore.  */
	if (PIPE_EMPTY(*inode) && !PIPE_WRITERS(*inode))
		return 0;

	filp->f_op = &read_fifo_fops;
	return pipe_read(filp, buf, count, ppos);
}

static unsigned int
connect_poll(struct file *filp, poll_table *wait)
{
	struct inode *inode = filp->f_dentry->d_inode;
	unsigned int mask = 0;

	poll_wait(filp, PIPE_WAIT(*inode), wait);

	/* Reading only -- no need for aquiring the semaphore.  */
	if (!PIPE_EMPTY(*inode)) {
		filp->f_op = &read_fifo_fops;
		mask = POLLIN | POLLRDNORM;
	} else if (PIPE_WRITERS(*inode)) {
		filp->f_op = &read_fifo_fops;
		mask = POLLOUT | POLLWRNORM;
	}

	return mask;
}

static int
pipe_release(struct inode *inode, int decr, int decw)
{
	down(PIPE_SEM(*inode));
	PIPE_READERS(*inode) -= decr;
	PIPE_WRITERS(*inode) -= decw;
	if (!PIPE_READERS(*inode) && !PIPE_WRITERS(*inode)) {
		struct pipe_inode_info *info = inode->i_pipe;
		inode->i_pipe = NULL;
		free_page((unsigned long) info->base);
		kfree(info);
	} else {
		wake_up_interruptible(PIPE_WAIT(*inode));
	}
	up(PIPE_SEM(*inode));

	return 0;
}

static int
pipe_read_release(struct inode *inode, struct file *filp)
{
	return pipe_release(inode, 1, 0);
}

static int
pipe_write_release(struct inode *inode, struct file *filp)
{
	return pipe_release(inode, 0, 1);
}

static int
pipe_rdwr_release(struct inode *inode, struct file *filp)
{
	int decr, decw;

	decr = (filp->f_mode & FMODE_READ) != 0;
	decw = (filp->f_mode & FMODE_WRITE) != 0;
	return pipe_release(inode, decr, decw);
}

static int
pipe_read_open(struct inode *inode, struct file *filp)
{
	/* We could have perhaps used atomic_t, but this and friends
	   below are the only places.  So it doesn't seem worthwhile.  */
	down(PIPE_SEM(*inode));
	PIPE_READERS(*inode)++;
	up(PIPE_SEM(*inode));

	return 0;
}

static int
pipe_write_open(struct inode *inode, struct file *filp)
{
	down(PIPE_SEM(*inode));
	PIPE_WRITERS(*inode)++;
	up(PIPE_SEM(*inode));

	return 0;
}

static int
pipe_rdwr_open(struct inode *inode, struct file *filp)
{
	down(PIPE_SEM(*inode));
	if (filp->f_mode & FMODE_READ)
		PIPE_READERS(*inode)++;
	if (filp->f_mode & FMODE_WRITE)
		PIPE_WRITERS(*inode)++;
	up(PIPE_SEM(*inode));

	return 0;
}

/*
 * The file_operations structs are not static because they
 * are also used in linux/fs/fifo.c to do operations on FIFOs.
 */
struct file_operations connecting_fifo_fops = {
	llseek:		pipe_lseek,
	read:		connect_read,
	write:		bad_pipe_w,
	poll:		connect_poll,
	ioctl:		pipe_ioctl,
	open:		pipe_read_open,
	release:	pipe_read_release,
};

struct file_operations read_fifo_fops = {
	llseek:		pipe_lseek,
	read:		pipe_read,
	write:		bad_pipe_w,
	poll:		fifo_poll,
	ioctl:		pipe_ioctl,
	open:		pipe_read_open,
	release:	pipe_read_release,
};

struct file_operations write_fifo_fops = {
	llseek:		pipe_lseek,
	read:		bad_pipe_r,
	write:		pipe_write,
	poll:		fifo_poll,
	ioctl:		pipe_ioctl,
	open:		pipe_write_open,
	release:	pipe_write_release,
};

struct file_operations rdwr_fifo_fops = {
	llseek:		pipe_lseek,
	read:		pipe_read,
	write:		pipe_write,
	poll:		fifo_poll,
	ioctl:		pipe_ioctl,
	open:		pipe_rdwr_open,
	release:	pipe_rdwr_release,
};

struct file_operations read_pipe_fops = {
	llseek:		pipe_lseek,
	read:		pipe_read,
	write:		bad_pipe_w,
	poll:		pipe_poll,
	ioctl:		pipe_ioctl,
	open:		pipe_read_open,
	release:	pipe_read_release,
};

struct file_operations write_pipe_fops = {
	llseek:		pipe_lseek,
	read:		bad_pipe_r,
	write:		pipe_write,
	poll:		pipe_poll,
	ioctl:		pipe_ioctl,
	open:		pipe_write_open,
	release:	pipe_write_release,
};

struct file_operations rdwr_pipe_fops = {
	llseek:		pipe_lseek,
	read:		pipe_read,
	write:		pipe_write,
	poll:		pipe_poll,
	ioctl:		pipe_ioctl,
	open:		pipe_rdwr_open,
	release:	pipe_rdwr_release,
};

static struct inode * get_pipe_inode(void)
{
	struct inode *inode = get_empty_inode();
	unsigned long page;

	if (!inode)
		goto fail_inode;

	page = __get_free_page(GFP_USER);
	if (!page)
		goto fail_iput;

	inode->i_pipe = kmalloc(sizeof(struct pipe_inode_info), GFP_KERNEL);
	if (!inode->i_pipe)
		goto fail_page;

	inode->i_fop = &rdwr_pipe_fops;

	init_waitqueue_head(PIPE_WAIT(*inode));
	PIPE_BASE(*inode) = (char *) page;
	PIPE_START(*inode) = PIPE_LEN(*inode) = 0;
	PIPE_READERS(*inode) = PIPE_WRITERS(*inode) = 1;
	PIPE_WAITING_READERS(*inode) = PIPE_WAITING_WRITERS(*inode) = 0;

	/*
	 * Mark the inode dirty from the very beginning,
	 * that way it will never be moved to the dirty
	 * list because "mark_inode_dirty()" will think
	 * that it already _is_ on the dirty list.
	 */
	inode->i_state = I_DIRTY;
	inode->i_mode = S_IFIFO | S_IRUSR | S_IWUSR;
	inode->i_uid = current->fsuid;
	inode->i_gid = current->fsgid;
	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	inode->i_blksize = PAGE_SIZE;
	return inode;

fail_page:
	free_page(page);
fail_iput:
	iput(inode);
fail_inode:
	return NULL;
}

int do_pipe(int *fd)
{
	struct inode * inode;
	struct file *f1, *f2;
	int error;
	int i,j;

	error = -ENFILE;
	f1 = get_empty_filp();
	if (!f1)
		goto no_files;

	f2 = get_empty_filp();
	if (!f2)
		goto close_f1;

	inode = get_pipe_inode();
	if (!inode)
		goto close_f12;

	error = get_unused_fd();
	if (error < 0)
		goto close_f12_inode;
	i = error;

	error = get_unused_fd();
	if (error < 0)
		goto close_f12_inode_i;
	j = error;

	error = -ENOMEM;
	f1->f_dentry = f2->f_dentry = dget(d_alloc_root(inode));
	if (!f1->f_dentry)
		goto close_f12_inode_i_j;

	/* read file */
	f1->f_pos = f2->f_pos = 0;
	f1->f_flags = O_RDONLY;
	f1->f_op = &read_pipe_fops;
	f1->f_mode = 1;

	/* write file */
	f2->f_flags = O_WRONLY;
	f2->f_op = &write_pipe_fops;
	f2->f_mode = 2;

	fd_install(i, f1);
	fd_install(j, f2);
	fd[0] = i;
	fd[1] = j;
	return 0;

close_f12_inode_i_j:
	put_unused_fd(j);
close_f12_inode_i:
	put_unused_fd(i);
close_f12_inode:
	free_page((unsigned long) PIPE_BASE(*inode));
	kfree(inode->i_pipe);
	inode->i_pipe = NULL;
	iput(inode);
close_f12:
	put_filp(f2);
close_f1:
	put_filp(f1);
no_files:
	return error;	
}
