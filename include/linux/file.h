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

extern int __fput(struct file *);
extern void insert_file_free(struct file *file);

/* It does not matter which list it is on. */
extern inline void remove_filp(struct file *file)
{
	if(file->f_next)
		file->f_next->f_pprev = file->f_pprev;
	*file->f_pprev = file->f_next;
}

extern inline int fput(struct file *file)
{
	int count = file->f_count-1;
	int error = 0;

	if (!count) {
		locks_remove_flock(file);
		error = __fput(file);
		file->f_count = 0;
		remove_filp(file);
		insert_file_free(file);
	} else
		file->f_count = count;
	return error;
}

extern inline void put_filp(struct file *file)
{
	if(--file->f_count == 0) {
		remove_filp(file);
		insert_file_free(file);
	}
}

#endif
