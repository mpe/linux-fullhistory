#ifndef _LINUX_PIPE_FS_I_H
#define _LINUX_PIPE_FS_I_H

struct pipe_inode_info {
	wait_queue_head_t wait;
	char * base;
	unsigned int start;
	unsigned int lock;
	unsigned int rd_openers;
	unsigned int wr_openers;
	unsigned int readers;
	unsigned int writers;
};

#define PIPE_WAIT(inode)	((inode).i_pipe->wait)
#define PIPE_BASE(inode)	((inode).i_pipe->base)
#define PIPE_START(inode)	((inode).i_pipe->start)
#define PIPE_LEN(inode)		((inode).i_size)
#define PIPE_RD_OPENERS(inode)	((inode).i_pipe->rd_openers)
#define PIPE_WR_OPENERS(inode)	((inode).i_pipe->wr_openers)
#define PIPE_READERS(inode)	((inode).i_pipe->readers)
#define PIPE_WRITERS(inode)	((inode).i_pipe->writers)
#define PIPE_LOCK(inode)	((inode).i_pipe->lock)
#define PIPE_SIZE(inode)	PIPE_LEN(inode)

#define PIPE_EMPTY(inode)	(PIPE_SIZE(inode)==0)
#define PIPE_FULL(inode)	(PIPE_SIZE(inode)==PIPE_BUF)
#define PIPE_FREE(inode)	(PIPE_BUF - PIPE_LEN(inode))
#define PIPE_END(inode)		((PIPE_START(inode)+PIPE_LEN(inode))&\
							   (PIPE_BUF-1))
#define PIPE_MAX_RCHUNK(inode)	(PIPE_BUF - PIPE_START(inode))
#define PIPE_MAX_WCHUNK(inode)	(PIPE_BUF - PIPE_END(inode))

#endif
