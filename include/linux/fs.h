/*
 * This file has definitions for some important file table
 * structures etc.
 */

#ifndef _FS_H
#define _FS_H

#include <sys/types.h>
#include <sys/dirent.h>
#include <sys/vfs.h>

/* devices are as follows: (same as minix, so we can use the minix
 * file system. These are major numbers.)
 *
 * 0 - unused (nodev)
 * 1 - /dev/mem
 * 2 - /dev/fd
 * 3 - /dev/hd
 * 4 - /dev/ttyx
 * 5 - /dev/tty
 * 6 - /dev/lp
 * 7 - unnamed pipes
 * 8 - /dev/sd
 * 9 - /dev/st
 */

#define IS_SEEKABLE(x) ((x)>=1 && (x)<=3 || (x)==8)

#define MAY_EXEC 1
#define MAY_WRITE 2
#define MAY_READ 4

#define READ 0
#define WRITE 1
#define READA 2		/* read-ahead - don't pause */
#define WRITEA 3	/* "write-ahead" - silly, but somewhat useful */

void buffer_init(long buffer_end);

#define MAJOR(a) (((unsigned)(a))>>8)
#define MINOR(a) ((a)&0xff)

#define NR_OPEN 32
#define NR_INODE 128
#define NR_FILE 64
#define NR_SUPER 8
#define NR_HASH 307
#define NR_BUFFERS nr_buffers
#define BLOCK_SIZE 1024
#define BLOCK_SIZE_BITS 10
#define MAX_CHRDEV 16
#define MAX_BLKDEV 16

#ifndef NULL
#define NULL ((void *) 0)
#endif

#define PIPE_READ_WAIT(inode) ((inode).i_wait)
#define PIPE_WRITE_WAIT(inode) ((inode).i_wait2)
#define PIPE_HEAD(inode) ((inode).i_data[0])
#define PIPE_TAIL(inode) ((inode).i_data[1])
#define PIPE_READERS(inode) ((inode).i_data[2])
#define PIPE_WRITERS(inode) ((inode).i_data[3])
#define PIPE_SIZE(inode) ((PIPE_HEAD(inode)-PIPE_TAIL(inode))&(PAGE_SIZE-1))
#define PIPE_EMPTY(inode) (PIPE_HEAD(inode)==PIPE_TAIL(inode))
#define PIPE_FULL(inode) (PIPE_SIZE(inode)==(PAGE_SIZE-1))

#define NIL_FILP	((struct file *)0)
#define SEL_IN		1
#define SEL_OUT		2
#define SEL_EX		4

typedef char buffer_block[BLOCK_SIZE];

struct buffer_head {
	char * b_data;			/* pointer to data block (1024 bytes) */
	unsigned long b_blocknr;	/* block number */
	unsigned short b_dev;		/* device (0 = free) */
	unsigned char b_uptodate;
	unsigned char b_dirt;		/* 0-clean,1-dirty */
	unsigned char b_count;		/* users using this block */
	unsigned char b_lock;		/* 0 - ok, 1 -locked */
	struct task_struct * b_wait;
	struct buffer_head * b_prev;
	struct buffer_head * b_next;
	struct buffer_head * b_prev_free;
	struct buffer_head * b_next_free;
	struct buffer_head * b_reqnext;
};

struct inode {
	dev_t		i_dev;
	unsigned long	i_ino;
	umode_t		i_mode;
	nlink_t		i_nlink;
	uid_t		i_uid;
	gid_t		i_gid;
	dev_t		i_rdev;
	off_t		i_size;
	time_t		i_atime;
	time_t		i_mtime;
	time_t		i_ctime;
	unsigned long i_data[16];
	struct inode_operations * i_op;
	struct super_block * i_sb;
	struct task_struct * i_wait;
	struct task_struct * i_wait2;	/* for pipes */
	unsigned short i_count;
	unsigned char i_lock;
	unsigned char i_dirt;
	unsigned char i_pipe;
	unsigned char i_mount;
	unsigned char i_seek;
	unsigned char i_update;
};

struct file {
	unsigned short f_mode;
	unsigned short f_flags;
	unsigned short f_count;
	unsigned short f_reada;
	struct inode * f_inode;
	struct file_operations * f_op;
	off_t f_pos;
};

typedef struct {
	struct task_struct * old_task;
	struct task_struct ** wait_address;
} wait_entry;

typedef struct select_table_struct {
	int nr, woken;
	struct task_struct * current;
	struct select_table_struct * next_table;
	wait_entry entry[NR_OPEN*3];
} select_table;

