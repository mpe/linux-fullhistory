/*
 * Wrapper functions for accessing the file_struct fd array.
 */

#ifndef __LINUX_FILE_H
#define __LINUX_FILE_H

extern void __fput(struct file *);
extern void insert_file_free(struct file *file);

/*
 * Check whether the specified task has the fd open. Since the task
 * may not have a files_struct, we must test for p->files != NULL.
 */
extern inline struct file * fcheck_task(struct task_struct *p, unsigned int fd)
{
	struct file * file = NULL;

	if (p->files && fd < p->files->max_fds)
		file = p->files->fd[fd];
	return file;
}

/*
 * Check whether the specified fd has an open file.
 */
extern inline struct file * fcheck(unsigned int fd)
{
	struct file * file = NULL;

	if (fd < current->files->max_fds)
		file = current->files->fd[fd];
	return file;
}

extern inline struct file * fget(unsigned int fd)
{
	struct file * file = fcheck(fd);

	if (file)
		file->f_count++;
	return file;
}

/*
 * Install a file pointer in the fd array.
 */
extern inline void fd_install(unsigned int fd, struct file *file)
{
	current->files->fd[fd] = file;
}

/* It does not matter which list it is on. */
extern inline void remove_filp(struct file *file)
{
	if(file->f_next)
		file->f_next->f_pprev = file->f_pprev;
	*file->f_pprev = file->f_next;
}

extern inline void fput(struct file *file)
{
	int count = file->f_count-1;

	if (!count) {
		locks_remove_flock(file);
		__fput(file);
		file->f_count = 0;
		remove_filp(file);
		insert_file_free(file);
	} else
		file->f_count = count;
}

extern inline void put_filp(struct file *file)
{
	if(--file->f_count == 0) {
		remove_filp(file);
		insert_file_free(file);
	}
}

#endif
