/*
 *  linux/net/sunrpc/rpcclnt.c
 *
 *  This file contains the high-level RPC interface.
 *  It is modeled as a finite state machine to support both synchronous
 *  and asynchronous requests.
 *
 *  -	RPC header generation and argument serialization.
 *  -	Credential refresh.
 *  -	TCP reconnect handling (when finished).
 *  -	Retry of operation when it is suspected the operation failed because
 *	of uid squashing on the server, or when the credentials were stale
 *	and need to be refreshed, or when a packet was damaged in transit.
 *	This may be have to be moved to the VFS layer.
 *
 *  NB: BSD uses a more intelligent approach to guessing when a request
 *  or reply has been lost by keeping the RTO estimate for each procedure.
 *  We currently make do with a constant timeout value.
 *
 *  Copyright (C) 1992,1993 Rick Sladkey <jrs@world.std.com>
 *  Copyright (C) 1995,1996 Olaf Kirch <okir@monad.swb.de>
 */

#include <asm/system.h>
#include <asm/segment.h>

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <linux/in.h>
#include <linux/utsname.h>

#include <linux/sunrpc/clnt.h>


#define RPC_SLACK_SPACE		1024	/* total overkill */

#ifdef RPC_DEBUG
# define RPCDBG_FACILITY	RPCDBG_CALL
#endif

static struct wait_queue *	destroy_wait = NULL;


static void	call_bind(struct rpc_task *task);
static void	call_reserve(struct rpc_task *task);
static void	call_reserveresult(struct rpc_task *task);
static void	call_allocate(struct rpc_task *task);
static void	call_encode(struct rpc_task *task);
static void	call_decode(struct rpc_task *task);
static void	call_transmit(struct rpc_task *task);
static void	call_receive(struct rpc_task *task);
static void	call_status(struct rpc_task *task);
static void	call_refresh(struct rpc_task *task);
static void	call_refreshresult(struct rpc_task *task);
static void	call_timeout(struct rpc_task *task);
static void	call_reconnect(struct rpc_task *task);
static u32 *	call_header(struct rpc_task *task);
static u32 *	call_verify(struct rpc_task *task);


/*
 * Create an RPC client
 * FIXME: This should also take a flags argument (as in task->tk_flags).
 * It's called (among others) from pmap_create_client, which may in
 * turn be called by an async task. In this case, rpciod should not be
 * made to sleep too long.
 */
struct rpc_clnt *
rpc_create_client(struct rpc_xprt *xprt, char *servname,
		  struct rpc_program *program, u32 vers, int flavor)
{
	struct rpc_version	*version;
	struct rpc_clnt		*clnt = NULL;

	dprintk("RPC: creating %s client for %s (xprt %p)\n",
		program->name, servname, xprt);

	if (!xprt)
		goto out;
	if (vers >= program->nrvers || !(version = program->version[vers]))
		goto out;

	clnt = (struct rpc_clnt *) rpc_allocate(0, sizeof(*clnt));
	if (!clnt)
		goto out_no_clnt;
	memset(clnt, 0, sizeof(*clnt));

	clnt->cl_xprt     = xprt;
	clnt->cl_procinfo = version->procs;
	clnt->cl_maxproc  = version->nrprocs;
	clnt->cl_server   = servname;
	clnt->cl_protname = program->name;
	clnt->cl_port     = xprt->addr.sin_port;
	clnt->cl_prog     = program->number;
	clnt->cl_vers     = version->number;
	clnt->cl_prot     = IPPROTO_UDP;
	clnt->cl_stats    = program->stats;
	clnt->cl_bindwait = RPC_INIT_WAITQ("bindwait");

	if (!clnt->cl_port)
		clnt->cl_autobind = 1;

	if (!rpcauth_create(flavor, clnt))
		goto out_no_auth;

	/* save the nodename */
	clnt->cl_nodelen = strlen(system_utsname.nodename);
	if (clnt->cl_nodelen > UNX_MAXNODENAME)
		clnt->cl_nodelen = UNX_MAXNODENAME;
	memcpy(clnt->cl_nodename, system_utsname.nodename, clnt->cl_nodelen);
out:
	return clnt;

out_no_clnt:
	printk("RPC: out of memory in rpc_create_client\n");
	goto out;
out_no_auth:
	printk("RPC: Couldn't create auth handle (flavor %d)\n",
		flavor);
	rpc_free(clnt);
	clnt = NULL;
	goto out;
}

