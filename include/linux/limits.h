#ifndef _LINUX_LIMITS_H
#define _LINUX_LIMITS_H

#define NAME_MAX 255

/*
 * It's silly to have NR_OPEN bigger than NR_FILE, but I'll fix
 * that later. Anyway, now the file code is no longer dependent
 * on bitmaps in unsigned longs, but uses the new fd_set structure..
 *
 * Some programs (notably those using select()) may have to be 
 * recompiled to take full advantage of the new limits..
 */
#define NR_OPEN 256
#define NR_INODE 128
#define NR_FILE 128
#define NR_SUPER 16
#define NR_HASH 997
#define NR_FILE_LOCKS 32
#define BLOCK_SIZE 1024
#define BLOCK_SIZE_BITS 10
#define MAX_CHRDEV 32
#define MAX_BLKDEV 32

#define NGROUPS_MAX       32	/* supplemental group IDs are available */
#define ARG_MAX       131072	/* # bytes of args + environ for exec() */
#define CHILD_MAX        999    /* no limit :-) */
#define OPEN_MAX         256	/* # open files a process may have */
#define LINK_MAX         127	/* # links a file may have */
#define MAX_CANON        255	/* size of the canonical input queue */
#define MAX_INPUT        255	/* size of the type-ahead buffer */
#define NAME_MAX         255	/* # chars in a file name */
#define PATH_MAX        1024	/* # chars in a path name */
#define PIPE_BUF        4095	/* # bytes in atomic write to a pipe */

#endif
