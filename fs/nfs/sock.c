/*
 *  linux/fs/nfs/sock.c
 *
 *  Copyright (C) 1992, 1993  Rick Sladkey
 *
 *  low-level nfs remote procedure call interface
 *
 * FIXES
 *
 * 2/7/94 James Bottomley and Jon Peatfield DAMTP, Cambridge University
 *
 * An xid mismatch no longer causes the request to be trashed.
 *
 * Peter Eriksson - incorrect XID used to confuse Linux
 * Florian La Roche - use the correct max size, if reading a packet and
 *                    also verify, if the whole packet has been read...
 *                    more checks should be done in proc.c...
 *
 */

#ifdef MODULE
#include <linux/module.h>
#endif

#include <linux/sched.h>
#include <linux/nfs_fs.h>
#include <linux/errno.h>
#include <linux/socket.h>
#include <linux/fcntl.h>
#include <asm/segment.h>
#include <linux/in.h>
#include <linux/net.h>
#include <linux/mm.h>
#include <linux/rpcsock.h>

/* JEJB/JSP 2/7/94
 * this must match the value of NFS_SLACK_SPACE in linux/fs/nfs/proc.c 
 * ***FIXME*** should probably put this in nfs_fs.h */
#define NFS_SLACK_SPACE 1024

#define _S(nr) (1<<((nr)-1))

/*
 * We violate some modularity principles here by poking around
 * in some socket internals.  Besides having to call socket
 * functions from kernel-space instead of user space, the socket
 * interface does not lend itself well to being cleanly called
 * without a file descriptor.  Since the nfs calls can run on
 * behalf of any process, the superblock maintains a file pointer
 * to the server socket.
 */

int
nfs_rpc_call(struct nfs_server *server, int *start, int *end, int size)
{
	struct rpc_timeout	timeout;
	unsigned long		maxtimeo;
	unsigned long		oldmask;
	int			major_timeout_seen, result;

	timeout.init_timeout = server->timeo;
	timeout.max_timeout = maxtimeo = NFS_MAX_RPC_TIMEOUT*HZ/10;
	timeout.retries = server->retrans;
	timeout.exponential = 1;

	oldmask = current->blocked;
	current->blocked |= ~(_S(SIGKILL)
		| ((server->flags & NFS_MOUNT_INTR)
		? ((current->sig->action[SIGINT - 1].sa_handler == SIG_DFL
			? _S(SIGINT) : 0)
		| (current->sig->action[SIGQUIT - 1].sa_handler == SIG_DFL
			? _S(SIGQUIT) : 0))
		: 0));
	major_timeout_seen = 0;

	do {
		result = rpc_call(server->rsock, 
				&server->toaddr, sizeof(server->toaddr),
				start, ((char *) end) - ((char *) start),
				start, size + 1024,
				&timeout, 1);
		if (current->signal & ~current->blocked)
			result = -ERESTARTSYS;
		if (result == -ETIMEDOUT) {
			if (server->flags & NFS_MOUNT_SOFT) {
				printk("NFS server %s not responding, "
					"still trying.\n", server->hostname);
				result = -EIO;
				break;
			}
			if (!major_timeout_seen) {
				printk("NFS server %s not responding, "
					"timed out.\n", server->hostname);
				major_timeout_seen = 1;
			}
			if ((timeout.init_timeout <<= 1) >= maxtimeo)
				timeout.init_timeout = maxtimeo;
		} else if (result < 0) {
			printk("NFS: notice message: result = %d.\n", result);
		}
	} while (result == -ETIMEDOUT && !(server->flags & NFS_MOUNT_SOFT));

	if (result >= 0 && major_timeout_seen)
		printk("NFS server %s OK.\n", server->hostname);
	/* 20 is the minimum RPC reply header size */
	if (result >= 0 && result < 20) {
		printk("NFS: too small read memory size (%d bytes)\n", result);
		result = -EIO;
	}

	current->blocked = oldmask;
	return result;
}
