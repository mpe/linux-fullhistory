/*
 *  linux/fs/pipe.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/fcntl.h>
#include <linux/termios.h>
#include <linux/mm.h>
#include <linux/file.h>

#include <asm/poll.h>
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

/* We don't use the head/tail construction any more. Now we use the start/len*/
/* construction providing full use of PIPE_BUF (multiple of PAGE_SIZE) */
/* Florian Coosmann (FGC)                                ^ current = 1       */
/* Additionally, we now use locking technique. This prevents race condition  */
/* in case of paging and multiple read/write on the same pipe. (FGC)         */


static long pipe_read(struct inode * inode, struct file * filp,
	char * buf, unsigned long count)
{
	int chars = 0, size = 0, read = 0;
        char *pipebuf;

	if (filp->f_flags & O_NONBLOCK) {
		if (PIPE_LOCK(*inode))
			return -EAGAIN;
		if (PIPE_EMPTY(*inode))
			if (PIPE_WRITERS(*inode))
				return -EAGAIN;
			else
				return 0;
	} else while (PIPE_EMPTY(*inode) || PIPE_LOCK(*inode)) {
		if (PIPE_EMPTY(*inode)) {
			if (!PIPE_WRITERS(*inode))
				return 0;
		}
		if (current->signal & ~current->blocked)
			return -ERESTARTSYS;
		interruptible_sleep_on(&PIPE_WAIT(*inode));
	}
	PIPE_LOCK(*inode)++;
	while (count>0 && (size = PIPE_SIZE(*inode))) {
		chars = PIPE_MAX_RCHUNK(*inode);
		if (chars > count)
			chars = count;
		if (chars > size)
			chars = size;
		read += chars;
                pipebuf = PIPE_BASE(*inode)+PIPE_START(*inode);
		PIPE_START(*inode) += chars;
		PIPE_START(*inode) &= (PIPE_BUF-1);
		PIPE_LEN(*inode) -= chars;
		count -= chars;
		copy_to_user(buf, pipebuf, chars );
		buf += chars;
	}
	PIPE_LOCK(*inode)--;
	wake_up_interruptible(&PIPE_WAIT(*inode));
	if (read) {
		UPDATE_ATIME(inode);
		return read;
	}
	if (PIPE_WRITERS(*inode))
		return -EAGAIN;
	return 0;
}
	
static long pipe_write(struct inode * inode, struct file * filp,
	const char * buf, unsigned long count)
{
	int chars = 0, free = 0, written = 0;
	char *pipebuf;

	if (!PIPE_READERS(*inode)) { /* no readers */
		send_sig(SIGPIPE,current,0);
		return -EPIPE;
	}
/* if count <= PIPE_BUF, we have to make it atomic */
	if (count <= PIPE_BUF)
		free = count;
	else
		free = 1; /* can't do it atomically, wait for any free space */
	while (count>0) {
		while ((PIPE_FREE(*inode) < free) || PIPE_LOCK(*inode)) {
			if (!PIPE_READERS(*inode)) { /* no readers */
				send_sig(SIGPIPE,current,0);
				return written? :-EPIPE;
			}
			if (current->signal & ~current->blocked)
				return written? :-ERESTARTSYS;
			if (filp->f_flags & O_NONBLOCK)
				return written? :-EAGAIN;
			interruptible_sleep_on(&PIPE_WAIT(*inode));
		}
		PIPE_LOCK(*inode)++;
		while (count>0 && (free = PIPE_FREE(*inode))) {
			chars = PIPE_MAX_WCHUNK(*inode);
			if (chars > count)
				chars = count;
			if (chars > free)
				chars = free;
                        pipebuf = PIPE_BASE(*inode)+PIPE_END(*inode);
			written += chars;
			PIPE_LEN(*inode) += chars;
			count -= chars;
			copy_from_user(pipebuf, buf, chars );
			buf += chars;
		}
		PIPE_LOCK(*inode)--;
		wake_up_interruptible(&PIPE_WAIT(*inode));
		free = 1;
	}
	inode->i_ctime = inode->i_mtime = CURRENT_TIME;
	mark_inode_dirty(inode);
	return written;
}

static long long pipe_lseek(struct inode * inode, struct file * file,
	long long offset, int orig)
{
	return -ESPIPE;
}

static long bad_pipe_r(struct inode * inode, struct file * filp,
	char * buf, unsigned long count)
{
	return -EBADF;
}

static long bad_pipe_w(struct inode * inode, struct file * filp,
	const char * buf, unsigned long count)
{
	return -EBADF;
}

static int pipe_ioctl(struct inode *pino, struct file * filp,
	unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
		case FIONREAD:
			return put_user(PIPE_SIZE(*pino),(int *) arg);
		default:
			return -EINVAL;
	}
}

