/*
 * linux/fs/lockd/svc.c
 *
 * This is the central lockd service.
 *
 * FIXME: Separate the lockd NFS server functionality from the lockd NFS
 * 	  client functionality. Oh why didn't Sun create two separate
 *	  services in the first place?
 *
 * Authors:	Olaf Kirch (okir@monad.swb.de)
 *
 * Copyright (C) 1995, 1996 Olaf Kirch <okir@monad.swb.de>
 */

#define __KERNEL_SYSCALLS__
#include <linux/config.h>
#include <linux/module.h>

#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/nfs.h>
#include <linux/in.h>
#include <linux/uio.h>
#include <linux/version.h>
#include <linux/unistd.h>
#include <linux/malloc.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>

#include <linux/sunrpc/types.h>
#include <linux/sunrpc/stats.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/svc.h>
#include <linux/sunrpc/svcsock.h>
#include <linux/lockd/lockd.h>


#define NLMDBG_FACILITY		NLMDBG_SVC
#define LOCKD_BUFSIZE		(1024 + NLMSSVC_XDRSIZE)
#define BLOCKABLE_SIGS		(~(_S(SIGKILL) | _S(SIGSTOP)))
#define _S(sig)			(1 << ((sig) - 1))

extern struct svc_program	nlmsvc_program;
struct nlmsvc_binding *		nlmsvc_ops = NULL;
static int			nlmsvc_sema = 0;
static int			nlmsvc_pid = 0;
unsigned long			nlmsvc_grace_period = 0;
unsigned long			nlmsvc_timeout = 0;

/*
 * Currently the following can be set only at insmod time.
 * Ideally, they would be accessible through the sysctl interface.
 */
unsigned long			nlm_grace_period = 0;
unsigned long			nlm_timeout = LOCKD_DFLT_TIMEO;

/*
 * This is the lockd kernel thread
 */
static void
lockd(struct svc_rqst *rqstp)
{
	struct svc_serv	*serv = rqstp->rq_server;
	sigset_t	oldsigmask;
	int		err = 0;

	lock_kernel();
	/* Lock module and set up kernel thread */
	MOD_INC_USE_COUNT;
	/* exit_files(current); */
	exit_mm(current);
	current->session = 1;
	current->pgrp = 1;
	sprintf(current->comm, "lockd");

	/* kick rpciod */
	rpciod_up();

	dprintk("NFS locking service started (ver " LOCKD_VERSION ").\n");

	if (!nlm_timeout)
		nlm_timeout = LOCKD_DFLT_TIMEO;

#ifdef RPC_DEBUG
	nlmsvc_grace_period = 10 * HZ;
#else
	if (nlm_grace_period) {
		nlmsvc_grace_period += (1 + nlm_grace_period / nlm_timeout)
						* nlm_timeout * HZ;
	} else {
		nlmsvc_grace_period += 5 * nlm_timeout * HZ;
	}
#endif

	nlmsvc_grace_period += jiffies;
	nlmsvc_timeout = nlm_timeout * HZ;
	nlmsvc_pid = current->pid;

	/*
	 * The main request loop. We don't terminate until the last
	 * NFS mount or NFS daemon has gone away, and we've been sent a
	 * signal.
	 */
	while (nlmsvc_sema || !signalled()) {
		if (signalled())
			current->signal = 0;

		/*
		 * Retry any blocked locks that have been notified by
		 * the VFS. Don't do this during grace period.
		 * (Theoretically, there shouldn't even be blocked locks
		 * during grace period).
		 */
		if (!nlmsvc_grace_period) {
			current->timeout = nlmsvc_retry_blocked();
		} else if (nlmsvc_grace_period < jiffies)
			nlmsvc_grace_period = 0;

		/*
		 * Find a socket with data available and call its
		 * recvfrom routine.
		 */
		if ((err = svc_recv(serv, rqstp)) == -EAGAIN)
			continue;
		if (err < 0) {
			if (err != -EINTR)
				printk(KERN_WARNING
					"lockd: terminating on error %d\n",
					-err);
			break;
		}

		dprintk("lockd: request from %08x\n",
				(unsigned)ntohl(rqstp->rq_addr.sin_addr.s_addr));

		/*
		 * Look up the NFS client handle. The handle is needed for
		 * all but the GRANTED callback RPCs.
		 */
		if (nlmsvc_ops) {
			nlmsvc_ops->exp_readlock();
			rqstp->rq_client =
				nlmsvc_ops->exp_getclient(&rqstp->rq_addr);
		} else {
			rqstp->rq_client = NULL;
		}

		/* Process request with all signals blocked.  */
		oldsigmask = current->blocked;
		current->blocked = BLOCKABLE_SIGS;
		svc_process(serv, rqstp);
		current->blocked = oldsigmask;

		/* Unlock export hash tables */
		if (nlmsvc_ops)
			nlmsvc_ops->exp_unlock();
	}

	nlm_shutdown_hosts();

	/* Exit the RPC thread */
	svc_exit_thread(rqstp);

	/* release rpciod */
	rpciod_down();

	/* Release module */
	MOD_DEC_USE_COUNT;
	nlmsvc_pid = 0;
}

