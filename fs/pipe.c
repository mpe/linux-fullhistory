/*
 *  linux/fs/pipe.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <signal.h>
#include <errno.h>
#include <termios.h>
#include <fcntl.h>

#include <asm/segment.h>

#include <linux/sched.h>
#include <linux/kernel.h>

static int pipe_read(struct inode * inode, struct file * filp, char * buf, int count)
{
	int chars, size, read = 0;

	if (!(filp->f_flags & O_NONBLOCK))
		while (!PIPE_SIZE(*inode)) {
			wake_up(& PIPE_WRITE_WAIT(*inode));
			if (inode->i_count != 2) /* are there any writers? */
				return 0;
			if (current->signal & ~current->blocked)
				return -ERESTARTSYS;
			interruptible_sleep_on(& PIPE_READ_WAIT(*inode));
		}
	while (count>0 && (size = PIPE_SIZE(*inode))) {
		chars = PAGE_SIZE-PIPE_TAIL(*inode);
		if (chars > count)
			chars = count;
		if (chars > size)
			chars = size;
		count -= chars;
		read += chars;
		size = PIPE_TAIL(*inode);
		PIPE_TAIL(*inode) += chars;
		PIPE_TAIL(*inode) &= (PAGE_SIZE-1);
		while (chars-->0)
			put_fs_byte(((char *)inode->i_size)[size++],buf++);
	}
	wake_up(& PIPE_WRITE_WAIT(*inode));
	return read?read:-EAGAIN;
}
	
static int pipe_write(struct inode * inode, struct file * filp, char * buf, int count)
{
	int chars, size, written = 0;

	while (count>0) {
		while (!(size=(PAGE_SIZE-1)-PIPE_SIZE(*inode))) {
			wake_up(& PIPE_READ_WAIT(*inode));
			if (inode->i_count != 2) { /* no readers */
				current->signal |= (1<<(SIGPIPE-1));
				return written?written:-EINTR;
			}
			if (current->signal & ~current->blocked)
				return written?written:-EINTR;
			interruptible_sleep_on(&PIPE_WRITE_WAIT(*inode));
		}
		chars = PAGE_SIZE-PIPE_HEAD(*inode);
		if (chars > count)
			chars = count;
		if (chars > size)
			chars = size;
		count -= chars;
		written += chars;
		size = PIPE_HEAD(*inode);
		PIPE_HEAD(*inode) += chars;
		PIPE_HEAD(*inode) &= (PAGE_SIZE-1);
		while (chars-->0)
			((char *)inode->i_size)[size++]=get_fs_byte(buf++);
	}
	wake_up(& PIPE_READ_WAIT(*inode));
	return written;
}

static int pipe_lseek(struct inode * inode, struct file * file, off_t offset, int orig)
{
	return -ESPIPE;
}

static int pipe_readdir(struct inode * inode, struct file * file, struct dirent * de, int count)
{
	return -ENOTDIR;
}

static int bad_pipe_rw(struct inode * inode, struct file * filp, char * buf, int count)
{
	return -EBADF;
}

static int pipe_ioctl(struct inode *pino, struct file * filp,
	unsigned int cmd, unsigned int arg)
{
	switch (cmd) {
		case FIONREAD:
			verify_area((void *) arg,4);
			put_fs_long(PIPE_SIZE(*pino),(unsigned long *) arg);
			return 0;
		default:
			return -EINVAL;
	}
}

/*
 * Ok, these two routines should keep track of readers/writers,
 * but it's currently done with the inode->i_count checking.
 */
static void pipe_read_release(struct inode * inode, struct file * filp)
{
}

static void pipe_write_release(struct inode * inode, struct file * filp)
{
}

static struct file_operations read_pipe_fops = {
	pipe_lseek,
	pipe_read,
	bad_pipe_rw,
	pipe_readdir,
	NULL,		/* pipe_select */
	pipe_ioctl,
	NULL,		/* no special open code */
	pipe_read_release
};

static struct file_operations write_pipe_fops = {
	pipe_lseek,
	bad_pipe_rw,
	pipe_write,
	pipe_readdir,
	NULL,		/* pipe_select */
	pipe_ioctl,
	NULL,		/* no special open code */
	pipe_write_release
};

int sys_pipe(unsigned long * fildes)
{
	struct inode * inode;
	struct file * f[2];
	int fd[2];
	int i,j;

	j=0;
	for(i=0;j<2 && i<NR_FILE;i++)
		if (!file_table[i].f_count)
			(f[j++]=i+file_table)->f_count++;
	if (j==1)
		f[0]->f_count=0;
	if (j<2)
		return -1;
	j=0;
	for(i=0;j<2 && i<NR_OPEN;i++)
		if (!current->filp[i]) {
			current->filp[ fd[j]=i ] = f[j];
			j++;
		}
	if (j==1)
		current->filp[fd[0]]=NULL;
	if (j<2) {
		f[0]->f_count=f[1]->f_count=0;
		return -1;
	}
	if (!(inode=get_pipe_inode())) {
		current->filp[fd[0]] =
			current->filp[fd[1]] = NULL;
		f[0]->f_count = f[1]->f_count = 0;
		return -1;
	}
	f[0]->f_inode = f[1]->f_inode = inode;
	f[0]->f_pos = f[1]->f_pos = 0;
	f[0]->f_op = &read_pipe_fops;
	f[0]->f_mode = 1;		/* read */
	f[1]->f_op = &write_pipe_fops;
	f[1]->f_mode = 2;		/* write */
	put_fs_long(fd[0],0+fildes);
	put_fs_long(fd[1],1+fildes);
	return 0;
}
