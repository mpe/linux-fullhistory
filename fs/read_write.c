/*
 *  linux/fs/read_write.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/dirent.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/minix_fs.h>
#include <asm/segment.h>

/*
 * Count is not yet used: but we'll probably support reading several entries
 * at once in the future. Use count=1 in the library for future expansions.
 */
int sys_readdir(unsigned int fd, struct dirent * dirent, unsigned int count)
{
	struct file * file;
	struct inode * inode;

	if (fd >= NR_OPEN || !(file = current->filp[fd]) ||
	    !(inode = file->f_inode))
		return -EBADF;
	if (file->f_op && file->f_op->readdir) {
		verify_area(dirent, sizeof (*dirent));
		return file->f_op->readdir(inode,file,dirent);
	}
	return -EBADF;
}

int sys_lseek(unsigned int fd, off_t offset, unsigned int origin)
{
	struct file * file;
	int tmp, mem_dev;

	if (fd >= NR_OPEN || !(file=current->filp[fd]) || !(file->f_inode))
		return -EBADF;
	if (origin > 2)
		return -EINVAL;
	if (file->f_inode->i_pipe)
		return -ESPIPE;
	if (file->f_op && file->f_op->lseek)
		return file->f_op->lseek(file->f_inode,file,offset,origin);
	mem_dev = S_ISCHR(file->f_inode->i_mode);

/* this is the default handler if no lseek handler is present */
	switch (origin) {
		case 0:
			if (offset<0 && !mem_dev) return -EINVAL;
			file->f_pos=offset;
			break;
		case 1:
			if (file->f_pos+offset<0 && !mem_dev) return -EINVAL;
			file->f_pos += offset;
			break;
		case 2:
			if ((tmp=file->f_inode->i_size+offset)<0 && !mem_dev)
				return -EINVAL;
			file->f_pos = tmp;
	}
	if (mem_dev && file->f_pos < 0)
		return 0;
	return file->f_pos;
}

int sys_read(unsigned int fd,char * buf,unsigned int count)
{
	struct file * file;
	struct inode * inode;

	if (fd>=NR_OPEN || !(file=current->filp[fd]) || !(inode=file->f_inode))
		return -EBADF;
	if (!(file->f_mode & 1))
		return -EBADF;
	if (!count)
		return 0;
	verify_area(buf,count);
	if (file->f_op && file->f_op->read)
		return file->f_op->read(inode,file,buf,count);
/* these are the default read-functions */
	if (inode->i_pipe)
		return pipe_read(inode,file,buf,count);
	if (S_ISCHR(inode->i_mode))
		return char_read(inode,file,buf,count);
	if (S_ISBLK(inode->i_mode))
		return block_read(inode,file,buf,count);
	if (S_ISDIR(inode->i_mode) || S_ISREG(inode->i_mode))
		return minix_file_read(inode,file,buf,count);
	printk("(Read)inode->i_mode=%06o\n\r",inode->i_mode);
	return -EINVAL;
}

int sys_write(unsigned int fd,char * buf,unsigned int count)
{
	struct file * file;
	struct inode * inode;
	
	if (fd>=NR_OPEN || !(file=current->filp[fd]) || !(inode=file->f_inode))
		return -EBADF;
	if (!(file->f_mode&2))
		return -EBADF;
	if (!count)
		return 0;
	if (file->f_op && file->f_op->write)
		return file->f_op->write(inode,file,buf,count);
/* these are the default read-functions */
	if (inode->i_pipe)
		return pipe_write(inode,file,buf,count);
	if (S_ISCHR(inode->i_mode))
		return char_write(inode,file,buf,count);
	if (S_ISBLK(inode->i_mode))
		return block_write(inode,file,buf,count);
	if (S_ISREG(inode->i_mode))
		return minix_file_write(inode,file,buf,count);
	printk("(Write)inode->i_mode=%06o\n\r",inode->i_mode);
	return -EINVAL;
}
