/*
 * Wrapper functions for accessing the file_struct fd array.
 */

#ifndef __LINUX_FILE_H
#define __LINUX_FILE_H

extern void _fput(struct file *);

static inline struct file * fcheck_files(struct files_struct *files, unsigned int fd)
{
	struct file * file = NULL;

	if (fd < files->max_fds)
		file = files->fd[fd];
	return file;
}

/*
 * Check whether the specified fd has an open file.
 */
static inline struct file * fcheck(unsigned int fd)
{
	struct file * file = NULL;
	struct files_struct *files = current->files;

	if (fd < files->max_fds)
		file = files->fd[fd];
	return file;
}

static inline struct file * frip(struct files_struct *files, unsigned int fd)
{
	struct file * file = NULL;

	if (fd < files->max_fds)
		file = xchg(&files->fd[fd], NULL);
	return file;
}

static inline struct file * fget(unsigned int fd)
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
 * 23/12/1998 Marcin Dalecki <dalecki@cs.net.pl>: 
 * 
 * Since those functions where calling other functions, it was completely 
 * bogus to make them all "extern inline".
 *
 * The removal of this pseudo optimization saved me scandalous:
 *
 * 		3756 (i386 arch) 
 *
 * precious bytes from my kernel, even without counting all the code compiled
 * as module!
 *
 * I suspect there are many other similar "optimizations" across the
 * kernel...
 */
static inline void fput(struct file * file)
{
	if (atomic_dec_and_test(&file->f_count))
		_fput(file);
}
extern void put_filp(struct file *);

/*
 * Install a file pointer in the fd array.  
 *
 * The VFS is full of places where we drop the files lock between
 * setting the open_fds bitmap and installing the file in the file
 * array.  At any such point, we are vulnerable to a dup2() race
 * installing a file in the array before us.  We need to detect this and
 * fput() the struct file we are about to overwrite in this case.
 */

static inline void fd_install(unsigned int fd, struct file * file)
{
	struct files_struct *files = current->files;
	struct file * result;
	
	write_lock(&files->file_lock);
	result = xchg(&files->fd[fd], file);
	write_unlock(&files->file_lock);
	if (result)
		fput(result);
}

void put_files_struct(struct files_struct *fs);

#endif /* __LINUX_FILE_H */
