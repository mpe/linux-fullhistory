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

#include <linux/sched.h>
#include <linux/nfs_fs.h>
#include <linux/errno.h>
#include <linux/socket.h>
#include <linux/fcntl.h>
#include <linux/in.h>
#include <linux/net.h>
#include <linux/mm.h>
#include <linux/rpcsock.h>

#include <asm/segment.h>

#define _S(nr) (1<<((nr)-1))

/*
 * Place a synchronous call to the NFS server, meaning that the process
 * sleeps in rpc_call until it either receives a reply or a major timeout
 * occurs.
 * This is now merely a front-end to nfs_rpc_doio.
 */
int
nfs_rpc_call(struct nfs_server *server, int *start, int *end, int size)
{
	struct rpc_ioreq	req;

	size += 1024;		/* account for NFS slack space. ugly */

	req.rq_addr = &server->toaddr;
	req.rq_alen = sizeof(server->toaddr);
	req.rq_slot = NULL;

	req.rq_svec[0].iov_base = start;
	req.rq_svec[0].iov_len = (end - start) << 2;
	req.rq_slen = (end - start) << 2;
	req.rq_snr = 1;
	req.rq_rvec[0].iov_base = start;
	req.rq_rvec[0].iov_len = size;
	req.rq_rlen = size;
	req.rq_rnr = 1;

	return nfs_rpc_doio(server, &req, 0);
}

int
nfs_rpc_doio(struct nfs_server *server, struct rpc_ioreq *req, int async)
{
	struct rpc_timeout	timeout;
	unsigned long		maxtimeo;
	unsigned long		oldmask;
	int			major_timeout_seen, result;

	timeout.to_initval = server->timeo;
	timeout.to_maxval = NFS_MAX_RPC_TIMEOUT*HZ/10;
	timeout.to_retries = server->retrans;
	timeout.to_exponential = 1;

	oldmask = current->blocked;
	current->blocked |= ~(_S(SIGKILL)
		| ((server->flags & NFS_MOUNT_INTR)
		? ((current->sig->action[SIGINT - 1].sa_handler == SIG_DFL
			? _S(SIGINT) : 0)
		| (current->sig->action[SIGQUIT - 1].sa_handler == SIG_DFL
			? _S(SIGQUIT) : 0))
		: 0));

	major_timeout_seen = 0;
	maxtimeo = timeout.to_maxval;

	do {
		result = rpc_doio(server->rsock, req, &timeout, async);
		rpc_release(server->rsock, req);	/* Release slot */

		if (current->signal & ~current->blocked)
			result = -ERESTARTSYS;
		if (result == -ETIMEDOUT) {
			if (async)
				break;
			if (server->flags & NFS_MOUNT_SOFT) {
				printk("NFS server %s not responding, "
					"timed out.\n", server->hostname);
				result = -EIO;
				break;
			}
			if (!major_timeout_seen) {
				printk("NFS server %s not responding, "
					"still trying.\n", server->hostname);
				major_timeout_seen = 1;
			}
			if ((timeout.to_initval <<= 1) >= maxtimeo) {
				timeout.to_initval = maxtimeo;
			}
		} else if (result < 0 && result != -ERESTARTSYS) {
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