/*
 * Make a socket for lockd
 * FIXME: Move this to net/sunrpc/svc.c so that we can share this with nfsd.
 */
static int
lockd_makesock(struct svc_serv *serv, int protocol, unsigned short port)
{
	struct sockaddr_in	sin;

	dprintk("lockd: creating socket proto = %d\n", protocol);
	sin.sin_family      = AF_INET;
	sin.sin_addr.s_addr = INADDR_ANY;
	sin.sin_port        = htons(port);
	return svc_create_socket(serv, protocol, &sin);
}

int
lockd_up(void)
{
	struct svc_serv *	serv;
	int			error;

	if (nlmsvc_pid || nlmsvc_sema++)
		return 0;

	dprintk("lockd: creating service\n");
	if ((serv = svc_create(&nlmsvc_program, 0, NLMSVC_XDRSIZE)) == NULL)
		return -ENOMEM;

	if ((error = lockd_makesock(serv, IPPROTO_UDP, 0)) < 0
	 || (error = lockd_makesock(serv, IPPROTO_TCP, 0)) < 0) {
		svc_destroy(serv);
		return error;
	}

	if ((error = svc_create_thread(lockd, serv)) < 0)
		nlmsvc_sema--;

	/* Release server */
	svc_destroy(serv);
	return 0;
}

void
lockd_down(void)
{
	if (!nlmsvc_pid || --nlmsvc_sema > 0)
		return;

	kill_proc(nlmsvc_pid, SIGKILL, 1);
	nlmsvc_sema = 0;
	nlmsvc_pid = 0;
}

#ifdef MODULE
/* New module support in 2.1.18 */
#if LINUX_VERSION_CODE >= 0x020112
  EXPORT_NO_SYMBOLS;
  MODULE_AUTHOR("Olaf Kirch <okir@monad.swb.de>");
  MODULE_DESCRIPTION("NFS file locking service version " LOCKD_VERSION ".");
  MODULE_PARM(nlm_grace_period, "10-240l");
  MODULE_PARM(nlm_timeout, "3-20l");
#endif
int
init_module(void)
{
	nlmxdr_init();
	return 0;
}

void
cleanup_module(void)
{
	/* FIXME: delete all NLM clients */
	nlm_shutdown_hosts();
}
#endif

/*
 * Define NLM program and procedures
 */
static struct svc_version	nlmsvc_version1 = {
	1, 16, nlmsvc_procedures, NULL
};
static struct svc_version	nlmsvc_version3 = {
	3, 24, nlmsvc_procedures, NULL
};
#ifdef CONFIG_NFSD_NFS3
static struct svc_version	nlmsvc_version4 = {
	4, 24, nlmsvc_procedures4, NULL
};
#endif
static struct svc_version *	nlmsvc_version[] = {
	NULL,
	&nlmsvc_version1,
	NULL,
	&nlmsvc_version3,
#ifdef CONFIG_NFSD_NFS3
	&nlmsvc_version4,
#endif
};

static struct svc_stat		nlmsvc_stats;

#define NLM_NRVERS	(sizeof(nlmsvc_version)/sizeof(nlmsvc_version[0]))
struct svc_program		nlmsvc_program = {
	NLM_PROGRAM,		/* program number */
	1, NLM_NRVERS-1,	/* version range */
	NLM_NRVERS,		/* number of entries in nlmsvc_version */
	nlmsvc_version,		/* version table */
	"lockd",		/* service name */
	&nlmsvc_stats,		/* stats table */
};
