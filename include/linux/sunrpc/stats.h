/*
 * linux/include/linux/sunrpc/stats.h
 *
 * Client statistics collection for SUN RPC
 *
 * Copyright (C) 1996 Olaf Kirch <okir@monad.swb.de>
 */

#ifndef _LINUX_SUNRPC_STATS_H
#define _LINUX_SUNRPC_STATS_H

#include <linux/proc_fs.h>

struct rpc_stat {
	struct rpc_stat *	next;
	struct proc_dir_entry *	entry;
	struct rpc_program *	program;

	unsigned int		netcnt,
				netudpcnt,
				nettcpcnt,
				nettcpconn,
				netreconn;
	unsigned int		rpccnt,
				rpcretrans,
				rpcauthrefresh,
				rpcgarbage;
};

struct svc_stat {
	struct svc_stat *	next;
	struct proc_dir_entry *	entry;
	struct svc_program *	program;

	unsigned int		netcnt,
				netudpcnt,
				nettcpcnt,
				nettcpconn;
	unsigned int		rpccnt,
				rpcbadfmt,
				rpcbadauth,
				rpcbadclnt;
};

void		rpcstat_init(void);
void		rpcstat_exit(void);

void		rpcstat_register(struct rpc_stat *);
void		rpcstat_unregister(struct rpc_stat *);
int		rpcstat_get_info(struct rpc_stat *, char *, char **,
					off_t, int);
void		rpcstat_zero_info(struct rpc_program *);
void		svcstat_register(struct svc_stat *);
void		svcstat_unregister(struct svc_stat *);
int		svcstat_get_info(struct svc_stat *, char *, char **,
					off_t, int);
void		svcstat_zero_info(struct svc_program *);

#endif /* _LINUX_SUNRPC_STATS_H */
