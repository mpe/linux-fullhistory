/*
 * linux/include/linux/sunrpc/sched.h
 *
 * Scheduling primitives for kernel Sun RPC.
 *
 * Copyright (C) 1996, Olaf Kirch <okir@monad.swb.de>
 */

#ifndef _LINUX_SUNRPC_SCHED_H_
#define _LINUX_SUNRPC_SCHED_H_

#include <linux/timer.h>
#include <linux/tqueue.h>
#include <linux/sunrpc/types.h>

/*
 * Define this if you want to test the fast scheduler for async calls.
 * This is still experimental and may not work.
 */
#undef  CONFIG_RPC_FASTSCHED

/*
 * This is the RPC task struct
 */
struct rpc_task {
	struct rpc_task *	tk_prev;	/* wait queue links */
	struct rpc_task *	tk_next;
#ifdef RPC_DEBUG
	unsigned long		tk_magic;	/* 0xf00baa */
#endif
	struct rpc_task *	tk_next_task;	/* global list of tasks */
	struct rpc_task *	tk_prev_task;	/* global list of tasks */
	struct rpc_clnt *	tk_client;	/* RPC client */
	struct rpc_rqst *	tk_rqstp;	/* RPC request */
	struct rpc_cred *	tk_cred;	/* RPC credentials */
	int			tk_status;	/* result of last operation */
	struct rpc_wait_queue *	tk_rpcwait;	/* RPC wait queue we're on */

	/*
	 * RPC call state
	 */
	__u32			tk_proc;	/* procedure number */
	__u32 *			tk_buffer;	/* XDR buffer */
	void *			tk_argp;	/* argument storage */
	void *			tk_resp;	/* result storage */
	__u8			tk_garb_retry,
				tk_cred_retry,
				tk_suid_retry;

	/*
	 * callback	to be executed after waking up
	 * action	next procedure for async tasks
	 * exit		exit async task and report to caller
	 */
	void			(*tk_callback)(struct rpc_task *);
	void			(*tk_action)(struct rpc_task *);
	void			(*tk_exit)(struct rpc_task *);
	void *			tk_calldata;

	/*
	 * tk_timer is used for async processing by the RPC scheduling
	 * primitives. You should not access this directly unless
	 * you have a pathological interest in kernel oopses.
	 */
	struct timer_list	tk_timer;	/* kernel timer */
	struct wait_queue *	tk_wait;	/* sync: sleep on this q */
	unsigned long		tk_timeout;	/* timeout for rpc_sleep() */
	unsigned short		tk_flags;	/* misc flags */
#ifdef RPC_DEBUG
	unsigned short		tk_pid;		/* debugging aid */
#endif
};
#define tk_auth			tk_client->cl_auth
#define tk_xprt			tk_client->cl_xprt

typedef void			(*rpc_action)(struct rpc_task *);

/*
 * RPC task flags
 */
#define RPC_TASK_RUNNING	0x0001		/* is running */
#define RPC_TASK_ASYNC		0x0002		/* is an async task */
#define RPC_TASK_CALLBACK	0x0004		/* invoke callback */
#define RPC_TASK_SWAPPER	0x0008		/* is swapping in/out */
#define RPC_TASK_SETUID		0x0010		/* is setuid process */
#define RPC_TASK_CHILD		0x0020		/* is child of other task */
#define RPC_CALL_REALUID	0x0040		/* try using real uid */
#define RPC_CALL_MAJORSEEN	0x0080		/* major timeout seen */
#define RPC_TASK_ROOTCREDS	0x0100		/* force root creds */
#define RPC_TASK_DYNAMIC	0x0200		/* task was kmalloc'ed */
#define RPC_TASK_KILLED		0x0400		/* task was killed */
#define RPC_TASK_NFSWRITE	0x1000		/* an NFS writeback */

#define RPC_IS_RUNNING(t)	((t)->tk_flags & RPC_TASK_RUNNING)
#define RPC_IS_ASYNC(t)		((t)->tk_flags & RPC_TASK_ASYNC)
#define RPC_IS_SETUID(t)	((t)->tk_flags & RPC_TASK_SETUID)
#define RPC_IS_CHILD(t)		((t)->tk_flags & RPC_TASK_CHILD)
#define RPC_IS_SWAPPER(t)	((t)->tk_flags & RPC_TASK_SWAPPER)
#define RPC_DO_CALLBACK(t)	((t)->tk_flags & RPC_TASK_CALLBACK)
#define RPC_DO_ROOTOVERRIDE(t)	((t)->tk_flags & RPC_TASK_ROOTCREDS)
#define RPC_ASSASSINATED(t)	((t)->tk_flags & RPC_TASK_KILLED)

/*
 * RPC synchronization objects
 */
struct rpc_wait_queue {
	struct rpc_task *	task;
#ifdef RPC_DEBUG
	char *			name;
#endif
};

#ifndef RPC_DEBUG
# define RPC_INIT_WAITQ(name)	((struct rpc_wait_queue) { NULL })
#else
# define RPC_INIT_WAITQ(name)	((struct rpc_wait_queue) { NULL, name })
#endif

/*
 * Function prototypes
 */
struct rpc_task *rpc_new_task(struct rpc_clnt *, rpc_action, int flags);
struct rpc_task *rpc_new_child(struct rpc_clnt *, struct rpc_task *parent);
void		rpc_init_task(struct rpc_task *, struct rpc_clnt *,
					rpc_action exitfunc, int flags);
void		rpc_release_task(struct rpc_task *);
void		rpc_killall_tasks(struct rpc_clnt *);
void		rpc_execute(struct rpc_task *);
void		rpc_run_child(struct rpc_task *parent, struct rpc_task *child,
					rpc_action action);
int		rpc_add_wait_queue(struct rpc_wait_queue *, struct rpc_task *);
void		rpc_remove_wait_queue(struct rpc_task *);
void		rpc_sleep_on(struct rpc_wait_queue *, struct rpc_task *,
					rpc_action action, rpc_action timer);
void		rpc_cond_wait(struct rpc_wait_queue *, struct rpc_task *,
					unsigned char *,
					rpc_action action, rpc_action timer);
void		rpc_wake_up_task(struct rpc_task *);
void		rpc_wake_up(struct rpc_wait_queue *);
struct rpc_task *rpc_wake_up_next(struct rpc_wait_queue *);
void		rpc_wake_up_status(struct rpc_wait_queue *, int);
void		rpc_add_timer(struct rpc_task *, rpc_action);
void		rpc_del_timer(struct rpc_task *);
void		rpc_delay(struct rpc_task *, unsigned long);
void *		rpc_allocate(unsigned int flags, unsigned int);
void		rpc_free(void *);
int		rpciod_up(void);
void		rpciod_down(void);
void		rpciod_wake_up(void);
void		rpciod_tcp_dispatcher(void);
#ifdef RPC_DEBUG
void		rpc_show_tasks(void);
#endif

extern __inline__ void *
rpc_malloc(struct rpc_task *task, unsigned int size)
{
	return rpc_allocate(task->tk_flags, size);
}

extern __inline__ void
rpc_exit(struct rpc_task *task, int status)
{
	task->tk_status = status;
	task->tk_action = NULL;
}

#ifdef RPC_DEBUG
extern __inline__ char *
rpc_qname(struct rpc_wait_queue *q)
{
	return q->name? q->name : "unknown";
}
#endif

#endif /* _LINUX_SUNRPC_SCHED_H_ */