/*
 * Properly shut down an RPC client, terminating all outstanding
 * requests. Note that we must be certain that cl_oneshot and
 * cl_dead are cleared, or else the client would be destroyed
 * when the last task releases it.
 */
int
rpc_shutdown_client(struct rpc_clnt *clnt)
{
	dprintk("RPC: shutting down %s client for %s\n",
		clnt->cl_protname, clnt->cl_server);
	while (clnt->cl_users) {
#ifdef RPC_DEBUG
		printk("rpc_shutdown_client: client %s, tasks=%d\n",
			clnt->cl_protname, clnt->cl_users);
#endif
		/* Don't let rpc_release_client destroy us */
		clnt->cl_oneshot = 0;
		clnt->cl_dead = 0;
		rpc_killall_tasks(clnt);
		sleep_on(&destroy_wait);
	}
	return rpc_destroy_client(clnt);
}

/*
 * Delete an RPC client
 */
int
rpc_destroy_client(struct rpc_clnt *clnt)
{
	dprintk("RPC: destroying %s client for %s\n",
			clnt->cl_protname, clnt->cl_server);

	if (clnt->cl_auth) {
		rpcauth_destroy(clnt->cl_auth);
		clnt->cl_auth = NULL;
	}
	if (clnt->cl_xprt) {
		xprt_destroy(clnt->cl_xprt);
		clnt->cl_xprt = NULL;
	}
	rpc_free(clnt);
	return 0;
}

/*
 * Release an RPC client
 */
void
rpc_release_client(struct rpc_clnt *clnt)
{
	dprintk("RPC:      rpc_release_client(%p, %d)\n",
				clnt, clnt->cl_users);
	if (clnt->cl_users) {
		if (--(clnt->cl_users) > 0)
			return;
	} else
		printk("rpc_release_client: %s client already free??\n",
			clnt->cl_protname);

	wake_up(&destroy_wait);
	if (clnt->cl_oneshot || clnt->cl_dead)
		rpc_destroy_client(clnt);
}

/*
 * Default callback for async RPC calls
 */
static void
rpc_default_callback(struct rpc_task *task)
{
	rpc_release_task(task);
}

/*
 *	Export the signal mask handling for aysnchronous code that
 *	sleeps on RPC calls
 */
 
void rpc_clnt_sigmask(struct rpc_clnt *clnt, sigset_t *oldset)
{
	unsigned long	sigallow = sigmask(SIGKILL);
	unsigned long	irqflags;
	
	/* Turn off various signals */
	if (clnt->cl_intr) {
		struct k_sigaction *action = current->sig->action;
		if (action[SIGINT-1].sa.sa_handler == SIG_DFL)
			sigallow |= sigmask(SIGINT);
		if (action[SIGQUIT-1].sa.sa_handler == SIG_DFL)
			sigallow |= sigmask(SIGQUIT);
	}
	spin_lock_irqsave(&current->sigmask_lock, irqflags);
	*oldset = current->blocked;
	siginitsetinv(&current->blocked, sigallow & ~oldset->sig[0]);
	recalc_sigpending(current);
	spin_unlock_irqrestore(&current->sigmask_lock, irqflags);
}

void rpc_clnt_sigunmask(struct rpc_clnt *clnt, sigset_t *oldset)
{
	unsigned long	irqflags;
	
	spin_lock_irqsave(&current->sigmask_lock, irqflags);
	current->blocked = *oldset;
	recalc_sigpending(current);
	spin_unlock_irqrestore(&current->sigmask_lock, irqflags);
}

/*
 * New rpc_call implementation
 */
int
rpc_do_call(struct rpc_clnt *clnt, u32 proc, void *argp, void *resp,
				int flags, rpc_action func, void *data)
{
	struct rpc_task	my_task, *task = &my_task;
	sigset_t	oldset;
	int		async, status;

	/* If this client is slain all further I/O fails */
	if (clnt->cl_dead) 
		return -EIO;

	rpc_clnt_sigmask(clnt, &oldset);		

	/* Create/initialize a new RPC task */
	if ((async = (flags & RPC_TASK_ASYNC)) != 0) {
		if (!func)
			func = rpc_default_callback;
		status = -ENOMEM;
		if (!(task = rpc_new_task(clnt, func, flags)))
			goto out;
		task->tk_calldata = data;
	} else {
		rpc_init_task(task, clnt, NULL, flags);
	}

	/* Bind the user cred, set up the call info struct and
	 * execute the task */
	if (rpcauth_lookupcred(task) != NULL) {
		rpc_call_setup(task, proc, argp, resp, 0);
		rpc_execute(task);
	} else
		async = 0;

	status = 0;
	if (!async) {
		status = task->tk_status;
		rpc_release_task(task);
	}

out:
	rpc_clnt_sigunmask(clnt, &oldset);		

	return status;
}


