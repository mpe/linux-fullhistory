/*
 * linux/net/sunrpc/stats.c
 *
 * procfs-based user access to generic RPC statistics. The stats files
 * reside in /proc/net/rpc.
 *
 * The read routines assume that the buffer passed in is just big enough.
 * If you implement an RPC service that has its own stats routine which
 * appends the generic RPC stats, make sure you don't exceed the PAGE_SIZE
 * limit.
 *
 * Copyright (C) 1995, 1996, 1997 Olaf Kirch <okir@monad.swb.de>
 */

#define __NO_VERSION__
#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/svcsock.h>

#define RPCDBG_FACILITY	RPCDBG_MISC

static struct proc_dir_entry	*proc_net_rpc = NULL;

/*
 * Get RPC client stats
 */
int
rpc_proc_read(char *buffer, char **start, off_t offset, int count,
				int *eof, void *data)
{
	struct rpc_stat	*statp = (struct rpc_stat *) data;
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
		*eof = 1;
		return 0;
	}
	*start = buffer + offset;
	if ((len -= offset) > count)
		return count;
	*eof = 1;
	return len;
}

/*
 * Get RPC server stats
 */
int
svc_proc_read(char *buffer, char **start, off_t offset, int count,
				int *eof, void *data)
{
	struct svc_stat *statp	= (struct svc_stat *) data;
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
		*eof = 1;
		return 0;
	}
	*start = buffer + offset;
	if ((len -= offset) > count)
		return count;
	*eof = 1;
	return len;
}

/*
 * Register/unregister RPC proc files
 */
static inline struct proc_dir_entry *
do_register(const char *name, void *data, int issvc)
{
	struct proc_dir_entry	*ent;

	dprintk("RPC: registering /proc/net/rpc/%s\n", name);
	ent = create_proc_entry(name, 0, proc_net_rpc);
	ent->read_proc = issvc? svc_proc_read : rpc_proc_read;
	ent->data = data;

	return ent;
}

struct proc_dir_entry *
rpc_proc_register(struct rpc_stat *statp)
{
	return do_register(statp->program->name, statp, 0);
}

void
rpc_proc_unregister(const char *name)
{
	remove_proc_entry(name, proc_net_rpc);
}

struct proc_dir_entry *
svc_proc_register(struct svc_stat *statp)
{
	return do_register(statp->program->pg_name, statp, 1);
}

void
svc_proc_unregister(const char *name)
{
	remove_proc_entry(name, proc_net_rpc);
}

void
rpc_proc_init(void)
{
	dprintk("RPC: registering /proc/net/rpc\n");
	if (!proc_net_rpc) {
		struct proc_dir_entry *ent;
		ent = create_proc_entry("net/rpc", S_IFDIR, 0);
		if (ent) {
#ifdef MODULE
			ent->fill_inode = rpc_modcount;
#endif
			proc_net_rpc = ent;
		}
	}
}

void
rpc_proc_exit(void)
{
	dprintk("RPC: unregistering /proc/net/rpc\n");
	if (proc_net_rpc) {
		proc_net_rpc = NULL;
		remove_proc_entry("net/rpc", 0);
	}
}

#ifdef MODULE
/*
 * This is called as the proc_dir_entry fill_inode function
 * when an inode is going into or out of service (fill == 1
 * or 0 respectively).
 *
 * We use it here to keep the module from being unloaded
 * while /proc inodes are in use.
 */
void rpc_modcount(struct inode *inode, int fill)
{
	if (fill)
		MOD_INC_USE_COUNT;
	else
		MOD_DEC_USE_COUNT;
}

int
init_module(void)
{
#ifdef RPC_DEBUG
	rpc_register_sysctl();
#endif
	rpc_proc_init();
	return 0;
}

void
cleanup_module(void)
{
#ifdef RPC_DEBUG
	rpc_unregister_sysctl();
#endif
	rpc_proc_exit();
}
#endif
