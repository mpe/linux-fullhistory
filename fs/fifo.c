/*
 *  linux/fs/fifo.c
 *
 *  written by Paul H. Hargrove
 *
 *  Fixes:
 *	10-06-1999, AV: fixed OOM handling in fifo_open(), moved
 *			initialization there, switched to external
 *			allocation of pipe_inode_info.
 */

#include <linux/mm.h>
#include <linux/malloc.h>

static int fifo_open(struct inode *inode, struct file *filp)
{
	int ret;

	ret = -ERESTARTSYS;
	if (down_interruptible(PIPE_SEM(*inode)))
		goto err_nolock_nocleanup;

	if (! inode->i_pipe) {
		unsigned long page;
		struct pipe_inode_info *info;

		info = kmalloc(sizeof(struct pipe_inode_info),GFP_KERNEL);

		ret = -ENOMEM;
		if (!info)
			goto err_nocleanup;
		page = __get_free_page(GFP_KERNEL);
		if (!page) {
			kfree(info);
			goto err_nocleanup;
		}

		inode->i_pipe = info;

		init_waitqueue_head(PIPE_WAIT(*inode));
		PIPE_BASE(*inode) = (char *) page;
		PIPE_START(*inode) = PIPE_LEN(*inode) = 0;
		PIPE_READERS(*inode) = PIPE_WRITERS(*inode) = 0;
	}

	switch (filp->f_mode) {
	case 1:
	/*
	 *  O_RDONLY
	 *  POSIX.1 says that O_NONBLOCK means return with the FIFO
	 *  opened, even when there is no process writing the FIFO.
	 */
		filp->f_op = &connecting_fifo_fops;
		if (PIPE_READERS(*inode)++ == 0)
			wake_up_interruptible(PIPE_WAIT(*inode));

		if (!(filp->f_flags & O_NONBLOCK)) {
			while (!PIPE_WRITERS(*inode)) {
				if (signal_pending(current))
					goto err_rd;
				up(PIPE_SEM(*inode));
				interruptible_sleep_on(PIPE_WAIT(*inode));

				/* Note that using down_interruptible here
				   and similar places below is pointless,
				   since we have to acquire the lock to clean
				   up properly.  */
				down(PIPE_SEM(*inode));
			}
		}

		if (PIPE_WRITERS(*inode))
			filp->f_op = &read_fifo_fops;
		break;
	
	case 2:
	/*
	 *  O_WRONLY
	 *  POSIX.1 says that O_NONBLOCK means return -1 with
	 *  errno=ENXIO when there is no process reading the FIFO.
	 */
		ret = -ENXIO;
		if ((filp->f_flags & O_NONBLOCK) && !PIPE_READERS(*inode))
			goto err;

		filp->f_op = &write_fifo_fops;
		if (!PIPE_WRITERS(*inode)++)
			wake_up_interruptible(PIPE_WAIT(*inode));

		while (!PIPE_READERS(*inode)) {
			if (signal_pending(current))
				goto err_wr;
			up(PIPE_SEM(*inode));
			interruptible_sleep_on(PIPE_WAIT(*inode));
			down(PIPE_SEM(*inode));
		}
		break;
	
	case 3:
	/*
	 *  O_RDWR
	 *  POSIX.1 leaves this case "undefined" when O_NONBLOCK is set.
	 *  This implementation will NEVER block on a O_RDWR open, since
	 *  the process can at least talk to itself.
	 */
		filp->f_op = &rdwr_fifo_fops;

		PIPE_READERS(*inode)++;
		PIPE_WRITERS(*inode)++;
		if (PIPE_READERS(*inode) == 1 || PIPE_WRITERS(*inode) == 1)
			wake_up_interruptible(PIPE_WAIT(*inode));
		break;

	default:
		ret = -EINVAL;
		goto err;
	}

	/* Ok! */
	up(PIPE_SEM(*inode));
	return 0;

err_rd:
	if (!--PIPE_READERS(*inode))
		wake_up_interruptible(PIPE_WAIT(*inode));
	ret = -ERESTARTSYS;
	goto err;

err_wr:
	if (!--PIPE_WRITERS(*inode))
		wake_up_interruptible(PIPE_WAIT(*inode));
	ret = -ERESTARTSYS;
	goto err;

err:
	if (!PIPE_READERS(*inode) && !PIPE_WRITERS(*inode)) {
		struct pipe_inode_info *info = inode->i_pipe;
		inode->i_pipe = NULL;
		free_page((unsigned long)info->base);
		kfree(info);
	}

err_nocleanup:
	up(PIPE_SEM(*inode));

err_nolock_nocleanup:
	return ret;
}

/*
 * Dummy default file-operations: the only thing this does
 * is contain the open that then fills in the correct operations
 * depending on the access mode of the file...
 */
static struct file_operations def_fifo_fops = {
	open:		fifo_open,	/* will set read or write pipe_fops */
};

struct inode_operations fifo_inode_operations = {
	&def_fifo_fops,		/* default file operations */
};