void
rpc_call_setup(struct rpc_task *task, u32 proc,
				void *argp, void *resp, int flags)
{
	task->tk_action = call_bind;
	task->tk_proc   = proc;
	task->tk_argp   = argp;
	task->tk_resp   = resp;
	task->tk_flags |= flags;

	/* Increment call count */
	rpcproc_count(task->tk_client, proc)++;
}

/*
 * Restart an (async) RPC call. Usually called from within the
 * exit handler.
 */
void
rpc_restart_call(struct rpc_task *task)
{
	if (task->tk_flags & RPC_TASK_KILLED) {
		rpc_release_task(task);
		return;
	}
	task->tk_action = call_bind;
	rpcproc_count(task->tk_client, task->tk_proc)++;
}

/*
 * 0.	Get the server port number if not yet set
 */
static void
call_bind(struct rpc_task *task)
{
	struct rpc_clnt	*clnt = task->tk_client;

	task->tk_action = call_reserve;
	task->tk_status = 0;
	if (!clnt->cl_port)
		rpc_getport(task, clnt);
}

/*
 * 1.	Reserve an RPC call slot
 */
static void
call_reserve(struct rpc_task *task)
{
	struct rpc_clnt	*clnt = task->tk_client;

	dprintk("RPC: %4d call_reserve\n", task->tk_pid);
	if (!clnt->cl_port) {
		printk(KERN_NOTICE "%s: couldn't bind to server %s - %s.\n",
			clnt->cl_protname, clnt->cl_server,
			clnt->cl_softrtry? "giving up" : "retrying");
		if (!clnt->cl_softrtry) {
			rpc_delay(task, 5*HZ);
			return;
		}
		rpc_exit(task, -EIO);
		return;
	}
	if (!rpcauth_uptodatecred(task)) {
		task->tk_action = call_refresh;
		return;
	}
	task->tk_action  = call_reserveresult;
	task->tk_timeout = clnt->cl_timeout.to_resrvval;
	task->tk_status  = 0;
	clnt->cl_stats->rpccnt++;
	xprt_reserve(task);
}

/*
 * 1b.	Grok the result of xprt_reserve()
 */
static void
call_reserveresult(struct rpc_task *task)
{
	dprintk("RPC: %4d call_reserveresult (status %d)\n",
				task->tk_pid, task->tk_status);
	/*
	 * After a call to xprt_reserve(), we must have either
	 * a request slot or else an error status.
	 */
	if ((task->tk_status >= 0 && !task->tk_rqstp) ||
	    (task->tk_status < 0 && task->tk_rqstp))
		printk("call_reserveresult: status=%d, request=%p??\n",
		 task->tk_status, task->tk_rqstp);

	if (task->tk_status >= 0) {
		task->tk_action = call_allocate;
		goto out;
	} else if (task->tk_status == -EAGAIN) {
		task->tk_timeout = task->tk_client->cl_timeout.to_resrvval;
		task->tk_status = 0;
		xprt_reserve(task);
		goto out;
	} else if (task->tk_status == -ETIMEDOUT) {
		dprintk("RPC: task timed out\n");
		task->tk_action = call_timeout;
		goto out;
	} else {
		task->tk_action = NULL;
	}
	if (!task->tk_rqstp) {
		printk("RPC: task has no request, exit EIO\n");
		rpc_exit(task, -EIO);
	}
out:
	return;
}

/*
 * 2.	Allocate the buffer. For details, see sched.c:rpc_malloc.
 *	(Note: buffer memory is freed in rpc_task_release).
 */
