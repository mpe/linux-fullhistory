#ifndef __LINUX_FILE_H
#define __LINUX_FILE_H

extern inline struct file * fget(unsigned long fd)
{
	struct file * file = NULL;
	if (fd < NR_OPEN) {
		file = current->files->fd[fd];
		if (file)
			file->f_count++;
	}
	return file;
}

extern void __fput(struct file *, struct inode *);

extern inline void fput(struct file *file, struct inode *inode)
{
	int count = file->f_count-1;
	if (!count)
		__fput(file, inode);
	file->f_count = count;
}

#endif
