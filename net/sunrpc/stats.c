/*
 * linux/net/sunrpc/stats.c
 *
 * procfs-based user access to RPC statistics
 *
 * Everything is complicated by the fact that procfs doesn't pass the
 * proc_dir_info struct in the call to get_info. We need our own 
 * inode_ops for /proc/net/rpc (we want to have a write op for zeroing
 * the current stats, anyway).
 *
 * Copyright (C) 1995, 1996 Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/svcsock.h>

#define RPCDBG_FACILITY	RPCDBG_MISC

/*
 * Generic stats object (same for clnt and svc stats).
 * Must agree with first two fields of either.
 */
struct stats {
	struct stats *		next;
	struct proc_dir_entry *	entry;
};

/* Code disabled until updated to new dynamic /proc code */
#if 0

static struct stats *		rpc_stats = NULL;
static struct stats *		svc_stats = NULL;

static struct proc_dir_entry	proc_rpc = {
	0, 3, "rpc",
	S_IFDIR | S_IRUGO | S_IXUGO, 1, 0, 0,
	0, NULL,
	NULL, NULL,
	NULL,
	NULL, NULL,
};

/*
 * Register/unregister a stats file
 */
static void
stats_register(struct stats **q, struct stats *p)
{
	dprintk("RPC: registering /proc/net/rpc/%s\n",
				p->entry->name);
	/* return; */
	if (p->entry->low_ino)
		return;
	p->next = *q;
	*q = p;
	proc_register_dynamic(&proc_rpc, p->entry);
}

static void
stats_unregister(struct stats **q, struct stats *p)
{
	dprintk("RPC: unregistering /proc/net/rpc/%s\n",
				p->entry->name);
	/* return; */
	if (!p->entry->low_ino)
		return;
	while (*q) {
		if (*q == p) {
			*q = p->next;
			proc_unregister(&proc_rpc, p->entry->low_ino);
			return;
		}
		q = &((*q)->next);
	}
}

/*
 * Client stats handling
 */
void
rpcstat_register(struct rpc_stat *statp)
{
	stats_register(&rpc_stats, (struct stats *) statp);
}

void
rpcstat_unregister(struct rpc_stat *statp)
{
	stats_unregister(&rpc_stats, (struct stats *) statp);
}

int
rpcstat_get_info(struct rpc_stat *statp, char *buffer,
			char **start, off_t offset, int length)
{
	struct rpc_program *prog = statp->program;
	struct rpc_version *vers;
	int		len, i, j;

	len = sprintf(buffer,
		"net %d %d %d %d\n",
			statp->netcnt,
			statp->netudpcnt,
			statp->nettcpcnt,
			statp->nettcpconn);
	len += sprintf(buffer + len,
		"rpc %d %d %d\n",
			statp->rpccnt,
			statp->rpcretrans,
			statp->rpcauthrefresh);

	for (i = 0; i < prog->nrvers; i++) {
		if (!(vers = prog->version[i]))
			continue;
		len += sprintf(buffer + len, "proc%d %d",
					vers->number, vers->nrprocs);
		for (j = 0; j < vers->nrprocs; j++)
			len += sprintf(buffer + len, " %d",
					vers->procs[j].p_count);
		buffer[len++] = '\n';
	}

	if (offset >= len) {
		*start = buffer;
		return 0;
	}
	*start = buffer + offset;
	len -= offset;
	if (len > length)
		len = length;
	return len;
}

/*
 * Server stats handling
 */
void
svcstat_register(struct svc_stat *statp)
{
	stats_register(&svc_stats, (struct stats *) statp);
}

void
svcstat_unregister(struct svc_stat *statp)
{
	stats_unregister(&svc_stats, (struct stats *) statp);
}

int
svcstat_get_info(struct svc_stat *statp, char *buffer,
			char **start, off_t offset, int length)
{
	struct svc_program *prog = statp->program;
	struct svc_procedure *proc;
	struct svc_version *vers;
	int		len, i, j;

	len = sprintf(buffer,
		"net %d %d %d %d\n",
			statp->netcnt,
			statp->netudpcnt,
			statp->nettcpcnt,
			statp->nettcpconn);
	len += sprintf(buffer + len,
		"rpc %d %d %d %d %d\n",
			statp->rpccnt,
			statp->rpcbadfmt+statp->rpcbadauth+statp->rpcbadclnt,
			statp->rpcbadfmt,
			statp->rpcbadauth,
			statp->rpcbadclnt);

	for (i = 0; i < prog->pg_nvers; i++) {
		if (!(vers = prog->pg_vers[i]) || !(proc = vers->vs_proc))
			continue;
		len += sprintf(buffer + len, "proc%d %d", i, vers->vs_nproc);
		for (j = 0; j < vers->vs_nproc; j++, proc++)
			len += sprintf(buffer + len, " %d", proc->pc_count);
		buffer[len++] = '\n';
	}

	if (offset >= len) {
		*start = buffer;
		return 0;
	}
	*start = buffer + offset;
	if ((len -= offset) > length)
		len = length;
	return len;
}

/*
 * Register /proc/net/rpc
 */
void
rpcstat_init(void)
{
	dprintk("RPC: registering /proc/net/rpc\n");
	proc_rpc.ops = proc_net.ops;	/* cheat */
	proc_register_dynamic(&proc_net, &proc_rpc);
}

/*
 * Unregister /proc/net/rpc
 */
void
rpcstat_exit(void)
{
	while (rpc_stats)
		stats_unregister(&rpc_stats, rpc_stats);
	while (svc_stats)
		stats_unregister(&svc_stats, svc_stats);
	dprintk("RPC: unregistering /proc/net/rpc\n");
	proc_unregister(&proc_net, proc_rpc.low_ino);
}

#else

/* Various dummy functions */

int
rpcstat_get_info(struct rpc_stat *statp, char *buffer,
			char **start, off_t offset, int length)
{
	return 0;
}

int
svcstat_get_info(struct svc_stat *statp, char *buffer,
			char **start, off_t offset, int length)
{
	return 0;
}

void
rpcstat_register(struct rpc_stat *statp)
{
}

void
rpcstat_unregister(struct rpc_stat *statp)
{
}

void
svcstat_register(struct svc_stat *statp)
{
}

void
svcstat_unregister(struct svc_stat *statp)
{
}

void
rpcstat_init(void)
{
}

void
rpcstat_exit(void)
{
}

#endif