static void
call_allocate(struct rpc_task *task)
{
	struct rpc_clnt	*clnt = task->tk_client;
	unsigned int	bufsiz;

	dprintk("RPC: %4d call_allocate (status %d)\n", 
				task->tk_pid, task->tk_status);
	task->tk_action = call_encode;
	if (task->tk_buffer)
		return;

	/* FIXME: compute buffer requirements more exactly using
	 * auth->au_wslack */
	bufsiz = rpcproc_bufsiz(clnt, task->tk_proc) + RPC_SLACK_SPACE;

	if ((task->tk_buffer = rpc_malloc(task, bufsiz)) != NULL)
		return;
	printk("RPC: buffer allocation failed for task %p\n", task); 

	if (!signalled()) {
		xprt_release(task);
		task->tk_action = call_reserve;
		rpc_delay(task, HZ);
		return;
	}

	rpc_exit(task, -ERESTARTSYS);
}

/*
 * 3.	Encode arguments of an RPC call
 */
static void
call_encode(struct rpc_task *task)
{
	struct rpc_clnt	*clnt = task->tk_client;
	struct rpc_rqst	*req = task->tk_rqstp;
	unsigned int	bufsiz;
	kxdrproc_t	encode;
	int		status;
	u32		*p;

	dprintk("RPC: %4d call_encode (status %d)\n", 
				task->tk_pid, task->tk_status);

	task->tk_action = call_transmit;

	/* Default buffer setup */
	bufsiz = rpcproc_bufsiz(clnt, task->tk_proc)+RPC_SLACK_SPACE;
	req->rq_svec[0].iov_base = task->tk_buffer;
	req->rq_svec[0].iov_len  = bufsiz;
	req->rq_slen		 = 0;
	req->rq_snr		 = 1;
	req->rq_rvec[0].iov_base = task->tk_buffer;
	req->rq_rvec[0].iov_len  = bufsiz;
	req->rq_rlen		 = bufsiz;
	req->rq_rnr		 = 1;

	if (task->tk_proc > clnt->cl_maxproc) {
		printk(KERN_WARNING "%s (vers %d): bad procedure number %d\n",
			clnt->cl_protname, clnt->cl_vers, task->tk_proc);
		rpc_exit(task, -EIO);
		return;
	}

	/* Encode header and provided arguments */
	encode = rpcproc_encode(clnt, task->tk_proc);
	if (!(p = call_header(task))) {
		printk("RPC: call_header failed, exit EIO\n");
		rpc_exit(task, -EIO);
	} else
	if ((status = encode(req, p, task->tk_argp)) < 0) {
		printk(KERN_WARNING "%s: can't encode arguments: %d\n",
				clnt->cl_protname, -status);
		rpc_exit(task, status);
	}
}

/*
 * 4.	Transmit the RPC request
 */
static void
call_transmit(struct rpc_task *task)
{
	dprintk("RPC: %4d call_transmit (status %d)\n", 
				task->tk_pid, task->tk_status);

	task->tk_action = call_receive;
	task->tk_status = 0;
	xprt_transmit(task);
}

/*
 * 5.	Wait for the RPC reply
 */
static void
call_receive(struct rpc_task *task)
{
	dprintk("RPC: %4d call_receive (status %d)\n", 
		task->tk_pid, task->tk_status);

	task->tk_action = call_status;
	/* In case of error, evaluate status */
	if (task->tk_status < 0)
		return;

	/* If we have no decode function, this means we're performing
	 * a void call (a la lockd message passing). */
	if (!rpcproc_decode(task->tk_client, task->tk_proc)) {
		rpc_remove_wait_queue(task); /* remove from xprt_pending */
		task->tk_action = NULL;
		return;
	}

	xprt_receive(task);
}

/*
 * 6.	Sort out the RPC call status
 */
static void
call_status(struct rpc_task *task)
{
	struct rpc_clnt	*clnt = task->tk_client;
	struct rpc_rqst	*req;
	int		status = task->tk_status;

	dprintk("RPC: %4d call_status (status %d)\n", 
				task->tk_pid, task->tk_status);

	if (status >= 0) {
		task->tk_action = call_decode;
	} else if (status == -ETIMEDOUT) {
		task->tk_action = call_timeout;
	} else if (status == -EAGAIN) {
		if (!(req = task->tk_rqstp))
			task->tk_action = call_reserve;
		else if (!task->tk_buffer)
			task->tk_action = call_allocate;
		else if (req->rq_damaged)
			task->tk_action = call_encode;
		else
			task->tk_action = call_transmit;
	} else if (status == -ENOTCONN) {
		task->tk_action = call_reconnect;
	} else if (status == -ECONNREFUSED && clnt->cl_autobind) {
		task->tk_action = call_bind;
		clnt->cl_port = 0;
	} else {
		if (clnt->cl_chatty)
			printk("%s: RPC call returned error %d\n",
				clnt->cl_protname, -status);
		task->tk_action = NULL;
		return;
	}
}