static unsigned int pipe_poll(struct file * filp, poll_table * wait)
{
	unsigned int mask;
	struct inode * inode = filp->f_dentry->d_inode;

	poll_wait(&PIPE_WAIT(*inode), wait);
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
 * Arggh. Why does SunOS have to have different select() behaviour
 * for pipes and fifos? Hate-Hate-Hate. SunOS lacks POLLHUP..
 */
static unsigned int fifo_poll(struct file * filp, poll_table * wait)
{
	unsigned int mask;
	struct inode * inode = filp->f_dentry->d_inode;

	poll_wait(&PIPE_WAIT(*inode), wait);
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
static long connect_read(struct inode * inode, struct file * filp,
	char * buf, unsigned long count)
{
	if (PIPE_EMPTY(*inode) && !PIPE_WRITERS(*inode))
		return 0;
	filp->f_op = &read_fifo_fops;
	return pipe_read(inode,filp,buf,count);
}

static unsigned int connect_poll(struct file * filp, poll_table * wait)
{
	struct inode * inode = filp->f_dentry->d_inode;

	poll_wait(&PIPE_WAIT(*inode), wait);
	if (!PIPE_EMPTY(*inode)) {
		filp->f_op = &read_fifo_fops;
		return POLLIN | POLLRDNORM;
	}
	if (PIPE_WRITERS(*inode))
		filp->f_op = &read_fifo_fops;
	return POLLOUT | POLLWRNORM;
}

static int pipe_read_release(struct inode * inode, struct file * filp)
{
	PIPE_READERS(*inode)--;
	wake_up_interruptible(&PIPE_WAIT(*inode));
	return 0;
}

static int pipe_write_release(struct inode * inode, struct file * filp)
{
	PIPE_WRITERS(*inode)--;
	wake_up_interruptible(&PIPE_WAIT(*inode));
	return 0;
}

static int pipe_rdwr_release(struct inode * inode, struct file * filp)
{
	if (filp->f_mode & FMODE_READ)
		PIPE_READERS(*inode)--;
	if (filp->f_mode & FMODE_WRITE)
		PIPE_WRITERS(*inode)--;
	wake_up_interruptible(&PIPE_WAIT(*inode));
	return 0;
}

static int pipe_read_open(struct inode * inode, struct file * filp)
{
	PIPE_READERS(*inode)++;
	return 0;
}

static int pipe_write_open(struct inode * inode, struct file * filp)
{
	PIPE_WRITERS(*inode)++;
	return 0;
}

static int pipe_rdwr_open(struct inode * inode, struct file * filp)
{
	if (filp->f_mode & FMODE_READ)
		PIPE_READERS(*inode)++;
	if (filp->f_mode & FMODE_WRITE)
		PIPE_WRITERS(*inode)++;
	return 0;
}

/*
 * The file_operations structs are not static because they
 * are also used in linux/fs/fifo.c to do operations on fifo's.
 */
struct file_operations connecting_fifo_fops = {
	pipe_lseek,
	connect_read,
	bad_pipe_w,
	NULL,		/* no readdir */
	connect_poll,
	pipe_ioctl,
	NULL,		/* no mmap on pipes.. surprise */
	pipe_read_open,
	pipe_read_release,
	NULL
};

struct file_operations read_fifo_fops = {
	pipe_lseek,
	pipe_read,
	bad_pipe_w,
	NULL,		/* no readdir */
	fifo_poll,
	pipe_ioctl,
	NULL,		/* no mmap on pipes.. surprise */
	pipe_read_open,
	pipe_read_release,
	NULL
};

struct file_operations write_fifo_fops = {
	pipe_lseek,
	bad_pipe_r,
	pipe_write,
	NULL,		/* no readdir */
	fifo_poll,
	pipe_ioctl,
	NULL,		/* mmap */
	pipe_write_open,
	pipe_write_release,
	NULL
};

struct file_operations rdwr_fifo_fops = {
	pipe_lseek,
	pipe_read,
	pipe_write,
	NULL,		/* no readdir */
	fifo_poll,
	pipe_ioctl,
	NULL,		/* mmap */
	pipe_rdwr_open,
	pipe_rdwr_release,
	NULL
};

struct file_operations read_pipe_fops = {
	pipe_lseek,
	pipe_read,
	bad_pipe_w,
	NULL,		/* no readdir */
	pipe_poll,
	pipe_ioctl,
	NULL,		/* no mmap on pipes.. surprise */
	pipe_read_open,
	pipe_read_release,
	NULL
};

struct file_operations write_pipe_fops = {
	pipe_lseek,
	bad_pipe_r,
	pipe_write,
	NULL,		/* no readdir */
	pipe_poll,
	pipe_ioctl,
	NULL,		/* mmap */
	pipe_write_open,
	pipe_write_release,
	NULL
};

struct file_operations rdwr_pipe_fops = {
	pipe_lseek,
	pipe_read,
	pipe_write,
	NULL,		/* no readdir */
	pipe_poll,
	pipe_ioctl,
	NULL,		/* mmap */
	pipe_rdwr_open,
	pipe_rdwr_release,
	NULL
};

struct inode_operations pipe_inode_operations = {
	&rdwr_pipe_fops,
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* readlink */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};

int do_pipe(int *fd)
{
	struct inode * inode;
	struct file *f1, *f2;
	int error;
	int i,j;

	error = ENFILE;
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

	f1->f_dentry = f2->f_dentry = dget(d_alloc_root(inode, NULL));

	/* read file */
	f1->f_pos = f2->f_pos = 0;
	f1->f_flags = O_RDONLY;
	f1->f_op = &read_pipe_fops;
	f1->f_mode = 1;

	/* write file */
	f2->f_flags = O_WRONLY;
	f2->f_op = &write_pipe_fops;
	f2->f_mode = 2;
	current->files->fd[i] = f1;
	current->files->fd[j] = f2;
	fd[0] = i;
	fd[1] = j;
	return 0;

close_f12_inode_i:
	put_unused_fd(i);
close_f12_inode:
	iput(inode);
close_f12:
	put_filp(f2);
close_f1:
	put_filp(f1);
no_files:
	return error;	
}