struct super_block {
	unsigned long s_ninodes;
	unsigned long s_nzones;
	unsigned long s_imap_blocks;
	unsigned long s_zmap_blocks;
	unsigned long s_firstdatazone;
	unsigned long s_log_zone_size;
	unsigned long s_max_size;
	unsigned short s_magic;
/* These are only in memory */
	struct buffer_head * s_imap[8];
	struct buffer_head * s_zmap[8];
	unsigned short s_dev;
	struct inode * s_covered;
	struct inode * s_mounted;
	unsigned long s_time;
	struct task_struct * s_wait;
	unsigned char s_lock;
	unsigned char s_rd_only;
	unsigned char s_dirt;
	/* TUBE */
	struct super_operations *s_op;
};

struct file_operations {
	int (*lseek) (struct inode *, struct file *, off_t, int);
	int (*read) (struct inode *, struct file *, char *, int);
	int (*write) (struct inode *, struct file *, char *, int);
	int (*readdir) (struct inode *, struct file *, struct dirent *, int count);
	int (*select) (struct inode *, struct file *, int, select_table *);
	int (*ioctl) (struct inode *, struct file *, unsigned int, unsigned int);
	int (*open) (struct inode *, struct file *);
	void (*release) (struct inode *, struct file *);
};

struct inode_operations {
	struct file_operations * default_file_ops;
	int (*create) (struct inode *,const char *,int,int,struct inode **);
	int (*lookup) (struct inode *,const char *,int,struct inode **);
	int (*link) (struct inode *,struct inode *,const char *,int);
	int (*unlink) (struct inode *,const char *,int);
	int (*symlink) (struct inode *,const char *,int,const char *);
	int (*mkdir) (struct inode *,const char *,int,int);
	int (*rmdir) (struct inode *,const char *,int);
	int (*mknod) (struct inode *,const char *,int,int,int);
	int (*rename) (struct inode *,const char *,int,struct inode *,const char *,int);
	int (*readlink) (struct inode *,char *,int);
	struct inode * (*follow_link) (struct inode *, struct inode *);
	int (*bmap) (struct inode *,int);
	void (*truncate) (struct inode *);
};

struct super_operations {
	void (*read_inode)(struct inode *inode);
	void (*write_inode) (struct inode *inode);
	void (*put_inode) (struct inode *inode);
	void (*put_super)(struct super_block *sb);
	void (*write_super) (struct super_block *sb);
	void (*statfs) (struct super_block *sb, struct statfs *buf);
};

struct file_system_type {
	struct super_block *(*read_super)(struct super_block *sb,void *mode);
	char *name;
};

extern struct file_operations * chrdev_fops[MAX_CHRDEV];
extern struct file_operations * blkdev_fops[MAX_BLKDEV];

extern struct file_system_type *get_fs_type(char *name);

extern struct inode inode_table[NR_INODE];
extern struct file file_table[NR_FILE];
extern struct super_block super_block[NR_SUPER];
extern struct buffer_head * start_buffer;
extern int nr_buffers;

extern void check_disk_change(int dev);
extern void invalidate_inodes(int dev);
extern int floppy_change(struct buffer_head * first_block);
extern int ticks_to_floppy_on(unsigned int dev);
extern void floppy_on(unsigned int dev);
extern void floppy_off(unsigned int dev);
extern void sync_inodes(void);
extern void wait_on(struct inode * inode);
extern int bmap(struct inode * inode,int block);
extern struct inode * namei(const char * pathname);
extern struct inode * lnamei(const char * pathname);
extern int permission(struct inode * inode,int mask);
extern struct inode * _namei(const char * filename, struct inode * base,
	int follow_links);
extern int open_namei(const char * pathname, int flag, int mode,
	struct inode ** res_inode);
extern int do_mknod(const char * filename, int mode, int dev);
extern void iput(struct inode * inode);
extern struct inode * iget(int dev,int nr);
extern struct inode * get_empty_inode(void);
extern struct inode * get_pipe_inode(void);
extern struct buffer_head * get_hash_table(int dev, int block);
extern struct buffer_head * getblk(int dev, int block);
extern void ll_rw_block(int rw, struct buffer_head * bh);
extern void ll_rw_page(int rw, int dev, int nr, char * buffer);
extern void ll_rw_swap_file(int rw, int dev, unsigned int *b, int nb, char *buffer);
extern void brelse(struct buffer_head * buf);
extern struct buffer_head * bread(int dev,int block);
extern void bread_page(unsigned long addr,int dev,int b[4]);
extern struct buffer_head * breada(int dev,int block,...);
extern int sync_dev(int dev);
extern struct super_block * get_super(int dev);
extern void put_super(int dev);
extern int ROOT_DEV;

extern void mount_root(void);
extern void lock_super(struct super_block * sb);
extern void free_super(struct super_block * sb);

extern int char_read(struct inode *, struct file *, char *, int);
extern int block_read(struct inode *, struct file *, char *, int);

extern int char_write(struct inode *, struct file *, char *, int);
extern int block_write(struct inode *, struct file *, char *, int);

#endif