/*
 * 6a.	Handle RPC timeout
 * 	We do not release the request slot, so we keep using the
 *	same XID for all retransmits.
 */
static void
call_timeout(struct rpc_task *task)
{
	struct rpc_clnt	*clnt = task->tk_client;
	struct rpc_rqst	*req = task->tk_rqstp;

	if (req) {
		struct rpc_timeout *to = &req->rq_timeout;

		if (xprt_adjust_timeout(to)) {
			dprintk("RPC: %4d call_timeout (minor timeo)\n",
				task->tk_pid);
			goto minor_timeout;
		}
		to->to_initval <<= 1;
		if (to->to_initval > to->to_maxval)
			to->to_initval = to->to_maxval;
	}

	dprintk("RPC: %4d call_timeout (major timeo)\n", task->tk_pid);
	if (clnt->cl_softrtry) {
		if (clnt->cl_chatty && !task->tk_exit)
			printk("%s: server %s not responding, timed out\n",
				clnt->cl_protname, clnt->cl_server);
		rpc_exit(task, -EIO);
		return;
	}
	if (clnt->cl_chatty && !(task->tk_flags & RPC_CALL_MAJORSEEN)) {
		task->tk_flags |= RPC_CALL_MAJORSEEN;
		if (req)
			printk("%s: server %s not responding, still trying\n",
				clnt->cl_protname, clnt->cl_server);
		else 
			printk("%s: task %d can't get a request slot\n",
				clnt->cl_protname, task->tk_pid);
	}
	if (clnt->cl_autobind)
		clnt->cl_port = 0;

minor_timeout:
	if (!clnt->cl_port) {
		task->tk_action = call_bind;
	} else if (!req) {
		task->tk_action = call_reserve;
	} else if (req->rq_damaged) {
		task->tk_action = call_encode;
		clnt->cl_stats->rpcretrans++;
	} else {
		task->tk_action = call_transmit;
		clnt->cl_stats->rpcretrans++;
	}
	task->tk_status = 0;
}

/*
 * 6b.	Reconnect to the RPC server (TCP case)
 */
static void
call_reconnect(struct rpc_task *task)
{
	dprintk("RPC: %4d call_reconnect status %d\n",
				task->tk_pid, task->tk_status);
	if (task->tk_status == 0) {
		task->tk_action = call_status;
		task->tk_status = -EAGAIN;
		return;
	}
	task->tk_client->cl_stats->netreconn++;
	xprt_reconnect(task);
}

/*
 * 7.	Decode the RPC reply
 */
static void
call_decode(struct rpc_task *task)
{
	struct rpc_clnt	*clnt = task->tk_client;
	struct rpc_rqst	*req = task->tk_rqstp;
	kxdrproc_t	decode = rpcproc_decode(clnt, task->tk_proc);
	u32		*p;

	dprintk("RPC: %4d call_decode (status %d)\n", 
				task->tk_pid, task->tk_status);

	if (clnt->cl_chatty && (task->tk_flags & RPC_CALL_MAJORSEEN)) {
		printk("%s: server %s OK\n",
			clnt->cl_protname, clnt->cl_server);
		task->tk_flags &= ~RPC_CALL_MAJORSEEN;
	}

	if (task->tk_status < 12) {
		printk("%s: too small RPC reply size (%d bytes)\n",
			clnt->cl_protname, task->tk_status);
		rpc_exit(task, -EIO);
		return;
	}

	/* Verify the RPC header */
	if (!(p = call_verify(task)))
		return;

	/*
	 * The following is an NFS-specific hack to cater for setuid
	 * processes whose uid is mapped to nobody on the server.
	 */
	if (task->tk_client->cl_prog == 100003 && 
            (ntohl(*p) == NFSERR_ACCES || ntohl(*p) == NFSERR_PERM)) {
		if (RPC_IS_SETUID(task) && (task->tk_suid_retry)--) {
			dprintk("RPC: %4d retry squashed uid\n", task->tk_pid);
			task->tk_flags ^= RPC_CALL_REALUID;
			task->tk_action = call_encode;
			return;
		}
	}

	task->tk_action = NULL;
	task->tk_status = decode(req, p, task->tk_resp);
	dprintk("RPC: %4d call_decode result %d\n", task->tk_pid,
					task->tk_status);
}

