/*
 * Wrapper functions for accessing the file_struct fd array.
 */

#ifndef __LINUX_FILE_H
#define __LINUX_FILE_H

extern void _fput(struct file *);

/*
 * Check whether the specified task has the fd open. Since the task
 * may not have a files_struct, we must test for p->files != NULL.
 */
extern inline struct file * fcheck_task(struct task_struct *p, unsigned int fd)
{
	struct file * file = NULL;

	if (fd < p->files->max_fds)
		file = p->files->fd[fd];
	return file;
}

/*
 * Check whether the specified fd has an open file.
 */
extern inline struct file * fcheck(unsigned int fd)
{
	struct file * file = NULL;
	struct files_struct *files = current->files;

	if (fd < files->max_fds)
		file = files->fd[fd];
	return file;
}

extern inline struct file * frip(unsigned int fd)
{
	struct file * file = NULL;

	if (fd < current->files->max_fds)
		file = xchg(&current->files->fd[fd], NULL);
	return file;
}

extern inline struct file * fget(unsigned int fd)
{
	struct file * file = NULL;
	struct files_struct *files = current->files;

	read_lock(&files->file_lock);
	file = fcheck(fd);
	if (file)
		get_file(file);
	read_unlock(&files->file_lock);
	return file;
}

/*
 * Install a file pointer in the fd array.
 */
extern inline void fd_install(unsigned int fd, struct file * file)
{
	struct files_struct *files = current->files;

	write_lock(&files->file_lock);
	files->fd[fd] = file;
	write_unlock(&files->file_lock);
}

/*
 * 23/12/1998 Marcin Dalecki <dalecki@cs.net.pl>: 
 * 
 * Since those functions where calling other functions, it was compleatly 
 * bogous to make them all "extern inline".
 *
 * The removal of this pseudo optimization saved me scandaleous:
 *
 * 		3756 (i386 arch) 
 *
 * precious bytes from my kernel, even without counting all the code compiled
 * as module!
 *
 * I suspect there are many other similar "optimizations" across the
 * kernel...
 */
extern inline void fput(struct file * file)
{
	if (atomic_dec_and_test(&file->f_count))
		_fput(file);
}
extern void put_filp(struct file *);

#endif /* __LINUX_FILE_H */
