/*
 *  ncp_fs_sb.h
 *
 *  Copyright (C) 1995, 1996 by Volker Lendecke
 *
 */

#ifndef _NCP_FS_SB
#define _NCP_FS_SB

#include <linux/ncp_mount.h>
#include <linux/types.h>

#ifdef __KERNEL__

#define NCP_DEFAULT_BUFSIZE 1024

struct ncp_server {

	struct ncp_mount_data m; /* Nearly all of the mount data is of
				    interest for us later, so we store
				    it completely. */

	struct file *ncp_filp;	/* File pointer to ncp socket */
	struct file *wdog_filp;	/* File pointer to wdog socket */
	struct file *msg_filp;	/* File pointer to message socket */
	void *data_ready;	/* The wdog socket gets a new
				   data_ready callback. We store the
				   old one for checking purposes and
				   to reset it on unmounting. */

	u8         sequence;
	u8         task;
	u16        connection;	/* Remote connection number */

	u8         completion;	/* Status message from server */
	u8         conn_status;	/* Bit 4 = 1 ==> Server going down, no
				   requests allowed anymore.
				   Bit 0 = 1 ==> Server is down. */

	int        buffer_size;	/* Negotiated bufsize */

	int        reply_size;	/* Size of last reply */

	int        packet_size;
	unsigned char *packet;	/* Here we prepare requests and
				   receive replies */

	int        lock;	/* To prevent mismatch in protocols. */
	struct wait_queue *wait;

	int        current_size; /* for packet preparation */
	int        has_subfunction;
	int        ncp_reply_size;

        struct ncp_inode_info root;
	char       root_path;	/* '\0' */
};

static inline int
ncp_conn_valid(struct ncp_server *server)
{
	return ((server->conn_status & 0x11) == 0);
}

static inline void
ncp_invalidate_conn(struct ncp_server *server)
{
	server->conn_status |= 0x01;
}

#endif /* __KERNEL__ */

#endif