/*
 * 8.	Refresh the credentials if rejected by the server
 */
static void
call_refresh(struct rpc_task *task)
{
	dprintk("RPC: %4d call_refresh\n", task->tk_pid);

	xprt_release(task);	/* Must do to obtain new XID */
	task->tk_action = call_refreshresult;
	task->tk_status = 0;
	task->tk_client->cl_stats->rpcauthrefresh++;
	rpcauth_refreshcred(task);
}

/*
 * 8a.	Process the results of a credential refresh
 */
static void
call_refreshresult(struct rpc_task *task)
{
	dprintk("RPC: %4d call_refreshresult (status %d)\n", 
				task->tk_pid, task->tk_status);

	if (task->tk_status < 0) {
		task->tk_status = -EACCES;
		task->tk_action = NULL;
	} else
		task->tk_action = call_reserve;
}

/*
 * Call header serialization
 */
static u32 *
call_header(struct rpc_task *task)
{
	struct rpc_clnt *clnt = task->tk_client;
	struct rpc_xprt *xprt = clnt->cl_xprt;
	u32		*p = task->tk_buffer;

	/* FIXME: check buffer size? */
	if (xprt->stream)
		*p++ = 0;		/* fill in later */
	*p++ = task->tk_rqstp->rq_xid;	/* XID */
	*p++ = htonl(RPC_CALL);		/* CALL */
	*p++ = htonl(RPC_VERSION);	/* RPC version */
	*p++ = htonl(clnt->cl_prog);	/* program number */
	*p++ = htonl(clnt->cl_vers);	/* program version */
	*p++ = htonl(task->tk_proc);	/* procedure */
	return rpcauth_marshcred(task, p);
}

/*
 * Reply header verification
 */
static u32 *
call_verify(struct rpc_task *task)
{
	u32	*p = task->tk_buffer, n;

	p += 1;	/* skip XID */

	if ((n = ntohl(*p++)) != RPC_REPLY) {
		printk("call_verify: not an RPC reply: %x\n", n);
		goto garbage;
	}
	if ((n = ntohl(*p++)) != RPC_MSG_ACCEPTED) {
		int	error = -EACCES;

		if ((n = ntohl(*p++)) != RPC_AUTH_ERROR) {
			printk("call_verify: RPC call rejected: %x\n", n);
		} else
		switch ((n = ntohl(*p++))) {
		case RPC_AUTH_REJECTEDCRED:
		case RPC_AUTH_REJECTEDVERF:
			if (!task->tk_cred_retry--)
				break;
			dprintk("RPC: %4d call_verify: retry stale creds\n",
							task->tk_pid);
			rpcauth_invalcred(task);
			task->tk_action = call_refresh;
			return NULL;
		case RPC_AUTH_BADCRED:
		case RPC_AUTH_BADVERF:
			/* possibly garbled cred/verf? */
			if (!task->tk_garb_retry--)
				break;
			dprintk("RPC: %4d call_verify: retry garbled creds\n",
							task->tk_pid);
			task->tk_action = call_encode;
			return NULL;
		case RPC_AUTH_TOOWEAK:
			printk("call_verify: server requires stronger "
			       "authentication.\n");
		default:
			printk("call_verify: unknown auth error: %x\n", n);
			error = -EIO;
		}
		dprintk("RPC: %4d call_verify: call rejected %d\n",
						task->tk_pid, n);
		rpc_exit(task, error);
		return NULL;
	}
	if (!(p = rpcauth_checkverf(task, p))) {
		printk("call_verify: auth check failed\n");
		goto garbage;		/* bad verifier, retry */
	}
	switch ((n = ntohl(*p++))) {
	case RPC_SUCCESS:
		return p;
	case RPC_GARBAGE_ARGS:
		break;			/* retry */
	default:
		printk("call_verify: server accept status: %x\n", n);
		/* Also retry */
	}

garbage:
	dprintk("RPC: %4d call_verify: server saw garbage\n", task->tk_pid);
	task->tk_client->cl_stats->rpcgarbage++;
	if (task->tk_garb_retry--) {
		printk("RPC: garbage, retrying %4d\n", task->tk_pid);
		task->tk_action = call_encode;
		return NULL;
	}
	printk("RPC: garbage, exit EIO\n");
	rpc_exit(task, -EIO);
	return NULL;
}
